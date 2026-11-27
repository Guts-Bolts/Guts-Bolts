// Blockonomics Bitcoin checkout and existing Stripe Bolts refund safeguards.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { handleBitcoin, bitcoinWork, orderData, parseOrder, BITCOIN_PRICES } from '../bitcoin.js';
import { handleStripe, sessionParams, takeBackWork } from '../stripe.js';
import { makeServer } from './helpers.mjs';

const env = { BLOCKONOMICS_API_KEY: 'bn_test_key', BLOCKONOMICS_CALLBACK_SECRET: 'callback-secret' };
const at = (path) => new URL('https://gutsandbolts.net' + path);
const post = (path, body) => new Request(at(path), { method: 'POST', body: JSON.stringify(body), headers: { 'content-type': 'application/json' } });
async function withFetch(fake, fn) { const real = globalThis.fetch; globalThis.fetch = fake; try { return await fn(); } finally { globalThis.fetch = real; } }
const intent = (o = {}) => Object.assign({ id: 'pi_Abcdef12345678', amount: '4.99', currency: 'USD', status: 'paid', extra_data: orderData('7', 'bolts_500', 'nonce123') }, o);

test('Bitcoin pack prices match the Stripe pack values and remain hidden without a Blockonomics key', async () => {
  assert.deepEqual(Object.keys(BITCOIN_PRICES), ['bolts_200', 'bolts_500', 'bolts_1100', 'bolts_2400', 'bolts_6500']);
  const visible = await (await handleBitcoin(new Request(at('/api/bitcoin/prices')), env, at('/api/bitcoin/prices'))).json();
  assert.deepEqual(visible.items.bolts_500, { bolts: 500, amount: 499, currency: 'usd' });
  const hidden = await (await handleBitcoin(new Request(at('/api/bitcoin/prices')), {}, at('/api/bitcoin/prices'))).json();
  assert.deepEqual(hidden.items, {});
});

test('intent extra_data identifies only an account and a supported Bolt pack', () => {
  assert.deepEqual(parseOrder(orderData('7', 'bolts_1100', 'abc123')), { user: '7', item: 'bolts_1100' });
  assert.equal(parseOrder('gb-7-bwc_month-abc123'), null);
  assert.equal(parseOrder('gb-7-bolts_999-abc123'), null);
  assert.equal(parseOrder('forged-order'), null);
});

test('only paid Blockonomics intents with exact USD pack price issue credits', () => {
  assert.deepEqual(bitcoinWork(intent()), { kind: 'bolts', user: '7', bolts: 500, ref: 'blockonomics:pi_Abcdef12345678', via: 'bitcoin' });
  for (const status of ['unpaid', 'processing', 'underpaid']) assert.equal(bitcoinWork(intent({ status })), null);
  assert.equal(bitcoinWork(intent({ amount: '1.99' })), null);
  assert.equal(bitcoinWork(intent({ currency: 'EUR' })), null);
  assert.equal(bitcoinWork(intent({ extra_data: 'not-ours' })), null);
});

test('checkout creates a fiat payment intent and returns the internal widget checkout URL', async () => {
  let sent, url;
  await withFetch(async (u, init) => { url = String(u); sent = JSON.parse(init.body); return new Response(JSON.stringify({ data: { id: 'pi_Abcdef12345678', status: 'unpaid' } }), { status: 200 }); }, async () => {
    const result = await (await handleBitcoin(post('/api/bitcoin/checkout', { userId: '7', item: 'bolts_2400' }), env, at('/api/bitcoin/checkout'))).json();
    assert.equal(url, 'https://www.blockonomics.co/api/v2/payment_intents');
    assert.equal(sent.amount, '19.99');
    assert.equal(sent.currency, 'USD');
    assert.deepEqual(parseOrder(sent.extra_data), { user: '7', item: 'bolts_2400' });
    assert.equal(result.id, 'pi_Abcdef12345678');
    assert.equal(result.url, 'https://gutsandbolts.net/bitcoin-checkout.html?intent=pi_Abcdef12345678');
  });
  assert.equal((await handleBitcoin(post('/api/bitcoin/checkout', { userId: '7', item: 'bwc_month' }), env, at('/api/bitcoin/checkout'))).status, 400);
  assert.equal((await handleBitcoin(post('/api/bitcoin/checkout', { item: 'bolts_500' }), env, at('/api/bitcoin/checkout'))).status, 400);
  assert.equal((await handleBitcoin(post('/api/bitcoin/checkout', { userId: '7', item: 'bolts_500' }), {}, at('/api/bitcoin/checkout'))).status, 503);
});

