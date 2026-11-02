// The new Guts & Bolts logo picture (white GUTS, red BOLTS, gear and splatter).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const bytes = (p) => readFileSync(new URL('../../' + p, import.meta.url));
const read = (p) => bytes(p).toString('utf8');
const pngSize = (p) => { const b = bytes(p); assert.equal(b.toString('latin1', 1, 4), 'PNG', p); return [b.readUInt32BE(16), b.readUInt32BE(20)]; };

test('the logo pictures are real PNGs, wide like the logo', () => {
  for (const p of ['website/img/logo.png', 'assets/logo/logo.png', 'assets/logo/logo-full.png']) {
    const [w, h] = pngSize(p);
    assert.ok(w > h * 2.5 && w < h * 3.5, `${p} is ${w}x${h}`);
  }
});

test('both website top bars show the logo picture instead of text', () => {
  assert.match(read('website/index.html'), /<a class="logo" href="\.\/"><img src="img\/logo\.png" alt="Guts &amp; Bolts"/);
  assert.match(read('website/app/index.html'), /<a class="logo" href="#\/"><img src="\.\.\/img\/logo\.png" alt="Guts &amp; Bolts"/);
});

test('the Player bakes the logo in and draws it on the banner and loading screen', () => {
  assert.match(read('CMakeLists.txt'), /gb_embed\("\$\{CMAKE_SOURCE_DIR\}\/assets\/logo\/logo\.png" kLogoPng/);
  assert.match(read('src/renderer/Textures.cpp'), /id == kLogoPicture/);
  const app = read('src/player/PlayerApp.cpp');
  assert.equal((app.match(/Classic::logoPicture\(/g) || []).length, 2);
  assert.doesNotMatch(app, /Classic::logo\(dl/);
});
