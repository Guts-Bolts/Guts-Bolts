// Gutstober timed items go by asset ID, never by name: a player's own upload called
// "Pumpkin" (like item 140) must not get an off-sale countdown.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { GUTSTOBER_ITEMS } from '../server.js';

test("a player's \"Pumpkin\" upload isn't a timed item", () => {
  const s = makeServer();
  const srv = s.signUp('srv');
  const pants = s.addGame(srv, 'Pumpkin', { kind: 'pants', price: 0 });
  s.timeGutstoberItems();
  assert.equal(pants.timedFor, undefined);
  assert.ok(!pants.offsaleAt);
  assert.equal(s.publicAsset(pants, srv).offsaleAt, 0);
});

test('items timed by the old name check go back on sale', () => {
  const s = makeServer();
  const srv = s.signUp('srv2');
  const pants = s.addGame(srv, 'Pumpkin', { kind: 'pants', timedFor: 'gutstober', offsaleAt: 1793491200 });
  const hat = s.addGame(s.staff, 'Pumpkin', { kind: 'hat', timedFor: 'gutstober', offsaleAt: 1793491200 });
  const staffSet = s.addGame(srv, 'Spooky Shirt', { kind: 'shirt', timedFor: 'set', offsaleAt: 1793491200 });
  s.timeGutstoberItems();
  assert.equal(pants.timedFor, undefined);
  assert.equal(pants.offsaleAt, 0);
  assert.equal(hat.timedFor, 'gutstober', "Guts' own Gutstober items stay timed");
  assert.equal(staffSet.offsaleAt, 1793491200, 'dates staff picked by hand are kept');
});

test('only items on the list are timed, whatever they are called', () => {
  const s = makeServer();
  const hat = s.addGame(s.staff, 'Spooky Hat', { kind: 'hat' });
  const other = s.addGame(s.staff, 'Pumpkin Hat', { kind: 'hat' });
  GUTSTOBER_ITEMS.add(hat.id);
  try {
    s.timeGutstoberItems();
  } finally {
    GUTSTOBER_ITEMS.delete(hat.id);
  }
  assert.equal(hat.timedFor, 'gutstober');
  assert.ok(hat.offsaleAt > 0);
  assert.equal(other.timedFor, undefined, 'the name alone does nothing');
});