test('callback secret is required and fulfillment relies on the API status, not callback claims', async () => {
  const delivered = [];
  const deliver = async (env, job) => { delivered.push(job); return { ok: true }; };
  let fetches = 0;
  await withFetch(async () => { fetches++; return new Response(JSON.stringify({ data: intent({ status: 'processing' }) })); }, async () => {
    const bad = await handleBitcoin(new Request('https://gutsandbolts.net/api/bitcoin/webhook?secret=wrong&id=pi_Abcdef12345678'), env, at('/api/bitcoin/webhook'), deliver);
    assert.equal(bad.status, 403);
    const good = await handleBitcoin(new Request('https://gutsandbolts.net/api/bitcoin/webhook?secret=callback-secret&id=pi_Abcdef12345678'), env, at('/api/bitcoin/webhook'), deliver);
    assert.equal(good.status, 200);
  });
  assert.equal(fetches, 1);
  assert.equal(delivered.length, 0);
  await withFetch(async () => new Response(JSON.stringify({ data: paidIntent })), async () => {
    const good = await handleBitcoin(new Request('https://gutsandbolts.net/api/bitcoin/webhook?secret=callback-secret&id=pi_Abcdef12345678'), env, at('/api/bitcoin/webhook'), deliver);
    assert.equal(good.status, 200);
  });
  assert.deepEqual(delivered[0], bitcoinWork(intent()));
});

test('payment status check credits once and keeps provider failures retryable', async () => {
  const s = makeServer(), user = s.signUp('Builder');
  const paidIntent = intent({ extra_data: orderData(String(user.userId), 'bolts_500', 'nonce123') });
  const delivered = async (env, job) => s.stripeEvent(job);
  await withFetch(async () => new Response(JSON.stringify({ data: intent() })), async () => {
    const first = await (await handleBitcoin(post('/api/bitcoin/check', { id: 'pi_Abcdef12345678' }), env, at('/api/bitcoin/check'), delivered)).json();
    const second = await (await handleBitcoin(post('/api/bitcoin/check', { id: 'pi_Abcdef12345678' }), env, at('/api/bitcoin/check'), delivered)).json();
    assert.equal(first.paid, true);
    assert.equal(second.paid, true);
    assert.equal(s.balance(user), 600);
    assert.equal(user.ledger.at(-1)[1], 'Bought 500 Bolts with Bitcoin');
  });
  await withFetch(async () => new Response('API unavailable', { status: 503 }), async () => {
    assert.equal((await handleBitcoin(post('/api/bitcoin/check', { id: 'pi_Abcdef12345678' }), env, at('/api/bitcoin/check'), delivered)).status, 502);
  });
});

test('the Bitcoin widget page and worker route are wired in', () => {
  const page = readFileSync(new URL('../../website/bitcoin-checkout.html', import.meta.url), 'utf8');
  const checkout = readFileSync(new URL('../../website/bitcoin-checkout.js', import.meta.url), 'utf8');
  assert.match(page, /checkout\.js/);
  assert.doesNotMatch(page, /<script(?![^>]*\bsrc=)[^>]*>/i);
  assert.match(checkout, /Blockonomics\.checkout/);
  assert.match(checkout, /intent_id: id/);
  const app = readFileSync(new URL('../../website/app/app.js', import.meta.url), 'utf8');
  assert.match(app, /data-act="bitcoinBuy"/);
  assert.match(app, /fetch\('\/api\/bitcoin\/check'/);
  const index = readFileSync(new URL('../index.js', import.meta.url), 'utf8');
  assert.match(index, /'\/api\/bitcoin\/webhook'\) return handleBitcoin/);
  assert.match(index, /'\/api\/bitcoin\/checkout'/);
});

