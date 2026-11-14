// The Guts&Bolts website and server on Cloudflare.
//
//   /api   one signed request -> one answer   (the website and the apps)
//   /ws    a WebSocket for the multiplayer relay (the apps)
//   /download/...  the apps themselves (downloads.js)
//   /api/oauth/... "Log in with Guts&Bolts" for partner apps like Lyreto (see oauth below)
//   /mcp   Studio's AI tools for those apps, online (mcp.js)
//   /avatar/...    the avatar pictures those apps show
//   /api/turnstile the "are you a person?" check (shield.js)
//   else   the website's files (website/)
//
// Every request to the server goes through the risk check first (shield.js): floods and
// bots are stopped there, before they reach the server.
//
// Normally the Guts&Bolts server itself runs here too (server.js, a Durable
// Object), so it's online even when your computer is off. If GB_SERVER is set
// (host:port), /api is passed on to that server instead, over TCP.
import { connect } from 'cloudflare:sockets';
import { GbServerObject } from './server.js';
import { RemoteControlObject } from './remote-control.js';
import { readLimitedText } from './http.js';
import { withSecurityHeaders } from './security.js';
import { handleDownload } from './downloads.js';
import { handleMcp } from './mcp.js';
import { handleAds, withAdsMeta } from './ads.js';
import { handleStripe } from './stripe.js';
import { check, passCheck, tagged, cachedPicture } from './shield.js';
import { checkSupabaseHealth, listSupabaseActivity, supabasePublicConfig } from './supabase.js';
export { GbServerObject, RemoteControlObject };

const MAX_REQUEST = 40 * 1024 * 1024;   // a whole game, base64
const MAX_REPLY   = 48 * 1024 * 1024;
const TIMEOUT_MS  = 60 * 1000;

function reply(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { 'content-type': 'application/json', 'cache-control': 'no-store' },
  });
}

function splitAddress(address) {
  const a = (address || '').trim();
  const colon = a.lastIndexOf(':');
  if (colon > 0 && a.indexOf(':') === colon) return { hostname: a.slice(0, colon), port: Number(a.slice(colon + 1)) || 7780 };
  return { hostname: a, port: 7780 };
}

// Send one message to the server and read one message back.
async function ask(address, text) {
  const body = new TextEncoder().encode(text);
  const frame = new Uint8Array(4 + body.length);
  new DataView(frame.buffer).setUint32(0, body.length);
  frame.set(body, 4);

  const socket = connect(splitAddress(address));
  const timer = new Promise((_, fail) => setTimeout(() => fail(new Error('timeout')), TIMEOUT_MS));
  try {
    const work = (async () => {
      const writer = socket.writable.getWriter();
      await writer.write(frame);
      writer.releaseLock();
      const reader = socket.readable.getReader();
      let got = new Uint8Array(0), need = -1;
      for (;;) {
        const { value, done } = await reader.read();
        if (done) throw new Error('closed');
        const joined = new Uint8Array(got.length + value.length);
        joined.set(got); joined.set(value, got.length);
        got = joined;
        if (need < 0 && got.length >= 4) {
          need = new DataView(got.buffer).getUint32(0);
          if (need > MAX_REPLY) throw new Error('too big');
        }
        if (need >= 0 && got.length >= 4 + need) return new TextDecoder().decode(got.subarray(4, 4 + need));
      }
    })();
    return await Promise.race([work, timer]);
  } finally {
    socket.close().catch(() => {});
  }
}

// --- "Log in with Guts&Bolts" (OAuth 2.0, for partner apps; CONNECT_APPS in server.js) -----
//   GET  /api/oauth/authorize?response_type=code&client_id=..&redirect_uri=..&state=..
//        -> the website's Connect page, where the person presses Approve; it sends them on to
//           redirect_uri?code=..&state=..
//   POST /api/oauth/token  (form or JSON: grant_type=authorization_code, code, redirect_uri,
//        client_id, client_secret; or the id and secret as HTTP Basic auth)
//        -> { access_token, token_type: "Bearer", expires_in, scope }
//   GET  /api/oauth/userinfo  (Authorization: Bearer <access_token>)
//        -> { id, username, avatar }   (id is their user number, as text; avatar a picture's URL or null)
//   POST /api/oauth/revoke  (token, + client_id/client_secret or Basic auth) -> 200 (RFC 7009)
// authorize takes scope=... (space-separated, see CONNECT_SCOPES in server.js); none means "identify".
// The server answers them as connect.* requests (unsigned: the secret or the token is the proof).

