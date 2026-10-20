// Auctions: staff put an Exclusive item up for bids, the top bidders win it when time runs out.
// Bolts are held while you're winning and come back straight away when someone outbids you.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

function setup(copies = 2, minBid = 10) {
  const s = makeServer();
  const crown = s.addGame(s.staff, 'Bidding Crown', { kind: 'hat', exclusive: true, price: 0 });
  const r = s.call(s.staff, 'auction.start', { id: crown.id, copies, minBid, hours: 24 });
  assert.ok(r.ok, r.error);
  return { s, crown };
}
const bolts = (s, u) => s.balance(u);

test('only staff start auctions, and only on Exclusive items', () => {
  const s = makeServer();
  const ann = s.signUp('Ann');
  const hat = s.addGame(s.staff, 'Plain Hat', { kind: 'hat', price: 5 });
  const exclusive = s.addGame(s.staff, 'Rare Hat', { kind: 'hat', exclusive: true });
  assert.equal(s.call(ann, 'auction.start', { id: exclusive.id, copies: 1, minBid: 5, hours: 2 }).ok, false);
  assert.match(s.call(s.staff, 'auction.start', { id: hat.id, copies: 1, minBid: 5, hours: 2 }).error, /Exclusive/);
  assert.match(s.call(s.staff, 'auction.start', { id: exclusive.id, copies: 1, minBid: 5, hours: 0 }).error, /hours/);
  assert.ok(s.call(s.staff, 'auction.start', { id: exclusive.id, copies: 1, minBid: 5, hours: 2 }).ok);
  assert.match(s.call(s.staff, 'auction.start', { id: exclusive.id, copies: 1, minBid: 5, hours: 2 }).error, /already/);
  // (Not before it's passed the staff check: nobody else could see it to bid.)
  const pending = s.addGame(s.staff, 'Pending Tee', { kind: 'tshirt', exclusive: true, review: 'pending' });
  assert.match(s.call(s.staff, 'auction.start', { id: pending.id, copies: 1, minBid: 5, hours: 2 }).error, /staff check/);
});

test('a bid holds your Bolts; being outbid gives them back and rings the bell', () => {
  const { s, crown } = setup(1, 10);
  const ann = s.signUp('Ann'), bob = s.signUp('Bob');
  assert.match(s.call(ann, 'auction.bid', { id: crown.id, amount: 9 }).error, /at least 10/);
  assert.ok(s.call(ann, 'auction.bid', { id: crown.id, amount: 20 }).ok);
  assert.equal(bolts(s, ann), 80);
  // Bob has to beat 20.
  assert.match(s.call(bob, 'auction.bid', { id: crown.id, amount: 20 }).error, /at least 21/);
  const r = s.call(bob, 'auction.bid', { id: crown.id, amount: 30 });
  assert.ok(r.ok);
  assert.equal(bolts(s, bob), 70);
  assert.equal(bolts(s, ann), 100);   // all of Ann's back
  assert.equal(ann.notes[0].kind, 'auction');
  assert.match(ann.notes[0].text, /outbid/);
  assert.deepEqual(r.asset.auction.bids.map((b) => b.name), ['Bob']);
  assert.equal(r.asset.auction.need, 31);
  assert.equal(r.asset.auction.myBid, 30);
});

test('raising your own bid only takes the difference', () => {
  const { s, crown } = setup(1, 10);
  const ann = s.signUp('Ann');
  assert.ok(s.call(ann, 'auction.bid', { id: crown.id, amount: 15 }).ok);
  assert.match(s.call(ann, 'auction.bid', { id: crown.id, amount: 15 }).error, /more than your last bid/);
  assert.ok(s.call(ann, 'auction.bid', { id: crown.id, amount: 40 }).ok);
  assert.equal(bolts(s, ann), 60);
  assert.equal(crown.auction.bids.length, 1);
  assert.match(s.call(ann, 'auction.bid', { id: crown.id, amount: 200 }).error, /more Bolts/);
});

test('the top bidders win when time runs out; a tie goes to who bid first', () => {
  const { s, crown } = setup(2, 10);
  const [ann, bob, cat] = ['Ann', 'Bob', 'Cat'].map((n) => s.signUp(n));
  assert.ok(s.call(ann, 'auction.bid', { id: crown.id, amount: 25 }).ok);
  assert.ok(s.call(bob, 'auction.bid', { id: crown.id, amount: 25 }).ok);
  // Two copies, both taken at 25: Cat has to bid 26, which knocks out Bob (he bid 25 last).
  assert.match(s.call(cat, 'auction.bid', { id: crown.id, amount: 25 }).error, /at least 26/);
  assert.ok(s.call(cat, 'auction.bid', { id: crown.id, amount: 26 }).ok);
  assert.equal(bolts(s, bob), 100);
  crown.auction.endsAt = Math.floor(Date.now() / 1000) - 1;
  s.finishAuctions();
  assert.ok(ann.owned.includes(crown.id));
  assert.ok(cat.owned.includes(crown.id));
  assert.ok(!bob.owned.includes(crown.id));
  assert.equal(bolts(s, ann), 75);   // the winning bids are paid
  assert.equal(bolts(s, cat), 74);
  assert.match(ann.notes[0].text, /You won/);
  assert.equal(crown.auction.done, true);
  assert.match(s.call(bob, 'auction.bid', { id: crown.id, amount: 90 }).error, /over/);
  s.finishAuctions();   // (it only hands out once)
  assert.equal(ann.owned.filter((x) => x === crown.id).length, 1);
});

test('a bid in the last minutes adds time, so nobody wins at the last second', () => {
  const { s, crown } = setup(1, 10);
  const ann = s.signUp('Ann');
  const t = Math.floor(Date.now() / 1000);
  crown.auction.endsAt = t + 30;
  assert.ok(s.call(ann, 'auction.bid', { id: crown.id, amount: 10 }).ok);
  assert.ok(crown.auction.endsAt >= t + 299);
});

test('cancelling gives everyone their Bolts back; a running auction stops the item being deleted', () => {
  const { s, crown } = setup(2, 10);
  const ann = s.signUp('Ann');
  assert.ok(s.call(ann, 'auction.bid', { id: crown.id, amount: 50 }).ok);
  assert.match(s.call(s.staff, 'delete', { id: crown.id }).error, /Cancel the auction/);
  assert.equal(s.call(ann, 'auction.cancel', { id: crown.id }).ok, false);
  assert.ok(s.call(s.staff, 'auction.cancel', { id: crown.id }).ok);
  assert.equal(bolts(s, ann), 100);
  assert.equal(crown.auction.cancelled, true);
  s.finishAuctions();
  assert.ok(!ann.owned.includes(crown.id));
});

test('items up for auction show in the catalog\'s auction list, Exclusive or not', () => {
  const { s, crown } = setup(1, 10);
  const ann = s.signUp('Ann');
  assert.ok(!s.call(ann, 'list', { kind: 'clothing' }).assets.some((a) => a.id === crown.id));   // (still Exclusive)
  const list = s.call(ann, 'list', { kind: 'clothing', auctions: true }).assets;
  assert.deepEqual(list.map((a) => a.id), [crown.id]);
  assert.equal(list[0].auction.copies, 1);
  // Buying it is still not possible.
  assert.match(s.call(ann, 'buy', { id: crown.id }).error, /Exclusive/);
});
