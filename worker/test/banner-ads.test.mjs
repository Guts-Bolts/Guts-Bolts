// Banner ads (Google AdSense): ads.txt, the settings the website reads, which pages get ads,
// no ads for Boltworks Club, the cookie question in Europe, and the security headers.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { adsTxt, adsConfig, adClient, adSlot, needsConsent, handleAds } from '../ads.js';
import { withSecurityHeaders } from '../security.js';
import { adSpotWanted, adMode, AD_PAGES } from '../../website/app/ads.js';

const read = (p) => readFileSync(new URL('../../' + p, import.meta.url), 'utf8');
const from = (country) => Object.assign(new Request('https://gutsandbolts.net/ads-config'), country ? { cf: { country } } : {});
const env = { ADSENSE_CLIENT: 'ca-pub-9280614939930921', ADSENSE_SLOT: '1234567890' };

test('ads.txt names the AdSense account, in the format Google asks for', () => {
  assert.equal(adsTxt(env), 'google.com, pub-9280614939930921, DIRECT, f08c47fec0942fa0\n');
  assert.equal(adsTxt({ ADSENSE_CLIENT: 'pub-9280614939930921' }), 'google.com, pub-9280614939930921, DIRECT, f08c47fec0942fa0\n');
  assert.match(adsTxt({}), /^#/);
});

test('the publisher ID is set in wrangler.jsonc and the ad routes run the worker', () => {
  const w = read('wrangler.jsonc');
  assert.match(w, /"ADSENSE_CLIENT": "ca-pub-9280614939930921"/);
  for (const p of ['/ads.txt', '/ads-config', '/privacy', '/privacy.html']) assert.ok(w.includes('"' + p + '"'), p);
});

test('bad IDs are ignored', () => {
  assert.equal(adClient({ ADSENSE_CLIENT: 'ca-pub-12<script>' }), '');
  assert.equal(adSlot({ ADSENSE_SLOT: 'abc' }), '');
  assert.equal(adSlot(env), '1234567890');
});

test('Europe, the UK and Switzerland get the cookie question; the US does not', () => {
  for (const c of ['DE', 'FR', 'GB', 'NO', 'CH', 'IE']) assert.equal(needsConsent(from(c)), true, c);
  for (const c of ['US', 'CA', 'BR', 'JP']) assert.equal(needsConsent(from(c)), false, c);
  assert.equal(needsConsent(from('')), true);   // unknown: ask, to be safe
  assert.equal(adsConfig(from('US'), env).consent, false);
});

test('/ads.txt and /ads-config answer, other paths are left alone', async () => {
  const txt = handleAds(from('US'), env, new URL('https://gutsandbolts.net/ads.txt'));
  assert.match(txt.headers.get('content-type'), /text\/plain/);
  assert.match(await txt.text(), /pub-9280614939930921/);
  const cfg = await handleAds(from('FR'), env, new URL('https://gutsandbolts.net/ads-config')).json();
  assert.deepEqual(cfg, { client: 'ca-pub-9280614939930921', slot: '1234567890', consent: true, googleConsent: false });
  assert.equal(handleAds(from('US'), env, new URL('https://gutsandbolts.net/app/')), null);
});

test('ads only go on clean pages, and never for Boltworks Club members', () => {
  for (const p of ['home', 'games', 'catalog', 'updates']) assert.equal(adSpotWanted(p, { club: '' }), true, p);
  for (const p of ['forum', 'game', 'item', 'user', 'chat', 'messages', 'settings', 'staff', 'library', 'group'])
    assert.equal(adSpotWanted(p, { club: '' }), false, p);
  for (const tier of ['bwc', 'tbc', 'obc']) assert.equal(adSpotWanted('home', { club: tier }), false, tier);
  assert.equal(adSpotWanted('home', null), true);   // signed out
  assert.ok(!AD_PAGES.has('forum'));
});

test('what the ad spot shows', () => {
  const on = { client: 'ca-pub-9280614939930921', slot: '1', consent: false };
  assert.equal(adMode({}, '', { staff: true }), 'placeholder');   // not set up: staff see where it goes
  assert.equal(adMode({}, '', {}), '');                            // ...and everyone else sees nothing
  assert.equal(adMode(on, '', {}), 'ad');
  const eu = Object.assign({}, on, { consent: true });
  assert.equal(adMode(eu, '', {}), 'ask');
  assert.equal(adMode(eu, 'all', {}), 'ad');
  assert.equal(adMode(eu, 'necessary', {}), '');                   // "Only necessary" = no ads
  assert.equal(adMode(Object.assign({}, eu, { googleConsent: true }), '', {}), 'ad');   // Google asks instead
});

test('the security headers let Google\'s ad code load', () => {
  const r = withSecurityHeaders(new Response('<!doctype html>', { headers: { 'content-type': 'text/html' } }), new Request('https://gutsandbolts.net/'));
  const csp = r.headers.get('content-security-policy');
  const part = (name) => csp.split('; ').find((d) => d.startsWith(name + ' '));
  assert.match(part('script-src'), /https:\/\/pagead2\.googlesyndication\.com/);
  assert.match(part('frame-src'), /https:\/\/\*\.doubleclick\.net/);
  assert.match(part('connect-src'), /https:\/\/\*\.adtrafficquality\.google/);
  assert.doesNotMatch(part('script-src'), /unsafe-inline/);
});

test('the website wires it up: ads after each page, a Privacy page linked from the footers', () => {
  const app = read('website/app/app.js');
  assert.match(app, /import \{ placeAds \} from '\.\/ads\.js'/);
  assert.match(app, /placeAds\(name, me\)/);
  assert.match(read('website/app/ads.js'), /Want to remove ads\? /);
  assert.match(read('website/app/ads.js'), /Join Boltworks Club today!/);
  assert.match(read('website/app/index.html'), /href="ads\.css"/);
  assert.match(read('website/app/index.html'), /href="\.\.\/privacy"/);
  assert.match(read('website/index.html'), /href="privacy"/);
  const privacy = read('website/privacy.html');
  assert.match(privacy, /policies\.google\.com\/technologies\/partner-sites/);
  assert.doesNotMatch(privacy, /<script(?![^>]*\bsrc=)[^>]*>/i);   // no inline scripts (the CSP blocks them)
});
