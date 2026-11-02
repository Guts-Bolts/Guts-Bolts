// Creator stats: each play is copied to Supabase (when, new or returning; not who), and
// creator.playtimes reads the charts back for your own games only.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp } from './helpers.mjs';

const ENV = { SUPABASE_URL: 'https://pfpzmmsosyetkcoymoct.supabase.co', SUPABASE_SECRET_KEY: 'sb_secret_test_only' };

function withFetch(mock, run) {
  const original = globalThis.fetch;
  globalThis.fetch = mock;
  return Promise.resolve().then(run).finally(() => { globalThis.fetch = original; });
}
const plays = (s) => [...s.sql.exec("SELECT payload FROM supabase_outbox WHERE table_name = 'gb_play_events'")].map((r) => JSON.parse(r.payload));

test('a play is recorded once per visit, new or returning, without who played', () => {
  const s = makeServer(ENV);
  const maker = signUp(s, 'PlayMaker'), fan = signUp(s, 'PlayFan');
  const game = s.addGame(maker, 'Busy Game');
  s.writeFile(game.id, new TextEncoder().encode('{"nodes":[]}'));
  assert.equal(s.call(fan, 'get', { id: game.id }).ok, true);
  assert.equal(s.call(fan, 'get', { id: game.id }).ok, true);
  s.call(maker, 'get', { id: game.id });   // the creator's own plays don't count
  const rows = plays(s);
  assert.equal(rows.length, 2);
  assert.deepEqual(rows.map((r) => r.is_returning).sort(), [false, true]);
  assert(rows.every((r) => r.game_id === game.id && r.creator_id === maker.id && !JSON.stringify(r).includes(fan.id)));
});

test('creator.playtimes only reads your own games, through Supabase', async () => {
  const s = makeServer(ENV);
  const maker = signUp(s, 'PlayMaker2'), other = signUp(s, 'NotTheMaker');
  const game = s.addGame(maker, 'Busy Game 2');
  assert.equal(s.call(other, 'creator.playtimes', { game: game.id }).ok, false);
  assert.deepEqual(s.call(other, 'creator.playtimes', {}), { ok: true, playtimes: null });
  const want = s.call(maker, 'creator.playtimes', {});
  assert.deepEqual(want.supabase, { playtimes: [game.id] });
  await withFetch(async (input, init) => {
    assert.equal(new URL(input).pathname, '/rest/v1/rpc/gb_creator_playtimes');
    assert.deepEqual(JSON.parse(init.body), { p_games: [game.id], p_days: 90 });
    return Response.json({ hours: Array(24).fill(1), new: 3, returning: 2, guests: 0, days: [] });
  }, async () => {
    const r = await s.supabaseFollowUp(s.call(maker, 'creator.playtimes', {}));
    assert.equal(r.playtimes.new, 3);
    assert.equal(r.supabase, undefined);
  });
});
