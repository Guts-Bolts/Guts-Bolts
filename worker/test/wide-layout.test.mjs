// "Room to Breathe": the website fills big screens instead of sitting in a narrow
// 1060px column. website/wide.css is loaded last by both pages.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = (p) => readFileSync(new URL('../../' + p, import.meta.url), 'utf8');

test('both pages load wide.css after the other stylesheets and before the theme switch', () => {
  const app = read('website/app/index.html'), home = read('website/index.html');
  assert.match(app, /href="\.\.\/wide\.css"/);
  assert.match(home, /href="wide\.css"/);
  const lastApp = Math.max(...[...app.matchAll(/rel="stylesheet" href="([^"]+)"/g)].map((m) => m.index));
  assert.equal(lastApp, app.indexOf('rel="stylesheet" href="../wide.css"'), 'wide.css is the last stylesheet in app/index.html');
  assert.ok(app.indexOf('wide.css') < app.indexOf('theme.js'));
  assert.ok(home.indexOf('home-polish.css') < home.indexOf('wide.css') && home.indexOf('wide.css') < home.indexOf('theme.js'));
});

test('the page is much wider than the old 1060px box, with growing side gaps', () => {
  const css = read('website/wide.css');
  const width = Number(css.match(/--gb-page-width:\s*(\d+)px/)[1]);
  assert.ok(width >= 1600, 'page width ' + width);
  assert.match(css, /--gb-gutter:\s*clamp\(/);
  assert.match(css, /\.page[^{]*\{[^}]*max-width:\s*var\(--gb-page-width\)/s);
  // The video player never gets taller than the window.
  assert.match(css, /\.video-watch \.video-stage \{[^}]*max-height:\s*calc\(100vh/);
});
