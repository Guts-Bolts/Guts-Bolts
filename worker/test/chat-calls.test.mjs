// The chat panel: inbox, Do Not Disturb, voice calls and the Lyreto gateway (worker/chats.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { hmacHex } from '../chats.js';

const SECRET = 'test-bridge-secret-0123456789';

function setup(env = {}) {
  const s = makeServer(env);
  const alice = s.signUp('Alice'), bob = s.signUp('Bob'), carol = s.signUp('Carol'), dan = s.signUp('Dan');
  for (const [a, b] of [[alice, bob], [alice, carol], [bob, carol], [alice, dan]]) { a.friends.push(b.id); b.friends.push(a.id); s.saveUser(a, b); }
  return { s, alice, bob, carol, dan };
}
// A chat with everyone in it (invited and accepted).
function chatWith(s, owner, others, title = '') {
  const made = s.call(owner, 'party.create', { title, members: others.map((u) => u.id) });
  assert.ok(made.ok, made.error);
  for (const u of others) assert.ok(s.call(u, 'party.respond', { id: made.chat.id, accept: true }).ok);
  return made.chat.id;
}

test('the inbox sums chats up: last message, unread count, newest first', () => {
  const { s, alice, bob, carol } = setup();
  const one = chatWith(s, alice, [bob]);
  const two = chatWith(s, alice, [bob, carol], 'Builders');
  for (const u of [alice, bob, carol]) for (const c of u.chats) c.readAt[alice.id] = 1;   // (Alice read them long ago)
  s.call(bob, 'party.send', { id: one, body: 'hey' });
  s.call(bob, 'party.send', { id: one, body: 'you there?' });
  s.call(carol, 'party.send', { id: two, body: 'new map is up' });
  const r = s.call(alice, 'chat.inbox', {});
  assert.ok(r.ok, r.error);
  assert.deepEqual(r.chats.map((c) => c.id), [two, one]);
  const dm = r.chats.find((c) => c.id === one);
  assert.equal(dm.unread, 2);
  assert.equal(dm.last.body, 'you there?');
  assert.equal(dm.last.name, 'Bob');
  s.call(alice, 'party.read', { id: one });
  assert.equal(s.call(alice, 'chat.inbox', {}).chats.find((c) => c.id === one).unread, 0);
  assert.equal(JSON.stringify(r).includes('messages'), false, 'the inbox doesn\'t send whole histories');
});

test('Do Not Disturb: friends see it, and calls don\'t ring', () => {
  const { s, alice, bob } = setup();
  const dm = chatWith(s, alice, [bob]);
  const on = s.call(bob, 'presence.set', { dnd: true });
  assert.ok(on.ok && on.me.dnd);
  bob.lastSeen = Math.floor(Date.now() / 1000);
  assert.equal(s.call(alice, 'friends.list', {}).friends.find((f) => f.id === bob.id).dnd, true);
  assert.match(s.call(alice, 'call.start', { id: dm }).error, /Do Not Disturb/);
  assert.match(s.call(bob, 'call.start', { id: dm }).error, /Turn off Do Not Disturb/);
  s.call(bob, 'presence.set', { dnd: false });
  assert.ok(s.call(alice, 'call.start', { id: dm }).ok);
});

