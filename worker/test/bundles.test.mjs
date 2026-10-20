// Bundles: packs of catalog items sold together (buying one gives you everything in it), and
// the Headless Head, a bundle that hides your head. Only staff make bundles.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const hatOf = (s, maker, name, extra = {}) => s.addGame(maker, name, Object.assign({ kind: 'hat', price: 40 }, extra));
const bundle = (s, me, meta, price = 50) => s.call(me, 'upload', { kind: 'bundle', name: 'Pirate Pack', price, data: '', meta });

test('the Headless Head is in the catalog for 3100 Bolts, made by Guts', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const r = s.call(bob, 'list', { kind: 'clothing', limit: 100 });
  assert.ok(r.ok, r.error);
  const a = r.assets.find((x) => x.id === 'item-headless-head');
  assert.ok(a, 'it is in the catalog');
  assert.equal(a.kind, 'bundle');
  assert.equal(a.price, 3100);
  assert.equal(a.creator, s.staff.id);
  assert.equal(a.meta.headless, true);
  assert.ok(a.num > 0, 'it has an item number for its page');
  const only = s.call(bob, 'list', { kind: 'bundle', limit: 100 });
  assert.ok(only.assets.some((x) => x.id === 'item-headless-head'), 'the Bundles filter finds it');
});

test('buying the Headless Head costs 3100 and you can wear it', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  let r = s.call(bob, 'buy', { id: 'item-headless-head' });
  assert.equal(r.ok, false, 'new accounts only have 100 Bolts');
  s.add(bob, 3100, 'test', 'test');
  r = s.call(bob, 'buy', { id: 'item-headless-head' });
  assert.ok(r.ok, r.error);
  assert.equal(s.balance(bob), 100);
  assert.ok(bob.owned.includes('item-headless-head'));
  const av = { head: [1, 2, 3], torso: [1, 2, 3], leftArm: [1, 2, 3], rightArm: [1, 2, 3], leftLeg: [1, 2, 3], rightLeg: [1, 2, 3], wearing: ['item-headless-head'] };
  assert.ok(s.call(bob, 'avatar.set', { avatar: av }).ok);
  assert.deepEqual(bob.avatar.wearing, ['item-headless-head']);
  const p = s.call(bob, 'profile', { id: String(bob.userId) });
  assert.ok(p.wearing.some((w) => w.id === 'item-headless-head' && w.meta.headless), 'the profile shows it (so the head is drawn hidden)');
});

test('buying a bundle gives you everything in it', () => {
  const s = makeServer();
  const maker = s.signUp('maker'), bob = s.signUp('bob');
  const hat = hatOf(s, maker, 'Pirate Hat'), shirt = hatOf(s, maker, 'Pirate Shirt', { kind: 'shirt' });
  const r = bundle(s, s.staff, { items: [hat.id, shirt.id, hat.id] });
  assert.ok(r.ok, r.error);
  assert.deepEqual(r.asset.meta.items, [hat.id, shirt.id], 'repeats are dropped');
  assert.ok(s.call(bob, 'buy', { id: r.asset.id }).ok);
  assert.equal(s.balance(bob), 50);
  for (const id of [r.asset.id, hat.id, shirt.id]) assert.ok(bob.owned.includes(id));
  const inv = s.call(bob, 'profile.inventory', { id: String(bob.userId), cat: 'bundles' });
  assert.ok(inv.ok, inv.error);
  assert.equal(inv.total, 1);
  assert.ok(inv.items.some((x) => x.id === r.asset.id), 'it shows under Bundles');
});

test('only staff make bundles, and bundles hold real catalog items', () => {
  const s = makeServer();
  const maker = s.signUp('maker');
  const hat = hatOf(s, maker, 'Pirate Hat');
  let r = bundle(s, maker, { items: [hat.id] });
  assert.equal(r.ok, false);
  assert.match(r.error, /staff/);
  r = bundle(s, s.staff, { items: ['nope'] });
  assert.match(r.error, /no catalog item with ID nope/);
  r = bundle(s, s.staff, { items: [] });
  assert.match(r.error, /at least one item/);
  r = bundle(s, s.staff, { items: ['item-headless-head'] });
  assert.match(r.error, /another bundle/);
  r = bundle(s, s.staff, { items: [], headless: true });
  assert.ok(r.ok, r.error);
  assert.equal(r.asset.meta.headless, true);
});
