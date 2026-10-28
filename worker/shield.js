// The risk check: it stands in front of the server and looks at every request first
// (worker/index.js calls it), so floods and bots stop here instead of reaching the
// server, which is one Durable Object doing one thing at a time.
//
//   Step 1  score every request cheaply: how fast its IP is going, and whether it looks
//           like a real browser or the Guts&Bolts apps (no waiting on anything else).
//   Step 2  challenge the doubtful ones. A browser gets a Turnstile check ("are you a
//           person?", usually just a tick that passes by itself) and then a pass for an
//           hour. The apps can't show one, so a doubtful app request gets a tighter speed
//           limit instead.
//   Step 3  the slow, expensive requests (signing up, logging in, emails, uploads,
//           buying...) have their own stricter limits inside the server, per IP and per
//           account: COSTLY in worker/server.js (and src/server/ServerShield.cpp).
//
// Turnstile is off until TURNSTILE_SITEKEY (a variable) and TURNSTILE_SECRET (a Secret)
// are set in Cloudflare; until then doubtful browsers get the tighter speed limit too.
// TURNSTILE_HOSTNAMES (optional, default gutsandbolts.net) lists the sites the check may
// come from, split by commas.

// Everything worth tuning. The per-minute limits are counted by Cloudflare (the
// "ratelimits" in wrangler.jsonc, which must say the same numbers); these copies are only
// used where those aren't there (testing on your own computer without them).
export const SHIELD = {
  limits: {
    RATE_FLOOD:    1200,   // requests a minute from one IP to /api and /ws, all together. Past it: blocked until the minute is up
    RATE_PICTURES: 3000,   // the same for pictures (/thumb, /icon...). They're cached, so this only stops floods
    RATE_BUSY:     300,    // past this many a minute, an IP looks doubtful (Step 1 adds points)
    RATE_DOUBT:    60,     // a minute, for doubtful requests that can't be challenged (Step 2)
    RATE_WS:       30,     // new app connections (/ws) a minute from one IP
  },
  // Step 1's points. A request is challenged at challengeAt and blocked at blockAt.
  points: {
    noAgent:        40,   // no User-Agent at all (every browser and the apps send one)
    toolAgent:      30,   // a scripting tool's User-Agent (curl, python-requests...)
    notLikeBrowser: 20,   // says it's a browser but has none of the headers browsers always send
    otherSite:      40,   // sent from a page on another website
    busy:           30,   // over RATE_BUSY this minute
  },
  challengeAt: 40,
  blockAt: 100,
  passMinutes: 60,          // how long a passed Turnstile check lasts
  pictureCacheSeconds: 300, // how long Cloudflare keeps a copy of a picture (so a flood of the same one never reaches the server)
};

const TOOL_AGENT = /\b(curl|wget|python|aiohttp|go-http-client|java\/|okhttp|libwww|httpclient|axios|node-fetch|undici|scrapy|httpx|headlesschrome|phantomjs|masscan|zgrab|nikto|sqlmap)\b/i;

// Cloudflare's counters, or (without them) a rough one in this copy of the worker.
const memory = new Map();   // key -> { start, n }
async function over(env, name, key) {
  const limiter = env[name];
  if (limiter && typeof limiter.limit === 'function') {
    try { return !(await limiter.limit({ key })).success; } catch { return false; }   // (never lock people out over a hiccup)
  }
  const t = Date.now(), k = name + '|' + key;
  let c = memory.get(k);
  if (!c || t - c.start >= 60000) {
    if (memory.size > 20000) for (const [mk, mc] of memory) if (t - mc.start >= 60000) memory.delete(mk);
    c = { start: t, n: 0 };
    memory.set(k, c);
  }
  return ++c.n > SHIELD.limits[name];
}

export const clientIp = (request) => request.headers.get('cf-connecting-ip') || request.headers.get('x-real-ip') || 'unknown';

function turnstileOn(env) { return !!(env.TURNSTILE_SITEKEY && env.TURNSTILE_SECRET); }

