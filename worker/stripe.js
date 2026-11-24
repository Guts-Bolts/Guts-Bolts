// Card payments with Stripe Checkout (the embedded payment form): Boltworks Club tiers and Bolts packs.
//
//   POST /api/stripe/checkout  {userId, item} starts a Checkout Session and answers { client_secret }.
//   GET  /api/stripe/prices    what's on sale and for how much (read from Stripe).
//                              The page website/checkout.html shows Stripe's form with it.
//   GET  /api/stripe/config    the publishable key the page needs (safe to show anyone).
//   POST /api/stripe/webhook   Stripe tells us a payment went through (signature checked).
//
// Settings (Cloudflare: Workers & Pages > project-guts-and-bolts > Settings > Variables and Secrets):
//   STRIPE_SECRET_KEY       a Secret (sk_test_... or sk_live_...). Never put it in the code.
//   STRIPE_PUBLISHABLE_KEY  pk_test_... or pk_live_... (public, a plain variable is fine).
//   STRIPE_PRICE_...        one price ID (price_...) per thing on sale; the names are in ITEMS below.
//   STRIPE_WEBHOOK_SECRET   a Secret (whsec_...) from the webhook you add in the Stripe Dashboard.
// Until the secret key and a price are set, checkout answers "not set up yet" and nothing is charged.
// The webhook hands paid things to the game server (club-pay.js), which gives the tier or the Bolts.
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

// Everything you can buy. Each one's Stripe price ID is a Cloudflare setting (never in the code):
// make the product in the Stripe Dashboard, copy its price_... ID, and put it in the setting named here.
// Club tiers are subscriptions; Bolts packs are one-time payments. The Bolts count lives HERE, not in
// Stripe, so renaming a product in the Dashboard can't change what people get.
export const ITEMS = {
  bwc_month: { tier: 'bwc', every: 'month', env: 'STRIPE_PRICE_BWC_MONTH' },
  bwc_year: { tier: 'bwc', every: 'year', env: 'STRIPE_PRICE_BWC_YEAR' },
  tbc_month: { tier: 'tbc', every: 'month', env: 'STRIPE_PRICE_TBC_MONTH' },
  tbc_year: { tier: 'tbc', every: 'year', env: 'STRIPE_PRICE_TBC_YEAR' },
  obc_month: { tier: 'obc', every: 'month', env: 'STRIPE_PRICE_OBC_MONTH' },
  obc_year: { tier: 'obc', every: 'year', env: 'STRIPE_PRICE_OBC_YEAR' },
  bolts_200: { bolts: 200, env: 'STRIPE_PRICE_BOLTS_200' },
  bolts_500: { bolts: 500, env: 'STRIPE_PRICE_BOLTS_500' },
  bolts_1100: { bolts: 1100, env: 'STRIPE_PRICE_BOLTS_1100' },
  bolts_2400: { bolts: 2400, env: 'STRIPE_PRICE_BOLTS_2400' },
  bolts_6500: { bolts: 6500, env: 'STRIPE_PRICE_BOLTS_6500' },
};

// The price ID for an item, or '' when it isn't set up. (STRIPE_PRICE_ID is the older name for
// the plain monthly Boltworks Club price, from before there were tiers.)
export function priceOf(env, item) {
  const it = ITEMS[item];
  if (!it) return '';
  let v = String(env[it.env] || '').trim();
  if (!v && item === 'bwc_month') v = String(env.STRIPE_PRICE_ID || '').trim();
  return /^price_\w+$/.test(v) ? v : '';
}
// Which item a price ID belongs to ('' for one we don't sell), for upgrades made on Stripe's own page.
export function itemOfPrice(env, price) {
  if (!price) return '';
  for (const k of Object.keys(ITEMS)) if (priceOf(env, k) === price) return k;
  return '';
}

