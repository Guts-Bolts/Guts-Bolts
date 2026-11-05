// The chat panel's server side: the inbox, Do Not Disturb, voice calls and the Lyreto gateway.
// (The conversations themselves are the party.* requests in server.js: each member keeps a copy
// of every chat they're in, me.chats.)
//
//   chat.inbox     {}                 -> {chats: [summary], invites, dnd, calls: relay?}
//   presence.set   {dnd}              -> {me}           Do Not Disturb: no calls ring, friends see a red dot
//
// Voice calls, in any chat (up to kCallMost people). The browsers talk to each other with WebRTC;
// we only pass the "how to reach me" notes between them (signals) and keep track of who's in.
//   call.start   {id: chat}          -> {call}          rings the others (not the ones on Do Not Disturb)
//   call.join    {id}                -> {call, peers}   you send an offer to each of `peers`
//   call.signal  {id, to, kind, sdp} -> {}              kind: offer / answer (sdp: the browser's description)
//   call.poll    {id, after}         -> {call|null, signals: [{n, from, kind, sdp}]}   about once a second while in a call
//   call.leave   {id} / call.decline {id}
//   call.ice     {}                  -> {iceServers, relay}
// Without a TURN relay (Cloudflare Realtime TURN: secrets CF_TURN_KEY_ID + CF_TURN_KEY_TOKEN) calls
// connect the two computers directly, so each could see the other's internet address: then
// everyone in a call has to be friends with each other. With it, everything goes through
// Cloudflare and nobody's address is shown.
//
// The Lyreto gateway links one chat to one Lyreto channel, so Guts&Bolts players and Lyreto users
// talk together. The chat's owner makes a code (chat.bridge), someone types it into Lyreto's bridge,
// and Lyreto's server calls us at POST /api/bridge/lyreto (signed with LYRETO_BRIDGE_SECRET):
//   bridge.link   {code, channel, name}            -> {chat, title}
//   bridge.send   {chat, channel, id, author: {id, name}, text}  -> {id}
//   bridge.unlink {chat, channel}
// and we send what Guts&Bolts players say to LYRETO_BRIDGE_URL the same way (bridgeFlush).
// (The C++ server keeps Do Not Disturb and the inbox, but has no calls or gateway.)

import { say } from './server.js';   // (the chat filter, for what Lyreto users write)

const fail = (error) => ({ ok: false, error });
const okay = (extra) => Object.assign({ ok: true }, extra || {});
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => [...crypto.getRandomValues(new Uint8Array(n))].map((b) => b.toString(16).padStart(2, '0')).join('');

export const kCallMost = 6;          // people in one call (every browser talks to every other)
export const kCallRing = 45;         // seconds a call rings before it's missed
export const kCallQuiet = 20;        // seconds without a call.poll before someone's dropped
export const kSignalSize = 16000;    // one offer / answer
export const kBridgeSkew = 300;      // seconds a gateway request's time may be off
export const kBridgePerMinute = 60;  // Lyreto messages into one chat a minute
export const kBridgeCodeMinutes = 10;
export const LYRETO_BRIDGE_URL = 'https://lyreto.com/api/gateways/gutsandbolts/events';

