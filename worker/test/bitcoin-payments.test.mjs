// More ways to buy Bolts (worker/bitcoin.js, worker/stripe.js, worker/club-pay.js): Bitcoin through
// OpenNode (only a charge OpenNode itself says is paid gives Bolts, once), and card refunds or
// chargebacks taking bought Bolts back (never more than the pack gave).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { handleBitcoin, bitcoinWork, verifyHashedOrder, orderId, parseOrder, BITCOIN_PRICES } from '../bitcoin.js';
import { handleStripe, sessionParams, takeBackWork } from '../stripe.js';
import { makeServer } from './helpers.mjs';

const env = { OPENNODE_API_KEY: 'on_test_key' };
const at = (path) => new URL('https://gutsandbolts.net' + path);
const post = (path, body, type = 'application/json') => new Request(at(path), { method: 'POST', body: typeof body === 'string' ? body : JSON.stringify(body), headers: { 'content-type': type } });
async function hmac(id, key) {
  const k = await crypto.subtle.importKey('raw', new TextEncoder().encode(key), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  return [...new Uint8Array(await crypto.subtle.sign('HMAC', k, new TextEncoder().encode(id)))].map((b) => b.toString(16).padStart(2, '0')).join('');
}
const charge = (o = {}) => Object.assign({ id: 'abc12345-0000', status: 'paid', order_id: 'gb-7-bolts_500-k3j9', currency: 'USD', fiat_value: 4.99 }, o);
async function withFetch(fake, fn) {
  const real = globalThis.fetch;
  globalThis.fetch = fake;
  try { return await fn(); } finally { globalThis.fetch = real; }
}

test('Bitcoin prices match the card packs, and hide until the key is set', async () => {
  assert.deepEqual(Object.keys(BITCOIN_PRICES), ['bolts_200', 'bolts_500', 'bolts_1100', 'bolts_2400', 'bolts_6500']);
  const items = (await (await handleBitcoin(new Request(at('/api/bitcoin/prices')), env, at('/api/bitcoin/prices'))).json()).items;
  assert.deepEqual(items.bolts_500, { bolts: 500, amount: 499, currency: 'usd' });
  assert.deepEqual((await (await handleBitcoin(new Request(at('/api/bitcoin/prices')), {}, at('/api/bitcoin/prices'))).json()).items, {});
});

test('order ids say who and what, and nothing else gets through', () => {
  assert.deepEqual(parseOrder(orderId('7', 'bolts_1100', 'zz9')), { user: '7', item: 'bolts_1100' });
  assert.equal(parseOrder('gb-7-bwc_month-zz9'), null);    // memberships aren't sold for Bitcoin
  assert.equal(parseOrder('gb-7-bolts_999-zz9'), null);    // not a pack we sell
  assert.equal(parseOrder('someone-else-123'), null);
});

test('only a paid charge for at least the price gives Bolts', () => {
  assert.deepEqual(bitcoinWork(charge()), { kind: 'bolts', user: '7', bolts: 500, ref: 'opennode:abc12345-0000', via: 'bitcoin' });
  for (const status of ['unpaid', 'processing', 'underpaid', 'expired', 'failed', 'refunded']) assert.equal(bitcoinWork(charge({ status })), null);
  assert.equal(bitcoinWork(charge({ fiat_value: 1.99 })), null);       // a cheaper charge relabelled as a bigger pack
  assert.equal(bitcoinWork(charge({ currency: 'EUR' })), null);
  assert.equal(bitcoinWork(charge({ order_id: 'not-ours' })), null);
});

test('the webhook checks hashed_order, then asks OpenNode what really happened', async () => {
  assert.equal(await verifyHashedOrder('abc12345-0000', await hmac('abc12345-0000', 'on_test_key'), 'on_test_key'), true);
  assert.equal(await verifyHashedOrder('abc12345-0000', await hmac('abc12345-0000', 'wrong'), 'on_test_key'), false);
  const form = async (status, key = 'on_test_key') => 'id=abc12345-0000&status=' + status + '&hashed_order=' + await hmac('abc12345-0000', key);
  const got = [];
  const deliver = async (e, job) => { got.push(job); return { ok: true }; };
  const asked = [];
  // The webhook says "paid", but OpenNode's API says it's still processing: no Bolts.
  await withFetch(async (url, init) => { asked.push([String(url), init && init.headers.authorization]); return new Response(JSON.stringify({ data: charge({ status: 'processing' }) })); }, async () => {
    const r = await handleBitcoin(post('/api/bitcoin/webhook', await form('paid'), 'application/x-www-form-urlencoded'), env, at('/api/bitcoin/webhook'), deliver);
    assert.equal(r.status, 200);
  });
  assert.equal(got.length, 0);
  assert.deepEqual(asked[0], ['https://api.opennode.com/v1/charge/abc12345-0000', 'on_test_key']);
  // Paid for real: the Bolts go to the account in the order id.
  await withFetch(async () => new Response(JSON.stringify({ data: charge() })), async () => {
    const r = await handleBitcoin(post('/api/bitcoin/webhook', await form('paid'), 'application/x-www-form-urlencoded'), env, at('/api/bitcoin/webhook'), deliver);
    assert.equal(r.status, 200);
  });
  assert.equal(got[0].ref, 'opennode:abc12345-0000');
  // A forged webhook never even reaches OpenNode.
  let called = false;
  await withFetch(async () => { called = true; return new Response('{}'); }, async () => {
    const r = await handleBitcoin(post('/api/bitcoin/webhook', await form('paid', 'guess'), 'application/x-www-form-urlencoded'), env, at('/api/bitcoin/webhook'), deliver);
    assert.equal(r.status, 400);
  });
  assert.equal(called, false);
  // OpenNode down: answer an error so it can send it again.
  await withFetch(async () => new Response('nope', { status: 502 }), async () => {
    const r = await handleBitcoin(post('/api/bitcoin/webhook', await form('paid'), 'application/x-www-form-urlencoded'), env, at('/api/bitcoin/webhook'), deliver);
    assert.equal(r.status, 500);
  });
});

test('checkout makes a dollar-priced charge for a pack, and dev mode uses OpenNode\'s test network', async () => {
  let sent = null, where = '';
  const fake = async (url, init) => { where = String(url); sent = JSON.parse(init.body); return new Response(JSON.stringify({ data: { id: 'abc12345-0000', hosted_checkout_url: 'https://checkout.opennode.com/abc12345-0000' } }), { status: 201 }); };
  await withFetch(fake, async () => {
    const r = await (await handleBitcoin(post('/api/bitcoin/checkout', { userId: '7', item: 'bolts_2400' }), env, at('/api/bitcoin/checkout'))).json();
    assert.equal(r.url, 'https://checkout.opennode.com/abc12345-0000');
    assert.equal(where, 'https://api.opennode.com/v1/charges');
    assert.equal(sent.amount, '19.99');
    assert.equal(sent.currency, 'USD');
    assert.deepEqual(parseOrder(sent.order_id), { user: '7', item: 'bolts_2400' });
    assert.equal(sent.callback_url, 'https://gutsandbolts.net/api/bitcoin/webhook');
    await handleBitcoin(post('/api/bitcoin/checkout', { userId: '7', item: 'bolts_200' }), { ...env, OPENNODE_DEV: '1' }, at('/api/bitcoin/checkout'));
    assert.equal(where, 'https://dev-api.opennode.com/v1/charges');
  });
  assert.equal((await handleBitcoin(post('/api/bitcoin/checkout', { userId: '7', item: 'tbc_month' }), env, at('/api/bitcoin/checkout'))).status, 400);
  assert.equal((await handleBitcoin(post('/api/bitcoin/checkout', { item: 'bolts_500' }), env, at('/api/bitcoin/checkout'))).status, 400);
  const off = await handleBitcoin(post('/api/bitcoin/checkout', { userId: '7', item: 'bolts_500' }), {}, at('/api/bitcoin/checkout'));
  assert.equal(off.status, 503);
  assert.match((await off.json()).error, /OPENNODE_API_KEY/);
});

test('the server adds Bitcoin Bolts once', () => {
  const s = makeServer();
  const kid = s.signUp('Builder');
  const job = { kind: 'bolts', user: String(kid.userId), bolts: 500, ref: 'opennode:abc12345-0000' };
  const start = s.balance(kid);
  assert.equal(s.stripeEvent(job).ok, true);
  assert.equal(s.stripeEvent(job).already, true);
  assert.equal(s.balance(kid), start + 500);
  assert.match(kid.ledger.at(-1)[1], /with Bitcoin/);
  assert.equal(s.stripeEvent({ ...job, ref: 'opennode:../../x' }).ok, false);
});

test('card Bolts payments carry who paid onto the payment, for refunds later', () => {
  const p = sessionParams({ STRIPE_PRICE_BOLTS_500: 'price_b500' }, 'https://gutsandbolts.net', '7', 'bolts_500');
  assert.deepEqual(p.payment_intent_data, { metadata: { gb_user: '7', gb_item: 'bolts_500' } });
  assert.equal(sessionParams({ STRIPE_PRICE_BWC_MONTH: 'price_b' }, 'https://gutsandbolts.net', '7', 'bwc_month').payment_intent_data, undefined);
});

test('refunds take back their share of the Bolts, chargebacks the whole pack', () => {
  const pi = { id: 'pi_1', metadata: { gb_user: '7', gb_item: 'bolts_500' } };
  const ev = (type, o) => ({ type, data: { object: Object.assign({ payment_intent: 'pi_1' }, o) } });
  assert.deepEqual(takeBackWork(ev('charge.refunded', { amount: 499, amount_refunded: 499 }), pi), [{ kind: 'boltsBack', user: '7', bolts: 500, pi: 'pi_1', why: 'refund' }]);
  assert.equal(takeBackWork(ev('charge.refunded', { amount: 1000, amount_refunded: 500 }), pi)[0].bolts, 250);
  assert.equal(takeBackWork(ev('charge.dispute.created', { amount: 499 }), pi)[0].bolts, 500);
  assert.deepEqual(takeBackWork(ev('charge.refunded', { amount: 499, amount_refunded: 499 }), { id: 'pi_2', metadata: { gb_user: '7', gb_item: 'bwc_month' } }), []);
  assert.deepEqual(takeBackWork(ev('charge.refunded', { amount: 499, amount_refunded: 499 }), { id: 'pi_3', metadata: {} }), []);
});

test('the server never takes back more than the pack gave', () => {
  const s = makeServer();
  const kid = s.signUp('Builder');
  const u = String(kid.userId);
  s.stripeEvent({ kind: 'bolts', user: u, bolts: 500, ref: 'stripe:cs_1' });
  const start = s.balance(kid);
  assert.equal(s.stripeEvent({ kind: 'boltsBack', user: u, bolts: 250, pi: 'pi_1', why: 'refund' }).taken, 250);
  assert.equal(s.stripeEvent({ kind: 'boltsBack', user: u, bolts: 250, pi: 'pi_1', why: 'refund' }).already, true);   // sent twice
  assert.equal(s.stripeEvent({ kind: 'boltsBack', user: u, bolts: 500, pi: 'pi_1', why: 'chargeback' }).taken, 250);  // then charged back
  assert.equal(s.stripeEvent({ kind: 'boltsBack', user: u, bolts: 500, pi: 'pi_1', why: 'refund' }).already, true);
  assert.equal(s.balance(kid), start - 500);
  assert.equal(s.stripeEvent({ kind: 'boltsBack', user: u, bolts: 500, pi: 'nope' }).ok, false);
});

async function sign(body, secret, t) {
  const k = await crypto.subtle.importKey('raw', new TextEncoder().encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', k, new TextEncoder().encode(t + '.' + body)));
  return 't=' + t + ',v1=' + [...mac].map((b) => b.toString(16).padStart(2, '0')).join('');
}

test('the Stripe webhook looks up the payment for a refund, and asks again if Stripe is slow', async () => {
  const senv = { STRIPE_SECRET_KEY: 'sk_test_x', STRIPE_WEBHOOK_SECRET: 'whsec_z' };
  const body = JSON.stringify({ type: 'charge.refunded', created: 1, data: { object: { id: 'ch_1', payment_intent: 'pi_1', amount: 499, amount_refunded: 499 } } });
  const req = async () => new Request(at('/api/stripe/webhook'), { method: 'POST', body, headers: { 'stripe-signature': await sign(body, 'whsec_z', Math.floor(Date.now() / 1000)) } });
  const got = [];
  const deliver = async (e, job) => { got.push(job); return { ok: true }; };
  await withFetch(async (url) => {
    assert.equal(String(url), 'https://api.stripe.com/v1/payment_intents/pi_1');
    return new Response(JSON.stringify({ id: 'pi_1', metadata: { gb_user: '7', gb_item: 'bolts_500' } }));
  }, async () => {
    assert.equal((await handleStripe(await req(), senv, at('/api/stripe/webhook'), deliver)).status, 200);
  });
  assert.equal(got[0].kind, 'boltsBack');
  await withFetch(async () => new Response('{}', { status: 500 }), async () => {
    assert.equal((await handleStripe(await req(), senv, at('/api/stripe/webhook'), deliver)).status, 500);
  });
});

test('the Bolts page offers Bitcoin, and the worker routes it', () => {
  const app = readFileSync(new URL('../../website/app/app.js', import.meta.url), 'utf8');
  assert.match(app, /data-act="bitcoinBuy"/);
  assert.match(app, /fetch\('\/api\/bitcoin\/check'/);
  const index = readFileSync(new URL('../index.js', import.meta.url), 'utf8');
  assert.match(index, /'\/api\/bitcoin\/webhook'\) return handleBitcoin/);
  assert.match(index, /'\/api\/bitcoin\/checkout'/);
});
