// Boltworks Club extras (worker/club-pay.js): a ban for good or a deleted account cancels the
// Stripe membership, people on the early list get the launch discount once, and staff can make
// Boltworks Club items that members of a tier (or higher) get for free.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { sessionParams, stripeWork, handleStripe } from '../stripe.js';
import { clubAtLeast } from '../club-pay.js';
import { makeServer } from './helpers.mjs';

const env = { STRIPE_SECRET_KEY: 'sk_test_x', STRIPE_PRICE_BWC_MONTH: 'price_bwcm', STRIPE_PRICE_BOLTS_500: 'price_b500', STRIPE_COUPON_EARLY: 'EARLY25' };

// Catches the calls the server makes to Stripe, so nothing leaves the test.
async function withFetch(fn) {
  const real = globalThis.fetch, asked = [];
  globalThis.fetch = async (url, init = {}) => {
    asked.push({ url: String(url), method: init.method || 'GET', body: String(init.body || '') });
    return new Response(JSON.stringify({ id: 'x', status: 'canceled', client_secret: 'cs_secret' }));
  };
  try { await fn(asked); } finally { globalThis.fetch = real; }
}
const member = (s, u, tier, sub = 'sub_' + u.userId) =>
  s.stripeEvent({ kind: 'club', user: String(u.userId), sub, customer: 'cus_' + u.userId, tier, at: 1000 });
const item = (s, name, extra = {}) => s.addGame(s.staff, name, Object.assign({ kind: 'hat', price: 300 }, extra));

test('a ban for good cancels the Stripe membership; a ban for some days does not', async () => {
  const s = makeServer({ STRIPE_SECRET_KEY: 'sk_test_x' });
  const bob = s.signUp('Bob'), amy = s.signUp('Amy');
  member(s, bob, 'tbc'); member(s, amy, 'bwc');
  await withFetch(async (asked) => {
    assert.equal(s.call(s.staff, 'admin.ban', { to: String(amy.userId), reason: 'spam', days: 3 }).ok, true);
    assert.equal(s.paidTierOf(amy), 'bwc');
    assert.equal(asked.length, 0);
    assert.equal(s.call(s.staff, 'admin.ban', { to: String(bob.userId), reason: 'spam', days: 0 }).ok, true);
    assert.equal(s.paidTierOf(bob), '');
    await Promise.resolve();
    assert.deepEqual(asked.map((a) => a.method + ' ' + a.url), ['DELETE https://api.stripe.com/v1/subscriptions/sub_' + bob.userId]);
  });
  // Stripe's own "it's cancelled" event later changes nothing.
  assert.equal(s.stripeEvent({ kind: 'club', user: String(bob.userId), sub: 'sub_' + bob.userId, tier: '', at: 999 }).stale, true);
});

test('deleting an account cancels its membership too', async () => {
  const s = makeServer({ STRIPE_SECRET_KEY: 'sk_test_x' });
  const kid = s.signUp('Kid');
  member(s, kid, 'obc');
  await withFetch(async (asked) => {
    s.deleteAccount(kid);
    await Promise.resolve();
    assert.equal(asked.filter((a) => a.method === 'DELETE').length, 1);
  });
  assert.equal(s.paidTierOf(kid), '');
  assert.equal(kid.stripeCustomer, undefined);
  // No Stripe key: the tier still goes, nothing is sent.
  const s2 = makeServer(), other = s2.signUp('Other');
  member(s2, other, 'bwc');
  assert.equal(s2.cancelStripeSubs(other, 'test'), 1);
  assert.equal(s2.paidTierOf(other), '');
});

test('the early-list discount: only for people who registered, only once, only with a coupon set', () => {
  const s = makeServer({ STRIPE_COUPON_EARLY: 'EARLY25' });
  const fan = s.signUp('Fan'), late = s.signUp('Late');
  assert.equal(s.call(fan, 'boltworks.register').ok, true);
  assert.equal(s.call(fan, 'boltworks.status').earlyDiscount, true);
  assert.equal(s.call(late, 'boltworks.status').earlyDiscount, false);
  assert.equal(s.stripeEvent({ kind: 'early', user: String(fan.userId) }).early, true);
  // They used it: no second time.
  s.stripeEvent({ kind: 'club', user: String(fan.userId), sub: 'sub_f', tier: 'bwc', at: 5, early: true });
  assert.equal(s.stripeEvent({ kind: 'early', user: String(fan.userId) }).early, false);
  const s2 = makeServer(), fan2 = s2.signUp('Fan2');
  s2.call(fan2, 'boltworks.register');
  assert.equal(s2.earlyDiscountFor(fan2), false);   // no coupon set up
});