// --- the pass: proof that this browser passed a Turnstile check lately -----------------
// A cookie: "<expires>.<random>.<signature>", signed with the Turnstile secret, so only we can make one.
const enc = new TextEncoder();
let passKey = null, passKeyFor = '';
async function hmacKey(env) {
  if (passKeyFor !== env.TURNSTILE_SECRET) {
    passKey = await crypto.subtle.importKey('raw', enc.encode('gb-pass:' + env.TURNSTILE_SECRET), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign', 'verify']);
    passKeyFor = env.TURNSTILE_SECRET;
  }
  return passKey;
}
const toHex = (buf) => Array.from(new Uint8Array(buf), (b) => b.toString(16).padStart(2, '0')).join('');
const fromHex = (s) => new Uint8Array((s.match(/../g) || []).map((h) => parseInt(h, 16)));

async function makePass(env) {
  const body = (Math.floor(Date.now() / 1000) + SHIELD.passMinutes * 60) + '.' + toHex(crypto.getRandomValues(new Uint8Array(8)));
  return body + '.' + toHex(await crypto.subtle.sign('HMAC', await hmacKey(env), enc.encode(body)));
}

async function hasPass(request, env) {
  if (!turnstileOn(env)) return false;
  const m = /(?:^|;\s*)gb_pass=(\d+)\.([0-9a-f]{16})\.([0-9a-f]{64})(?:;|$)/.exec(request.headers.get('cookie') || '');
  if (!m || Number(m[1]) < Date.now() / 1000) return false;
  try {
    return await crypto.subtle.verify('HMAC', await hmacKey(env), fromHex(m[3]), enc.encode(m[1] + '.' + m[2]));
  } catch { return false; }
}

// --- Step 1: the score -------------------------------------------------------------------
function score(request, url) {
  const ua = request.headers.get('user-agent') || '';
  const p = SHIELD.points, why = [];
  let points = 0;
  const add = (name) => { points += p[name]; why.push(name); };
  if (!ua) add('noAgent');
  else if (TOOL_AGENT.test(ua)) add('toolAgent');
  const browser = /^Mozilla\//.test(ua);
  if (browser && !request.headers.get('accept-language') && !request.headers.get('sec-fetch-mode')) add('notLikeBrowser');
  const origin = request.headers.get('origin');
  if (origin && origin !== 'null' && origin !== url.origin) add('otherSite');
  return { points, why, browser };
}

const json = (obj, status, extra = {}) => new Response(JSON.stringify(obj), {
  status, headers: Object.assign({ 'content-type': 'application/json', 'cache-control': 'no-store' }, extra),
});
const slowDown = () => json({ ok: false, slowDown: true, error: 'Whoa, that\'s a lot of requests. Wait a minute and try again.' }, 429, { 'retry-after': '60' });

// Look at one request. Returns { stop: Response } to answer it here, or what the server should
// know about it: { ip, web (a browser), human (passed a check lately), sitekey (Turnstile is on) }.
//   kind: 'api' (/api), 'ws' (/ws, the apps' connection), 'mcp' (/mcp) or 'picture' (/thumb, /icon...)
export async function check(request, env, url, kind) {
  const ip = clientIp(request);
  if (kind === 'picture') return (await over(env, 'RATE_PICTURES', ip)) ? { stop: slowDown() } : { ip };
  if (await over(env, 'RATE_FLOOD', ip)) return { stop: slowDown() };
  if (kind === 'ws' && await over(env, 'RATE_WS', ip)) return { stop: slowDown() };
  // /mcp: a connected app's server, with a person's access token (worker/mcp.js). It looks like a bot
  // because it is one, so only the flood limit counts; the token and the relay's own limits do the rest.
  if (kind === 'mcp') return { ip };

  const s = score(request, url);
  if (await over(env, 'RATE_BUSY', ip)) { s.points += SHIELD.points.busy; s.why.push('busy'); }
  const human = await hasPass(request, env);
  const out = { ip, web: s.browser, human, sitekey: turnstileOn(env) ? env.TURNSTILE_SITEKEY : '' };
  if (human) return out;   // (a person passed a check: Step 3's limits still apply to them)
  if (s.points >= SHIELD.blockAt)
    return { stop: json({ ok: false, error: 'This request looks automated, so it was blocked.' }, 403) };
  if (s.points < SHIELD.challengeAt) return out;

  // Step 2: doubtful.
  if (kind === 'api' && s.browser && out.sitekey)
    return { stop: json({ ok: false, challenge: true, sitekey: out.sitekey, error: 'Quick check that you\'re a person, then try again.' }, 403) };
  if (await over(env, 'RATE_DOUBT', ip)) return { stop: slowDown() };
  return out;
}

// POST /api/turnstile { token }: check a finished Turnstile with Cloudflare and hand out a pass.
export async function passCheck(request, env, url) {
  if (request.method !== 'POST') return json({ ok: false, error: 'Use POST.' }, 405);
  if (!turnstileOn(env)) return json({ ok: false, error: 'The person check isn\'t turned on.' }, 404);
  const ip = clientIp(request);
  if (await over(env, 'RATE_FLOOD', ip) || await over(env, 'RATE_DOUBT', ip)) return slowDown();
  let token = '';
  try { token = (JSON.parse((await request.text()).slice(0, 4096)) || {}).token; } catch { token = ''; }
  const hostnames = new Set(String(env.TURNSTILE_HOSTNAMES || 'gutsandbolts.net').split(',').map((h) => h.trim()).filter(Boolean));
  const no = () => json({ ok: false, error: 'The check didn\'t pass. Try again.' }, 403);
  if (typeof token !== 'string' || !token || token.length > 2048) return no();
  let result;
  try {
    const r = await fetch('https://challenges.cloudflare.com/turnstile/v0/siteverify', {
      method: 'POST',
      headers: { 'content-type': 'application/x-www-form-urlencoded' },
      signal: AbortSignal.timeout(10000),
      body: new URLSearchParams({ secret: env.TURNSTILE_SECRET, response: token, remoteip: ip }),
    });
    if (!r.ok) return no();
    result = await r.json();
  } catch { return no(); }
  if (!result || result.success !== true || result.action !== 'gb_check' || !hostnames.has(result.hostname)) return no();
  const secure = url.protocol === 'https:' ? '; Secure' : '';
  return json({ ok: true }, 200, { 'set-cookie': 'gb_pass=' + await makePass(env) + '; Path=/; Max-Age=' + SHIELD.passMinutes * 60 + '; HttpOnly; SameSite=Strict' + secure });
}

// The request passed on to the server, with what the check found out. (Anything a visitor
// sent in these headers themselves is replaced, so the server can trust them.)
export function tagged(request, info) {
  const h = new Headers(request.headers);
  for (const k of ['x-gb-ip', 'x-gb-web', 'x-gb-human', 'x-gb-sitekey']) h.delete(k);
  h.set('x-gb-ip', info.ip || '');
  if (info.web) h.set('x-gb-web', '1');
  if (info.human) h.set('x-gb-human', '1');
  if (info.sitekey) h.set('x-gb-sitekey', info.sitekey);
  return new Request(request, { headers: h });
}

// Pictures: Cloudflare keeps a copy near the visitor for a few minutes, so asking for the
// same picture again (or a flood asking a million times) doesn't reach the server.
export async function cachedPicture(request, ctx, get) {
  if (request.method !== 'GET' || typeof caches === 'undefined') return get();
  const cache = caches.default;
  const hit = await cache.match(request);
  if (hit) {   // (browsers get the picture's own caching back)
    const out = new Response(hit.body, hit);
    out.headers.set('cache-control', hit.headers.get('x-gb-cache-control') || 'public, max-age=3600');
    out.headers.delete('x-gb-cache-control');
    return out;
  }
  const res = await get();
  if (res.status !== 200) return res;
  const copy = new Response(res.clone().body, res);
  copy.headers.set('x-gb-cache-control', res.headers.get('cache-control') || '');
  copy.headers.set('cache-control', 'public, max-age=' + SHIELD.pictureCacheSeconds);
  if (ctx && ctx.waitUntil) ctx.waitUntil(cache.put(request, copy));
  return res;
}