// Ask the server one thing, wherever it runs (here, or on a computer with GB_SERVER).
async function serverAsk(env, req) {
  if (!env.GB_SERVER) {
    const stub = env.GB_SERVER_OBJECT.get(env.GB_SERVER_OBJECT.idFromName('main'));
    const r = await stub.fetch(new Request('https://gb.internal/api', { method: 'POST', body: JSON.stringify(req) }));
    return r.json();
  }
  try {
    return JSON.parse(await ask(env.GB_SERVER, JSON.stringify(req)));
  } catch (e) {
    return { ok: false, error: 'Couldn\'t reach the Guts&Bolts server.', oauth: 'temporarily_unavailable' };
  }
}

async function handleRemoteControl(request, env, url) {
  if (request.method !== 'POST') return reply({ ok: false, error: 'Use POST.' }, 405);
  if (request.headers.get('content-type')?.split(';')[0] !== 'application/json')
    return reply({ ok: false, error: 'Expected JSON.' }, 415);
  if (request.headers.get('origin') !== url.origin) return reply({ ok: false, error: 'Same-origin requests only.' }, 403);
  const text = await readLimitedText(request, 12 * 1024);
  if (text === null) return reply({ ok: false, error: 'Request is too large.' }, 413);
  let body;
  try { body = JSON.parse(text); } catch { return reply({ ok: false, error: 'Bad request.' }, 400); }
  if (!body || typeof body !== 'object' || Array.isArray(body) ||
      Object.keys(body).sort().join(',') !== 'action,auth') return reply({ ok: false, error: 'Bad request.' }, 400);
  const auth = body.auth;
  const official = String(env.OFFICIAL || '').toLowerCase();
  if (!auth || typeof auth !== 'object' || Array.isArray(auth) ||
      Object.keys(auth).sort().join(',') !== 'account,args,nonce,op,sig,time' ||
      auth.op !== 'staff.database.stats' ||
      typeof auth.account !== 'string' || auth.account.toLowerCase() !== official ||
      !auth.args || typeof auth.args !== 'object' || Array.isArray(auth.args) || Object.keys(auth.args).length !== 0) {
    return reply({ ok: false, error: 'Only the official Guts&Bolts account can manage the home server.' }, 401);
  }
  // Verify the signed, nonce-protected Guts&Bolts account request on the authoritative server.
  const verified = await serverAsk(env, auth);
  if (!verified || !verified.ok) return reply({ ok: false, error: 'Staff authentication failed. Sign in again.' }, 401);
  const action = body.action;
  if (!action || typeof action !== 'object' || Array.isArray(action)) return reply({ ok: false, error: 'Bad action.' }, 400);
  const stub = env.REMOTE_CONTROL.getByName('home');
  try {
    const result = await stub.control(auth.account.toLowerCase(), action, request.headers.get('cf-connecting-ip') || 'unknown');
    return reply(result.body, result.status);
  } catch (error) {
    console.error(JSON.stringify({ message: 'remote control RPC failed', error: error instanceof Error ? error.message : 'unknown' }));
    return reply({ ok: false, error: 'Remote control is temporarily unavailable.' }, 503);
  }
}

const OAUTH_STATUS = { invalid_client: 401, invalid_token: 401, temporarily_unavailable: 503 };
function oauthError(r) {
  const word = r.oauth || 'server_error';
  return new Response(JSON.stringify({ error: word, error_description: r.error || '' }), {
    status: OAUTH_STATUS[word] || 400,
    headers: Object.assign({ 'content-type': 'application/json', 'cache-control': 'no-store' },
      word === 'invalid_token' ? { 'www-authenticate': 'Bearer error="invalid_token"' } : {}),
  });
}

