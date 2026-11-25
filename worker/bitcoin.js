// Paying for Bolts packs with Bitcoin, through OpenNode (opennode.com).
//
// Why OpenNode: Stripe can't take Bitcoin (its crypto option is stablecoins like USDC, which is a
// Stripe Dashboard switch, no code). OpenNode is a hosted Bitcoin checkout: it takes normal Bitcoin
// and Lightning, shows its own payment page, and can turn what comes in into dollars for us. Nothing
// to run ourselves (BTCPay Server would need its own always-on computer).
//
//   GET  /api/bitcoin/prices    which Bolts packs you can buy with Bitcoin, and for how many dollars.
//   POST /api/bitcoin/checkout  {userId, item} makes an OpenNode charge and answers { url, id }:
//                               the website sends the buyer to that url (OpenNode's payment page).
//   POST /api/bitcoin/webhook   OpenNode tells us a charge changed (paid, expired...).
//   POST /api/bitcoin/check     {id} looks a charge up again (for when the webhook got lost).
//
// Trust: OpenNode's webhook only proves which charge it is about (hashed_order = HMAC-SHA256 of the
// charge id with our API key). So we never believe the status it posts: the webhook and /check both ask
// OpenNode's API for the charge, and only a charge OpenNode itself says is "paid", for a pack we sell,
// for at least that pack's price in dollars, gives Bolts. The Bolts go in once per charge (ref
// opennode:<charge id>), however many times we hear about it.
//
// Bitcoin can't be charged back: once a payment is confirmed it's final. So, unlike cards, there's
// nothing to take back later. Memberships aren't sold for Bitcoin, because Bitcoin can't renew by itself.
//
// Settings (Cloudflare: Workers & Pages > project-guts-and-bolts > Settings > Variables and Secrets):
//   OPENNODE_API_KEY  a Secret: an OpenNode API key with the "Invoices" permission. Never in the code.
//   OPENNODE_DEV      set to 1 while testing with a key from dev.opennode.com (fake testnet Bitcoin).
// Until the key is set, Bitcoin answers "not set up yet" and the Bolts page doesn't show the button.
// Steps: STRIPE_INTEGRATION_TODO.md, "Bitcoin". Only the Cloudflare server takes payments, so the
// C++ server (src/server) has nothing to mirror.

import { ITEMS } from './stripe.js';

// What each Bolts pack costs in Bitcoin's checkout, in US cents (the same as the card prices).
// OpenNode works out how much Bitcoin that is at the moment someone pays.
export const BITCOIN_PRICES = { bolts_200: 199, bolts_500: 499, bolts_1100: 999, bolts_2400: 1999, bolts_6500: 4999 };

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), { status, headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });
}
const keyOf = (env) => String(env.OPENNODE_API_KEY || '').trim();
const apiOf = (env) => /^(1|true|yes)$/i.test(String(env.OPENNODE_DEV || '').trim()) ? 'https://dev-api.opennode.com' : 'https://api.opennode.com';
const userOk = (v) => /^\d{1,12}$/.test(String(v || ''));
const chargeIdOk = (v) => /^[A-Za-z0-9-]{8,80}$/.test(String(v || ''));

// order_id on the charge: who it's for and what they bought, like "gb-7-bolts_500-k3j9".
export function orderId(userId, item, nonce = Math.random().toString(36).slice(2, 8)) {
  return 'gb-' + userId + '-' + item + '-' + nonce;
}
export function parseOrder(order) {
  const m = /^gb-(\d{1,12})-(bolts_\d+)-[a-z0-9]{1,12}$/.exec(String(order || ''));
  return m && BITCOIN_PRICES[m[2]] ? { user: m[1], item: m[2] } : null;
}

