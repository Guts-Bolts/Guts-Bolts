// Game leaderboards: OrderedDataStore:GetSortedAsync (data.sorted) and the boards on a game's
// page, which come from the scores the server copies to Supabase.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp } from './helpers.mjs';

const ENV = { SUPABASE_URL: 'https://pfpzmmsosyetkcoymoct.supabase.co', SUPABASE_SECRET_KEY: 'sb_secret_test_only' };

function withFetch(mock, run) {
  const original = globalThis.fetch;
  globalThis.fetch = mock;
  return Promise.resolve().then(run).finally(() => { globalThis.fetch = original; });
}

test('data.sorted gives a store\'s numbers biggest first, a page at a time, and skips non-numbers', () => {
  const s = makeServer();
  const maker = signUp(s, 'BoardMaker');
  const game = s.addGame(maker, 'Coin Rush');
  const set = (key, value) => assert.equal(s.call(maker, 'data.set', { game: game.id, store: 'Coins', key, value }).ok, true);
  set('a', 10); set('b', 30); set('c', 20); set('d', 'not a number'); set('e', 5.5);
  s.call(maker, 'data.set', { game: game.id, store: 'Other', key: 'z', value: 999 });
  const keys = (r) => r.entries.map((e) => e.key + '=' + e.value);
  assert.deepEqual(keys(s.call(maker, 'data.sorted', { game: game.id, store: 'Coins' })), ['b=30', 'c=20', 'a=10', 'e=5.5']);
  assert.deepEqual(keys(s.call(maker, 'data.sorted', { game: game.id, store: 'Coins', ascending: true, limit: 2 })), ['e=5.5', 'a=10']);
  assert.deepEqual(keys(s.call(maker, 'data.sorted', { game: game.id, store: 'Coins', limit: 2, offset: 2 })), ['a=10', 'e=5.5']);
  assert.deepEqual(keys(s.call(maker, 'data.sorted', { game: game.id, store: 'Coins', min: 10, max: 20 })), ['c=20', 'a=10']);
  const stranger = signUp(s, 'BoardStranger');
  assert.equal(s.call(stranger, 'data.sorted', { game: game.id, store: 'Coins' }).ok, false, 'only the game\'s own servers');
});

test('ordered writes of a public game go to Supabase with the player\'s name; the newest value wins', () => {
  const s = makeServer(ENV);
  const maker = signUp(s, 'BoardMaker2'), player = signUp(s, 'TopPlayer');
  const game = s.addGame(maker, 'Coin Rush 2');
  s.call(maker, 'data.set', { game: game.id, store: 'Coins', key: 'Player_' + player.userId, value: 40, ordered: true });
  s.call(maker, 'data.increment', { game: game.id, store: 'Coins', key: 'Player_' + player.userId, delta: 2, ordered: true });
  s.call(maker, 'data.set', { game: game.id, store: 'Plain', key: 'x', value: 1 });   // not ordered: not copied
  const rows = [...s.sql.exec("SELECT payload FROM supabase_outbox WHERE table_name = 'gb_leaderboard_scores'")].map((r) => JSON.parse(r.payload));
  assert.equal(rows.length, 1);
  assert.equal(rows[0].score, 42);
  assert.equal(rows[0].player_name, 'TopPlayer');
  assert.equal(rows[0].player_user_id, player.userId);
  assert.equal(rows[0].store, 'Coins');
});

test('private games keep their scores off Supabase', () => {
  const s = makeServer(ENV);
  const maker = signUp(s, 'SecretMaker');
  const game = s.addGame(maker, 'Hidden Game', { access: 'private' });
  s.call(maker, 'data.set', { game: game.id, store: 'Coins', key: '1', value: 5, ordered: true });
  assert.equal([...s.sql.exec("SELECT id FROM supabase_outbox WHERE table_name = 'gb_leaderboard_scores'")].length, 0);
});

test('game.leaderboards groups Supabase rows into boards and copes with Supabase being down', async () => {
  const s = makeServer(ENV);
  const maker = signUp(s, 'BoardMaker3');
  const game = s.addGame(maker, 'Coin Rush 3');
  const first = s.call(maker, 'game.leaderboards', { id: game.id });
  assert.deepEqual(first.supabase, { leaderboards: game.id });
  await withFetch(async (input, init) => {
    assert.equal(new URL(input).pathname, '/rest/v1/rpc/gb_leaderboard_top');
    assert.equal(JSON.parse(init.body).p_game, game.id);
    return Response.json([
      { store: 'Coins', rank: 1, player_name: 'Bob', player_user_id: 13, score: 70 },
      { store: 'Coins', rank: 2, player_name: 'Alice', player_user_id: 12, score: 50 },
      { store: 'Wins', rank: 1, player_name: 'Alice', player_user_id: 12, score: 3 },
    ]);
  }, async () => {
    const r = await s.supabaseFollowUp(s.call(maker, 'game.leaderboards', { id: game.id }));
    assert.equal(r.supabase, undefined);
    assert.deepEqual(r.boards.map((b) => b.name + ':' + b.entries.map((e) => e.name).join(',')), ['Coins:Bob,Alice', 'Wins:Alice']);
  });
  await withFetch(async () => { throw new TypeError('network down'); }, async () => {
    const r = await s.supabaseFollowUp(s.call(maker, 'game.leaderboards', { id: game.id }));
    assert.equal(r.ok, true);
    assert.deepEqual(r.boards, []);
  });
  const hidden = s.addGame(maker, 'Hidden', { access: 'private' });
  assert.equal(s.call(signUp(s, 'Nosy'), 'game.leaderboards', { id: hidden.id }).ok, false);
});
