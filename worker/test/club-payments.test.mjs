// Paying for Boltworks Club tiers and Bolts packs (worker/stripe.js + worker/club-pay.js):
// the right Stripe settings per item, what each webhook event turns into, and the server
// giving the tier or the Bolts exactly once.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { sessionParams, handleStripe, stripeWork, priceOf, itemOfPrice, ITEMS } from '../stripe.js';
import { returnUrl } from '../club-pay.js';
import { makeServer } from './helpers.mjs';

const env = {
  STRIPE_SECRET_KEY: 'sk_test_x', STRIPE_PUBLISHABLE_KEY: 'pk_test_y', STRIPE_WEBHOOK_SECRET: 'whsec_z',
  STRIPE_PRICE_BWC_MONTH: 'price_bwcm', STRIPE_PRICE_TBC_MONTH: 'price_tbcm', STRIPE_PRICE_OBC_YEAR: 'price_obcy',
  STRIPE_PRICE_BOLTS_500: 'price_b500',
};
const post = (path, body) => { const r = new Request('https://gutsandbolts.net' + path, { method: 'POST', body: JSON.stringify(body) }); return [r, new URL(r.url)]; };

test('every item has its own price setting, and the old STRIPE_PRICE_ID still means monthly BWC', () => {
  assert.equal(priceOf(env, 'tbc_month'), 'price_tbcm');
  assert.equal(priceOf(env, 'tbc_year'), '');
  assert.equal(priceOf({ STRIPE_PRICE_ID: 'price_old' }, 'bwc_month'), 'price_old');
  assert.equal(itemOfPrice(env, 'price_obcy'), 'obc_year');
  assert.equal(itemOfPrice(env, 'price_nope'), '');
  assert.equal(new Set(Object.values(ITEMS).map((i) => i.env)).size, Object.keys(ITEMS).length);   // no two share a setting
  assert.deepEqual(Object.values(ITEMS).filter((i) => i.bolts).map((i) => i.bolts), [200, 500, 1100, 2400, 6500]);
});

test('a tier is a subscription; a Bolts pack is paid once', () => {
  const club = sessionParams(env, 'https://gutsandbolts.net', '7', 'tbc_month');
  assert.equal(club.mode, 'subscription');
  assert.deepEqual(club.line_items, [{ price: 'price_tbcm', quantity: 1 }]);
  assert.deepEqual(club.subscription_data, { metadata: { gb_user: '7', gb_item: 'tbc_month' } });
  assert.equal(club.payment_method_collection, 'always');
  const pack = sessionParams(env, 'https://gutsandbolts.net', '7', 'bolts_500');
  assert.equal(pack.mode, 'payment');
  assert.deepEqual(pack.metadata, { gb_user: '7', gb_item: 'bolts_500' });
  assert.equal(pack.subscription_data, undefined);
  assert.equal(pack.payment_method_collection, undefined);
});

test('checkout says which price is missing, and needs a logged-in user', async () => {
  const [y, uy] = post('/api/stripe/checkout', { userId: '7', item: 'bwc_year' });
  let r = await handleStripe(y, env, uy);
  assert.equal(r.status, 503);
  assert.match((await r.json()).error, /STRIPE_PRICE_BWC_YEAR/);
  const [a, u] = post('/api/stripe/checkout', { userId: '7', item: 'free_robux' });
  assert.equal((await handleStripe(a, env, u)).status, 400);
  const [b, u2] = post('/api/stripe/checkout', { item: 'bolts_500' });
  r = await handleStripe(b, env, u2);
  assert.equal(r.status, 400);
  assert.match((await r.json()).error, /Log in/);
});

test('the prices list only has what is set up, with Stripe\'s real amounts', async () => {
  const real = globalThis.fetch;
  const asked = [];
  globalThis.fetch = async (url) => {
    asked.push(String(url));
    const id = String(url).split('/').pop();
    return new Response(JSON.stringify({ id, active: id !== 'price_obcy', unit_amount: id === 'price_b500' ? 499 : 1199, currency: 'usd' }));
  };
  try {
    const items = (await (await handleStripe(new Request('https://gutsandbolts.net/api/stripe/prices'), env, new URL('https://gutsandbolts.net/api/stripe/prices'))).json()).items;
    assert.deepEqual(Object.keys(items).sort(), ['bolts_500', 'bwc_month', 'tbc_month']);   // obc_year is archived in Stripe
    assert.deepEqual(items.bolts_500, { bolts: 500, amount: 499, currency: 'usd' });
    assert.deepEqual(items.tbc_month, { tier: 'tbc', every: 'month', amount: 1199, currency: 'usd' });
    assert.ok(asked.every((u) => u.startsWith('https://api.stripe.com/v1/prices/')));
  } finally { globalThis.fetch = real; }
  const none = await (await handleStripe(new Request('https://gutsandbolts.net/api/stripe/prices'), {}, new URL('https://gutsandbolts.net/api/stripe/prices'))).json();
  assert.deepEqual(none.items, {});
});