test('card Bolts payments carry who paid on the payment intent for later refunds', () => {
  const p = sessionParams({ STRIPE_PRICE_BOLTS_500: 'price_b500' }, 'https://gutsandbolts.net', '7', 'bolts_500');
  assert.deepEqual(p.payment_intent_data, { metadata: { gb_user: '7', gb_item: 'bolts_500' } });
  assert.equal(sessionParams({ STRIPE_PRICE_BWC_MONTH: 'price_b' }, 'https://gutsandbolts.net', '7', 'bwc_month').payment_intent_data, undefined);
});

test('Stripe refunds and disputes take back the right number of Bolts', () => {
  const pi = { id: 'pi_1', metadata: { gb_user: '7', gb_item: 'bolts_500' } };
  const ev = (type, o) => ({ type, data: { object: Object.assign({ payment_intent: 'pi_1' }, o) } });
  assert.deepEqual(takeBackWork(ev('charge.refunded', { amount: 499, amount_refunded: 499 }), pi), [{ kind: 'boltsBack', user: '7', bolts: 500, pi: 'pi_1', why: 'refund' }]);
  assert.equal(takeBackWork(ev('charge.refunded', { amount: 1000, amount_refunded: 500 }), pi)[0].bolts, 250);
  assert.equal(takeBackWork(ev('charge.dispute.created', { amount: 499 }), pi)[0].bolts, 500);
  assert.deepEqual(takeBackWork(ev('charge.refunded', { amount: 499, amount_refunded: 499 }), { id: 'pi_2', metadata: { gb_user: '7', gb_item: 'bwc_month' } }), []);
});

test('Stripe refund take-backs remain capped and idempotent', () => {
  const s = makeServer(), u = s.signUp('Builder'), user = String(u.userId);
  s.stripeEvent({ kind: 'bolts', user, bolts: 500, ref: 'stripe:cs_1' });
  const before = s.balance(u);
  assert.equal(s.stripeEvent({ kind: 'boltsBack', user, bolts: 250, pi: 'pi_1', why: 'refund' }).taken, 250);
  assert.equal(s.stripeEvent({ kind: 'boltsBack', user, bolts: 250, pi: 'pi_1', why: 'refund' }).already, true);
  assert.equal(s.stripeEvent({ kind: 'boltsBack', user, bolts: 500, pi: 'pi_1', why: 'chargeback' }).taken, 250);
  assert.equal(s.stripeEvent({ kind: 'boltsBack', user, bolts: 500, pi: 'pi_1', why: 'refund' }).already, true);
  assert.equal(s.balance(u), before - 500);
  assert.equal(s.stripeEvent({ kind: 'boltsBack', user, bolts: 500, pi: 'not-a-payment' }).ok, false);
});

async function sign(body, secret, t) {
  const k = await crypto.subtle.importKey('raw', new TextEncoder().encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', k, new TextEncoder().encode(t + '.' + body)));
  return 't=' + t + ',v1=' + [...mac].map((b) => b.toString(16).padStart(2, '0')).join('');
}

test('Stripe webhook retrieves the refund payment intent and retries if Stripe is unavailable', async () => {
  const senv = { STRIPE_SECRET_KEY: 'sk_test_x', STRIPE_WEBHOOK_SECRET: 'whsec_z' };
  const body = JSON.stringify({ type: 'charge.refunded', created: 1, data: { object: { id: 'ch_1', payment_intent: 'pi_1', amount: 499, amount_refunded: 499 } } });
  const req = async () => new Request(at('/api/stripe/webhook'), { method: 'POST', body, headers: { 'stripe-signature': await sign(body, 'whsec_z', Math.floor(Date.now() / 1000)) } });
  const got = [], deliver = async (env, job) => { got.push(job); return { ok: true }; };
  await withFetch(async (url) => { assert.equal(String(url), 'https://api.stripe.com/v1/payment_intents/pi_1'); return new Response(JSON.stringify({ id: 'pi_1', metadata: { gb_user: '7', gb_item: 'bolts_500' } })); }, async () => {
    assert.equal((await handleStripe(await req(), senv, at('/api/stripe/webhook'), deliver)).status, 200);
  });
  assert.equal(got[0].kind, 'boltsBack');
  await withFetch(async () => new Response('{}', { status: 500 }), async () => {
    assert.equal((await handleStripe(await req(), senv, at('/api/stripe/webhook'), deliver)).status, 500);
  });
});
