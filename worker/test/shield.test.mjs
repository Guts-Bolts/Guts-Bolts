// The risk check: Step 1 scores requests (worker/shield.js), Step 2 challenges doubtful ones,
// Step 3 limits the expensive requests inside the server (COSTLY in server.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, device } from './helpers.mjs';
import { check, passCheck, SHIELD } from '../shield.js';

const site = new URL('https://gutsandbolts.net/api');
const BROWSER = { 'user-agent': 'Mozilla/5.0 (Windows NT 10.0) Chrome/130', 'accept-language': 'en', 'sec-fetch-mode': 'cors' };
let ipN = 1;
const req = (headers, ip = '10.0.0.' + ipN++) => new Request(site, { method: 'POST', headers: Object.assign({ 'cf-connecting-ip': ip }, headers) });

test('Step 1: browsers and the apps go straight through, tools and floods don\'t', async () => {
  for (const h of [BROWSER, { 'user-agent': 'GutsAndBolts' }]) {
    const r = await check(req(h), {}, site, 'api');
    assert.equal(r.stop, undefined);
  }
  // A script with no User-Agent is doubtful: no Turnstile here, so it gets the tighter limit.
  const ip = '10.9.9.9';
  let stopped = 0;
  for (let i = 0; i < SHIELD.limits.RATE_DOUBT + 5; i++) if ((await check(req({}, ip), {}, site, 'api')).stop) stopped++;
  assert.equal(stopped, 5);
  // Too many from one IP is a flood, whoever it is.
  const flood = '10.8.8.8';
  let last;
  for (let i = 0; i <= SHIELD.limits.RATE_FLOOD; i++) last = await check(req(BROWSER, flood), {}, site, 'api');
  assert.equal(last.stop.status, 429);
});

test('Step 1: a Cloudflare rate-limit error falls back to the local counter', async () => {
  const ip = '10.6.6.6';
  const broken = { RATE_FLOOD: { limit: async () => { throw new Error('simulated Cloudflare outage'); } } };
  for (let i = 0; i < SHIELD.limits.RATE_FLOOD; i++) {
    const r = await check(req(BROWSER, ip), broken, site, 'api');
    assert.equal(r.stop, undefined);
  }
  const blocked = await check(req(BROWSER, ip), broken, site, 'api');
  assert.equal(blocked.stop.status, 429);
});

test('Step 2: a doubtful browser is asked for the person check once Turnstile is on', async () => {
  const env = { TURNSTILE_SITEKEY: 'site-key', TURNSTILE_SECRET: 'shh' };
  const r = await check(req({ 'user-agent': 'Mozilla/5.0', origin: 'https://elsewhere.example' }), env, site, 'api');
  assert.equal(r.stop.status, 403);
  const body = await r.stop.json();
  assert.equal(body.challenge, true);
  assert.equal(body.sitekey, 'site-key');
  // A normal visitor isn't, but the server learns Turnstile is on (for signing up).
  const ok = await check(req(BROWSER), env, site, 'api');
  assert.equal(ok.stop, undefined);
  assert.equal(ok.sitekey, 'site-key');
  assert.equal(ok.web, true);
  // A forged pass doesn't count.
  const forged = await check(req(Object.assign({ cookie: 'gb_pass=9999999999.0011223344556677.' + 'ab'.repeat(32) }, BROWSER)), env, site, 'api');
  assert.equal(forged.human, false);
});

