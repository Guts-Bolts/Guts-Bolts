// Favourite catalog items (a wishlist): a heart on any item's page, Catalog > My Favorites,
// and the "Most Favorited" sort. (src/server/ServerItems.cpp has the same rules.)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const hatFile = Buffer.from(JSON.stringify({ format: 'gbaccessory', version: 1, kind: 'hat',
  node: { id: 1, name: 'Hat', kind: 'Model', children: [] } })).toString('base64');
function hat(s, name) {
  const r = s.call(s.staff, 'upload', { kind: 'hat', name, data: hatFile });
  assert.ok(r.ok, r.error);
  return s.assets.get(r.asset.id);
}

test('you can favourite an item, it counts once, and you can take it back', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const cap = hat(s, 'Cap');
  let r = s.call(bob, 'item.favorite', { id: cap.id, on: true });
  assert.ok(r.ok, r.error);
  assert.equal(r.asset.favorites, 1);
  assert.equal(r.asset.myFavorite, true);
  r = s.call(bob, 'item.favorite', { id: cap.id, on: true });   // (twice doesn't count twice)
  assert.equal(r.asset.favorites, 1);
  // Other people see the count, but it isn't their favourite.
  const amy = s.signUp('amy');
  const info = s.call(amy, 'asset.info', { id: cap.id });
  assert.equal(info.asset.favorites, 1);
  assert.equal(info.asset.myFavorite, false);
  r = s.call(bob, 'item.favorite', { id: cap.id, on: false });
  assert.ok(r.ok, r.error);
  assert.equal(r.asset.favorites, 0);
  assert.equal(r.asset.myFavorite, false);
});

test('My Favorites lists only your favourite items', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const cap = hat(s, 'Cap'), crown = hat(s, 'Crown');
  hat(s, 'Beanie');
  assert.ok(s.call(bob, 'item.favorite', { id: cap.id }).ok);
  assert.ok(s.call(bob, 'item.favorite', { id: crown.id }).ok);
  const r = s.call(bob, 'list', { kind: 'clothing', favorites: true });
  assert.ok(r.ok, r.error);
  assert.deepEqual(r.assets.map((a) => a.name).sort(), ['Cap', 'Crown']);
  assert.equal(s.call(s.signUp('amy'), 'list', { kind: 'clothing', favorites: true }).assets.length, 0);
});

test('"Most Favorited" puts the most hearted items first', () => {
  const s = makeServer();
  const cap = hat(s, 'Cap'), crown = hat(s, 'Crown'), beanie = hat(s, 'Beanie');
  const people = ['a1', 'a2', 'a3'].map((n) => s.signUp(n));
  for (const p of people) assert.ok(s.call(p, 'item.favorite', { id: crown.id }).ok);
  assert.ok(s.call(people[0], 'item.favorite', { id: beanie.id }).ok);
  const r = s.call(people[0], 'list', { kind: 'clothing', sort: 'favorited' });
  const names = r.assets.map((a) => a.name);
  assert.equal(names[0], 'Crown');
  assert.equal(names[1], 'Beanie');
  assert.ok(names.indexOf('Cap') > 1);
  assert.equal(r.assets[0].favorites, 3);
  assert.equal(r.assets.find((a) => a.id === cap.id).favorites, 0);
});

test("guests can't favourite, games and missing items can't be favourited as items", () => {
  const s = makeServer();
  const guest = s.user('c'.repeat(64));
  const cap = hat(s, 'Cap');
  const g = s.call(guest, 'item.favorite', { id: cap.id });
  assert.equal(g.ok, false);
  const bob = s.signUp('bob');
  const game = s.addGame(bob, 'Obby');
  assert.equal(s.call(bob, 'item.favorite', { id: game.id }).ok, false);
  assert.equal(s.call(bob, 'item.favorite', { id: 'nope' }).ok, false);
  // (Favourite games still work, and don't land in My Favorites.)
  assert.ok(s.call(bob, 'game.favorite', { id: game.id }).ok);
  assert.equal(s.call(bob, 'list', { kind: 'clothing', favorites: true }).assets.length, 0);
});

test('deleting an account takes its hearts off the items', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const cap = hat(s, 'Cap');
  assert.ok(s.call(bob, 'item.favorite', { id: cap.id }).ok);
  assert.equal(cap.favorites, 1);
  s.deleteAccount(bob);
  assert.equal(cap.favorites, 0);
});
