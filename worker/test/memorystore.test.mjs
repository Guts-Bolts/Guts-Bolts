// MemoryStoreService: quick data shared by the running servers of one game (mem.*).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { GUEST_OK } from '../server.js';
import { MemoryStore } from '../memorystore.js';

// A live server of `game` hosted by `host` (what relay.host makes), with `players` joined.
function hostServer(s, host, game, players = 0) {
  const id = 's-' + Math.random().toString(16).slice(2, 10);
  const sess = { id, game: game.id, title: game.name, host: host.id, code: '', priv: false, max: 12,
    created: Math.floor(Date.now() / 1000), control: 'c-' + id, players: new Set(), dedicated: false };
  for (let i = 0; i < players; i++) sess.players.add('p-' + id + '-' + i);
  s.sessions.set(id, sess);
  return sess;
}

test('every server of a game sees the same sorted map, in order', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), ben = s.signUp('ben');
  const game = s.addGame(ann, 'Leaderboard');
  hostServer(s, ann, game);
  hostServer(s, ben, game);
  for (const [who, key, score] of [[ann, 'ann', 30], [ben, 'ben', 50], [ann, 'cat', 10], [ben, 'dan', 50]]) {
    const r = s.call(who, 'mem.set', { game: game.id, map: 'Scores', sorted: true, key, value: { score }, sortKey: score, expiration: 600 });
    assert.ok(r.ok, r.error);
  }
  const got = s.call(ben, 'mem.get', { game: game.id, map: 'Scores', sorted: true, key: 'ann' });
  assert.deepEqual([got.found, got.value, got.sortKey], [true, { score: 30 }, 30]);
  const top = s.call(ann, 'mem.range', { game: game.id, map: 'Scores', sorted: true, direction: 'Descending', count: 3 });
  assert.ok(top.ok, top.error);
  assert.deepEqual(top.items.map((i) => i.key), ['dan', 'ben', 'ann']);   // ties: by key
  const low = s.call(ann, 'mem.range', { game: game.id, map: 'Scores', sorted: true, direction: 'Ascending', count: 10, lower: { sortKey: 10 } });
  assert.deepEqual(low.items.map((i) => i.key), ['ann', 'ben', 'dan']);
  // A hash map with the same name is a different thing.
  assert.equal(s.call(ann, 'mem.get', { game: game.id, map: 'Scores', key: 'ann' }).found, false);
  assert.ok(s.call(ann, 'mem.remove', { game: game.id, map: 'Scores', sorted: true, key: 'ann' }).ok);
  assert.equal(s.call(ben, 'mem.get', { game: game.id, map: 'Scores', sorted: true, key: 'ann' }).found, false);
});

test('only a running server of the game can use it; others are told to keep it to themselves', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), cat = s.signUp('cat');
  const game = s.addGame(ann, 'Private');
  const r = s.call(cat, 'mem.get', { game: game.id, map: 'M', key: 'k' });
  assert.equal(r.ok, false);
  assert.equal(r.local, true);
  assert.match(r.error, /running server/);
  assert.ok(GUEST_OK.has('mem.set') && GUEST_OK.has('mem.qread'));   // (guests can host games too)
});

test('values need an expiration, and go away after it', () => {
  const m = new MemoryStore();
  assert.match(m.run('mem.set', { map: 'M', key: 'k', value: 1 }, 100).error, /expiration/);
  assert.match(m.run('mem.set', { map: 'M', key: 'k', value: 1, expiration: 46 * 86400 }, 100).error, /45 days/);
  assert.ok(m.run('mem.set', { map: 'M', key: 'k', value: 1, expiration: 10 }, 100).ok);
  assert.equal(m.run('mem.get', { map: 'M', key: 'k' }, 109).found, true);
  assert.equal(m.run('mem.get', { map: 'M', key: 'k' }, 111).found, false);
  assert.equal(m.empty, true);
  assert.equal(m.used, 0);
});

test('a game has 64 KB + 1 KB per player', () => {
  const m = new MemoryStore();
  const big = 'x'.repeat(30 * 1024);
  assert.ok(m.run('mem.set', { map: 'M', key: 'a', value: big, expiration: 60 }, 0).ok);
  assert.ok(m.run('mem.set', { map: 'M', key: 'b', value: big, expiration: 60 }, 0).ok);
  assert.match(m.run('mem.set', { map: 'M', key: 'c', value: big, expiration: 60 }, 0).error, /full/);
  m.players = 40;   // 104 KB
  assert.ok(m.run('mem.set', { map: 'M', key: 'c', value: big, expiration: 60 }, 0).ok);
  assert.match(m.run('mem.set', { map: 'M', key: 'd', value: 'x'.repeat(33 * 1024), expiration: 60 }, 0).error, /too big/);
  // Replacing a value only counts the difference.
  assert.ok(m.run('mem.set', { map: 'M', key: 'c', value: big, expiration: 60 }, 0).ok);
});

test('queues: highest priority first, read items hide until removed or timed out', () => {
  const m = new MemoryStore();
  for (const [value, priority] of [['a', 0], ['b', 5], ['c', 0]])
    assert.ok(m.run('mem.qadd', { queue: 'Match', value, priority, expiration: 600 }, 0).ok);
  const r1 = m.run('mem.qread', { queue: 'Match', count: 2, invisible: 30 }, 0);
  assert.deepEqual(r1.items, ['b', 'a']);
  const r2 = m.run('mem.qread', { queue: 'Match', count: 2 }, 1);
  assert.deepEqual(r2.items, ['c']);   // the others are hidden
  assert.deepEqual(m.run('mem.qread', { queue: 'Match', count: 2, all: true }, 1).items, []);   // all or nothing
  assert.ok(m.run('mem.qremove', { queue: 'Match', id: r1.id }, 2).ok);
  // r2 wasn't removed: it comes back after 30 seconds.
  assert.deepEqual(m.run('mem.qread', { queue: 'Match', count: 5 }, 40).items, ['c']);
});