const ev = (type, object, created = 1000) => ({ type, created, data: { object } });

test('webhook events turn into jobs: Bolts once paid, tiers on and off', () => {
  assert.deepEqual(stripeWork(ev('checkout.session.completed', { id: 'cs_1', mode: 'payment', payment_status: 'paid', metadata: { gb_user: '7', gb_item: 'bolts_500' } }), env),
    [{ kind: 'bolts', user: '7', bolts: 500, ref: 'stripe:cs_1' }]);
  // Not paid yet (a slow bank payment): nothing until async_payment_succeeded.
  assert.deepEqual(stripeWork(ev('checkout.session.completed', { id: 'cs_2', mode: 'payment', payment_status: 'unpaid', metadata: { gb_user: '7', gb_item: 'bolts_500' } }), env), []);
  assert.equal(stripeWork(ev('checkout.session.async_payment_succeeded', { id: 'cs_2', mode: 'payment', payment_status: 'paid', metadata: { gb_user: '7', gb_item: 'bolts_500' } }), env)[0].bolts, 500);
  assert.deepEqual(stripeWork(ev('checkout.session.completed', { id: 'cs_3', mode: 'subscription', subscription: 'sub_1', customer: 'cus_1', metadata: { gb_user: '7', gb_item: 'tbc_month' } }), env),
    [{ kind: 'club', user: '7', sub: 'sub_1', customer: 'cus_1', tier: 'tbc', at: 1000 }]);
  // An upgrade on Stripe's page: the tier comes from the new price, not the old metadata.
  const sub = (status, price) => ({ id: 'sub_1', status, customer: 'cus_1', metadata: { gb_user: '7', gb_item: 'bwc_month' }, items: { data: [{ price: { id: price } }] } });
  assert.equal(stripeWork(ev('customer.subscription.updated', sub('active', 'price_obcy')), env)[0].tier, 'obc');
  assert.equal(stripeWork(ev('customer.subscription.updated', sub('past_due', 'price_bwcm')), env)[0].tier, 'bwc');
  assert.equal(stripeWork(ev('customer.subscription.updated', sub('unpaid', 'price_bwcm')), env)[0].tier, '');
  assert.equal(stripeWork(ev('customer.subscription.deleted', sub('canceled', 'price_bwcm')), env)[0].tier, '');
  // No user number (made by hand in the Dashboard), or something we don't sell: ignored.
  assert.deepEqual(stripeWork(ev('checkout.session.completed', { id: 'cs_4', mode: 'payment', payment_status: 'paid', metadata: {} }), env), []);
  assert.deepEqual(stripeWork(ev('invoice.paid', { id: 'in_1', metadata: { gb_user: '7' } }), env), []);
});

