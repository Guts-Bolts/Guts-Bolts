// The Products page (website/products.html) and the pages for the download apps
// (website/products/<id>.html), plus the "Apps to download" row on Home.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { APPS } from '../../website/app/products.js';

const read = (p) => readFileSync(new URL('../../' + p, import.meta.url), 'utf8');
const pages = ['website/products.html', 'website/products/sysfix.html', 'website/products/contentforge.html'];

test('every product page uses the shared red and white look and the light/dark switch', () => {
  for (const p of pages) {
    const s = read(p);
    assert.match(s, /href="\/products\/product\.css"/, p);
    assert.match(s, /src="\/theme\.js"/, p);
    assert.match(s, /<header class="topbar">/, p);
    assert.match(s, /name="viewport"/, p);
    assert.ok(!/Marketplace/.test(s), p + ' says Library, never Marketplace');
  }
  assert.ok(!existsSync(new URL('../../website/products.css', import.meta.url)), 'the old dark stylesheet is gone');
});

test('the product pages only link to things people can open', () => {
  // The Sysfix and ContentForge code is private on GitHub, so no links to it.
  for (const p of pages) assert.ok(!/github\.com\/Guts-Bolts\/(Sysfix|ContentForge)\b/i.test(read(p)), p);
});

test('each download app has a page, a card on the Products page and a card on Home', () => {
  const list = read('website/products.html');
  const app = read('website/app/app.js');
  assert.match(app, /APPS\.map/);
  for (const a of APPS) {
    assert.match(a.id, /^[a-z]+$/);
    assert.ok(a.name && a.blurb && a.platform, a.id);
    assert.ok(['out', 'test'].includes(a.stage), a.id);
    assert.equal(a.href.toLowerCase(), '/products/' + a.id);
    assert.ok(existsSync(new URL('../../website/products/' + a.id + '.html', import.meta.url)), a.id + ' has a page');
    assert.match(list, new RegExp('id="' + a.id + '"'), a.id + ' is on the Products page');
  }
});

test('ContentForge download stays off until the release is really on the mirror', () => {
  const cf = read('website/products/contentforge.html');
  const js = read('website/products/product.js');
  assert.match(cf, /class="dl off" data-release-check="\/download\/products\/contentforge\/latest\/ContentForge-Windows\.zip"/);
  assert.match(js, /method: 'HEAD'/);
  assert.match(js, /classList\.remove\('off'\)/);
});

test('the Worker serves /products and the product pages at short addresses', () => {
  const index = read('worker/index.js');
  const wrangler = read('wrangler.jsonc');
  assert.match(wrangler, /"\/products", "\/products\/\*"/);
  const m = index.match(/const productPage = (\/.*\/i)\.exec/);
  assert.ok(m, 'the route is there');
  const re = eval(m[1]);
  for (const ok of ['/products', '/products/', '/products/Sysfix', '/products/sysfix/', '/products/ContentForge']) assert.ok(re.test(ok), ok);
  for (const no of ['/products/sysfix.css', '/products/product.js', '/products/other', '/productsx']) assert.ok(!re.test(no), no);
  // Ask for the short asset address: a ".html" one gets a redirect back to the short one, forever.
  assert.ok(!/productPage\[1\][^;]*\.html'/.test(index));
});
