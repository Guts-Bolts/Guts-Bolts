// The legal pages and "official site" files: privacy choices (Europe, US states, GPC), Google's
// Privacy & messaging tag, robots.txt, sitemap.xml, security.txt, and the new support topics.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { handleLegal, robotsTxt, sitemapXml, securityTxt, LEGAL_PAGES } from '../legal.js';
import { adMode, personalizedAds, messagesTagUrl } from '../../website/app/ads.js';

const read = (p) => readFileSync(new URL('../../' + p, import.meta.url), 'utf8');
const get = (path) => handleLegal(new Request('https://gutsandbolts.net' + path), new URL('https://gutsandbolts.net' + path));

test('"do not sell or share" and GPC turn off personalized ads, but still show ads outside Europe', () => {
  assert.equal(personalizedAds('', false), true);
  assert.equal(personalizedAds('all', false), true);
  assert.equal(personalizedAds('npa', false), false);
  assert.equal(personalizedAds('necessary', false), false);
  assert.equal(personalizedAds('all', true), false);   // GPC wins
  const us = { client: 'ca-pub-9280614939930921', slot: '1', consent: false };
  assert.equal(adMode(us, 'npa', {}), 'ad');
  const eu = Object.assign({}, us, { consent: true });
  assert.equal(adMode(eu, 'npa', {}), 'ask');           // Europe needs a real yes
});

test('Google\'s Privacy & messaging tag is built from the publisher ID, with ad-block recovery on', () => {
  assert.equal(messagesTagUrl('ca-pub-9280614939930921'), 'https://fundingchoicesmessages.google.com/i/pub-9280614939930921?ers=1');
  assert.equal(messagesTagUrl(''), '');
  assert.equal(messagesTagUrl('ca-pub-1"><script>'), '');
});

test('robots.txt, sitemap.xml, security.txt and gpc.json answer', async () => {
  const robots = await get('/robots.txt').text();
  assert.match(robots, /Sitemap: https:\/\/gutsandbolts\.net\/sitemap\.xml/);
  assert.match(robots, /Disallow: \/api/);
  assert.equal(robotsTxt('https://x.test'), robotsTxt('https://x.test'));
  const map = await get('/sitemap.xml').text();
  for (const p of LEGAL_PAGES) assert.ok(map.includes('<loc>https://gutsandbolts.net' + p + '</loc>'), p);
  assert.equal(sitemapXml('https://a.test').split('<url>').length - 1, 9);
  const sec = securityTxt('https://gutsandbolts.net', Date.UTC(2026, 10, 19));
  assert.match(sec, /^Contact: https:\/\/github\.com\/Guts-Bolts\/Guts-Bolts\/security\/advisories\/new$/m);
  assert.match(sec, /^Expires: 2027-11-19T00:00:00\.000Z$/m);
  assert.match(await get('/.well-known/security.txt').text(), /Policy: /);
  assert.deepEqual(await get('/.well-known/gpc.json').json(), { gpc: true, lastUpdate: '2026-11-19' });
  const terms = get('/terms');
  assert.equal(terms.status, 302);
  assert.equal(terms.headers.get('location'), 'https://gutsandbolts.net/app/#/terms');
  assert.equal(get('/app/'), null);
});

test('the new routes run the worker (so they get the security headers)', () => {
  const w = read('wrangler.jsonc');
  for (const p of ['/cookies', '/copyright', '/contact', '/terms', '/robots.txt', '/sitemap.xml', '/.well-known/security.txt', '/.well-known/gpc.json'])
    assert.ok(w.includes('"' + p + '"'), p);
  assert.match(read('worker/index.js'), /handleLegal\(request, url\)/);
});

test('the legal pages exist, link each other, have no inline scripts, and open Privacy choices', () => {
  for (const name of ['privacy', 'cookies', 'copyright', 'contact']) {
    const page = read('website/' + name + '.html');
    for (const other of ['/privacy', '/cookies', '/copyright', '/contact', '/app/#/terms']) assert.ok(page.includes('href="' + other + '"'), name + ' -> ' + other);
    assert.match(page, /data-privacy-choices/);
    assert.match(page, /src="\/legal\.js"/);
    assert.doesNotMatch(page, /<script(?![^>]*\bsrc=)[^>]*>/i);
    assert.doesNotMatch(page, /@gmail\.com|@yahoo\.|@outlook\./i);   // never a personal email
  }
  const privacy = read('website/privacy.html');
  assert.match(privacy, /US state privacy rights/);
  assert.match(privacy, /Global Privacy Control/);
  assert.match(read('website/copyright.html'), /penalty of perjury/);
  for (const f of ['website/app/index.html', 'website/index.html']) {
    const page = read(f);
    assert.match(page, /data-privacy-choices/, f);
    assert.match(page, /cookies"/, f);
    assert.match(page, /copyright"/, f);
  }
});

test('"My data" and "Copyright" support tickets work in both servers', () => {
  assert.match(read('worker/server.js'), /TICKET_TOPICS = \['password', 'account', 'underage', 'privacy', 'copyright', 'other'\]/);
  const cpp = read('src/server/ServerSupport.cpp');
  assert.match(cpp, /t == "privacy"/);
  assert.match(cpp, /t == "copyright"/);
  assert.match(read('website/app/app.js'), /\['privacy', 'My data/);
});
