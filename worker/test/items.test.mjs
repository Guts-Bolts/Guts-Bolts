// Uploading and editing catalog items: no colour box, hats come from Studio, and
// shirts and pants are always a picture made from the template.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

// Just enough of a .png for the server to read its size.
function png(w, h) {
  const d = new Uint8Array(64);
  d.set([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a], 0);
  new DataView(d.buffer).setUint32(16, w);
  new DataView(d.buffer).setUint32(20, h);
  return Buffer.from(d).toString('base64');
}
const hatFile = Buffer.from(JSON.stringify({ format: 'gbaccessory', version: 1, kind: 'hat',
  node: { id: 1, name: 'Hat', kind: 'Model', children: [] } })).toString('base64');

test('shirts and pants need a template picture, and the colour is ignored', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const none = s.call(bob, 'upload', { kind: 'shirt', name: 'Plain', meta: { color: [255, 0, 0] }, data: '' });
  assert.equal(none.ok, false);
  assert.match(none.error, /template/);
  const wrong = s.call(bob, 'upload', { kind: 'pants', name: 'Tiny', data: png(10, 10) });
  assert.equal(wrong.ok, false);
  const r = s.call(bob, 'upload', { kind: 'shirt', name: 'Bolt Tee', meta: { color: [255, 0, 0] }, data: png(585, 559) });
  assert.ok(r.ok, r.error);
  const a = s.assets.get(r.asset.id);
  assert.equal(a.meta.color, undefined);
  assert.equal(a.meta.image, true);
});

test('hats are uploaded from Studio, never made from a colour and a shape', () => {
  const s = makeServer();
  const old = s.call(s.staff, 'upload', { kind: 'hat', name: 'Red Cap', meta: { color: [200, 0, 0], style: 2 }, data: '' });
  assert.equal(old.ok, false);
  assert.match(old.error, /Studio/);
  const r = s.call(s.staff, 'upload', { kind: 'hat', name: 'Studio Hat', meta: { color: [200, 0, 0] }, data: hatFile });
  assert.ok(r.ok, r.error);
  const a = s.assets.get(r.asset.id);
  assert.equal(a.meta.model, true);
  assert.equal(a.meta.color, undefined);
  assert.equal(a.meta.style, undefined);
});

test('editing an item ignores a colour', () => {
  const s = makeServer();
  const bob = s.signUp('bob2');
  const up = s.call(bob, 'upload', { kind: 'pants', name: 'Jeans', data: png(585, 559) });
  assert.ok(up.ok, up.error);
  const r = s.call(bob, 'item.edit', { id: up.asset.id, name: 'Blue Jeans', color: [0, 0, 255], offsaleAt: 0 });
  assert.ok(r.ok, r.error);
  const a = s.assets.get(up.asset.id);
  assert.equal(a.name, 'Blue Jeans');
  assert.equal(a.meta.color, undefined);
});
