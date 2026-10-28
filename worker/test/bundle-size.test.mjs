// Bundles and body parts made in Studio can be up to 8 MB (it used to be 1 MB, too small for
// a detailed body). src/online/Protocol.cpp maxSize has the same limit.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

// A detailed mesh: a strip of `n` triangles, written with lots of decimals (like Studio does).
function detailed(n) {
  const v = [], f = [];
  for (let i = 0; i < n + 2; i++) v.push(Math.sin(i) * 0.987654321, Math.cos(i) * 0.123456789, (i % 7) / 7.123456);
  for (let i = 0; i < n; i++) f.push([i, i + 1, i + 2]);
  return { v, f, size: [2, 2, 1], pos: [0, 1.5, 0] };
}
const upload = (s, kind, body, meta) => s.call(s.staff, 'upload', { kind, name: 'Detailed Body', price: 0,
  data: Buffer.from(JSON.stringify({ format: 'gbbundle', body })).toString('base64'), meta });

test('a bundle bigger than 1 MB uploads now', () => {
  const s = makeServer();
  const body = { Torso: detailed(19000), Head: detailed(19000) };
  assert.ok(JSON.stringify(body).length > 1.5 * 1024 * 1024, 'the test body really is over 1 MB');
  const r = upload(s, 'bundle', body, { body: true });
  assert.ok(r.ok, r.error);
  const p = upload(s, 'bodypart', { Torso: detailed(19000) }, { bodyPart: 'Torso' });
  assert.ok(p.ok, p.error);
});

test('over 8 MB is still too big, and the message says so in MB', () => {
  const s = makeServer();
  const r = s.call(s.staff, 'upload', { kind: 'bundle', name: 'Huge', price: 0,
    data: Buffer.alloc(8 * 1024 * 1024 + 1, 32).toString('base64'), meta: { body: true } });
  assert.equal(r.ok, false);
  assert.match(r.error, /the most is 8 MB/);
});