// What OpenNode sends: hashed_order must be HMAC-SHA256(charge id, our API key), in hex.
export async function verifyHashedOrder(id, hashed, apiKey) {
  if (!id || !hashed || !apiKey) return false;
  const k = await crypto.subtle.importKey('raw', new TextEncoder().encode(apiKey), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', k, new TextEncoder().encode(String(id))));
  const hex = [...mac].map((b) => b.toString(16).padStart(2, '0')).join('');
  const s = String(hashed).toLowerCase();
  return s.length === hex.length && [...s].reduce((d, c, i) => d | (c.charCodeAt(0) ^ hex.charCodeAt(i)), 0) === 0;
}

// A charge as OpenNode's API reports it -> the job for the game server, or null (nothing to give yet).
// Kept apart from the network bits so the tests can check it.
export function bitcoinWork(charge) {
  if (!charge || typeof charge !== 'object' || !chargeIdOk(charge.id)) return null;
  if (charge.status !== 'paid') return null;   // unpaid, processing, underpaid, expired, failed, refunded
  const order = parseOrder(charge.order_id);
  if (!order) return null;
  // Paid enough? We made the charge in USD, so its fiat price must still be at least the pack's price.
  const cents = Math.round(Number(charge.fiat_value ?? charge.source_fiat_value) * 100);
  if (String(charge.currency || '').toUpperCase() !== 'USD' || !(cents >= BITCOIN_PRICES[order.item])) return null;
  return { kind: 'bolts', user: order.user, bolts: ITEMS[order.item].bolts, ref: 'opennode:' + charge.id, via: 'bitcoin' };
}

async function getCharge(env, id) {
  const r = await fetch(apiOf(env) + '/v1/charge/' + encodeURIComponent(id), { headers: { authorization: keyOf(env) } });
  if (!r.ok) throw new Error('OpenNode answered ' + r.status);
  const j = await r.json();
  return j && j.data || null;
}

// Hands a job to the game server (club-pay.js stripeEvent takes Bitcoin Bolts too).
async function toServer(env, job) {
  if (!env.GB_SERVER_OBJECT) return { ok: false, error: 'Payments only work on the Cloudflare server.' };
  const stub = env.GB_SERVER_OBJECT.get(env.GB_SERVER_OBJECT.idFromName('main'));
  const r = await stub.fetch(new Request('https://gb.internal/stripe-event', { method: 'POST', body: JSON.stringify(job) }));
  return r.json();
}

// Looks a charge up at OpenNode and gives the Bolts if it's paid. Answers { paid, status } or throws.
async function settle(env, id, deliver) {
  const charge = await getCharge(env, id);
  const job = bitcoinWork(charge);
  if (!job) return { paid: false, status: String(charge && charge.status || 'unknown') };
  const r = await deliver(env, job);
  if (!r || (!r.ok && r.retry)) throw new Error(r && r.error || 'server hiccup');
  console.log('Bitcoin charge', id, 'user', job.user, r.ok ? 'done' : 'failed: ' + r.error);
  return { paid: !!r.ok, status: 'paid', bolts: job.bolts };
}

async function checkout(request, env, url) {
  if (request.method !== 'POST') return json({ error: 'Use POST.' }, 405);
  if (!keyOf(env)) return json({ error: 'Bitcoin payments aren\'t set up yet (OPENNODE_API_KEY is missing).' }, 503);
  let body = {};
  try { body = JSON.parse(await request.text() || '{}') || {}; } catch { body = {}; }
  const item = String(body.item || '');
  if (!BITCOIN_PRICES[item]) return json({ error: 'Only Bolts packs can be bought with Bitcoin.' }, 400);
  if (!userOk(body.userId)) return json({ error: 'Log in first, so we know whose account it goes to.' }, 400);
  const origin = /^https:\/\/([a-z0-9-]+\.)*gutsandbolts\.net$/.test(url.origin) || /^http:\/\/(localhost|127\.0\.0\.1)(:\d{1,5})?$/.test(url.origin) ? url.origin : 'https://gutsandbolts.net';
  const bolts = ITEMS[item].bolts;
  const r = await fetch(apiOf(env) + '/v1/charges', {
    method: 'POST',
    headers: { authorization: keyOf(env), 'content-type': 'application/json' },
    body: JSON.stringify({
      amount: (BITCOIN_PRICES[item] / 100).toFixed(2), currency: 'USD',
      description: bolts.toLocaleString('en-US') + ' Bolts (Guts&Bolts)',
      order_id: orderId(String(body.userId), item),
      callback_url: origin + '/api/bitcoin/webhook',
      success_url: origin + '/app/?bitcoin=paid#/bolts',
      auto_settle: false,
    }),
  }).catch(() => null);
  const j = r ? await r.json().catch(() => ({})) : {};
  const c = j && j.data || {};
  if (!r || !r.ok || typeof c.hosted_checkout_url !== 'string' || !chargeIdOk(c.id)) {
    console.error('OpenNode charge failed:', r ? r.status : 'no answer', j && j.message);
    const why = j && typeof j.message === 'string' ? ' OpenNode says: ' + j.message.slice(0, 300) : ' Try again in a bit.';
    return json({ error: 'Couldn\'t start the Bitcoin payment.' + why }, 502);
  }
  return json({ url: c.hosted_checkout_url, id: c.id });
}

async function webhook(request, env, deliver) {
  if (request.method !== 'POST') return json({ error: 'Use POST.' }, 405);
  if (!keyOf(env)) return json({ error: 'Not set up.' }, 503);
  const text = await request.text();
  if (text.length > 64 * 1024) return json({ error: 'Too big.' }, 413);
  // OpenNode posts a form; take JSON too, in case that changes.
  let f = {};
  try { f = text.trim().startsWith('{') ? JSON.parse(text) : Object.fromEntries(new URLSearchParams(text)); } catch { f = {}; }
  if (!chargeIdOk(f.id) || !(await verifyHashedOrder(f.id, f.hashed_order, keyOf(env)))) return json({ error: 'Bad signature.' }, 400);
  try {
    await settle(env, f.id, deliver);
  } catch (e) {
    console.error('Bitcoin webhook', f.id, String(e && e.message || e));
    return json({ error: 'Try again.' }, 500);   // OpenNode can send it again; /check also catches it
  }
  return json({ received: true });
}

// The Bolts page asks this when someone comes back from OpenNode's page, in case the webhook is slow
// or got lost. Safe for anyone to call: it only ever gives Bolts for a charge OpenNode says is paid,
// to the account the charge was made for, once.
async function check(request, env, deliver) {
  if (request.method !== 'POST') return json({ error: 'Use POST.' }, 405);
  if (!keyOf(env)) return json({ error: 'Bitcoin payments aren\'t set up yet.' }, 503);
  let body = {};
  try { body = JSON.parse(await request.text() || '{}') || {}; } catch { body = {}; }
  if (!chargeIdOk(body.id)) return json({ error: 'That isn\'t a Bitcoin payment.' }, 400);
  try {
    return json(await settle(env, String(body.id), deliver));
  } catch {
    return json({ error: 'Couldn\'t reach OpenNode. Try again in a bit.' }, 502);
  }
}

// Answers the /api/bitcoin/... addresses, or null for anything else.
export function handleBitcoin(request, env, url, deliver = toServer) {
  if (url.pathname === '/api/bitcoin/prices') {
    const items = {};
    if (keyOf(env)) for (const [k, cents] of Object.entries(BITCOIN_PRICES)) items[k] = { bolts: ITEMS[k].bolts, amount: cents, currency: 'usd' };
    return json({ items });
  }
  if (url.pathname === '/api/bitcoin/checkout') return checkout(request, env, url);
  if (url.pathname === '/api/bitcoin/webhook') return webhook(request, env, deliver);
  if (url.pathname === '/api/bitcoin/check') return check(request, env, deliver);
  return null;
}