async function handleOauth(request, env, url) {
  const path = url.pathname.slice('/api/oauth/'.length);
  if (path === 'authorize') {
    const go = new URL('/app/', url.origin);
    go.hash = '#/connect?' + url.searchParams.toString();
    return Response.redirect(go.toString(), 302);
  }
  if (path === 'token' || path === 'revoke') {
    if (request.method !== 'POST') return oauthError({ oauth: 'invalid_request', error: 'Use POST.' });
    const text = await readLimitedText(request, 8192);
    if (text === null) return oauthError({ oauth: 'invalid_request', error: 'Request body is too large.' });
    let body = {};
    if ((request.headers.get('content-type') || '').includes('json')) {
      try { body = JSON.parse(text) || {}; } catch { body = {}; }
    } else {
      body = Object.fromEntries(new URLSearchParams(text));
    }
    const args = {};
    for (const k of ['grant_type', 'code', 'redirect_uri', 'client_id', 'client_secret', 'token'])
      if (typeof body[k] === 'string') args[k] = body[k];
    const basic = /^Basic\s+(.+)$/i.exec(request.headers.get('authorization') || '');
    if (basic) {
      try {
        const [id, secret] = atob(basic[1].trim()).split(':');
        args.client_id = decodeURIComponent(id || ''); args.client_secret = decodeURIComponent(secret || '');
      } catch { return oauthError({ oauth: 'invalid_client', error: 'Bad Basic auth.' }); }
    }
    const r = await serverAsk(env, { op: path === 'token' ? 'connect.token' : 'connect.revoke', args });
    if (!r.ok) return oauthError(r);
    if (path === 'revoke') return new Response(null, { status: 200, headers: { 'cache-control': 'no-store' } });
    return reply({ access_token: r.access_token, token_type: r.token_type, expires_in: r.expires_in, scope: r.scope });
  }
  if (path === 'userinfo') {
    const bearer = /^Bearer\s+(\S+)$/i.exec(request.headers.get('authorization') || '');
    if (!bearer) return oauthError({ oauth: 'invalid_token', error: 'Send the access token as "Authorization: Bearer <token>".' });
    const r = await serverAsk(env, { op: 'connect.userinfo', args: { token: bearer[1], origin: url.origin } });
    if (!r.ok) return oauthError(r);
    return reply(r.user);
  }
  return oauthError({ oauth: 'invalid_request', error: 'Unknown address. Use /api/oauth/authorize, /token, /revoke or /userinfo.' });
}

export default {
  async scheduled(controller, env, ctx) {
    const id = env.GB_SERVER_OBJECT.idFromName('main');
    const stub = env.GB_SERVER_OBJECT.get(id);
    ctx.waitUntil(stub.backupToR2().catch((error) => {
      console.error('Scheduled database backup failed:', error && error.message || error);
      throw error;
    }));
  },
  async fetch(request, env, ctx) {
    const response = await route(request, env, ctx);
    return withSecurityHeaders(response, request);
  },
};