test('Step 2: passing the check gives a pass that lets a doubtful browser through', async () => {
  const env = { TURNSTILE_SITEKEY: 'site-key', TURNSTILE_SECRET: 'shh' };
  const realFetch = globalThis.fetch;
  let answer = { success: true, action: 'gb_check', hostname: 'gutsandbolts.net' };
  globalThis.fetch = async () => new Response(JSON.stringify(answer));
  try {
    const send = () => passCheck(new Request('https://gutsandbolts.net/api/turnstile', { method: 'POST', body: JSON.stringify({ token: 'tok' }),
      headers: { 'cf-connecting-ip': '10.7.7.' + ipN++ } }), env, new URL('https://gutsandbolts.net/api/turnstile'));
    const ok = await send();
    assert.equal(ok.status, 200);
    const cookie = ok.headers.get('set-cookie');
    assert.match(cookie, /^gb_pass=\d+\.[0-9a-f]{16}\.[0-9a-f]{64}; .*HttpOnly; SameSite=Strict; Secure$/);
    const doubtful = { 'user-agent': 'Mozilla/5.0', origin: 'https://elsewhere.example', cookie: cookie.split(';')[0] };
    const r = await check(req(doubtful), env, site, 'api');
    assert.equal(r.stop, undefined);
    assert.equal(r.human, true);
    // Checks made for another site or another action don't count.
    answer = { success: true, action: 'other', hostname: 'gutsandbolts.net' };
    assert.equal((await send()).status, 403);
    answer = { success: true, action: 'gb_check', hostname: 'evil.example' };
    assert.equal((await send()).status, 403);
    answer = { success: false };
    assert.equal((await send()).status, 403);
  } finally {
    globalThis.fetch = realFetch;
  }
});

test('Step 2: signing up on the website needs the person check (once it\'s on)', () => {
  const s = makeServer();
  const d = device(s, 41);
  const r = s.handle({ op: 'account.signup' }, {});   // (unsigned: just checks it's refused properly)
  assert.equal(r.ok, false);
  const signedUp = (from) => {
    const dd = device(s, 42 + (ipN++ % 100));
    const q = dd.call('ping');
    assert.ok(q.ok, q.error);
    return s.costlyCheck('account.signup', s.user(dd.account), from);
  };
  assert.equal(signedUp({ web: true, sitekey: 'k' }).stop.challenge, true);
  assert.equal(signedUp({ web: true, sitekey: 'k', human: true }).stop, undefined);
  assert.equal(signedUp({ web: false, sitekey: 'k' }).stop, undefined, 'the apps can\'t show it');
  assert.equal(signedUp({ web: true }).stop, undefined, 'Turnstile is off');
  assert.ok(d.account);
});

test('Step 3: expensive requests have their own limits, per account and per IP', () => {
  const s = makeServer();
  // Wrong logins count every time (guesses), from one device...
  const d = signer(60);
  const [each, perIp] = [20, 60];
  let r;
  for (let i = 0; i < each; i++) {
    r = s.handle(d.call('account.login', { username: 'nobody', auth: 'x' }), { ip: '1.2.3.4' });
    assert.equal(r.slowDown, undefined, r.error);
  }
  r = s.handle(d.call('account.login', { username: 'nobody', auth: 'x' }), { ip: '1.2.3.4' });
  assert.equal(r.slowDown, true);
  assert.match(r.error, /too often/);
  // ...and from one IP across lots of devices.
  let stopped = false;
  for (let n = 0; n < perIp / each + 2 && !stopped; n++) {
    const dd = signer(70 + n);
    for (let i = 0; i < each; i++) if (s.handle(dd.call('account.login', { username: 'nobody', auth: 'x' }), { ip: '5.6.7.8' }).slowDown) stopped = true;
  }
  assert.ok(stopped, 'the IP ran out');
  // Staff have no limit.
  for (let i = 0; i < 5; i++) assert.equal(s.costlyCheck('forum.post', s.staff, { ip: '1.1.1.1' }).stop, undefined);
});

test('Step 3: only requests that worked count (except guesses), and new accounts get half', () => {
  const s = makeServer();
  const u = s.signUp('newbie');
  u.created = Math.floor(Date.now() / 1000);   // joined just now
  const half = 10 / 2;   // forum.post: 10 per account
  for (let i = 0; i < 20; i++) {   // failing ones don't count
    const g = s.costlyCheck('forum.post', u, {});
    assert.equal(g.stop, undefined);
  }
  for (let i = 0; i < half; i++) s.costlyCheck('forum.post', u, {}).count();
  assert.equal(s.costlyCheck('forum.post', u, {}).stop.slowDown, true);
  const old = s.signUp('oldtimer');
  old.created = 1;
  for (let i = 0; i < half; i++) s.costlyCheck('forum.post', old, {}).count();
  assert.equal(s.costlyCheck('forum.post', old, {}).stop, undefined);
});

// A device whose call() hands back the signed request instead of sending it.
const signer = (n) => device({ handle: (q) => q }, n);
