// Guts&Bolts as a platform: Home is for the whole site, game-only pages live under
// Games, and the product list in website/app/products.js drives the top bar's second row.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { PRODUCTS, productOf, productById, activeLink } from '../../website/app/products.js';

const read = (p) => readFileSync(new URL('../../' + p, import.meta.url), 'utf8');
const app = read('website/app/app.js') + read('website/app/art.js');   // (art.js adds the Art pages)
const page = read('website/app/index.html');

test('every product has the bits the site needs', () => {
  const ids = new Set();
  for (const p of PRODUCTS) {
    assert.match(p.id, /^[a-z]+$/);
    assert.ok(!ids.has(p.id), 'ids are unique: ' + p.id); ids.add(p.id);
    assert.ok(['open', 'soon', 'studio'].includes(p.status), p.id);
    assert.ok(p.name && p.blurb && p.href, p.id);
    if (p.status !== 'open') assert.ok((p.plans || []).length, p.id + ' says what is planned');
  }
});

test('each page belongs to at most one product, and each one is a real page', () => {
  const seen = new Map();
  for (const p of PRODUCTS) for (const name of p.pages) {
    assert.ok(!seen.has(name), `${name} is in both ${seen.get(name)} and ${p.id}`);
    seen.set(name, p.id);
    assert.match(app, new RegExp(`pages\\.${name} = `), `pages.${name} exists in app.js`);
  }
});

test('game-only pages are under Games; site-wide pages belong to no product', () => {
  for (const name of ['games', 'game', 'catalog', 'item', 'create', 'library', 'configure', 'events'])
    assert.equal(productOf(name).id, 'games', name);
  for (const name of ['home', 'user', 'settings', 'updates', 'notifications', 'bolts', 'avatar'])
    assert.equal(productOf(name), null, name);
  assert.equal(productOf('forum').id, 'communities');
  assert.equal(productById('videos').status, 'soon');
  assert.equal(productById('nope'), null);
});

test('the second bar lights the right link', () => {
  assert.equal(activeLink('games', '#/games'), 'games');
  assert.equal(activeLink('games', '#/games?sort=new'), 'games-list');
  assert.equal(activeLink('game', '#/game/12'), 'games');
  assert.equal(activeLink('item', '#/item/5'), 'catalog');
  assert.equal(activeLink('create', '#/create/library'), 'library');
  assert.equal(activeLink('create', '#/create/games'), 'create');
});

test('the top bar has a link for every product, and no game-only links of its own', () => {
  for (const p of PRODUCTS) assert.match(page, new RegExp(`data-product="${p.id}"`), p.id);
  const nav = page.slice(page.indexOf('<nav class="nav">'), page.indexOf('<nav class="section-bar"'));
  for (const gameOnly of ['#/catalog', '#/create', '#/events']) assert.ok(!nav.includes(`href="${gameOnly}"`), gameOnly);
  assert.match(page, /<nav class="section-bar" id="sectionBar"/);
  assert.match(page, /href="platform\.css"/);
  // The chat badge finds the first #nav a[data-page=chat]: the top bar's Chat link.
  assert.ok(nav.includes('data-page="chat"'));
});

test('Home is the platform home; the old game front page is the Games home', () => {
  assert.match(app, /const gamesHome = async \(\) =>/);
  assert.match(app, /if \(!mine && !location\.hash\.includes\('\?'\)\) \{ await gamesHome\(\); return; \}/);
  const home = app.slice(app.indexOf('pages.home = async'), app.indexOf('pages.soon = async'));
  assert.match(home, /GROUPS\.filter/, 'Home shows the products in their groups (products.js GROUPS)');
  assert.ok(!home.includes('auction'), 'auctions are on the Games home, not Home');
  const games = app.slice(app.indexOf('const gamesHome'), app.indexOf('pages.home = async'));
  assert.match(games, /<div id="liveAuctions">/);
  assert.match(games, /Featured Games/);
});