async function route(request, env, ctx) {
    const url = new URL(request.url);
    // The site lives at gutsandbolts.net. The old workers.dev address only keeps answering
    // older apps (the server and its pictures); people opening it in a browser get moved.
    if (url.hostname.endsWith('.workers.dev') && !/^\/(api|ws|thumb|icon|shot|wear|decal|artpic|pasteraw|download|avatar)(\/|$)/.test(url.pathname))
      return Response.redirect('https://gutsandbolts.net' + url.pathname + url.search, 301);
    // Share links (sharelinks.js): gutsandbolts.net/s/<code> opens the website's #/s/<code> page, which counts the click.
    const share = /^\/s\/([A-Za-z0-9]{4,16})\/?$/.exec(url.pathname);
    if (share) return Response.redirect(url.origin + '/#/s/' + share[1], 302);
    if (url.pathname.startsWith('/download/')) return handleDownload(request, env, url);
    const ads = handleAds(request, env, url);   // /ads.txt and /ads-config (ads.js)
    if (ads) return ads;
    if (url.pathname === '/api/turnstile') return passCheck(request, env, url);
    // Stripe's own servers call this (signature-checked in stripe.js), so it skips the bot check.
    if (url.pathname === '/api/stripe/webhook') return handleStripe(request, env, url);
    const stripePath = url.pathname === '/api/stripe/checkout' || url.pathname === '/api/stripe/config';
    const picture = /^\/(thumb|icon|shot|wear|decal|artpic|pasteraw|avatar)\//.test(url.pathname);
    const mcp = url.pathname === '/mcp' || url.pathname.startsWith('/mcp/') || url.pathname.startsWith('/.well-known/oauth-');
    const supabaseHealthPath = url.pathname === '/api/supabase/health' || url.pathname === '/api/supabase/live';
    const supabaseActivityPath = /^\/api\/activity\/(auctions|forum)$/.test(url.pathname);
    const kind = picture ? 'picture' : (url.pathname === '/ws' || url.pathname === '/ws/remote') ? 'ws' : url.pathname === '/api' || url.pathname === '/api/remote' || url.pathname.startsWith('/api/oauth/') || supabaseHealthPath || supabaseActivityPath || stripePath ? 'api' : mcp ? 'mcp' : '';
    let info = null;
    if (kind) {
      info = await check(request, env, url, kind);
      if (info.stop) return info.stop;
    }
    if (picture) return cachedPicture(request, ctx, () => pictureFor(request, env, url));
    if (url.pathname.startsWith('/api/oauth/')) return handleOauth(request, env, url);
    if (stripePath) return handleStripe(request, env, url);   // Boltworks Club payments (stripe.js)
    if (mcp) return handleMcp(request, env, url, serverAsk);
    if (url.pathname === '/api/info') {
      return reply({ ok: true, server: env.GB_SERVER || 'cloudflare', time: Math.floor(Date.now() / 1000) });
    }
    if (url.pathname === '/api/supabase/live') {
      // Where the website listens for live pings (Supabase Realtime). Public on purpose.
      if (request.method !== 'GET') return reply({ ok: false, error: 'Use GET.' }, 405);
      const live = supabasePublicConfig(env);
      return reply(live ? { ok: true, live } : { ok: false, error: 'Live updates are off on this server.' });
    }
    if (supabaseHealthPath) {
      if (request.method !== 'GET') return reply({ ok: false, error: 'Use GET.' }, 405);
      const health = await checkSupabaseHealth(env);
      return reply({ ok: health.connected, supabase: health }, health.connected ? 200 : 503);
    }
    if (supabaseActivityPath) {
      if (request.method !== 'GET') return reply({ ok: false, error: 'Use GET.' }, 405);
      const table = url.pathname.endsWith('/auctions') ? 'gb_auction_events' : 'gb_forum_activity';
      try {
        const events = await listSupabaseActivity(env, table, url.searchParams.get('limit'));
        return reply({ ok: true, events });
      } catch {
        return reply({ ok: false, error: 'Activity feed is temporarily unavailable.' }, 503);
      }
    }
    // The server on Cloudflare: one Durable Object holds everything.
    const builtIn = () => env.GB_SERVER_OBJECT.get(env.GB_SERVER_OBJECT.idFromName('main'));
    if (url.pathname === '/ws/remote') return env.REMOTE_CONTROL.get(env.REMOTE_CONTROL.idFromName('home')).fetch(tagged(request, info));
    if (url.pathname === '/api/remote') return handleRemoteControl(request, env, url);
    if (url.pathname === '/ws') return builtIn().fetch(tagged(request, info));
    // Lyreto's server talking to the chat gateway (worker/chats.js checks its signature).
    if (url.pathname === '/api/bridge/lyreto') {
      if (request.method !== 'POST') return reply({ ok: false, error: 'Use POST.' }, 405);
      if (env.GB_SERVER) return reply({ ok: false, error: 'The Lyreto gateway only runs on the main Guts&Bolts server.' }, 404);
      return builtIn().fetch(request);
    }
    if (url.pathname === '/api') {
      if (request.method !== 'POST') return reply({ ok: false, error: 'Use POST.' }, 405);
      if (!env.GB_SERVER) {
        const len = Number(request.headers.get('content-length') || 0);
        if (len > MAX_REQUEST) return reply({ ok: false, error: 'That\'s too big to send.' }, 413);
        return builtIn().fetch(tagged(request, info));
      }
      const len = Number(request.headers.get('content-length') || 0);
      if (len > MAX_REQUEST) return reply({ ok: false, error: 'That\'s too big to send.' }, 413);
      const text = await readLimitedText(request, MAX_REQUEST);
      if (text === null) return reply({ ok: false, error: 'That\'s too big to send.' }, 413);
      let req;
      try {
        req = JSON.parse(text);   // only pass on proper requests
      } catch {
        return reply({ ok: false, error: 'That wasn\'t a proper request.' }, 400);
      }
      if (!req || typeof req !== 'object' || Array.isArray(req)) return reply({ ok: false, error: 'That wasn\'t a proper request.' }, 400);
      // Who's asking, for the server's own limits (it trusts this when started with --behind-worker).
      req.from = { ip: info.ip, web: !!info.web, human: !!info.human, sitekey: info.sitekey || '' };
      try {
        const answer = await ask(env.GB_SERVER, JSON.stringify(req));
        return new Response(answer, { headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });
      } catch (e) {
        return reply({ ok: false, error: 'Couldn\'t reach the Guts&Bolts server (' + (e && e.message || 'error') + '). Is it running?' });
      }
    }
    return withAdsMeta(await env.ASSETS.fetch(request), env);
}

