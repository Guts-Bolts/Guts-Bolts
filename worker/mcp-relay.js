// The Studio side of the online MCP endpoint (worker/mcp.js), inside the server's Durable Object.
// Based on the plan the Lyreto team sent (MIT).
//
// Studio dials OUT to /ws (the same WebSocket the multiplayer relay uses) when the person ticks
// "Allow connected apps to use AI tools" in the Assistant tab, and says, as a normal signed request:
//     { op: 'mcp.serve', account, nonce, time, sig, args: { studio: '<version>', game: '<name>' }, id }
// The answer is { ok: true, t: 'mcp.ready', id }, and from then on that connection is the account's Studio:
//   server -> Studio  { t: 'mcp.call', id, message, app, scopes }   one JSON-RPC message from an app
//   Studio -> server  { t: 'mcp.reply', id, reply }                 Studio's JSON-RPC answer (or null)
//   Studio -> server  { t: 'ping' } every ~20 s                     keeps it alive (kHostSilence)
//   Studio -> server  { t: 'mcp.status', game }                     the open game changed
// One Studio per account: a newer one replaces the older one (which gets { t: 'mcp.replaced' }).
//
// worker/mcp.js reaches it with an internal request (never from the internet):
//   POST https://gb.internal/mcp-call  { token, message, timeoutMs } -> { ok, reply } | { ok: false, offline|timeout|busy|denied, error }
//   POST https://gb.internal/mcp-call  { token, status: true }       -> { ok, online, game, studio }
// The token and the tool are checked again here, so nothing slips past if one layer has a bug.

import { canCall, canUseMethod, hasStudio } from './mcp-policy.js';
import { CONNECT_APPS, tokenScopes } from './server.js';

const kMaxCallMs = 120000, kMaxReplyBytes = 8 << 20, kMaxPendingPerStudio = 16;
const rid = () => [...crypto.getRandomValues(new Uint8Array(8))].map((b) => b.toString(16).padStart(2, '0')).join('');
const kOffline = 'Studio is offline. Open Guts&Bolts Studio and tick "Allow connected apps to use AI tools" in the Assistant tab.';

