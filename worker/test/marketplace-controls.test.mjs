import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test('staff can take a catalog item off sale and restore it without deleting it', () => {
  const s = makeServer();
  const item = s.addGame(s.staff, 'Staff Offsale Hat', { kind: 'hat', price: 10 });
  const buyer = s.signUp('Buyer');
  assert.ok(s.call(s.staff, 'item.edit', { id: item.id, staffOffsale: true }).ok);
  assert.match(s.call(buyer, 'buy', { id: item.id }).error, /off sale/i);
  assert.ok(s.call(s.staff, 'item.edit', { id: item.id, staffOffsale: false }).ok);
  assert.ok(s.call(buyer, 'buy', { id: item.id }).ok);
});

test('staff public catalog preview hides Exclusive items while staff view retains them', () => {
  const s = makeServer();
  const hidden = s.addGame(s.staff, 'Hidden Staff Item', { kind: 'hat', exclusive: true });
  const visible = s.addGame(s.staff, 'Public Catalog Item', { kind: 'hat', price: 1 });
  const staffView = s.call(s.staff, 'list', { kind: 'clothing' }).assets;
  const publicView = s.call(s.staff, 'list', { kind: 'clothing', publicView: true }).assets;
  assert.ok(staffView.some((a) => a.id === hidden.id));
  assert.ok(!publicView.some((a) => a.id === hidden.id));
  assert.ok(publicView.some((a) => a.id === visible.id));
});

test('standard Limited items cannot be resold', () => {
  const s = makeServer();
  const item = s.addGame(s.staff, 'Non-resellable Limited', { kind: 'hat', price: 10 });
  const buyer = s.signUp('LimitedBuyer');
  assert.ok(s.call(s.staff, 'item.limited', { id: item.id, stock: 2, limitedType: 'limited' }).ok);
  assert.ok(s.call(buyer, 'buy', { id: item.id }).ok);
  assert.match(s.call(buyer, 'resale.list', { id: item.id, serial: 1, price: 20 }).error, /not resellable/i);
});
