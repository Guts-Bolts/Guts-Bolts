// The Guts&Bolts fonts: Inter for text and Plus Jakarta Sans for headings on the
// website, Inter Medium baked into Studio and the Player. All free (OFL).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';

const read = (p) => readFileSync(new URL('../../' + p, import.meta.url), 'utf8');
const has = (p) => existsSync(new URL('../../' + p, import.meta.url));

test('the font files and their free licences are there', () => {
  const font = read('website/fonts/font.css');
  assert.match(font, /--gb-font:\s*"GB Text"/);
  assert.match(font, /--gb-display:\s*"GB Display"/);
  for (const [, file] of font.matchAll(/url\("([^"]+)"\)/g)) assert.ok(has('website/fonts/' + file), file);
  for (const f of ['website/fonts/OFL.txt', 'website/fonts/OFL-PlusJakartaSans.txt', 'assets/fonts/Inter-Medium.otf', 'assets/fonts/OFL.txt'])
    assert.ok(has(f), f);
});

test('both website pages load the fonts, the polish and the light/dark switch', () => {
  const home = read('website/index.html'), app = read('website/app/index.html');
  assert.match(home, /href="fonts\/font\.css"/);
  assert.match(app, /href="\.\.\/fonts\/font\.css"/);
  assert.match(home, /href="home-polish\.css"/);
  assert.match(app, /href="polish\.css"/);
  assert.match(home, /<script src="theme\.js"><\/script>/);
  assert.match(app, /<script src="\.\.\/theme\.js"><\/script>/);
  // The switch must come after the stylesheets (it rewrites their dark-mode rules) and before </head> (no white flash).
  assert.ok(app.indexOf('polish.css') < app.indexOf('theme.js') && app.indexOf('theme.js') < app.indexOf('</head>'));
});

test('no old fonts left, except typewriter ones for code', () => {
  for (const p of ['website/app/app.css', 'website/app/polish.css', 'website/home-polish.css', 'website/index.html']) {
    for (const [, value] of read(p).matchAll(/font(?:-family)?:([^;}]+)/g)) {
      if (/inherit|monospace|var\(--gb-(font|display)\)/.test(value) || !/[A-Za-z]{4,}/.test(value.replace(/bold|normal/g, ''))) continue;
      assert.fail(`${p} still uses ${value.trim()}`);
    }
  }
});

test('dark mode can be picked by hand as well as following the device', () => {
  const js = read('website/theme.js');
  assert.match(js, /gb\.theme/);                   // remembered on this device
  assert.match(js, /prefers-color-scheme/);        // rewrites the dark-mode rules
  assert.match(js, /'not all'/);                   // light: dark rules off
  const css = read('website/app/polish.css');
  assert.match(css, /@media \(prefers-color-scheme: dark\)/);
  assert.match(css, /\.theme-toggle/);
});

test('Studio and the Player bake the font in and load it first', () => {
  assert.match(read('CMakeLists.txt'), /gb_embed\("\$\{CMAKE_SOURCE_DIR\}\/assets\/fonts\/Inter-Medium\.otf" kUiFontData/);
  const theme = read('src/editor/Theme.cpp');
  assert.ok(theme.indexOf('AddFontFromMemoryTTF') < theme.indexOf('segoeui.ttf'));
});