// What we ask Stripe for. The fixed values were picked in Stripe's Checkout Studio.
export function sessionParams(env, origin, userId, item = 'bwc_month') {
  // Club tiers are monthly or yearly memberships (subscriptions); Bolts packs are paid once.
  const mode = ITEMS[item] && ITEMS[item].bolts ? 'payment' : 'subscription';
  const params = {
    ui_mode: 'form',
    mode,
    billing_address_collection: 'auto',
    phone_number_collection: { enabled: false },
    automatic_tax: { enabled: false },
    submit_type: 'auto',
    integration_identifier: 'custom_embedded_web_0001',
    line_items: [{ price: priceOf(env, item), quantity: 1 }],
    return_url: origin + '/app/?checkout={CHECKOUT_SESSION_ID}#/bolts',
  };
  if (mode === 'subscription') params.payment_method_collection = 'always';
  // Who's paying (their user number) and for what, so the webhook gives them the right thing.
  if (userId) {
    params.client_reference_id = 'gb-user-' + userId;
    params.metadata = { gb_user: String(userId), gb_item: item };
    if (mode === 'subscription') params.subscription_data = { metadata: { gb_user: String(userId), gb_item: item } };
  }
  return params;
}

function stripeFetch(env, path, form) {
  const key = String(env.STRIPE_SECRET_KEY || '').trim();
  return fetch('https://api.stripe.com/v1/' + path, form ? {
    method: 'POST',
    headers: { authorization: 'Bearer ' + key, 'stripe-version': STRIPE_API_VERSION, 'content-type': 'application/x-www-form-urlencoded' },
    body: formBody(form),
  } : { headers: { authorization: 'Bearer ' + key, 'stripe-version': STRIPE_API_VERSION } });
}

async function createSession(request, env, url) {
  if (request.method !== 'POST') return json({ error: 'Use POST.' }, 405);
  const key = String(env.STRIPE_SECRET_KEY || '').trim();
  let body = {};
  try { body = JSON.parse(await request.text() || '{}') || {}; } catch { body = {}; }
  const item = typeof body.item === 'string' && ITEMS[body.item] ? body.item : body.item ? '' : 'bwc_month';
  // Says which setting is missing (never its value), so whoever sets it up can see what to fix.
  if (!key) return json({ error: 'Payments aren\'t set up yet (STRIPE_SECRET_KEY is missing).' }, 503);
  if (!item) return json({ error: 'That isn\'t something you can buy.' }, 400);
  const price = priceOf(env, item);
  if (!price) return json({ error: 'That isn\'t for sale yet (' + ITEMS[item].env + (item === 'bwc_month' ? ' or STRIPE_PRICE_ID' : '') + ' is missing).' }, 503);
  const userId = /^\d{1,12}$/.test(String(body.userId || '')) ? String(body.userId) : '';
  if (!userId) return json({ error: 'Log in first, so we know whose account it goes to.' }, 400);
  const r = await stripeFetch(env, 'checkout/sessions', sessionParams(env, url.origin, userId, item));
  const session = await r.json().catch(() => ({}));
  if (!r.ok || !session.client_secret) {
    console.error('Stripe checkout failed:', session.error && session.error.message || r.status);
    // Stripe's own message (like "No such price ... exists in test mode") never holds the key.
    const why = session.error && typeof session.error.message === 'string' ? session.error.message.slice(0, 300) : '';
    return json({ error: 'Couldn\'t start the payment.' + (why ? ' Stripe says: ' + why : ' Try again in a bit.') }, 502);
  }
  return json({ client_secret: session.client_secret });
}

// GET /api/stripe/prices: what's on sale and its real price, read from Stripe (so the website
// always shows what Stripe will charge). Kept for 10 minutes so the Bolts page stays quick.
let priceCache = { at: 0, key: '', items: null };
async function prices(env) {
  if (!String(env.STRIPE_SECRET_KEY || '').trim()) return json({ items: {} });
  const ids = Object.keys(ITEMS).map((k) => [k, priceOf(env, k)]).filter((x) => x[1]);
  const cacheKey = ids.map((x) => x.join('=')).join(',');
  if (priceCache.items && priceCache.key === cacheKey && Date.now() - priceCache.at < 600000) return json({ items: priceCache.items });
  const items = {};
  await Promise.all(ids.map(async ([k, id]) => {
    const it = ITEMS[k];
    const base = it.bolts ? { bolts: it.bolts } : { tier: it.tier, every: it.every };
    try {
      const r = await stripeFetch(env, 'prices/' + encodeURIComponent(id));
      const p = await r.json();
      if (!r.ok || p.active === false) return;   // archived in Stripe: not for sale
      items[k] = Object.assign(base, { amount: p.unit_amount, currency: p.currency });
    } catch {
      items[k] = Object.assign(base, { amount: null, currency: '' });   // Stripe didn't answer: still for sale
    }
  }));
  priceCache = { at: Date.now(), key: cacheKey, items };
  return json({ items });
}

