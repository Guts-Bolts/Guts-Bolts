// Boltworks Club payments with Stripe Checkout (worker/stripe.js): the session settings,
// the "not set up yet" answer, the webhook signature check, and the page.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { sessionParams, formBody, verifySignature, handleStripe, STRIPE_API_VERSION } from '../stripe.js';
import { withSecurityHeaders } from '../security.js';

const read = (p) => readFileSync(new URL('../../' + p, import.meta.url), 'utf8');
const env = { STRIPE_SECRET_KEY: 'sk_test_x', STRIPE_PRICE_ID: 'price_123', STRIPE_PUBLISHABLE_KEY: 'pk_test_y', STRIPE_WEBHOOK_SECRET: 'whsec_z' };

test('the Checkout Session uses the Checkout Studio settings', () => {
  const p = sessionParams(env, 'https://gutsandbolts.net', '42');
  assert.equal(p.ui_mode, 'form');
  assert.equal(p.mode, 'subscription');
  assert.equal(p.billing_address_collection, 'auto');
  assert.deepEqual(p.phone_number_collection, { enabled: false });
  assert.deepEqual(p.automatic_tax, { enabled: false });
  assert.equal(p.payment_method_collection, 'always');
  assert.equal(p.submit_type, 'auto');
  assert.equal(p.integration_identifier, 'custom_embedded_web_0001');
  assert.deepEqual(p.line_items, [{ price: 'price_123', quantity: 1 }]);
  assert.equal(p.client_reference_id, 'gb-user-42');
  assert.equal(STRIPE_API_VERSION, '2026-03-25.dahlia');
  const body = formBody(p);
  assert.equal(body.get('line_items[0][price]'), 'price_123');
  assert.equal(body.get('phone_number_collection[enabled]'), 'false');
});

test('without the keys, checkout says it isn\'t set up and calls nobody', async () => {
  const req = new Request('https://gutsandbolts.net/api/stripe/checkout', { method: 'POST', body: '{}' });
  const r = await handleStripe(req, {}, new URL(req.url));
  assert.equal(r.status, 503);
  const cfg = await (await handleStripe(new Request('https://gutsandbolts.net/api/stripe/config'), env, new URL('https://gutsandbolts.net/api/stripe/config'))).json();
  assert.equal(cfg.publishableKey, 'pk_test_y');
  assert.equal(handleStripe(new Request('https://gutsandbolts.net/api/x'), env, new URL('https://gutsandbolts.net/api/x')), null);
});

test('checkout asks Stripe for a session and passes back only the client secret', async () => {
  const real = globalThis.fetch;
  let sent = null;
  globalThis.fetch = async (url, init) => { sent = { url, init }; return new Response(JSON.stringify({ id: 'cs_1', client_secret: 'cs_1_secret' })); };
  try {
    const req = new Request('https://gutsandbolts.net/api/stripe/checkout', { method: 'POST', body: JSON.stringify({ userId: '7' }) });
    const r = await handleStripe(req, env, new URL(req.url));
    assert.deepEqual(await r.json(), { client_secret: 'cs_1_secret' });
    assert.equal(sent.url, 'https://api.stripe.com/v1/checkout/sessions');
    assert.equal(sent.init.headers['stripe-version'], '2026-03-25.dahlia');
    assert.match(String(sent.init.body), /ui_mode=form/);
  } finally { globalThis.fetch = real; }
});

async function sign(body, secret, t) {
  const k = await crypto.subtle.importKey('raw', new TextEncoder().encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', k, new TextEncoder().encode(t + '.' + body)));
  return 't=' + t + ',v1=' + [...mac].map((b) => b.toString(16).padStart(2, '0')).join('');
}

test('the webhook only trusts messages really signed by Stripe', async () => {
  const body = JSON.stringify({ type: 'checkout.session.completed', data: { object: { id: 'cs_1', metadata: { gb_user: '7' } } } });
  const now = Math.floor(Date.now() / 1000);
  assert.equal(await verifySignature(body, await sign(body, 'whsec_z', now), 'whsec_z', now), true);
  assert.equal(await verifySignature(body, await sign(body, 'wrong', now), 'whsec_z', now), false);
  assert.equal(await verifySignature(body + ' ', await sign(body, 'whsec_z', now), 'whsec_z', now), false);
  assert.equal(await verifySignature(body, await sign(body, 'whsec_z', now - 3600), 'whsec_z', now), false);   // too old
  const req = (sig) => new Request('https://gutsandbolts.net/api/stripe/webhook', { method: 'POST', body, headers: { 'stripe-signature': sig } });
  assert.equal((await handleStripe(req(await sign(body, 'whsec_z', now)), env, new URL('https://gutsandbolts.net/api/stripe/webhook'))).status, 200);
  assert.equal((await handleStripe(req('t=1,v1=00'), env, new URL('https://gutsandbolts.net/api/stripe/webhook'))).status, 400);
});

test('the page loads Stripe.js from Stripe, and the security headers allow it', () => {
  const page = read('website/checkout.html');
  assert.match(page, /<script src="https:\/\/js\.stripe\.com\/dahlia\/stripe\.js"><\/script>/);
  assert.match(page, /id="checkout-form"/);
  assert.match(read('website/checkout.js'), /initCheckoutFormSdk\(\{ clientSecret, appearance \}\)/);
  const csp = withSecurityHeaders(new Response('', { headers: { 'content-type': 'text/html' } }), new Request('https://gutsandbolts.net/')).headers.get('content-security-policy');
  for (const d of ['script-src', 'frame-src', 'connect-src'])
    assert.match(csp.split('; ').find((x) => x.startsWith(d + ' ')), /https:\/\/js\.stripe\.com/, d);
  assert.ok(!read('worker/stripe.js').match(/sk_(test|live)_[A-Za-z0-9]{8,}/));   // no real keys in the code
});