async function sign(body, secret, t) {
  const k = await crypto.subtle.importKey('raw', new TextEncoder().encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', k, new TextEncoder().encode(t + '.' + body)));
  return 't=' + t + ',v1=' + [...mac].map((b) => b.toString(16).padStart(2, '0')).join('');
}

test('the webhook hands jobs to the server, and asks Stripe to retry when the server hiccups', async () => {
  const body = JSON.stringify(ev('checkout.session.completed', { id: 'cs_1', mode: 'payment', payment_status: 'paid', metadata: { gb_user: '7', gb_item: 'bolts_500' } }));
  const req = async () => new Request('https://gutsandbolts.net/api/stripe/webhook', { method: 'POST', body, headers: { 'stripe-signature': await sign(body, 'whsec_z', Math.floor(Date.now() / 1000)) } });
  const url = new URL('https://gutsandbolts.net/api/stripe/webhook');
  const got = [];
  let r = await handleStripe(await req(), env, url, async (e, job) => { got.push(job); return { ok: true }; });
  assert.equal(r.status, 200);
  assert.equal(got[0].bolts, 500);
  r = await handleStripe(await req(), env, url, async () => ({ ok: false, retry: true }));
  assert.equal(r.status, 500);
  r = await handleStripe(await req(), env, url, async () => { throw new Error('down'); });
  assert.equal(r.status, 500);
  r = await handleStripe(await req(), env, url, async () => ({ ok: false, error: 'No account' }));   // retrying won't help
  assert.equal(r.status, 200);
});

test('the server adds bought Bolts once, however many times Stripe sends it', () => {
  const s = makeServer();
  const kid = s.signUp('Builder');
  const job = { kind: 'bolts', user: String(kid.userId), bolts: 500, ref: 'stripe:cs_1' };
  const start = s.balance(kid);
  assert.equal(s.stripeEvent(job).ok, true);
  assert.equal(s.stripeEvent(job).already, true);
  assert.equal(s.balance(kid), start + 500);
  assert.equal(s.stripeEvent({ ...job, ref: 'promo:x' }).ok, false);   // only Stripe payments
  assert.equal(s.stripeEvent({ ...job, user: '99999' }).ok, false);
});

test('a paid membership shows the badge, follows upgrades, and ends', () => {
  const s = makeServer();
  const kid = s.signUp('Builder');
  const club = (tier, at, sub = 'sub_1') => s.stripeEvent({ kind: 'club', user: String(kid.userId), sub, customer: 'cus_1', tier, at });
  assert.equal(s.clubOf(kid), '');
  club('tbc', 100);
  assert.equal(s.clubOf(kid), 'tbc');
  assert.equal(kid.stripeCustomer, 'cus_1');
  assert.equal(s.publicUser(kid).club, 'tbc');
  // An older "not paid yet" event arriving late doesn't take it away, even in the same second.
  assert.equal(club('', 90).stale, true);
  assert.equal(club('', 100).stale, true);
  assert.equal(s.clubOf(kid), 'tbc');
  club('obc', 200);
  assert.equal(s.clubOf(kid), 'obc');
  club('bwc', 250, 'sub_2');   // a second, smaller membership: the best one shows
  assert.equal(s.clubOf(kid), 'obc');
  club('', 300);
  assert.equal(s.clubOf(kid), 'bwc');
  club('', 300, 'sub_2');
  assert.equal(s.clubOf(kid), '');
  assert.equal(s.call(kid, 'boltworks.status').canManage, true);
  assert.equal(s.call(kid, 'boltworks.status').paid, '');
});

test('Manage membership needs a card on file and the Stripe key, and only links back to this site', () => {
  const s = makeServer({ STRIPE_SECRET_KEY: 'sk_test_x' });
  const kid = s.signUp('Builder');
  assert.match(s.call(kid, 'club.manage').error, /haven't bought/);
  kid.stripeCustomer = 'cus_1';
  const r = s.call(kid, 'club.manage', { origin: 'https://evil.example' });
  assert.deepEqual(r.stripePortal, { customer: 'cus_1', returnUrl: 'https://gutsandbolts.net/app/#/bolts' });
  assert.equal(returnUrl('https://beta.gutsandbolts.net'), 'https://beta.gutsandbolts.net/app/#/bolts');
  assert.equal(returnUrl('http://localhost:8787'), 'http://localhost:8787/app/#/bolts');
  const s2 = makeServer(), kid2 = s2.signUp('Other');
  kid2.stripeCustomer = 'cus_2';
  assert.match(s2.call(kid2, 'club.manage').error, /aren't set up/);   // no key on this server
});

test('the Bolts page sells tiers and packs once prices exist, and keeps the teaser until then', () => {
  const app = readFileSync(new URL('../../website/app/app.js', import.meta.url), 'utf8');
  assert.match(app, /fetch\('\/api\/stripe\/prices'\)/);
  assert.match(app, /onSale \? clubShop\(items, club\) : html`<section class="boltworks-teaser">/);
  assert.match(app, /data-act="clubManage"/);
  assert.match(readFileSync(new URL('../../website/checkout.js', import.meta.url), 'utf8'), /JSON\.stringify\(\{ userId, item \}\)/);
  assert.match(readFileSync(new URL('../index.js', import.meta.url), 'utf8'), /'\/api\/stripe\/prices'/);
});