// What a Stripe event means for Guts&Bolts, as a list of jobs for the game server:
//   { kind: 'bolts', user, bolts, ref }                  a Bolts pack was paid for (ref stops double-counting)
//   { kind: 'club', user, sub, customer, tier, at }     a membership started, changed or ended (tier '' = ended)
// Kept apart from the network bits so the tests can check it.
const LIVE = new Set(['active', 'trialing', 'past_due']);   // past_due: Stripe is still retrying the card
export function stripeWork(event, env) {
  const o = event && event.data && event.data.object || {};
  const at = Number(event && event.created) || 0;
  const meta = o.metadata || {};
  const user = /^\d{1,12}$/.test(String(meta.gb_user || '')) ? String(meta.gb_user) : '';
  const idOf = (x) => typeof x === 'string' ? x : x && x.id || '';
  if (!user) return [];
  if (event.type === 'checkout.session.completed' || event.type === 'checkout.session.async_payment_succeeded') {
    const it = ITEMS[meta.gb_item];
    if (!it) return [];
    if (o.mode === 'payment' && it.bolts && o.payment_status === 'paid')
      return [{ kind: 'bolts', user, bolts: it.bolts, ref: 'stripe:' + o.id }];
    if (o.mode === 'subscription' && it.tier && idOf(o.subscription))
      return [{ kind: 'club', user, sub: idOf(o.subscription), customer: idOf(o.customer), tier: it.tier, at }];
    return [];
  }
  if (event.type === 'customer.subscription.created' || event.type === 'customer.subscription.updated' || event.type === 'customer.subscription.deleted') {
    const price = o.items && o.items.data && o.items.data[0] && o.items.data[0].price;
    const it = ITEMS[itemOfPrice(env, idOf(price))] || ITEMS[meta.gb_item];
    if (!it || !it.tier || !o.id) return [];
    const live = event.type !== 'customer.subscription.deleted' && LIVE.has(o.status);
    return [{ kind: 'club', user, sub: o.id, customer: idOf(o.customer), tier: live ? it.tier : '', at }];
  }
  return [];
}

// Hands a job to the game server (the Durable Object), on an address only the Worker can reach.
async function toServer(env, job) {
  if (!env.GB_SERVER_OBJECT) return { ok: false, error: 'Payments only work on the Cloudflare server.' };
  const stub = env.GB_SERVER_OBJECT.get(env.GB_SERVER_OBJECT.idFromName('main'));
  const r = await stub.fetch(new Request('https://gb.internal/stripe-event', { method: 'POST', body: JSON.stringify(job) }));
  return r.json();
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

async function webhook(request, env, deliver = toServer) {
  if (request.method !== 'POST') return json({ error: 'Use POST.' }, 405);
  const secret = String(env.STRIPE_WEBHOOK_SECRET || '').trim();
  if (!secret) return json({ error: 'Webhook not set up.' }, 503);
  const body = await request.text();
  if (body.length > 512 * 1024) return json({ error: 'Too big.' }, 413);
  if (!(await verifySignature(body, request.headers.get('stripe-signature'), secret))) return json({ error: 'Bad signature.' }, 400);
  let event;
  try { event = JSON.parse(body); } catch { return json({ error: 'Bad JSON.' }, 400); }
  for (const job of stripeWork(event, env)) {
    let r;
    try { r = await deliver(env, job); } catch (e) { r = { ok: false, error: String(e && e.message || e), retry: true }; }
    console.log('Stripe', event.type, job.kind, 'user', job.user, r && r.ok ? 'done' : 'failed: ' + (r && r.error));
    // Our server hiccupped: answer an error so Stripe sends it again later (doing it twice is safe).
    if (!r || r.retry) return json({ error: 'Try again.' }, 500);
  }
  return json({ received: true });
}

// Answers the /api/stripe/... addresses, or null for anything else.
export function handleStripe(request, env, url, deliver) {
  if (url.pathname === '/api/stripe/checkout') return createSession(request, env, url);
  if (url.pathname === '/api/stripe/prices') return prices(env);
  if (url.pathname === '/api/stripe/config') return json({ publishableKey: String(env.STRIPE_PUBLISHABLE_KEY || '').trim() });
  if (url.pathname === '/api/stripe/webhook') return webhook(request, env, deliver);
  return null;
}
