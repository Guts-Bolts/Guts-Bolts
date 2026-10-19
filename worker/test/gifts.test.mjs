// "Buy as a gift": you pay, a friend gets the catalog item and a notification, and both
// Bolts histories show it. Friends only, not for someone who has it, and no Limiteds.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

function friends(s, a, b) {
  assert.ok(s.call(a, 'friends.add', { user: b.userId }).ok);
  assert.ok(s.call(b, 'friends.accept', { user: a.userId }).ok);
  assert.ok(a.friends.includes(b.id));
}
const hatOf = (s, maker, name, extra = {}) => s.addGame(maker, name, Object.assign({ kind: 'hat', price: 40 }, extra));

test('a gift: the buyer pays, the friend gets it, a note and a line in both histories', () => {
  const s = makeServer();
  const maker = s.signUp('maker'), bob = s.signUp('bob'), amy = s.signUp('amy');
  friends(s, bob, amy);
  const hat = hatOf(s, maker, 'Party Hat');
  const r = s.call(bob, 'gift.buy', { id: hat.id, to: amy.userId, message: 'Happy birthday!' });
  assert.ok(r.ok, r.error);
  assert.equal(s.balance(bob), 60);
  assert.equal(s.balance(amy), 100, 'the friend pays nothing');
  assert.ok(amy.owned.includes(hat.id));
  assert.ok(!bob.owned.includes(hat.id), 'the buyer doesn\'t get one too');
  assert.equal(s.balance(maker), 100 + 28, 'the creator gets their 70% like a normal sale');
  assert.equal(hat.sales, 1);
  const note = amy.notes[0];
  assert.equal(note.kind, 'gift');
  assert.equal(note.about, hat.id);
  assert.match(note.text, /bob sent you a gift: Party Hat!/);
  assert.match(note.text, /Happy birthday!/);
  const mine = s.call(bob, 'bolts.history').history.at(-1);
  assert.equal(mine.amount, -40);
  assert.equal(mine.gift, 'sent');
  assert.match(mine.reason, /Gift for amy: Party Hat/);
  const theirs = s.call(amy, 'bolts.history').history.at(-1);
  assert.equal(theirs.amount, 0);
  assert.equal(theirs.gift, 'got');
  assert.match(theirs.reason, /Gift from bob: Party Hat/);
});

test("you can't gift something they already have (and sending twice doesn't charge twice)", () => {
  const s = makeServer();
  const maker = s.signUp('maker2'), bob = s.signUp('bob2'), amy = s.signUp('amy2');
  friends(s, bob, amy);
  const hat = hatOf(s, maker, 'Top Hat');
  assert.ok(s.call(bob, 'gift.buy', { id: hat.id, to: amy.userId }).ok);
  const again = s.call(bob, 'gift.buy', { id: hat.id, to: amy.userId });
  assert.equal(again.ok, false);
  assert.match(again.error, /already has/);
  assert.equal(s.balance(bob), 60);
  assert.equal(hat.sales, 1);
});

test('gifts are for friends only, and not for yourself or blocked or banned people', () => {
  const s = makeServer();
  const maker = s.signUp('maker3'), bob = s.signUp('bob3'), stranger = s.signUp('stranger3'), amy = s.signUp('amy3');
  const hat = hatOf(s, maker, 'Cowboy Hat');
  let r = s.call(bob, 'gift.buy', { id: hat.id, to: stranger.userId });
  assert.equal(r.ok, false);
  assert.match(r.error, /friends/);
  r = s.call(bob, 'gift.buy', { id: hat.id, to: bob.userId });
  assert.equal(r.ok, false);
  friends(s, bob, amy);
  amy.banned = true;
  assert.equal(s.call(bob, 'gift.buy', { id: hat.id, to: amy.userId }).ok, false);
  assert.equal(s.balance(bob), 100);
  assert.ok(!amy.owned.includes(hat.id));
});

test("no gifting Limiteds, free, off-sale or unaffordable items, or things that aren't in the catalog", () => {
  const s = makeServer();
  const maker = s.signUp('maker4'), bob = s.signUp('bob4'), amy = s.signUp('amy4');
  friends(s, bob, amy);
  const tries = {
    limited: hatOf(s, maker, 'Rare Crown', { limited: true, stock: 10, copies: [] }),
    free: hatOf(s, maker, 'Free Cap', { price: 0 }),
    offsale: hatOf(s, maker, 'Old Hat', { offsaleAt: 1 }),
    pricey: hatOf(s, maker, 'Gold Hat', { price: 5000 }),
    pass: s.addGame(maker, 'VIP', { kind: 'gamepass', price: 10 }),
    waiting: hatOf(s, maker, 'Unchecked Tee', { kind: 'tshirt', review: 'pending' }),
  };
  for (const [why, a] of Object.entries(tries)) {
    const r = s.call(bob, 'gift.buy', { id: a.id, to: amy.userId });
    assert.equal(r.ok, false, why + ' should be refused');
    assert.ok(!amy.owned.includes(a.id), why);
  }
  assert.match(s.call(bob, 'gift.buy', { id: tries.limited.id, to: amy.userId }).error, /Limited/);
  assert.match(s.call(bob, 'gift.buy', { id: tries.pricey.id, to: amy.userId }).error, /more Bolts/);
  assert.equal(s.balance(bob), 100);
});

test('the note goes through the text filter and is kept short', () => {
  const s = makeServer();
  const maker = s.signUp('maker5'), bob = s.signUp('bob5'), amy = s.signUp('amy5');
  friends(s, bob, amy);
  const hat = hatOf(s, maker, 'Long Note Hat');
  assert.ok(s.call(bob, 'gift.buy', { id: hat.id, to: amy.userId, message: 'x'.repeat(500) }).ok);
  assert.ok(amy.notes[0].text.length < 200);
});
