// Wishlist alerts: an item you favourited gets cheaper or comes back on sale, and you get a
// note on the bell. (src/server/ServerItems.cpp wishlistAlert has the same rules, and
// tests/ServerWishlistAlertTests.cpp checks it.)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const hatFile = Buffer.from(JSON.stringify({ format: 'gbaccessory', version: 1, kind: 'hat',
  node: { id: 1, name: 'Hat', kind: 'Model', children: [] } })).toString('base64');
function hat(s, name, price) {
  const r = s.call(s.staff, 'upload', { kind: 'hat', name, data: hatFile, price });
  assert.ok(r.ok, r.error);
  return s.assets.get(r.asset.id);
}
const wishNotes = (u) => (u.notes || []).filter((n) => n.kind === 'wishlist');

test('a price drop rings everyone who favourited it (but not a price rise)', () => {
  const s = makeServer();
  const cap = hat(s, 'Cap', 100);
  const bob = s.signUp('bob'), amy = s.signUp('amy'), cat = s.signUp('cat');
  assert.ok(s.call(bob, 'item.favorite', { id: cap.id }).ok);
  assert.ok(s.call(amy, 'item.favorite', { id: cap.id }).ok);
  assert.ok(s.call(s.staff, 'item.edit', { id: cap.id, price: 150 }).ok);
  assert.equal(wishNotes(bob).length, 0, 'dearer: nobody rung');
  assert.ok(s.call(s.staff, 'item.edit', { id: cap.id, price: 60 }).ok);
  assert.equal(wishNotes(bob).length, 1);
  assert.equal(wishNotes(amy).length, 1);
  assert.equal(wishNotes(cat).length, 0, 'not favourited: not rung');
  assert.match(wishNotes(bob)[0].text, /Cap from your favourites dropped from 150 to 60 Bolts/);
  assert.equal(wishNotes(bob)[0].about, cap.id);
  assert.equal(wishNotes(s.staff).length, 0, 'the person who changed it isn\'t rung');
  // Just renaming it rings nobody.
  assert.ok(s.call(s.staff, 'item.edit', { id: cap.id, name: 'Cool Cap' }).ok);
  assert.equal(wishNotes(bob).length, 1);
  // Free counts as a drop.
  assert.ok(s.call(s.staff, 'item.edit', { id: cap.id, price: 0 }).ok);
  assert.match(wishNotes(bob)[0].text, /Cool Cap from your favourites is free now/);
});

test('coming back on sale rings people, while off sale nothing does', () => {
  const s = makeServer();
  const cap = hat(s, 'Cap', 100);
  const bob = s.signUp('bob');
  assert.ok(s.call(bob, 'item.favorite', { id: cap.id }).ok);
  assert.ok(s.call(s.staff, 'item.edit', { id: cap.id, staffOffsale: true }).ok);
  assert.ok(s.call(s.staff, 'item.edit', { id: cap.id, price: 10 }).ok);
  assert.equal(wishNotes(bob).length, 0, 'cheaper but still off sale');
  assert.ok(s.call(s.staff, 'item.edit', { id: cap.id, staffOffsale: false }).ok);
  assert.equal(wishNotes(bob).length, 1);
  assert.match(wishNotes(bob)[0].text, /Cap from your favourites is back on sale \(10 Bolts\)/);
});

test('people who own it already, or took the favourite back, aren\'t rung', () => {
  const s = makeServer();
  const cap = hat(s, 'Cap', 100);
  const bob = s.signUp('bob'), amy = s.signUp('amy');
  assert.ok(s.call(bob, 'item.favorite', { id: cap.id }).ok);
  assert.ok(s.call(amy, 'item.favorite', { id: cap.id }).ok);
  assert.ok(s.call(amy, 'item.favorite', { id: cap.id, on: false }).ok);
  bob.owned.push(cap.id);
  assert.ok(s.call(s.staff, 'item.edit', { id: cap.id, price: 50 }).ok);
  assert.equal(wishNotes(bob).length, 0);
  assert.equal(wishNotes(amy).length, 0);
});
