// Staff > Numbers: now from the server, history from Supabase, staff only.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp } from './helpers.mjs';

const ENV = { SUPABASE_URL: 'https://pfpzmmsosyetkcoymoct.supabase.co', SUPABASE_SECRET_KEY: 'sb_secret_test_only' };

function withFetch(mock, run) {
  const original = globalThis.fetch;
  globalThis.fetch = mock;
  return Promise.resolve().then(run).finally(() => { globalThis.fetch = original; });
}

test('only staff see the numbers; right-now numbers come from the server', () => {
  const s = makeServer(ENV);
  const player = signUp(s, 'NotStaff');
  assert.equal(s.call(player, 'admin.numbers', {}).ok, false);
  player.lastSeen = Math.floor(Date.now() / 1000);
  s.addGame(player, 'Counted Game');
  const r = s.call(s.staff, 'admin.numbers', {});
  assert.equal(r.ok, true);
  assert(r.now.accounts >= 2);
  assert(r.now.online >= 1);
  assert(r.now.games >= 1);
  assert.deepEqual(r.supabase, { numbers: true });
});

test('history and the week\'s top games come from Supabase, with game names', async () => {
  const s = makeServer(ENV);
  const maker = signUp(s, 'TopMaker');
  const game = s.addGame(maker, 'Most Played');
  await withFetch(async (input) => {
    assert.equal(new URL(input).pathname, '/rest/v1/rpc/gb_staff_numbers');
    return Response.json({ days: [{ day: '2026-11-01', accounts: 8, peakOnline: 5, peakPlaying: 0, plays: 0 }],
      topGames: [{ game: game.id, plays: 7 }, { game: 'gone', plays: 1 }] });
  }, async () => {
    const r = await s.supabaseFollowUp(s.call(s.staff, 'admin.numbers', {}));
    assert.equal(r.days.length, 1);
    assert.deepEqual(r.topGames.map((g) => g.name + ':' + g.plays), ['Most Played:7', '(deleted):1']);
  });
});

test('a reading goes to Supabase at most every 5 minutes', async () => {
  const s = makeServer(ENV);
  const calls = [];
  await withFetch(async (input, init) => { calls.push(JSON.parse(init.body)); return new Response(null, { status: 204 }); }, async () => {
    await s.recordDailyNumbers();
    await s.recordDailyNumbers();
  });
  assert.equal(calls.length, 1);
  assert.match(calls[0].p_day, /^\d{4}-\d{2}-\d{2}$/);
  assert(calls[0].p_accounts >= 1);
});