test('a call: ring, answer, swap offers, hang up', () => {
  const { s, alice, bob } = setup();
  const dm = chatWith(s, alice, [bob]);
  const started = s.call(alice, 'call.start', { id: dm });
  assert.ok(started.ok, started.error);
  assert.equal(s.call(bob, 'chat.inbox', {}).chats[0].call.ringing, true);
  assert.equal(s.call(alice, 'chat.inbox', {}).chats[0].call.ringing, false);
  const joined = s.call(bob, 'call.join', { id: dm });
  assert.ok(joined.ok, joined.error);
  assert.deepEqual(joined.peers, [alice.id], 'the one joining calls everyone already in');
  assert.ok(s.call(bob, 'call.signal', { id: dm, to: alice.id, kind: 'offer', sdp: 'v=0 offer' }).ok);
  const got = s.call(alice, 'call.poll', { id: dm, after: 0 });
  assert.deepEqual(got.signals.map((x) => [x.from, x.kind, x.sdp]), [[bob.id, 'offer', 'v=0 offer']]);
  assert.equal(s.call(alice, 'call.poll', { id: dm, after: got.signals[0].n }).signals.length, 0);
  assert.ok(s.call(alice, 'call.signal', { id: dm, to: bob.id, kind: 'answer', sdp: 'v=0 answer' }).ok);
  assert.equal(s.call(bob, 'call.poll', { id: dm, after: 0 }).signals[0].kind, 'answer');
  // Not to people outside the call, nothing odd, nothing huge.
  assert.match(s.call(bob, 'call.signal', { id: dm, to: 'someone', kind: 'offer', sdp: 'x' }).error, /isn't in the call/);
  assert.match(s.call(bob, 'call.signal', { id: dm, to: alice.id, kind: 'eval', sdp: 'x' }).error, /Unknown/);
  assert.match(s.call(bob, 'call.signal', { id: dm, to: alice.id, kind: 'offer', sdp: 'x'.repeat(16001) }).error, /too big/);
  s.call(alice, 'call.leave', { id: dm });
  s.call(bob, 'call.leave', { id: dm });
  assert.equal(s.call(alice, 'chat.inbox', {}).chats[0].call, null);
  const said = s.call(alice, 'party.read', { id: dm }).chat.messages.filter((m) => m.system).map((m) => m.body);
  assert.match(said[0], /started a call/);
  assert.match(said[1], /Call ended/);
});

test('declining a one-to-one call ends it as missed; outsiders can\'t join', () => {
  const { s, alice, bob, carol } = setup();
  const dm = chatWith(s, alice, [bob]);
  s.call(alice, 'call.start', { id: dm });
  assert.match(s.call(carol, 'call.join', { id: dm }).error, /no longer available/);
  assert.ok(s.call(bob, 'call.decline', { id: dm }).ok);
  assert.equal(s.call(alice, 'chat.inbox', {}).chats[0].call, null);
  assert.equal(s.call(alice, 'party.read', { id: dm }).chat.messages.at(-1).body, 'Missed call');
});

test('without a relay, everyone in a call must be friends with each other', () => {
  const { s, alice, bob, dan } = setup();   // bob and dan aren't friends
  const group = chatWith(s, alice, [bob, dan]);
  assert.ok(s.call(alice, 'call.start', { id: group }).ok);
  assert.ok(s.call(bob, 'call.join', { id: group }).ok);
  assert.match(s.call(dan, 'call.join', { id: group }).error, /friends/);
  assert.equal(s.call(dan, 'call.ice', {}).relay, false);
});

test('with a TURN relay, friends of friends can share a call', () => {
  const { s, alice, bob, dan } = setup({ CF_TURN_KEY_ID: 'k', CF_TURN_KEY_TOKEN: 't' });
  const group = chatWith(s, alice, [bob, dan]);
  s.call(alice, 'call.start', { id: group });
  s.call(bob, 'call.join', { id: group });
  assert.ok(s.call(dan, 'call.join', { id: group }).ok);
  const ice = s.call(dan, 'call.ice', {});
  assert.ok(ice.ok && ice.relay);
  assert.equal(JSON.stringify(ice), '{"ok":true,"relay":true}', 'the TURN login comes after, never from here');
});

test('a block stops calls', () => {
  const { s, alice, bob } = setup();
  const dm = chatWith(s, alice, [bob]);
  const b = s.call(bob, 'block.add', { user: alice.id });
  assert.ok(b.ok, b.error);
  assert.match(s.call(alice, 'call.start', { id: dm }).error, /block/);
});

test('the Lyreto gateway: link with a code, messages both ways, signatures checked', async () => {
  const { s, alice, bob } = setup({ LYRETO_BRIDGE_SECRET: SECRET });
  const group = chatWith(s, alice, [bob], 'Crew');
  assert.match(s.call(bob, 'chat.bridge', { id: group }).error, /owner/);
  const { code } = s.call(alice, 'chat.bridge', { id: group });
  assert.match(code, /^GB-[0-9A-F]{6}-[0-9A-F]{6}$/);

  const signed = async (body, secret = SECRET, time = Math.floor(Date.now() / 1000)) => {
    const text = JSON.stringify(body);
    const req = new Request('https://gutsandbolts.net/api/bridge/lyreto', { method: 'POST', body: text,
      headers: { 'x-lyreto-time': String(time), 'x-lyreto-signature': await hmacHex(secret, time + '.' + text) } });
    return s.bridgeRequest(req, text);
  };
  assert.match((await signed({ op: 'bridge.link', args: { code, channel: 'ch1', name: 'general' } }, 'wrong-secret-0123456789')).error, /signature/);
  assert.match((await signed({ op: 'bridge.link', args: { code, channel: 'ch1', name: 'general' } }, SECRET, 1)).error, /time/);
  const linked = await signed({ op: 'bridge.link', args: { code, channel: 'ch1', name: 'general' } });
  assert.ok(linked.ok, linked.error);
  assert.equal(linked.chat, group);
  assert.match((await signed({ op: 'bridge.link', args: { code, channel: 'ch1', name: 'general' } })).error, /Already done/, 'the same request can\'t be sent twice');
  assert.match((await signed({ op: 'bridge.link', args: { code, channel: 'ch1', name: 'general', n: 2 } })).error, /wrong or too old/, 'a code works once');
  assert.equal(s.call(bob, 'chat.inbox', {}).chats[0].bridge.name, 'general');

  // Lyreto -> Guts&Bolts
  const sent = await signed({ op: 'bridge.send', args: { chat: group, channel: 'ch1', author: { id: 'u9', name: 'kermit' }, text: 'hi from lyreto' } });
  assert.ok(sent.ok, sent.error);
  const last = s.call(bob, 'party.read', { id: group }).chat.messages.at(-1);
  assert.deepEqual([last.name, last.body, last.via], ['kermit', 'hi from lyreto', 'lyreto']);
  assert.match((await signed({ op: 'bridge.send', args: { chat: group, channel: 'other', text: 'x' } })).error, /isn't linked/);

  // Guts&Bolts -> Lyreto (signed the same way)
  const seen = [];
  const old = globalThis.fetch;
  globalThis.fetch = async (url, init) => { seen.push({ url, init }); return new Response('{}'); };
  try {
    s.call(alice, 'party.send', { id: group, body: 'hello lyreto' });
    await s.bridgeFlush();
  } finally { globalThis.fetch = old; }
  assert.equal(seen.length, 1);
  const body = JSON.parse(seen[0].init.body);
  assert.equal(body.events[0].message.text, 'hello lyreto');
  assert.equal(body.events[0].channel, 'ch1');
  const t = seen[0].init.headers['x-gutsandbolts-time'];
  assert.equal(seen[0].init.headers['x-gutsandbolts-signature'], await hmacHex(SECRET, t + '.' + seen[0].init.body));
  assert.ok(!seen[0].init.body.includes('lyreto:u9'));

  assert.ok(s.call(alice, 'chat.unbridge', { id: group }).ok);
  assert.equal(s.call(bob, 'chat.inbox', {}).chats[0].bridge, null);
});

test('the gateway is off until its secret is set', () => {
  const { s, alice, bob } = setup();
  const group = chatWith(s, alice, [bob]);
  assert.match(s.call(alice, 'chat.bridge', { id: group }).error, /isn't turned on/);
});



test('cloud Team Create paginates edit events into bounded poll responses', () => {
  const { s, alice, bob } = setup();
  const made = s.call(alice, 'team.create', { snapshot: '{"scene":"empty"}' });
  assert.ok(made.ok, made.error);
  assert.ok(s.call(bob, 'team.join', { code: made.code }).ok);
  for (let i = 0; i < 15; i++) {
    const pushed = s.call(bob, 'team.push', { session: made.session, kind: 'ops', payload: JSON.stringify({ n: i }) });
    assert.ok(pushed.ok, pushed.error);
  }
  const first = s.call(alice, 'team.poll', { session: made.session, after: 0 });
  assert.equal(first.events.length, 12);
  const last = first.events.at(-1).seq;
  const second = s.call(alice, 'team.poll', { session: made.session, after: last });
  assert.equal(second.events.length, 3);
  assert.equal(second.events.at(-1).seq, 15);
  assert.ok(s.call(alice, 'team.leave', { session: made.session }).ok);
});

test('cloud Team Create snapshots compact acknowledged events and force stale clients to resync', () => {
  const { s, alice, bob } = setup();
  const made = s.call(alice, 'team.create', { title: 'Snapshot test', snapshot: '{"scene":"v1"}' });
  assert.ok(made.ok, made.error);
  const joined = s.call(bob, 'team.join', { code: made.code });
  assert.ok(joined.ok, joined.error);
  const event = s.call(bob, 'team.push', { session: made.session, kind: 'ops', payload: '{"change":1}' });
  assert.ok(event.ok, event.error);
  const saved = s.call(alice, 'team.snapshot', { session: made.session, snapshot: '{"scene":"v2"}', upto: event.seq });
  assert.ok(saved.ok, saved.error);
  assert.equal(saved.snapshotSeq, event.seq);
  const stale = s.call(bob, 'team.poll', { session: made.session, after: 0 });
  assert.ok(stale.ok && stale.resync);
  assert.equal(stale.snapshot, '{"scene":"v2"}');
  assert.equal(stale.snapshotSeq, event.seq);
  assert.equal(stale.events.length, 0);
  assert.match(s.call(bob, 'team.snapshot', { session: made.session, snapshot: '{"bad":true}', upto: event.seq }).error, /Only the session host/);
  assert.ok(s.call(alice, 'team.leave', { session: made.session }).ok);
});

test('cloud Team Create: invite, authenticate, sync edits and presence, share voice chat', () => {
  const { s, alice, bob, carol } = setup();
  const made = s.call(alice, 'team.create', { title: 'Cloud build', snapshot: '{"scene":"start"}' });
  assert.ok(made.ok, made.error);
  assert.match(made.code, /^[0-9A-F]{16}$/);
  assert.equal(made.member, 0);
  assert.equal(made.members.length, 1);

  const joined = s.call(bob, 'team.join', { code: made.code });
  assert.ok(joined.ok, joined.error);
  assert.equal(joined.session, made.session);
  assert.equal(joined.chat, made.chat);
  assert.equal(joined.member, 1);
  assert.equal(joined.owner, false);
  const resumed = s.call(alice, 'team.join', { code: made.code });
  assert.ok(resumed.ok, resumed.error);
  assert.equal(resumed.owner, true);
  assert.equal(resumed.member, 0);
  assert.deepEqual(joined.members.map((m) => m.name).sort(), ['Alice', 'Bob']);

  const pushed = s.call(bob, 'team.push', { session: made.session, kind: 'ops', payload: '{"up":[{"id":1}]}' });
  assert.ok(pushed.ok, pushed.error);
  assert.equal(pushed.seq, 1);
  const poll = s.call(alice, 'team.poll', { session: made.session, after: 0 });
  assert.ok(poll.ok, poll.error);
  assert.equal(poll.events.length, 1);
  assert.equal(poll.events[0].account, bob.id);
  assert.equal(poll.events[0].payload, '{"up":[{"id":1}]}');

  const presence = s.call(bob, 'team.presence', { session: made.session, selected: 42, cursor: { x: 1.5, y: 2, z: -3 } });
  assert.ok(presence.ok, presence.error);
  const seen = s.call(alice, 'team.poll', { session: made.session, after: 1 });
  const bobPresence = seen.members.find((m) => m.account === bob.id);
  assert.equal(bobPresence.selected, 42);
  assert.deepEqual(bobPresence.cursor, { x: 1.5, y: 2, z: -3 });

  const said = s.call(bob, 'team.push', { session: made.session, kind: 'chat', payload: 'Can someone review this script?' });
  assert.ok(said.ok, said.error);
  const history = s.call(alice, 'party.read', { id: made.chat });
  assert.equal(history.chat.messages.at(-1).body, 'Can someone review this script?');

  // Team sessions create a normal Guts&Bolts group chat, so the existing WebRTC call
  // operations and the website chat dock can be reused without a second voice system.
  const started = s.call(alice, 'call.start', { id: made.chat });
  assert.ok(started.ok, started.error);
  const call = s.call(bob, 'call.join', { id: made.chat });
  assert.ok(call.ok, call.error);
  assert.deepEqual(call.peers, [alice.id]);

  assert.match(s.call(carol, 'team.poll', { session: made.session, after: 0 }).error, /not a member/);
  assert.match(s.call(carol, 'team.join', { code: 'DEADBEEFDEADBEEF' }).error, /invalid or has expired/);
  assert.match(s.call(bob, 'team.push', { session: made.session, kind: 'ops', payload: 'x'.repeat(65537) }).error, /too large/);
  assert.ok(s.call(bob, 'team.leave', { session: made.session }).ok);
  assert.match(s.call(bob, 'team.poll', { session: made.session, after: 0 }).error, /not a member/);
  assert.ok(s.call(alice, 'team.leave', { session: made.session }).ok);
  assert.match(s.call(alice, 'team.poll', { session: made.session, after: 0 }).error, /ended/);
});

<<<<<<< HEAD
test('cloud Team Create invite codes that look like phone numbers still join', () => {
  const { s, alice, bob } = setup();
  const made = s.call(alice, 'team.create', { title: 'Digits', snapshot: '{"scene":"v1"}' });
  assert.ok(made.ok, made.error);
  // (A code like this used to be deleted by the chat filter, so ~3% of invites failed.)
  const forced = '0438723973EA3F9C';
  s.sql.exec('UPDATE teamsessions SET code = ? WHERE id = ?', forced, made.session);
  const joined = s.call(bob, 'team.join', { code: forced.toLowerCase() });
  assert.ok(joined.ok, joined.error);
=======

test('cloud Team Create treats invite codes and session IDs as opaque protocol tokens', () => {
  const { s, alice, bob } = setup();
  const made = s.call(alice, 'team.create', { snapshot: '{"scene":"opaque-token-test"}' });
  assert.ok(made.ok, made.error);

  // Ten consecutive digits look like a phone number to the user-text filter,
  // but are valid hexadecimal protocol data and must not be sanitized.
  const code = '1234567890ABCDEF';
  s.sql.exec('UPDATE teamsessions SET code = ? WHERE id = ?', code, made.session);
  const joined = s.call(bob, 'team.join', { code });
  assert.ok(joined.ok, joined.error);
  assert.equal(joined.session, made.session);

  const session = 'tc-1234567890abcdef';
  s.sql.exec('UPDATE teamsessions SET id = ? WHERE id = ?', session, made.session);
  s.sql.exec('UPDATE teammembers SET session = ? WHERE session = ?', session, made.session);
  const pushed = s.call(bob, 'team.push', { session, kind: 'ops', payload: '{"change":true}' });
  assert.ok(pushed.ok, pushed.error);
>>>>>>> origin/main
});