test('checkout adds the coupon for early-list people (tiers only), and the webhook remembers it', async () => {
  const p = sessionParams(env, 'https://gutsandbolts.net', '7', 'bwc_month', true);
  assert.deepEqual(p.discounts, [{ coupon: 'EARLY25' }]);
  assert.equal(p.metadata.gb_early, '1');
  assert.equal(sessionParams(env, 'https://gutsandbolts.net', '7', 'bwc_month', false).discounts, undefined);
  assert.equal(sessionParams(env, 'https://gutsandbolts.net', '7', 'bolts_500', true).discounts, undefined);
  assert.equal(sessionParams({ ...env, STRIPE_COUPON_EARLY: '' }, 'https://gutsandbolts.net', '7', 'bwc_month', true).discounts, undefined);
  const done = { type: 'checkout.session.completed', created: 9, data: { object: { id: 'cs_1', mode: 'subscription', subscription: 'sub_1', customer: 'cus_1', metadata: { gb_user: '7', gb_item: 'bwc_month', gb_early: '1' } } } };
  assert.equal(stripeWork(done, env)[0].early, true);

  const asks = [];
  const deliver = async (e, job) => { asks.push(job); return { ok: true, early: true }; };
  await withFetch(async (asked) => {
    const call = (item) => { const r = new Request('https://gutsandbolts.net/api/stripe/checkout', { method: 'POST', body: JSON.stringify({ userId: '7', item }) }); return handleStripe(r, env, new URL(r.url), deliver); };
    assert.equal((await call('bwc_month')).status, 200);
    assert.match(asked[0].body, /discounts%5B0%5D%5Bcoupon%5D=EARLY25/);
    await call('bolts_500');
    assert.doesNotMatch(asked[1].body, /discounts/);
  });
  assert.deepEqual(asks, [{ kind: 'early', user: '7' }]);   // Bolts packs don't ask
});

test('tiers count upward: OBC members get TBC and BWC items too', () => {
  assert.equal(clubAtLeast('obc', 'bwc'), true);
  assert.equal(clubAtLeast('tbc', 'tbc'), true);
  assert.equal(clubAtLeast('bwc', 'tbc'), false);
  assert.equal(clubAtLeast('', 'bwc'), false);
  assert.equal(clubAtLeast('obc', 'nope'), false);
});

test('staff make a Boltworks Club item: members get it now, new members get it when they join', () => {
  const s = makeServer();
  const bwc = s.signUp('Blue'), obc = s.signUp('Red'), none = s.signUp('Nobody');
  member(s, bwc, 'bwc'); member(s, obc, 'obc');
  const hat = item(s, 'Turbo Cap');
  const own = s.addGame(none, 'My Hat', { kind: 'hat', price: 5 });   // a creator can't make their own item a club item
  assert.match(s.call(none, 'item.edit', { id: own.id, clubGift: 'tbc' }).error, /Only staff/);
  assert.match(s.call(s.staff, 'item.edit', { id: hat.id, clubGift: 'gold' }).error, /bwc, tbc or obc/);
  const r = s.call(s.staff, 'item.edit', { id: hat.id, clubGift: 'tbc' });
  assert.equal(r.ok, true);
  assert.equal(r.clubGiven, 1);
  assert.equal(r.asset.clubGift, 'tbc');
  assert.ok(obc.owned.includes(hat.id));
  assert.ok(!bwc.owned.includes(hat.id));   // BWC is below TBC
  assert.ok(obc.notes.some((n) => n.text === 'Boltworks Club gave you Turbo Cap!' && n.about === hat.id));
  // Someone who joins later gets it with their welcome.
  const late = s.signUp('Late');
  member(s, late, 'tbc');
  assert.ok(late.owned.includes(hat.id));
  // Saving it again doesn't hand it out twice.
  assert.equal(s.call(s.staff, 'item.edit', { id: hat.id, clubGift: 'tbc' }).clubGiven, undefined);
});

test('Boltworks Club items can\'t be bought or gifted; members can claim them; they keep them after leaving', () => {
  const s = makeServer();
  const kid = s.signUp('Kid'), pal = s.signUp('Pal');
  const hat = item(s, 'Blue Bolt');
  s.call(s.staff, 'item.edit', { id: hat.id, clubGift: 'bwc' });
  assert.match(s.call(kid, 'buy', { id: hat.id }).error, /Boltworks Club item/);
  assert.match(s.call(pal, 'gift.buy', { id: hat.id, to: String(kid.userId) }).error, /can't be gifts/);
  assert.match(s.call(kid, 'club.claim', { id: hat.id }).error, /Boltworks Club item/);
  assert.match(s.call(kid, 'club.claim', { id: item(s, 'Plain').id }).error, /isn't a Boltworks Club item/);
  // Joining hands it out by itself; claiming after that says "already".
  member(s, kid, 'bwc', 'sub_k');
  assert.ok(kid.owned.includes(hat.id));
  assert.equal(s.call(kid, 'club.claim', { id: hat.id }).already, true);
  // Membership ends: the item stays.
  s.stripeEvent({ kind: 'club', user: String(kid.userId), sub: 'sub_k', tier: '', at: 2000 });
  assert.equal(s.paidTierOf(kid), '');
  assert.ok(kid.owned.includes(hat.id));
  // A Limited can't be a club item.
  const lim = item(s, 'Rare', { limited: true });
  assert.match(s.call(s.staff, 'item.edit', { id: lim.id, clubGift: 'obc' }).error, /Limited/);
});
