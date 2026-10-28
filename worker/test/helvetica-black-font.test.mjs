// One heavy font everywhere (Helvetica Black if you have it, Inter Black if not).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';

const read = (p) => readFileSync(new URL('../../' + p, import.meta.url), 'utf8');

test('the font file and its free licence are there', () => {
  const font = read('website/fonts/font.css');
  assert.match(font, /local\("Helvetica Black"\)/);
  assert.match(font, /url\("Inter-Black\.woff2"\)/);
  for (const f of ['website/fonts/Inter-Black.woff2', 'website/fonts/OFL.txt', 'assets/fonts/Inter-Black.otf', 'assets/fonts/OFL.txt'])
    assert.ok(existsSync(new URL('../../' + f, import.meta.url)), f);
});

test('both website pages load the font', () => {
  assert.match(read('website/index.html'), /href="fonts\/font\.css"/);
  assert.match(read('website/app/index.html'), /href="\.\.\/fonts\/font\.css"/);
});

test('no old fonts left, except typewriter ones for code', () => {
  for (const p of ['website/app/app.css', 'website/index.html']) {
    for (const [, value] of read(p).matchAll(/font(?:-family)?:([^;}]+)/g)) {
      if (/inherit|monospace|var\(--gb-font\)/.test(value) || !/[A-Za-z]{4,}/.test(value.replace(/bold|normal/g, ''))) continue;
      assert.fail(`${p} still uses ${value.trim()}`);
    }
  }
});

test('Studio and the Player bake the font in and load it first', () => {
  assert.match(read('CMakeLists.txt'), /gb_embed\("\$\{CMAKE_SOURCE_DIR\}\/assets\/fonts\/Inter-Black\.otf" kUiFontData/);
  const theme = read('src/editor/Theme.cpp');
  assert.ok(theme.indexOf('AddFontFromMemoryTTF') < theme.indexOf('segoeui.ttf'));
});
