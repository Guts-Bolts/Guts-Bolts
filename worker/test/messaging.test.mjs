// MessagingService: messages between the running servers of one game (msg.publish / msg.poll).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { GUEST_OK } from '../server.js';

// A live server of `game` hosted by `host` (what relay.host makes), with `players` joined.
function hostServer(s, host, game, players = 0) {
  const id = 's-' + Math.random().toString(16).slice(2, 10);
  const sess = { id, game: game.id, title: game.name, host: host.id, code: '', priv: false, max: 12,
    created: Math.floor(Date.now() / 1000), control: 'c-' + id, players: new Set(), dedicated: false };
  for (let i = 0; i < players; i++) sess.players.add('p-' + id + '-' + i);
  s.sessions.set(id, sess);
  return sess;
}

test('a message reaches every server of the game, the sender too', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), ben = s.signUp('ben');
  const game = s.addGame(ann, 'Lobby');
  hostServer(s, ann, game);
  hostServer(s, ben, game);
  const a0 = s.call(ann, 'msg.poll', { game: game.id });
  const b0 = s.call(ben, 'msg.poll', { game: game.id });
  assert.ok(a0.ok && a0.server, a0.error);
  assert.equal(a0.messages.length, 0);
  const sent = s.call(ann, 'msg.publish', { game: game.id, topic: 'Shout', message: { text: 'hi', n: 3 } });
  assert.ok(sent.ok, sent.error);
  for (const [who, from] of [[ann, a0.last], [ben, b0.last]]) {
    const r = s.call(who, 'msg.poll', { game: game.id, after: from, topics: ['Shout'] });
    assert.ok(r.ok, r.error);
    assert.equal(r.messages.length, 1);
    assert.equal(r.messages[0].topic, 'Shout');
    assert.deepEqual(r.messages[0].message, { text: 'hi', n: 3 });
    assert.equal(typeof r.messages[0].sent, 'number');
    // Asking again from the new place gives nothing more.
    const again = s.call(who, 'msg.poll', { game: game.id, after: r.last });
    assert.equal(again.messages.length, 0);
  }
});

test('other games never see the messages, and topics filter', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), cat = s.signUp('cat');
  const g1 = s.addGame(ann, 'One'), g2 = s.addGame(cat, 'Two');
  hostServer(s, ann, g1);
  hostServer(s, cat, g2);
  assert.ok(s.call(ann, 'msg.publish', { game: g1.id, topic: 'A', message: 1 }).ok);
  assert.ok(s.call(ann, 'msg.publish', { game: g1.id, topic: 'B', message: 2 }).ok);
  const other = s.call(cat, 'msg.poll', { game: g2.id, after: 0 });
  assert.equal(other.messages.length, 0);
  const onlyB = s.call(ann, 'msg.poll', { game: g1.id, after: 0, topics: ['B'] });
  assert.deepEqual(onlyB.messages.map((m) => m.message), [2]);
  assert.equal(onlyB.last, 2);
  // Someone not running a server of g1 can't read or send its messages.
  const spy = s.call(cat, 'msg.poll', { game: g1.id, after: 0 });
  assert.equal(spy.ok, true);
  assert.equal(spy.server, false);
  assert.equal(spy.messages.length, 0);
  const sneak = s.call(cat, 'msg.publish', { game: g1.id, topic: 'A', message: 'x' });
  assert.equal(sneak.ok, false);
  assert.match(sneak.error, /running server/);
});

test('topic names and message sizes are checked', () => {
  const s = makeServer();
  const ann = s.signUp('ann');
  const game = s.addGame(ann, 'Sizes');
  hostServer(s, ann, game);
  assert.match(s.call(ann, 'msg.publish', { game: game.id, topic: '', message: 1 }).error, /1-80/);
  assert.match(s.call(ann, 'msg.publish', { game: game.id, topic: 'x'.repeat(81), message: 1 }).error, /1-80/);
  assert.ok(s.call(ann, 'msg.publish', { game: game.id, topic: 'x'.repeat(80), message: 1 }).ok);
  assert.ok(s.call(ann, 'msg.publish', { game: game.id, topic: 'Big', message: 'a'.repeat(1022) }).ok);   // 1024 with the quotes
  const big = s.call(ann, 'msg.publish', { game: game.id, topic: 'Big', message: 'a'.repeat(1023) });
  assert.equal(big.ok, false);
  assert.match(big.error, /1025 bytes/);
});

test('each server can send 150 + 60 per player a minute', () => {
  const s = makeServer();
  const ann = s.signUp('ann');
  const game = s.addGame(ann, 'Busy');
  hostServer(s, ann, game, 1);   // the host plays too: 2 people
  const allowed = 150 + 60 * 2;
  for (let i = 0; i < allowed; i++) {
    const r = s.call(ann, 'msg.publish', { game: game.id, topic: 'T', message: i });
    assert.ok(r.ok, 'message ' + i + ': ' + r.error);
  }
  const over = s.call(ann, 'msg.publish', { game: game.id, topic: 'T', message: 'one too many' });
  assert.equal(over.ok, false);
  assert.match(over.error, /too many messages/);
  // Polling hands them out a page at a time.
  const first = s.call(ann, 'msg.poll', { game: game.id, after: 0 });
  assert.equal(first.messages.length, 200);
  const rest = s.call(ann, 'msg.poll', { game: game.id, after: first.last });
  assert.equal(rest.messages.length, allowed - 200);
});

test('old messages are dropped after a while, and guests can use it', () => {
  const s = makeServer();
  const ann = s.signUp('ann');
  const game = s.addGame(ann, 'Old');
  hostServer(s, ann, game);
  assert.ok(s.call(ann, 'msg.publish', { game: game.id, topic: 'T', message: 'old' }).ok);
  s.msgs.get(game.id).list[0].sent -= 60;   // a minute ago
  const r = s.call(ann, 'msg.poll', { game: game.id, after: 0 });
  assert.equal(r.messages.length, 0);
  assert.equal(r.last, 1);
  // A server restart (after is past the newest message): start again from the start.
  const fresh = s.call(ann, 'msg.poll', { game: game.id, after: 99 });
  assert.ok(fresh.ok);
  assert.equal(fresh.last, 1);
  // Guests host games too.
  const guest = s.user('c'.repeat(64));
  assert.equal(guest.userId, 0);
  hostServer(s, guest, game);
  assert.ok(s.call(guest, 'msg.poll', { game: game.id }).server);
  assert.ok(GUEST_OK.has('msg.publish') && GUEST_OK.has('msg.poll'));
  assert.ok(s.call(guest, 'msg.publish', { game: game.id, topic: 'T', message: 'hi' }).ok);
});