// A picture: /thumb, /icon, /shot, /wear and /decal from the server on Cloudflare, /avatar from either.
async function pictureFor(request, env, url) {
  if (!env.GB_SERVER) return env.GB_SERVER_OBJECT.get(env.GB_SERVER_OBJECT.idFromName('main')).fetch(request);
  if (url.pathname.startsWith('/artpic/')) {   // art (art.js; the C++ server answers art.picture)
    const [id, which] = url.pathname.slice(8).split('/');
    const r = await serverAsk(env, { op: 'art.picture', args: { id: decodeURIComponent(id || ''), thumb: which === 'thumb' } });
    if (!r.ok || !r.data) return new Response('No picture.', { status: 404 });
    const bin = atob(r.data), data = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) data[i] = bin.charCodeAt(i);
    const type = { png: 'image/png', jpg: 'image/jpeg', gif: 'image/gif', webp: 'image/webp' }[r.type] || 'application/octet-stream';
    return new Response(data, { headers: { 'content-type': type, 'cache-control': 'public, max-age=3600', 'x-content-type-options': 'nosniff' } });
  }
  if (url.pathname.startsWith('/pasteraw/')) {   // a paste's text alone (paste.js; the C++ server answers paste.raw)
    let id = '';
    try { id = decodeURIComponent(url.pathname.slice(10)); } catch { id = ''; }
    const r = await serverAsk(env, { op: 'paste.raw', args: { id } });
    if (!r.ok) return new Response('No paste.', { status: 404, headers: { 'content-type': 'text/plain; charset=utf-8' } });
    return new Response(r.text, { headers: { 'content-type': 'text/plain; charset=utf-8', 'x-content-type-options': 'nosniff',
      'cache-control': 'public, max-age=60', 'content-security-policy': "default-src 'none'; sandbox" } });
  }
  if (!url.pathname.startsWith('/avatar/')) return new Response('No picture.', { status: 404 });
  const r = await serverAsk(env, { op: 'connect.avatarPicture', args: { id: url.pathname.slice(8).replace(/\.png$/, '') } });
  if (!r.ok) return new Response('No picture.', { status: 404 });
  const bin = atob(r.data), data = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) data[i] = bin.charCodeAt(i);
  return new Response(data, { headers: { 'content-type': 'image/png', 'cache-control': 'public, max-age=3600', 'x-content-type-options': 'nosniff' } });
}
