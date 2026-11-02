// Better search: assets' searchable words go to Supabase (only when they change), and a
// search also brings back Supabase's near matches, checked by the normal catalog rules.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp } from './helpers.mjs';

const ENV = { SUPABASE_URL: 'https://pfpzmmsosyetkcoymoct.supabase.co', SUPABASE_SECRET_KEY: 'sb_secret_test_only' };

function withFetch(mock, run) {
  const original = globalThis.fetch;
  globalThis.fetch = mock;
  return Promise.resolve().then(run).finally(() => { globalThis.fetch = original; });
}
const searchRows = (s) => [...s.sql.exec("SELECT payload FROM supabase_outbox WHERE table_name = 'gb_search_items'")].map((r) => JSON.parse(r.payload));

test('games are sent to search once, again only when their words change, and private ones are marked removed', () => {
  const s = makeServer(ENV);
  const maker = signUp(s, 'SearchMaker');
  const game = s.addGame(maker, 'Zombie Survival', { description: 'Survive the night', genres: ['Horror'] });
  let row = searchRows(s).find((r) => r.id === game.id);
  assert.equal(row.name, 'Zombie Survival');
  assert.equal(row.section, 'game');
  assert.equal(row.creator_name, 'SearchMaker');
  assert.equal(row.removed, false);
  game.plays = 1; s.saveAsset(game);
  s.sql.exec('DELETE FROM supabase_outbox');
  game.plays = 2; s.saveAsset(game);   // one more play doesn't send it again (popularity only counts when it doubles)
  assert.equal(searchRows(s).length, 0);
  game.description = 'Survive the night, with friends'; s.saveAsset(game);
  assert.equal(searchRows(s)[0].description, 'Survive the night, with friends');
  s.sql.exec('DELETE FROM supabase_outbox');
  game.access = 'private'; s.saveAsset(game);
  row = searchRows(s).find((r) => r.id === game.id);
  assert.equal(row.removed, true);
});

test('a search adds Supabase near matches after the plain ones, never private games', async () => {
  const s = makeServer(ENV);
  const maker = signUp(s, 'SearchMaker2'), visitor = signUp(s, 'Searcher');
  const zombie = s.addGame(maker, 'Zombie Survival');
  const hidden = s.addGame(maker, 'Zombie Secret', { access: 'private' });
  const plain = s.call(visitor, 'list', { kind: 'game', query: 'zombe' });
  assert.equal(plain.assets.length, 0, 'the typo finds nothing the old way');
  assert.equal(plain.supabase.search.q, 'zombe');
  Object.defineProperty(plain, 'asker', { value: visitor, enumerable: false });
  await withFetch(async (input, init) => {
    assert.equal(new URL(input).pathname, '/rest/v1/rpc/gb_search');
    assert.deepEqual(JSON.parse(init.body), { p_query: 'zombe', p_section: 'game', p_limit: 100 });
    return Response.json([{ id: hidden.id, score: 2 }, { id: zombie.id, score: 1.4 }, { id: 'gone', score: 1 }]);
  }, async () => {
    const r = await s.supabaseFollowUp(plain);
    assert.deepEqual(r.assets.map((a) => a.name), ['Zombie Survival']);
    assert.equal(r.nearMatches, 1);
    assert.equal(JSON.stringify(r).includes('Zombie Secret'), false);
  });
});

test('search still works when Supabase is down', async () => {
  const s = makeServer(ENV);
  const maker = signUp(s, 'SearchMaker3');
  s.addGame(maker, 'Pizza Delivery');
  const r = s.call(maker, 'list', { kind: 'game', query: 'pizza' });
  Object.defineProperty(r, 'asker', { value: maker, enumerable: false });
  await withFetch(async () => { throw new TypeError('down'); }, async () => {
    const out = await s.supabaseFollowUp(r);
    assert.deepEqual(out.assets.map((a) => a.name), ['Pizza Delivery']);
  });
});

test('without Supabase nothing is queued and search is the plain one', () => {
  const s = makeServer();
  const maker = signUp(s, 'SearchMaker4');
  s.addGame(maker, 'Pizza Delivery');
  assert.equal(searchRows(s).length, 0);
  assert.equal(s.call(maker, 'list', { kind: 'game', query: 'pizza' }).supabase, undefined);
});
