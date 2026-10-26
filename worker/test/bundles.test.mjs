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

// A bundle with a new body, made in Studio's Bundles window: meshes for the rig's parts.
const cube = (extra = {}) => Object.assign({
  v: [-0.5, -0.5, -0.5, 0.5, -0.5, -0.5, 0.5, 0.5, -0.5, -0.5, 0.5, -0.5, -0.5, -0.5, 0.5, 0.5, -0.5, 0.5, 0.5, 0.5, 0.5, -0.5, 0.5, 0.5],
  f: [[0, 3, 2, 1], [4, 5, 6, 7], [0, 1, 5, 4], [2, 3, 7, 6], [1, 2, 6, 5], [0, 4, 7, 3]],
  size: [1, 1, 0.5], pos: [0, 1.5, 0],
}, extra);
const bodyBundle = (s, me, body, meta = {}) => s.call(me, 'upload', { kind: 'bundle', name: 'Robot Body', price: 0,
  data: Buffer.from(JSON.stringify({ format: 'gbbundle', body })).toString('base64'), meta: Object.assign({ body: true }, meta) });
const bodyPart = (s, me, name, part) => s.call(me, 'upload', { kind: 'bodypart', name, price: 0,
  data: Buffer.from(JSON.stringify({ format: 'gbbundle', body: { [part]: cube() } })).toString('base64'), meta: { bodyPart: part } });

test('standalone body parts can be catalogued and equipped separately', () => {
  const s = makeServer(), bob = s.signUp('bob');
  const torso = bodyPart(s, s.staff, 'Steel Torso', 'Torso');
  const arm = bodyPart(s, s.staff, 'Steel Left Arm', 'Left Arm');
  assert.ok(torso.ok, torso.error);
  assert.ok(arm.ok, arm.error);
  assert.equal(torso.asset.kind, 'bodypart');
  assert.equal(torso.asset.meta.bodyPart, 'Torso');
  assert.ok(bob.owned.includes(torso.asset.id) === false);
  assert.ok(s.call(bob, 'buy', { id: torso.asset.id }).ok);
  assert.ok(s.call(bob, 'buy', { id: arm.asset.id }).ok);
  const set = s.call(bob, 'avatar.set', { avatar: { head: [1,2,3], torso: [1,2,3], leftArm: [1,2,3], rightArm: [1,2,3], leftLeg: [1,2,3], rightLeg: [1,2,3], wearing: [torso.asset.id, arm.asset.id] } });
  assert.ok(set.ok, set.error);
  const profile = s.call(bob, 'profile', { id: String(bob.userId) });
  assert.ok(profile.wearing.some((x) => x.id === torso.asset.id && x.meta.bodyPart === 'Torso'));
  assert.ok(profile.wearing.some((x) => x.id === arm.asset.id && x.meta.bodyPart === 'Left Arm'));
});

test('standalone body parts must contain exactly their declared slot and only staff can upload them', () => {
  const s = makeServer(), maker = s.signUp('maker');
  let r = bodyPart(s, maker, 'Unauthorized Torso', 'Torso');
  assert.equal(r.ok, false);
  r = s.call(s.staff, 'upload', { kind: 'bodypart', name: 'Wrong Slot', price: 0,
    data: Buffer.from(JSON.stringify({ format: 'gbbundle', body: { Head: cube(), Torso: cube() } })).toString('base64'), meta: { bodyPart: 'Torso' } });
  assert.equal(r.ok, false);
  assert.match(r.error, /only its selected body part/);
});

test('staff make bundles with a new body, and the body is checked', () => {
  const s = makeServer();
  let r = bodyBundle(s, s.staff, { Torso: cube({ smooth: true, junk: 'x' }), 'Left Arm': cube() });
  assert.ok(r.ok, r.error);
  assert.equal(r.asset.meta.body, true);
  const saved = JSON.parse(new TextDecoder().decode(s.readFile(r.asset.id)));
  assert.deepEqual(Object.keys(saved.body).sort(), ['Left Arm', 'Torso']);
  assert.equal(saved.body.Torso.smooth, true);
  assert.equal(saved.body.Torso.junk, undefined, 'only the mesh is kept');

  const maker = s.signUp('maker');
  r = bodyBundle(s, maker, { Torso: cube() });
  assert.match(r.error, /staff/);
  r = bodyBundle(s, s.staff, { Tail: cube() });
  assert.match(r.error, /Head, Torso, arms and legs/);
  r = bodyBundle(s, s.staff, { Torso: cube({ f: [[0, 1, 99]] }) });
  assert.match(r.error, /mesh looks wrong/);
  r = bodyBundle(s, s.staff, { Torso: cube({ v: [5, 0, 0, 0, 0, 0, 0, 1, 0] }) });
  assert.match(r.error, /mesh looks wrong/);
  r = bodyBundle(s, s.staff, { Torso: cube({ size: [100, 1, 1] }) });
  assert.match(r.error, /size/);
  r = bodyBundle(s, s.staff, {});
  assert.match(r.error, /body looks wrong/);
  r = s.call(s.staff, 'upload', { kind: 'bundle', name: 'X', price: 0, data: '', meta: { body: true } });
  assert.match(r.error, /Bundles window/);
});

test('bundles can\'t be swapped for something else afterwards', () => {
  const s = makeServer();
  const r = bodyBundle(s, s.staff, { Head: cube() });
  assert.ok(r.ok, r.error);
  const u = s.call(s.staff, 'update', { id: r.asset.id, data: Buffer.from('{}').toString('base64') });
  assert.match(u.error, /can't be changed/);
});