// Adds the mcp* methods to GbServerObject (server.js calls this once, at the bottom).
export function installMcpRelay(Cls) {
  const P = Cls.prototype;

  P.mcpInit = function () {
    if (!this.mcpHosts) { this.mcpHosts = new Map(); this.mcpPending = new Map(); }   // account -> conn id; call id -> waiter
  };

  // A message on a /ws connection that's (becoming) a Studio serving apps.
  P.mcpMessage = function (c, req) {
    this.mcpInit();
    if (req.op === 'mcp.serve') {
      const answer = (r) => { if ('id' in req) r.id = req.id; this.sendTo(c, r); };
      if (c.mode !== 'request' && c.mode !== 'mcp') return answer({ ok: false, error: 'This connection is busy with something else.' });
      let chk;
      try { chk = this.checkRequest(req); } finally { this.flush(); }
      if (chk.bad) return answer(chk.bad);
      const me = chk.me;
      if (!(me.userId > 0)) return answer({ ok: false, error: 'Sign up or log in to let connected apps use Studio.' });
      const old = this.mcpHosts.get(me.id);
      if (old && old !== c.id) {
        const oc = this.conns.get(old);
        if (oc) { this.sendTo(oc, { t: 'mcp.replaced' }); this.drop(old); }
      }
      const a = chk.args;
      Object.assign(c, { mode: 'mcp', account: me.id, mcpStudio: String(a.studio || '').slice(0, 40), mcpGame: String(a.game || '').slice(0, 100) });
      this.mcpHosts.set(me.id, c.id);
      this.scheduleSweep();
      return answer({ ok: true, t: 'mcp.ready' });
    }
    if (c.mode !== 'mcp') return this.sendTo(c, { ok: false, error: 'Say mcp.serve first.' });
    if (req.t === 'mcp.status') { c.mcpGame = String(req.game || '').slice(0, 100); return; }
    if (req.t === 'mcp.reply') {
      const w = this.mcpPending.get(String(req.id));
      if (!w || w.conn !== c.id) return;   // late, or not this Studio's to answer
      this.mcpPending.delete(String(req.id));
      clearTimeout(w.timer);
      w.done({ ok: true, reply: req.reply === undefined ? null : req.reply });
    }
    // (anything else, like { t: 'ping' }, just keeps it alive)
  };

  // The Studio connection closed (from drop): everything waiting on it hears "offline" now.
  P.mcpGone = function (c) {
    this.mcpInit();
    if (this.mcpHosts.get(c.account) === c.id) this.mcpHosts.delete(c.account);
    for (const [id, w] of this.mcpPending) {
      if (w.conn !== c.id) continue;
      this.mcpPending.delete(id);
      clearTimeout(w.timer);
      w.done({ ok: false, offline: true, error: 'Studio closed while working on that.' });
    }
  };

  // Send one JSON-RPC message to the account's Studio and wait for its answer.
  P.mcpCall = function (account, message, timeoutMs, app, scopes) {
    this.mcpInit();
    const c = this.conns.get(this.mcpHosts.get(account) || '');
    if (!c || c.mode !== 'mcp') return Promise.resolve({ ok: false, offline: true, error: kOffline });
    let mine = 0;
    for (const w of this.mcpPending.values()) if (w.conn === c.id) mine++;
    if (mine >= kMaxPendingPerStudio) return Promise.resolve({ ok: false, busy: true, error: 'Studio is busy with other calls. Try again in a moment.' });
    const id = rid(), ms = Math.max(1000, Math.min(kMaxCallMs, Number(timeoutMs) || 30000));
    return new Promise((done) => {
      const timer = setTimeout(() => {
        this.mcpPending.delete(id);
        done({ ok: false, timeout: true, error: 'Studio didn\'t answer in ' + Math.round(ms / 1000) + ' s.' });
      }, ms);
      this.mcpPending.set(id, { conn: c.id, done, timer });
      this.sendTo(c, { t: 'mcp.call', id, message, app, scopes });
    });
  };

  // POST https://gb.internal/mcp-call (from worker/mcp.js only: see fetch() in server.js).
  P.mcpFetch = async function (request) {
    this.mcpInit();
    const out = (o, status = 200) => new Response(JSON.stringify(o), { status, headers: { 'content-type': 'application/json' } });
    let body;
    try { body = await request.json(); } catch { return out({ ok: false, error: 'Bad request.' }, 400); }
    if (!body || typeof body !== 'object') return out({ ok: false, error: 'Bad request.' }, 400);
    const tok = this.liveToken(typeof body.token === 'string' ? body.token : '');
    const u = tok ? this.users.get(tok.account) : null;
    if (!u || u.deleted || u.banned) return out({ ok: false, oauth: 'invalid_token', error: 'That access token is wrong, too old, or was disconnected.' });
    const scopes = tokenScopes(tok);
    if (!hasStudio(scopes)) return out({ ok: false, oauth: 'insufficient_scope', error: 'This app wasn\'t allowed to use Studio.' });
    if (body.status) {
      const c = this.conns.get(this.mcpHosts.get(u.id) || '');
      const on = !!(c && c.mode === 'mcp');
      return out({ ok: true, online: on, game: on ? c.mcpGame : '', studio: on ? c.mcpStudio : '' });
    }
    const m = body.message && typeof body.message === 'object' && !Array.isArray(body.message) ? body.message : null;
    if (!m || typeof m.method !== 'string') return out({ ok: false, error: 'Bad JSON-RPC message.' }, 400);
    if (m.method === 'tools/call') {
      const verdict = canCall(m.params && m.params.name, scopes);
      if (!verdict.ok) return out({ ok: false, denied: true, error: verdict.message });
    } else if (m.method !== 'initialize' && !canUseMethod(m.method, scopes)) {
      return out({ ok: false, denied: true, error: 'Not allowed: ' + String(m.method).slice(0, 60) });
    }
    const r = await this.mcpCall(u.id, m, body.timeoutMs, CONNECT_APPS[tok.app].name, scopes);
    if (r.ok && JSON.stringify(r.reply).length > kMaxReplyBytes) return out({ ok: false, error: 'Studio\'s answer was too big to pass on.' });
    return out(r);
  };
}
