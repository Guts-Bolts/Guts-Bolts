// Card payments with Stripe Checkout (the embedded payment form), for Boltworks Club.
//
//   POST /api/stripe/checkout  starts a Checkout Session and answers { client_secret }.
//                              The page website/checkout.html shows Stripe's form with it.
//   GET  /api/stripe/config    the publishable key the page needs (safe to show anyone).
//   POST /api/stripe/webhook   Stripe tells us a payment went through (signature checked).
//
// Settings (Cloudflare: Workers & Pages > project-guts-and-bolts > Settings > Variables and Secrets):
//   STRIPE_SECRET_KEY       a Secret (sk_test_... or sk_live_...). Never put it in the code.
//   STRIPE_PUBLISHABLE_KEY  pk_test_... or pk_live_... (public, a plain variable is fine).
//   STRIPE_PRICE_ID         the Boltworks Club monthly price (price_...) from the Stripe Dashboard.
//   STRIPE_WEBHOOK_SECRET   a Secret (whsec_...) from the webhook you add in the Stripe Dashboard.
// Until the secret key and price are set, checkout answers "not set up yet" and nothing is charged.
// Steps for all of this: STRIPE_INTEGRATION_TODO.md. Only the Cloudflare server does payments;
// the C++ server (src/server) doesn't serve the website, so it has nothing to mirror.

export const STRIPE_API_VERSION = '2026-03-25.dahlia';

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), { status, headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });
}

// Stripe's API takes form fields, with nested ones written like line_items[0][price].
export function formBody(obj, prefix = '', out = new URLSearchParams()) {
  for (const [k, v] of Object.entries(obj)) {
    if (v === undefined || v === null) continue;
    const key = prefix ? `${prefix}[${k}]` : k;
    if (typeof v === 'object') formBody(v, key, out);
    else out.append(key, String(v));
  }
  return out;
}

// What we ask Stripe for. The fixed values were picked in Stripe's Checkout Studio.
export function sessionParams(env, origin, userId) {
  // Boltworks Club is a monthly membership, so this is a subscription.
  const mode = 'subscription';
  const params = {
    ui_mode: 'form',
    mode,
    billing_address_collection: 'auto',
    phone_number_collection: { enabled: false },
    automatic_tax: { enabled: false },
    submit_type: 'auto',
    integration_identifier: 'custom_embedded_web_0001',
    line_items: [{ price: String(env.STRIPE_PRICE_ID || '').trim(), quantity: 1 }],
    return_url: origin + '/app/?checkout={CHECKOUT_SESSION_ID}#/bolts',
  };
  if (mode === 'subscription') params.payment_method_collection = 'always';
  // Who's paying (their user number), so staff can match the payment to the account.
  if (userId) {
    params.client_reference_id = 'gb-user-' + userId;
    params.metadata = { gb_user: String(userId) };
    params.subscription_data = { metadata: { gb_user: String(userId) } };
  }
  return params;
}

async function createSession(request, env, url) {
  if (request.method !== 'POST') return json({ error: 'Use POST.' }, 405);
  const key = String(env.STRIPE_SECRET_KEY || '').trim();
  const price = String(env.STRIPE_PRICE_ID || '').trim();
  if (!key || !/^price_\w+$/.test(price)) return json({ error: 'Payments aren\'t set up yet.' }, 503);
  let body = {};
  try { body = JSON.parse(await request.text() || '{}') || {}; } catch { body = {}; }
  const userId = /^\d{1,12}$/.test(String(body.userId || '')) ? String(body.userId) : '';
  const r = await fetch('https://api.stripe.com/v1/checkout/sessions', {
    method: 'POST',
    headers: { authorization: 'Bearer ' + key, 'stripe-version': STRIPE_API_VERSION, 'content-type': 'application/x-www-form-urlencoded' },
    body: formBody(sessionParams(env, url.origin, userId)),
  });
  const session = await r.json().catch(() => ({}));
  if (!r.ok || !session.client_secret) {
    console.error('Stripe checkout failed:', session.error && session.error.message || r.status);
    return json({ error: 'Couldn\'t start the payment. Try again in a bit.' }, 502);
  }
  return json({ client_secret: session.client_secret });
}

// Checks the Stripe-Signature header: "t=<time>,v1=<HMAC-SHA256 of 't.body' with the webhook secret>".
export async function verifySignature(body, header, secret, nowSeconds = Math.floor(Date.now() / 1000), tolerance = 300) {
  if (!header || !secret) return false;
  const parts = Object.create(null), sigs = [];
  for (const piece of String(header).split(',')) {
    const [k, v] = piece.split('=');
    if (k === 'v1') sigs.push(v); else parts[k] = v;
  }
  const t = Number(parts.t);
  if (!t || !sigs.length || Math.abs(nowSeconds - t) > tolerance) return false;
  const k = await crypto.subtle.importKey('raw', new TextEncoder().encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', k, new TextEncoder().encode(t + '.' + body)));
  const hex = [...mac].map((b) => b.toString(16).padStart(2, '0')).join('');
  return sigs.some((s) => s && s.length === hex.length && [...s].reduce((d, c, i) => d | (c.charCodeAt(0) ^ hex.charCodeAt(i)), 0) === 0);
}

async function webhook(request, env) {
  if (request.method !== 'POST') return json({ error: 'Use POST.' }, 405);
  const secret = String(env.STRIPE_WEBHOOK_SECRET || '').trim();
  if (!secret) return json({ error: 'Webhook not set up.' }, 503);
  const body = await request.text();
  if (body.length > 512 * 1024) return json({ error: 'Too big.' }, 413);
  if (!(await verifySignature(body, request.headers.get('stripe-signature'), secret))) return json({ error: 'Bad signature.' }, 400);
  let event;
  try { event = JSON.parse(body); } catch { return json({ error: 'Bad JSON.' }, 400); }
  if (event.type === 'checkout.session.completed') {
    const s = event.data && event.data.object || {};
    // TODO (STRIPE_INTEGRATION_TODO.md): give the Boltworks Club tier automatically. For now, staff
    // give it by hand on the Staff page, using the user number in the payment's details in Stripe.
    console.log('Boltworks Club checkout completed:', s.id, 'user', s.metadata && s.metadata.gb_user || '?');
  }
  return json({ received: true });
}

// Answers the /api/stripe/... addresses, or null for anything else.
export function handleStripe(request, env, url) {
  if (url.pathname === '/api/stripe/checkout') return createSession(request, env, url);
  if (url.pathname === '/api/stripe/config') return json({ publishableKey: String(env.STRIPE_PUBLISHABLE_KEY || '').trim() });
  if (url.pathname === '/api/stripe/webhook') return webhook(request, env);
  return null;
}
