// Bitcoin Bolts packs using Blockonomics Payment Intents (BTC checkout widget).
// The server creates USD-denominated intents, verifies callbacks by fetching the intent itself,
// and credits the game server only when Blockonomics reports status=paid. Credits are idempotent.
import { ITEMS } from './stripe.js';

export const BITCOIN_PRICES = { bolts_200: 199, bolts_500: 499, bolts_1100: 999, bolts_2400: 1999, bolts_6500: 4999 };
const API = 'https://www.blockonomics.co/api/v2';
const json = (obj, status = 200) => new Response(JSON.stringify(obj), { status, headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });
const keyOf = (env) => String(env.BLOCKONOMICS_API_KEY || '').trim();
const userOk = (v) => /^\d{1,12}$/.test(String(v || ''));
const intentIdOk = (v) => /^pi_[A-Za-z0-9]{8,80}$/.test(String(v || ''));
const nonce = () => Math.random().toString(36).slice(2, 10) || 'nonce';
export function orderData(user, item, n = nonce()) { return 'gb-' + user + '-' + item + '-' + n; }
export function parseOrder(data) {
  const m = /^gb-(\d{1,12})-(bolts_\d+)-[a-z0-9]{1,12}$/.exec(String(data || ''));
  return m && BITCOIN_PRICES[m[2]] ? { user: m[1], item: m[2] } : null;
}
export function bitcoinWork(intent) {
  if (!intent || typeof intent !== 'object' || !intentIdOk(intent.id) || intent.status !== 'paid') return null;
  const order = parseOrder(intent.extra_data);
  if (!order) return null;
  const amount = Math.round(Number(intent.amount) * 100);
  if (!Number.isFinite(amount) || amount !== BITCOIN_PRICES[order.item] || String(intent.currency || '').toUpperCase() !== 'USD') return null;
  return { kind: 'bolts', user: order.user, bolts: ITEMS[order.item].bolts, ref: 'blockonomics:' + intent.id, via: 'bitcoin' };
}
async function getIntent(env, id) {
  const r = await fetch(API + '/payment_intents/' + encodeURIComponent(id), { headers: { authorization: 'Bearer ' + keyOf(env) } });
  if (!r.ok) throw new Error('Blockonomics answered ' + r.status);
  const j = await r.json();
  return j && j.data || null;
}
async function toServer(env, job) {
  if (!env.GB_SERVER_OBJECT) return { ok: false, error: 'Payments only work on the Cloudflare server.' };
  const stub = env.GB_SERVER_OBJECT.get(env.GB_SERVER_OBJECT.idFromName('main'));
  const r = await stub.fetch(new Request('https://gb.internal/stripe-event', { method: 'POST', body: JSON.stringify(job) }));
  return r.json();
}
async function settle(env, id, deliver) {
  const intent = await getIntent(env, id);
  if (!intent || intent.id !== id) throw new Error('Blockonomics returned no matching payment intent');
  const job = bitcoinWork(intent);
  if (!job) return { paid: false, status: String(intent && intent.status || 'unknown') };
  const r = await deliver(env, job);
  if (!r || !r.ok) throw new Error(r && r.error || 'Could not credit Bolts');
  console.log('Blockonomics intent', id, 'user', job.user, 'credited');
  return { paid: true, status: 'paid', bolts: job.bolts };
}
async function checkout(request, env, url) {
  if (request.method !== 'POST') return json({ error: 'Use POST.' }, 405);
  if (!keyOf(env)) return json({ error: 'Bitcoin payments are not configured (BLOCKONOMICS_API_KEY is missing).' }, 503);
  let body = {}; try { body = JSON.parse(await request.text() || '{}') || {}; } catch {}
  const item = String(body.item || '');
  if (!BITCOIN_PRICES[item]) return json({ error: 'Only Bolts packs can be paid for with Bitcoin.' }, 400);
  if (!userOk(body.userId)) return json({ error: 'Log in first, so we know whose account to credit.' }, 400);
  const origin = /^https:\/\/([a-z0-9-]+\.)*gutsandbolts\.net$/.test(url.origin) || /^http:\/\/(localhost|127\.0\.0\.1)(:\d{1,5})?$/.test(url.origin) ? url.origin : 'https://gutsandbolts.net';
  const payload = { amount: (BITCOIN_PRICES[item] / 100).toFixed(2), currency: 'USD', extra_data: orderData(String(body.userId), item) };
  const storeMatch = String(env.BLOCKONOMICS_MATCH_CALLBACK || '').trim();
  if (storeMatch) payload.match_callback = storeMatch;
  const r = await fetch(API + '/payment_intents', {
    method: 'POST', headers: { authorization: 'Bearer ' + keyOf(env), 'content-type': 'application/json' }, body: JSON.stringify(payload),
  }).catch(() => null);
  const j = r ? await r.json().catch(() => ({})) : {};
  const intent = j && j.data || {};
  if (!r || !r.ok || !intentIdOk(intent.id)) {
    console.error('Blockonomics intent creation failed:', r ? r.status : 'no response', j && j.message);
    return json({ error: 'Could not start the Bitcoin checkout. Try again in a moment.' }, 502);
  }
  return json({ id: intent.id, url: origin + '/bitcoin-checkout.html?intent=' + encodeURIComponent(intent.id) });
}
async function callback(request, env, deliver) {
  if (request.method !== 'GET') return json({ error: 'Use GET.' }, 405);
  const secret = String(env.BLOCKONOMICS_CALLBACK_SECRET || '').trim();
  if (!secret || new URL(request.url).searchParams.get('secret') !== secret) return json({ error: 'Forbidden.' }, 403);
  const id = new URL(request.url).searchParams.get('id');
  if (!intentIdOk(id)) return json({ error: 'Invalid payment intent.' }, 400);
  try { await settle(env, id, deliver); } catch (e) {
    console.error('Blockonomics callback', id, String(e && e.message || e));
    return json({ error: 'Please retry.' }, 500);
  }
  return json({ received: true });
}
async function check(request, env, deliver) {
  if (request.method !== 'POST') return json({ error: 'Use POST.' }, 405);
  if (!keyOf(env)) return json({ error: 'Bitcoin payments are not configured yet.' }, 503);
  let body = {}; try { body = JSON.parse(await request.text() || '{}') || {}; } catch {}
  if (!intentIdOk(body.id)) return json({ error: 'Invalid payment intent.' }, 400);
  try { return json(await settle(env, body.id, deliver)); } catch {
    return json({ error: 'Could not check Blockonomics right now. Please retry.' }, 502);
  }
}
export function handleBitcoin(request, env, url, deliver = toServer) {
  if (url.pathname === '/api/bitcoin/prices') {
    const items = {};
    if (keyOf(env)) for (const [k, cents] of Object.entries(BITCOIN_PRICES)) items[k] = { bolts: ITEMS[k].bolts, amount: cents, currency: 'usd' };
    return json({ items });
  }
  if (url.pathname === '/api/bitcoin/checkout') return checkout(request, env, url);
  if (url.pathname === '/api/bitcoin/webhook') return callback(request, env, deliver);
  if (url.pathname === '/api/bitcoin/check') return check(request, env, deliver);
  return null;
}
