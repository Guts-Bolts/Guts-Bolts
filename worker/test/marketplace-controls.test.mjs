// Marketplace controls: staff off-sale, public catalog preview, and Limited vs Limited U.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test('staff can take an item off sale and restore it without deleting it', () => {
  const s = makeServer();
  const item = s.addGame(s.staff, 'Staff Offsale Hat', { kind: 'hat', price: 10 });
  const buyer = s.signUp('buyer');
  assert.ok(s.call(s.staff, 'item.edit', { id: item.id, staffOffsale: true }).ok);
  assert.equal(s.call(buyer, 'buy', { id: item.id }).ok, false);
  assert.match(s.call(buyer, 'buy', { id: item.id }).error, /off sale/i);
  assert.equal(s.call(buyer, 'item.edit', { id: item.id, staffOffsale: false }).ok, false, 'players cannot change staff controls');
  assert.ok(s.call(s.staff, 'item.edit', { id: item.id, staffOffsale: false }).ok);
  assert.ok(s.call(buyer, 'buy', { id: item.id }).ok);
  assert.ok(s.assets.has(item.id), 'the item remains in the catalog');
});

test('staff public preview applies player visibility rules', () => {
  const s = makeServer();
  const hidden = s.addGame(s.staff, 'Staff Exclusive Hat', { kind: 'hat', exclusive: true });
  const visible = s.addGame(s.staff, 'Public Catalog Hat', { kind: 'hat', price: 10 });
  const staffItems = s.call(s.staff, 'list', { kind: 'clothing' }).assets;
  const publicItems = s.call(s.staff, 'list', { kind: 'clothing', publicView: true }).assets;
  assert.ok(staffItems.some((a) => a.id === hidden.id));
  assert.ok(!publicItems.some((a) => a.id === hidden.id));
  assert.ok(publicItems.some((a) => a.id === visible.id));
  const playerItems = s.call(s.signUp('player'), 'list', { kind: 'clothing' }).assets;
  assert.deepEqual(publicItems.map((a) => a.id).sort(), playerItems.map((a) => a.id).sort());
});

test('standard Limited is labelled and cannot be resold or traded', () => {
  const s = makeServer();
  const owner = s.signUp('limitedowner'), other = s.signUp('otherowner');
  const standard = s.addGame(s.staff, 'Standard Limited', { kind: 'hat', price: 10 });
  const tradable = s.addGame(s.staff, 'Limited U', { kind: 'hat', price: 10 });
  assert.ok(s.call(s.staff, 'item.limited', { id: standard.id, stock: 1, limitedType: 'limited' }).ok);
  assert.ok(s.call(s.staff, 'item.limited', { id: tradable.id, stock: 5, limitedType: 'limited_u' }).ok);
  assert.equal(s.call(owner, 'asset.info', { id: standard.id }).asset.limitedType, 'limited');
  assert.equal(s.call(owner, 'asset.info', { id: tradable.id }).asset.limitedType, 'limited_u');
  assert.ok(s.call(s.staff, 'item.edit', { id: standard.id, staffOffsale: true }).ok);
  const offsale = s.call(owner, 'buy', { id: standard.id });
  assert.match(offsale.error, /off sale/i);
  assert.doesNotMatch(offsale.error, /reseller/i);
  assert.ok(s.call(s.staff, 'item.edit', { id: standard.id, staffOffsale: false }).ok);
  assert.ok(s.call(owner, 'buy', { id: standard.id }).ok);
  const soldOut = s.call(other, 'buy', { id: standard.id });
  assert.match(soldOut.error, /sold out/i);
  assert.doesNotMatch(soldOut.error, /reseller/i);
  assert.ok(s.call(other, 'buy', { id: tradable.id }).ok);
  assert.match(s.call(owner, 'resale.list', { id: standard.id, serial: 1, price: 20 }).error, /not resellable/i);
  const theirTradeInventory = s.call(owner, 'trade.inventory', { user: other.id });
  assert.ok(theirTradeInventory.items.some((item) => item.id === tradable.id));
  assert.ok(!theirTradeInventory.items.some((item) => item.id === standard.id));
  const myTradeInventory = s.call(other, 'trade.inventory', { user: owner.id });
  assert.ok(!myTradeInventory.items.some((item) => item.id === standard.id));
  assert.equal(s.call(owner, 'trade.send', {
    to: other.id, give: [{ id: standard.id, serial: 1 }], get: [{ id: tradable.id, serial: 1 }],
  }).ok, false);
  assert.ok(s.call(other, 'resale.list', { id: tradable.id, serial: 1, price: 20 }).ok);
});

test('existing Limited items without a type remain Limited U for backwards compatibility', () => {
  const s = makeServer();
  const owner = s.signUp('legacyowner');
  const item = s.addGame(s.staff, 'Legacy Limited', {
    kind: 'hat', price: 10, limited: true, stock: 1, sales: 0, copies: [],
  });
  assert.equal(s.call(owner, 'asset.info', { id: item.id }).asset.limitedType, 'limited_u');
});