const enc = new TextEncoder();
export async function hmacHex(secret, text) {
  const key = await crypto.subtle.importKey('raw', enc.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  return [...new Uint8Array(await crypto.subtle.sign('HMAC', key, enc.encode(text)))].map((b) => b.toString(16).padStart(2, '0')).join('');
}
const sameText = (a, b) => {
  if (typeof a !== 'string' || typeof b !== 'string' || a.length !== b.length) return false;
  let d = 0;
  for (let i = 0; i < a.length; i++) d |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return d === 0;
};

export function installChats(Cls) {
  const P = Cls.prototype;

  P.chatsInit = function () {
    if (this.calls) return;
    this.calls = new Map();        // chat id -> call (in memory: calls end if the server restarts)
    this.bridgeCodes = new Map();  // code -> {chat, owner, until}
    this.bridgeOut = [];           // messages waiting to go to Lyreto
    this.bridgeSent = new Map();   // chat id -> times Lyreto sent messages (the limit)
    this.sql.exec('CREATE TABLE IF NOT EXISTS chatbridges (chat TEXT PRIMARY KEY, member TEXT NOT NULL, channel TEXT NOT NULL, name TEXT NOT NULL, at INTEGER NOT NULL)');
    this.sql.exec(`CREATE TABLE IF NOT EXISTS teamsessions (
      id TEXT PRIMARY KEY, code TEXT UNIQUE NOT NULL, owner TEXT NOT NULL, title TEXT NOT NULL,
      chat TEXT NOT NULL, snapshot TEXT NOT NULL, seq INTEGER NOT NULL DEFAULT 0, snapshot_seq INTEGER NOT NULL DEFAULT 0, created INTEGER NOT NULL
    );
    CREATE TABLE IF NOT EXISTS teammembers (
      session TEXT NOT NULL, account TEXT NOT NULL, member_id INTEGER NOT NULL, name TEXT NOT NULL,
      color INTEGER NOT NULL, selected TEXT NOT NULL DEFAULT '0', cursor TEXT NOT NULL DEFAULT '',
      seen INTEGER NOT NULL, PRIMARY KEY (session, account), UNIQUE (session, member_id)
    );
    CREATE TABLE IF NOT EXISTS teamevents (
      session TEXT NOT NULL, seq INTEGER NOT NULL, account TEXT NOT NULL, kind TEXT NOT NULL,
      payload TEXT NOT NULL, at INTEGER NOT NULL, PRIMARY KEY (session, seq)
    );
    CREATE INDEX IF NOT EXISTS teamevents_session_seq ON teamevents(session, seq);`);
  };
  P.turnOn = function () { return !!(this.env.CF_TURN_KEY_ID && this.env.CF_TURN_KEY_TOKEN); };
  P.bridgeOn = function () { return typeof this.env.LYRETO_BRIDGE_SECRET === 'string' && this.env.LYRETO_BRIDGE_SECRET.length >= 16; };

  // A member's copy of a chat (null if they're not in it).
  P.chatOf = function (u, id) {
    const c = u && Array.isArray(u.chats) ? u.chats.find((x) => x.id === id) : null;
    return c && (c.members || []).some((m) => m.id === u.id) ? c : null;
  };
  // Give every member the new copy (like party.*'s syncChat).
  P.chatSync = function (chat) {
    for (const m of chat.members || []) {
      const u = this.users.get(m.id);
      if (!u || u.userId === 0) continue;
      u.chats = Array.isArray(u.chats) ? u.chats : [];
      const i = u.chats.findIndex((x) => x.id === chat.id);
      if (i < 0) u.chats.unshift(structuredClone(chat)); else u.chats[i] = structuredClone(chat);
      u.chats = u.chats.slice(0, 100);
      this.saveUser(u);
    }
  };
  // Something changed in a chat: everyone's page looks again (live pings).
  P.chatPing = function (chat, event = 'chat') {
    for (const m of chat.members || []) this.livePing(this.users.get(m.id), event);
  };
  // party.send / party.game posted `message` in `chat` (after chatSync): ping, and pass it to Lyreto.
  P.chatPosted = function (chat, message) {
    this.chatsInit();
    this.chatPing(chat);
    if (chat.bridge && chat.bridge.channel && message.via !== 'lyreto' && this.bridgeOn())
      this.bridgeOut.push({ type: 'message', chat: chat.id, channel: chat.bridge.channel,
        message: { id: message.id, text: message.body, at: message.at, author: { userId: (this.users.get(message.from) || {}).userId || 0, name: message.name } } });
  };
  P.chatSay = function (chat, body) {   // a grey line in the chat (calls starting and ending...)
    const message = { id: hex(8), from: '', name: '', body, at: now(), system: true };
    chat.messages = [...(chat.messages || []), message].slice(-200);
    chat.updated = message.at;
    this.chatSync(chat);
    this.chatPing(chat);
  };

  P.chatOp = function (name, me, args) {
    this.chatsInit();
    if (me.userId === 0) return fail('Sign up first.');
    if (name === 'presence.set') {
      if ('dnd' in args) me.dnd = args.dnd === true;
      this.saveUser(me);
      for (const id of me.friends || []) this.livePing(this.users.get(id), 'friends');
      return okay({ me: this.meJson(me) });
    }
    if (name.startsWith('team.')) return this.teamOp(name, me, args);
    if (name === 'chat.inbox') return this.chatInbox(me);
    if (name === 'call.ice') {
      if (!this.turnOn()) return okay({ relay: false, iceServers: [{ urls: 'stun:stun.cloudflare.com:3478' }] });
      const answer = okay({ relay: true });
      Object.defineProperty(answer, 'iceJob', { value: true, enumerable: false });
      return answer;
    }
    if (name.startsWith('call.')) return this.callOp(name, me, args);
    if (name === 'chat.bridge' || name === 'chat.unbridge') return this.bridgeOwnerOp(name, me, args);
    return fail('Unknown request.');
  };

  // Cloud Team Create: signed account requests share a durable session and its team chat.
  P.teamMemberRows = function (session) {
    return [...this.sql.exec('SELECT member_id, account, name, color, selected, cursor, seen FROM teammembers WHERE session = ? ORDER BY member_id', session)];
  };
  P.teamMembers = function (session) {
    const t = now();
    return this.teamMemberRows(session).filter((m) => t - m.seen <= 45).map((m) => {
      let cursor = null;
      try { cursor = m.cursor ? JSON.parse(m.cursor) : null; } catch { cursor = null; }
      return { id: m.member_id, account: m.account, name: m.name, color: m.color, selected: Number(m.selected) || 0, cursor };
    });
  };
  P.teamChatSync = function (chat) {
    this.chatSync(chat);
    for (const m of chat.members || []) this.livePing(this.users.get(m.id), 'team');
  };
  P.teamChatOf = function (session, account) {
    const row = [...this.sql.exec('SELECT chat, owner FROM teamsessions WHERE id = ?', session)][0];
    if (!row) return null;
    const owner = this.users.get(row.owner), chat = this.chatOf(owner, row.chat);
    return chat && (chat.members || []).some((m) => m.id === account) ? chat : null;
  };
  P.teamOp = function (name, me, args) {
    if (me.userId === 0) return fail('Sign up first to use cloud Team Create.');
    const clean = (v, max) => say(typeof v === 'string' ? v : '', max);
    if (name === 'team.create') {
      const snapshot = typeof args.snapshot === 'string' ? args.snapshot : '';
      if (!snapshot || snapshot.length > 512 * 1024) return fail('Save a smaller scene before starting cloud Team Create (512 KB session snapshot limit).');
      const id = 'tc-' + hex(8), code = hex(16).toUpperCase(), chatId = 'tc-' + hex(8), t = now();
      const title = clean(args.title, 60) || 'Studio Team';
      const chat = { id: chatId, title, owner: me.id, members: [{ id: me.id, userId: me.userId, name: me.username || me.name }],
        messages: [], readAt: { [me.id]: t }, created: t, updated: t };
      me.chats = Array.isArray(me.chats) ? me.chats : [];
      me.chats.unshift(chat); me.chats = me.chats.slice(0, 100); this.saveUser(me);
      this.sql.exec('INSERT INTO teamsessions (id, code, owner, title, chat, snapshot, seq, created) VALUES (?, ?, ?, ?, ?, ?, 0, ?)',
        id, code, me.id, title, chatId, snapshot, t);
      this.sql.exec("INSERT INTO teammembers (session, account, member_id, name, color, selected, cursor, seen) VALUES (?, ?, 0, ?, 0, '0', '', ?)",
        id, me.id, me.username || me.name, t);
      return okay({ session: id, code, chat: chatId, member: 0, seq: 0, snapshot, members: this.teamMembers(id) });
    }
    if (name === 'team.join') {
      const code = clean(args.code, 24).toUpperCase();
      if (!/^[0-9A-F]{16}$/.test(code)) return fail('Enter the 16-character Team Create invite code.');
      const row = [...this.sql.exec('SELECT id, owner, title, chat, snapshot, seq, snapshot_seq FROM teamsessions WHERE code = ?', code)][0];
      if (!row) return fail('That Team Create invite is invalid or has expired.');
      const existing = [...this.sql.exec('SELECT member_id FROM teammembers WHERE session = ? AND account = ?', row.id, me.id)][0];
      let memberId = existing ? existing.member_id : -1;
      if (memberId < 0) {
        const ids = this.teamMemberRows(row.id).map((m) => m.member_id);
        if (ids.length >= 20) return fail('This Team Create session is full (20 people maximum).');
        memberId = Math.max(0, ...ids) + 1;
        const owner = this.users.get(row.owner), chat = this.chatOf(owner, row.chat);
        if (!chat) return fail('The Team Create owner is no longer available.');
        this.sql.exec("INSERT INTO teammembers (session, account, member_id, name, color, selected, cursor, seen) VALUES (?, ?, ?, ?, ?, '0', '', ?)",
          row.id, me.id, memberId, me.username || me.name, memberId % 8, now());
        if (!(chat.members || []).some((m) => m.id === me.id)) chat.members.push({ id: me.id, userId: me.userId, name: me.username || me.name });
        chat.readAt = chat.readAt || {}; chat.readAt[me.id] = now(); chat.updated = now();
        this.teamChatSync(chat);
      } else {
        this.sql.exec('UPDATE teammembers SET seen = ? WHERE session = ? AND account = ?', now(), row.id, me.id);
      }
      return okay({ session: row.id, code, chat: row.chat, member: memberId, owner: row.owner === me.id, seq: row.snapshot_seq, snapshotSeq: row.snapshot_seq, latestSeq: row.seq, snapshot: row.snapshot, members: this.teamMembers(row.id) });
    }
    const session = clean(args.session, 40);
    const row = [...this.sql.exec('SELECT id, owner, title, chat, snapshot, seq, snapshot_seq FROM teamsessions WHERE id = ?', session)][0];
    if (!row) return fail('That Team Create session has ended.');
    const member = [...this.sql.exec('SELECT member_id FROM teammembers WHERE session = ? AND account = ?', session, me.id)][0];
    if (!member) return fail('You are not a member of that Team Create session.');
    if (name === 'team.poll') {
      const after = Math.max(0, Math.floor(Number(args.after) || 0));
      let cursor = '';
      if (args.cursor && typeof args.cursor === 'object') {
        const x = Number(args.cursor.x), y = Number(args.cursor.y), z = Number(args.cursor.z);
        if ([x, y, z].every(Number.isFinite) && Math.abs(x) < 100000 && Math.abs(y) < 100000 && Math.abs(z) < 100000)
          cursor = JSON.stringify({ x, y, z });
      }
      this.sql.exec('UPDATE teammembers SET selected = ?, cursor = ?, seen = ? WHERE session = ? AND account = ?',
        String(Math.max(0, Math.floor(Number(args.selected) || 0))), cursor, now(), session, me.id);
      if (after < row.snapshot_seq)
        return okay({ session, seq: row.seq, resync: true, snapshotSeq: row.snapshot_seq, snapshot: row.snapshot,
          events: [], members: this.teamMembers(session), chat: row.chat });
      const events = [...this.sql.exec('SELECT seq, account, kind, payload, at FROM teamevents WHERE session = ? AND seq > ? ORDER BY seq LIMIT 100', session, after)]
        .map((e) => ({ seq: e.seq, account: e.account, kind: e.kind, payload: e.payload, at: e.at }));
      return okay({ session, seq: row.seq, events, members: this.teamMembers(session), chat: row.chat });
    }
    if (name === 'team.snapshot') {
      if (me.id !== row.owner) return fail('Only the session host can publish a scene snapshot.');
      const snapshot = typeof args.snapshot === 'string' ? args.snapshot : '';
      if (!snapshot || snapshot.length > 512 * 1024) return fail('The scene snapshot is empty or exceeds 512 KB.');
      const upto = Math.max(row.snapshot_seq, Math.min(row.seq, Math.floor(Number(args.upto) || 0)));
      this.sql.exec('UPDATE teamsessions SET snapshot = ?, snapshot_seq = ? WHERE id = ?', snapshot, upto, session);
      this.sql.exec('DELETE FROM teamevents WHERE session = ? AND seq <= ?', session, upto);
      return okay({ snapshotSeq: upto });
    }
    if (name === 'team.push') {
      const kind = clean(args.kind, 12);
      if (!['ops', 'chat'].includes(kind)) return fail('Unknown Team Create event.');
      const payload = typeof args.payload === 'string' ? args.payload : '';
      if (!payload || payload.length > (kind === 'ops' ? 64 * 1024 : 300)) return fail('That Team Create update is empty or too large.');
      const backlog = [...this.sql.exec('SELECT COALESCE(SUM(LENGTH(payload)), 0) AS bytes FROM teamevents WHERE session = ?', session)][0];
      if ((backlog && backlog.bytes || 0) + payload.length > 8 * 1024 * 1024)
        return fail('This session needs a host scene checkpoint before more edits can be queued.');
      if (kind === 'chat') {
        // Reuse the ordinary party.send pipeline for text filtering, block checks,
        // daily message limits, chat synchronization, notifications and live pings.
        const posted = this.op('party.send', me, { id: row.chat, body: payload });
        if (!posted.ok) return posted;
      }
      const seq = row.seq + 1, at = now();
      this.sql.exec('INSERT INTO teamevents (session, seq, account, kind, payload, at) VALUES (?, ?, ?, ?, ?, ?)',
        session, seq, me.id, kind, payload, at);
      this.sql.exec('UPDATE teamsessions SET seq = ? WHERE id = ?', seq, session);
      return okay({ seq });
    }
    if (name === 'team.presence') {
      const selected = String(Math.max(0, Math.floor(Number(args.selected) || 0)));
      let cursor = '';
      if (args.cursor && typeof args.cursor === 'object') {
        const x = Number(args.cursor.x), y = Number(args.cursor.y), z = Number(args.cursor.z);
        if ([x, y, z].every(Number.isFinite) && Math.abs(x) < 100000 && Math.abs(y) < 100000 && Math.abs(z) < 100000)
          cursor = JSON.stringify({ x, y, z });
      }
      this.sql.exec('UPDATE teammembers SET selected = ?, cursor = ?, seen = ? WHERE session = ? AND account = ?',
        selected, cursor, now(), session, me.id);
      return okay({ members: this.teamMembers(session) });
    }
    if (name === 'team.leave') {
      const chat = this.teamChatOf(session, me.id);
      if (this.calls && this.calls.has(row.chat)) this.callOp('call.leave', me, { id: row.chat });
      this.sql.exec('DELETE FROM teammembers WHERE session = ? AND account = ?', session, me.id);
      if (me.id === row.owner) {
        this.sql.exec('DELETE FROM teamevents WHERE session = ?', session);
        this.sql.exec('DELETE FROM teammembers WHERE session = ?', session);
        this.sql.exec('DELETE FROM teamsessions WHERE id = ?', session);
        if (chat) for (const m of chat.members || []) {
          const u = this.users.get(m.id);
          if (u) { u.chats = (u.chats || []).filter((c) => c.id !== chat.id); this.saveUser(u); }
        }
      } else if (chat) {
        chat.members = (chat.members || []).filter((m) => m.id !== me.id);
        me.chats = (me.chats || []).filter((c) => c.id !== chat.id);
        this.saveUser(me);
        chat.updated = now(); this.teamChatSync(chat);
      }
      return okay();
    }
    return fail('Unknown Team Create request.');
  };

  P.chatInbox = function (me) {
    this.callSweep();
    const chats = (me.chats || []).filter((c) => (c.members || []).some((m) => m.id === me.id)).map((c) => {
      const msgs = c.messages || [], last = msgs[msgs.length - 1] || null;
      const readAt = (c.readAt || {})[me.id] || 0;
      let unread = 0;
      for (let i = msgs.length - 1; i >= 0 && unread < 99; i--) { if (msgs[i].at <= readAt) break; if (msgs[i].from !== me.id) unread++; }
      const members = (c.members || []).map((m) => {
        const u = this.users.get(m.id);
        const pr = u && m.id !== me.id ? this.presence(me, u) : null;
        return { id: m.id, userId: m.userId, name: m.name, online: pr ? pr.online : undefined, dnd: pr ? pr.dnd : undefined };
      });
      const call = this.calls.get(c.id);
      return { id: c.id, title: c.title, owner: c.owner, members, updated: c.updated || c.created, unread,
        last: last ? { name: last.name, body: last.body, at: last.at, system: !!last.system, via: last.via, mine: last.from === me.id } : null,
        call: call ? this.callJson(call, me) : null, bridge: c.bridge ? { name: c.bridge.name } : null };
    }).sort((a, b) => b.updated - a.updated);
    return okay({ chats, invites: me.partyInvites || [], dnd: !!me.dnd, relay: this.turnOn(), bridge: this.bridgeOn() });
  };

  // --- Calls ---------------------------------------------------------------------------
  P.callJson = function (call, me) {
    const members = [...call.members.keys()].map((id) => { const u = this.users.get(id); return { id, userId: u ? u.userId : 0, name: u ? u.username || u.name : '?' }; });
    return { id: call.id, chat: call.chat, by: call.by, started: call.started, members, relay: this.turnOn(),
      ringing: !call.members.has(me.id) && !call.declined.has(me.id) && call.rings.has(me.id) && now() - call.started < kCallRing };
  };
  P.callEnd = function (call, why) {
    this.calls.delete(call.chat);
    const owner = this.users.get(call.by);
    const chat = this.chatOf(owner, call.chat) || [...call.everyone].map((id) => this.chatOf(this.users.get(id), call.chat)).find(Boolean);
    if (!chat) return;
    const mins = Math.max(1, Math.round((now() - (call.answered || call.started)) / 60));
    this.chatSay(chat, why || (call.everyone.size > 1 ? 'Call ended · ' + mins + ' min' : 'Missed call'));
  };
  // Drop people who stopped asking (closed the page), and calls nobody answered.
  P.callSweep = function () {
    const t = now();
    for (const call of [...this.calls.values()]) {
      for (const [id, m] of call.members) if (t - m.seen > kCallQuiet) call.members.delete(id);
      if (!call.members.size) this.callEnd(call);
      else if (call.everyone.size === 1 && t - call.started > kCallRing) this.callEnd(call, 'Missed call');
    }
  };
  // Can `me` be in a call with everyone already in it? (Blocks; and friends only when calls connect directly.)
  P.callCheck = function (me, call, chat) {
    for (const m of chat.members) {
      const u = this.users.get(m.id);
      if (u && u.id !== me.id && (this.blocks(me, u) || this.blocks(u, me))) return 'You can\'t call in this chat because of a block.';
    }
    if (call && call.members.size >= kCallMost) return 'That call is full (' + kCallMost + ' people at most).';
    if (!this.turnOn() && call)
      for (const id of call.members.keys())
        if (id !== me.id && !(me.friends || []).includes(id))
          return 'Everyone in a call has to be friends with each other here (calls connect your computers directly).';
    return '';
  };
  P.callOp = function (name, me, args) {
    this.callSweep();
    const chatId = typeof args.id === 'string' ? args.id : '';
    const chat = this.chatOf(me, chatId);
    if (!chat) return fail('That conversation is no longer available.');
    let call = this.calls.get(chat.id);
    const t = now();
    if (name === 'call.start') {
      if (call) return this.callOp('call.join', me, args);
      const others = chat.members.filter((m) => m.id !== me.id).map((m) => this.users.get(m.id)).filter(Boolean);
      if (!others.length) return fail('There\'s nobody else in this chat to call.');
      if (me.dnd) return fail('Turn off Do Not Disturb to start a call.');
      if (others.length === 1 && others[0].dnd) return fail((others[0].username || others[0].name) + ' is on Do Not Disturb.');
      const bad = this.callCheck(me, null, chat);
      if (bad) return fail(bad);
      call = { id: 'c-' + hex(6), chat: chat.id, by: me.id, started: t, answered: 0, members: new Map([[me.id, { at: t, seen: t }]]),
        everyone: new Set([me.id]), rings: new Set(others.filter((u) => !u.dnd).map((u) => u.id)), declined: new Set(), signals: [], seq: 0 };
      this.calls.set(chat.id, call);
      this.chatSay(chat, (me.username || me.name) + ' started a call');
      for (const u of others) this.livePing(u, 'call');
      return okay({ call: this.callJson(call, me), peers: [] });
    }
    if (!call) return name === 'call.poll' ? okay({ call: null, signals: [] }) : fail('That call has ended.');
    if (name === 'call.join') {
      if (call.members.has(me.id)) return okay({ call: this.callJson(call, me), peers: [] });
      const bad = this.callCheck(me, call, chat);
      if (bad) return fail(bad);
      const peers = [...call.members.keys()];
      call.members.set(me.id, { at: t, seen: t });
      call.everyone.add(me.id);
      call.declined.delete(me.id);
      if (!call.answered) call.answered = t;
      for (const id of peers) this.livePing(this.users.get(id), 'call');
      return okay({ call: this.callJson(call, me), peers });
    }
    if (name === 'call.decline') {
      call.declined.add(me.id);
      // A one-to-one call the other person said no to is over.
      if (chat.members.length === 2 && call.everyone.size === 1) this.callEnd(call, 'Missed call');
      else for (const id of call.members.keys()) this.livePing(this.users.get(id), 'call');
      return okay();
    }
    if (name === 'call.leave') {
      call.members.delete(me.id);
      call.signals = call.signals.filter((s) => s.to !== me.id && s.from !== me.id);
      if (!call.members.size || (call.answered && call.members.size < 2)) this.callEnd(call);   // (nobody left to talk to)
      else for (const id of call.members.keys()) this.livePing(this.users.get(id), 'call');
      return okay();
    }
    if (!call.members.has(me.id)) return name === 'call.poll' ? okay({ call: this.callJson(call, me), signals: [] }) : fail('Join the call first.');
    call.members.get(me.id).seen = t;
    if (name === 'call.signal') {
      const to = typeof args.to === 'string' ? args.to : '', kind = args.kind, sdp = args.sdp;
      if (!call.members.has(to) || to === me.id) return fail('That person isn\'t in the call.');
      if (kind !== 'offer' && kind !== 'answer') return fail('Unknown signal.');
      if (typeof sdp !== 'string' || !sdp || sdp.length > kSignalSize) return fail('That signal is too big.');
      if (call.signals.filter((s) => s.from === me.id).length >= 40) return fail('Too many signals.');
      call.signals.push({ n: ++call.seq, from: me.id, to, kind, sdp, at: t });
      call.signals = call.signals.filter((s) => t - s.at < 60);
      this.livePing(this.users.get(to), 'call');
      return okay();
    }
    if (name === 'call.poll') {
      const after = Number.isFinite(args.after) ? args.after : 0;
      return okay({ call: this.callJson(call, me), signals: call.signals.filter((s) => s.to === me.id && s.n > after).map((s) => ({ n: s.n, from: s.from, kind: s.kind, sdp: s.sdp })) });
    }
    return fail('Unknown request.');
  };
  // call.ice with a TURN relay: ask Cloudflare for short-lived TURN logins (after the answer).
  P.iceFetch = async function (answer) {
    try {
      const r = await fetch('https://rtc.live.cloudflare.com/v1/turn/keys/' + encodeURIComponent(this.env.CF_TURN_KEY_ID) + '/credentials/generate-ice-servers', {
        method: 'POST', headers: { authorization: 'Bearer ' + this.env.CF_TURN_KEY_TOKEN, 'content-type': 'application/json' },
        body: JSON.stringify({ ttl: 3600 }), signal: AbortSignal.timeout(5000) });
      const body = r.ok ? await r.json() : null;
      const servers = body && (Array.isArray(body.iceServers) ? body.iceServers : body.iceServers ? [body.iceServers] : null);
      if (servers && servers.length) return okay({ relay: true, iceServers: servers });
    } catch { /* fall through */ }
    return fail('Couldn\'t get ready for a call right now. Try again in a moment.');
  };

  // --- The Lyreto gateway ------------------------------------------------------------------
  P.bridgeOwnerOp = function (name, me, args) {
    const chat = this.chatOf(me, typeof args.id === 'string' ? args.id : '');
    if (!chat) return fail('That conversation is no longer available.');
    if (chat.owner !== me.id) return fail('Only the chat\'s owner can link it to Lyreto.');
    if (name === 'chat.unbridge') {
      if (chat.bridge) {
        this.bridgeOut.push({ type: 'unlinked', chat: chat.id, channel: chat.bridge.channel });
        delete chat.bridge;
        this.sql.exec('DELETE FROM chatbridges WHERE chat = ?', chat.id);
        this.chatSay(chat, (me.username || me.name) + ' unlinked this chat from Lyreto');
      }
      return okay();
    }
    if (!this.bridgeOn()) return fail('The Lyreto gateway isn\'t turned on yet.');
    for (const [k, v] of this.bridgeCodes) if (v.until < now() || v.chat === chat.id) this.bridgeCodes.delete(k);
    const code = 'GB-' + hex(3).toUpperCase() + '-' + hex(3).toUpperCase();
    this.bridgeCodes.set(code, { chat: chat.id, owner: me.id, until: now() + kBridgeCodeMinutes * 60 });
    return okay({ code, minutes: kBridgeCodeMinutes });
  };
  // A bridged chat, found from the gateway's side (any member's copy).
  P.bridgedChat = function (chatId, channel) {
    const row = [...this.sql.exec('SELECT member FROM chatbridges WHERE chat = ? AND channel = ?', chatId, channel)][0];
    if (!row) return null;
    let chat = this.chatOf(this.users.get(row.member), chatId);
    if (!chat) {   // that member left: anyone still in it
      for (const u of this.users.values()) { chat = this.chatOf(u, chatId); if (chat) break; }
      if (chat) this.sql.exec('UPDATE chatbridges SET member = ? WHERE chat = ?', chat.members[0].id, chatId);
    }
    return chat && chat.bridge && chat.bridge.channel === channel ? chat : null;
  };
  // POST /api/bridge/lyreto: { op, args }, with X-Lyreto-Time and X-Lyreto-Signature (HMAC-SHA256 of "time.body").
  P.bridgeRequest = async function (request, text) {
    if (!this.bridgeOn()) return fail('The Lyreto gateway isn\'t turned on.');
    this.chatsInit();
    const time = Number(request.headers.get('x-lyreto-time')), sig = String(request.headers.get('x-lyreto-signature') || '');
    if (!Number.isFinite(time) || Math.abs(now() - time) > kBridgeSkew) return fail('Bad time.');
    if (!sameText(sig, await hmacHex(this.env.LYRETO_BRIDGE_SECRET, time + '.' + text))) return fail('Bad signature.');
    const nonce = 'lyreto:' + sig;
    if ([...this.sql.exec('SELECT key FROM nonces WHERE key = ?', nonce)].length) return fail('Already done.');
    this.sql.exec('INSERT INTO nonces (key, time) VALUES (?, ?)', nonce, now());
    let req;
    try { req = JSON.parse(text); } catch { return fail('Bad request.'); }
    const args = req && typeof req.args === 'object' && req.args ? req.args : {};
    const r = this.bridgeOp(String(req && req.op || ''), args);
    this.flush();
    return r;
  };
  P.bridgeOp = function (op, args) {
    this.chatsInit();
    const s = (v, n) => (typeof v === 'string' || typeof v === 'number' ? String(v).trim().slice(0, n) : '');
    const channel = s(args.channel, 100);
    if (!channel) return fail('Which channel?');
    if (op === 'bridge.link') {
      const want = this.bridgeCodes.get(s(args.code, 20).toUpperCase());
      if (!want || want.until < now()) return fail('That code is wrong or too old. Make a new one in the Guts&Bolts chat.');
      this.bridgeCodes.delete(s(args.code, 20).toUpperCase());
      const chat = this.chatOf(this.users.get(want.owner), want.chat);
      if (!chat || chat.owner !== want.owner) return fail('That chat isn\'t there any more.');
      const name = say(s(args.name, 60), 60) || 'Lyreto';
      chat.bridge = { app: 'lyreto', channel, name, at: now() };
      this.sql.exec('INSERT OR REPLACE INTO chatbridges (chat, member, channel, name, at) VALUES (?, ?, ?, ?, ?)', chat.id, want.owner, channel, name, now());
      this.chatSay(chat, 'This chat is now linked to the Lyreto channel "' + name + '". Messages here are shared with it.');
      return okay({ chat: chat.id, title: chat.title });
    }
    const chat = this.bridgedChat(s(args.chat, 40), channel);
    if (!chat) return fail('That chat isn\'t linked to this channel.');
    if (op === 'bridge.unlink') {
      delete chat.bridge;
      this.sql.exec('DELETE FROM chatbridges WHERE chat = ?', chat.id);
      this.chatSay(chat, 'This chat was unlinked from Lyreto.');
      return okay();
    }
    if (op === 'bridge.send') {
      const t = now();
      const times = (this.bridgeSent.get(chat.id) || []).filter((x) => x > t - 60);
      if (times.length >= kBridgePerMinute) return fail('Too many messages. Slow down.');
      const body = say(s(args.text, 1000), 1000, true);
      if (!body) return fail('Write a message first.');
      const author = args.author && typeof args.author === 'object' ? args.author : {};
      const name = say(s(author.name, 32), 32) || 'Lyreto user';
      times.push(t); this.bridgeSent.set(chat.id, times);
      const message = { id: hex(8), from: 'lyreto:' + s(author.id, 64), name, body, at: t, via: 'lyreto' };
      chat.messages = [...(chat.messages || []), message].slice(-200);
      chat.updated = t;
      this.chatSync(chat);
      this.chatPosted(chat, message);
      return okay({ id: message.id });
    }
    return fail('Unknown request.');
  };
  // Send what Guts&Bolts players said to Lyreto (after the request).
  P.bridgeFlush = async function () {
    const batch = this.bridgeOut.splice(0, 50);
    if (!batch.length || !this.bridgeOn()) return;
    const body = JSON.stringify({ events: batch }), time = now();
    try {
      await fetch(this.env.LYRETO_BRIDGE_URL || LYRETO_BRIDGE_URL, { method: 'POST', body, signal: AbortSignal.timeout(5000),
        headers: { 'content-type': 'application/json', 'x-gutsandbolts-time': String(time),
          'x-gutsandbolts-signature': await hmacHex(this.env.LYRETO_BRIDGE_SECRET, time + '.' + body) } });
    } catch { /* Lyreto didn't answer: those messages just don't show there */ }
  };
}
