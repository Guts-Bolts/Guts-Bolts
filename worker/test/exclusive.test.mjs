// Exclusive items (hidden from the catalog, can't be bought) and gift boxes (owners get
// another item once the box goes off sale), plus staff giving items away.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const listed = (s, me, id) => s.call(me, 'list', { kind: 'clothing' }).assets.some((a) => a.id === id);

test('Exclusive items are hidden from the catalog and can\'t be bought', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const hat = s.addGame(s.staff, 'Event Hat', { kind: 'hat' });
  assert.ok(listed(s, bob, hat.id));
  assert.equal(s.call(bob, 'item.edit', { id: hat.id, exclusive: true }).ok, false, 'only staff');
  assert.ok(s.call(s.staff, 'item.edit', { id: hat.id, exclusive: true }).ok);
  assert.ok(!listed(s, bob, hat.id), 'gone from the catalog');
  assert.ok(listed(s, s.staff, hat.id), 'staff still see it');
  assert.equal(s.call(bob, 'asset.info', { id: hat.id }).asset.exclusive, true, 'its page still works by link');
  const buy = s.call(bob, 'buy', { id: hat.id });
  assert.equal(buy.ok, false);
  assert.match(buy.error, /Exclusive/);
  // Staff give it out; then it's in their inventory (the Avatar page) like anything else.
  assert.equal(s.call(bob, 'item.give', { id: hat.id, to: 'bob' }).ok, false, 'only staff');
  assert.ok(s.call(s.staff, 'item.give', { id: hat.id, to: String(bob.userId) }).ok);
  assert.ok(bob.owned.includes(hat.id));
  assert.ok(s.call(bob, 'list', { owned: true }).assets.some((a) => a.id === hat.id));
  assert.equal(bob.notes[0].kind, 'gift');
});

test('Exclusive items can\'t be bought as a gift either', () => {
  const s = makeServer();
  const bob = s.signUp('bob'), amy = s.signUp('amy');
  assert.ok(s.call(bob, 'friends.add', { user: amy.userId }).ok);
  assert.ok(s.call(amy, 'friends.accept', { user: bob.userId }).ok);
  const hat = s.addGame(s.staff, 'Event Hat', { kind: 'hat', price: 40 });
  assert.ok(s.call(s.staff, 'item.edit', { id: hat.id, exclusive: true }).ok);
  const r = s.call(bob, 'gift.buy', { id: hat.id, to: amy.userId });
  assert.equal(r.ok, false);
  assert.match(r.error, /Exclusive/);
  assert.ok(!amy.owned.includes(hat.id));
});

test('a gift box gives its owners the hidden item once it goes off sale', () => {
  const s = makeServer();
  const bob = s.signUp('bob'), amy = s.signUp('amy'), late = s.signUp('late');
  const reward = s.addGame(s.staff, 'Secret Hat', { kind: 'hat', exclusive: true });
  const box = s.addGame(s.staff, 'Gift Box', { kind: 'hat' });
  assert.ok(s.call(s.staff, 'item.edit', { id: box.id, giftItem: String(reward.num), offsaleAt: Math.floor(Date.now() / 1000) + 3600 }).ok);
  assert.equal(box.giftItem, reward.id, 'its number works as the ID');
  assert.equal(s.call(bob, 'asset.info', { id: box.id }).asset.giftItem, undefined, 'players don\'t see what\'s inside');
  assert.equal(s.call(s.staff, 'asset.info', { id: box.id }).asset.giftItem.name, 'Secret Hat');
  assert.ok(s.call(bob, 'buy', { id: box.id }).ok);
  assert.ok(s.call(amy, 'buy', { id: box.id }).ok);
  s.openGiftBoxes(true);
  assert.ok(!bob.owned.includes(reward.id), 'not before it goes off sale');
  box.offsaleAt = Math.floor(Date.now() / 1000) - 1;
  s.openGiftBoxes(true);
  assert.ok(bob.owned.includes(reward.id) && amy.owned.includes(reward.id));
  assert.ok(!late.owned.includes(reward.id), 'only owners');
  assert.equal(reward.sales, 2);
  s.openGiftBoxes(true);
  assert.equal(reward.sales, 2, 'once each');
  assert.equal(bob.notes[0].kind, 'gift');
});

test('a Limited gift box gives one per copy, even when the copy is traded', () => {
  const s = makeServer();
  const bob = s.signUp('bob'), amy = s.signUp('amy');
  const reward = s.addGame(s.staff, 'Secret Hat', { kind: 'hat', exclusive: true });
  const box = s.addGame(s.staff, 'Limited Box', { kind: 'hat', limited: true, stock: 5, copies: [{ serial: 1, owner: bob.id, price: 0 }], sales: 1,
    giftItem: reward.id, offsaleAt: 1 });
  bob.owned.push(box.id);
  s.openGiftBoxes(true);
  assert.ok(bob.owned.includes(reward.id));
  box.copies[0].owner = amy.id; amy.owned.push(box.id);   // (as a trade does)
  s.openGiftBoxes(true);
  assert.ok(!amy.owned.includes(reward.id), 'that copy was already opened');
});

test('gift boxes check what they give', () => {
  const s = makeServer();
  const box = s.addGame(s.staff, 'Gift Box', { kind: 'hat' });
  assert.match(s.call(s.staff, 'item.edit', { id: box.id, giftItem: '99999' }).error, /no item/);
  assert.match(s.call(s.staff, 'item.edit', { id: box.id, giftItem: box.id }).error, /itself/);
  assert.ok(s.call(s.staff, 'item.edit', { id: box.id, giftItem: '' }).ok);
});
