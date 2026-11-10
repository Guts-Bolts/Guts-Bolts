// Bones in bundles: a body part can carry its own bones (Normal or Jiggle) and the
// weights that say which bone moves which corner, or just "bend" (elbows and knees).
// src/online/Protocol.cpp bundleRig checks the same things.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

function box() {
  const v = [];
  for (let i = 0; i < 8; i++) v.push(i & 1 ? 0.5 : -0.5, i & 2 ? 0.5 : -0.5, i & 4 ? 0.5 : -0.5);
  const f = [[0, 1, 3, 2], [4, 6, 7, 5], [0, 4, 5, 1], [2, 3, 7, 6], [0, 2, 6, 4], [1, 5, 7, 3]];
  return { v, f, size: [1, 2, 1], pos: [0, 0, 0] };
}
const bones = () => [
  { name: 'Upper', parent: -1, pos: [0, 1, 0], rot: [0, 0, 180] },
  { name: 'Floppy', parent: 0, pos: [0, 1, 0], rot: [0, 0, 0], type: 'Jiggle', stiffness: 0.5, damping: 0.3 },
];
const upload = (s, body) => s.call(s.staff, 'upload', { kind: 'bundle', name: 'Bendy', price: 0,
  data: Buffer.from(JSON.stringify({ format: 'gbbundle', body })).toString('base64'), meta: { body: true } });

test('a body part with bones and weights uploads', () => {
  const s = makeServer();
  const arm = { ...box(), bones: bones(), w: Array.from({ length: 8 }, (_, i) => [i < 4 ? 0 : 1, 1]) };
  const r = upload(s, { 'Left Arm': arm });
  assert.ok(r.ok, r.error);
});

test('arms and legs can bend, a head cannot', () => {
  const s = makeServer();
  assert.ok(upload(s, { 'Right Leg': { ...box(), bend: true } }).ok);
  const r = upload(s, { Head: { ...box(), bend: true } });
  assert.equal(r.ok, false);
  assert.match(r.error, /Only arms and legs can bend/);
});

test('wrong bones are turned down', () => {
  const s = makeServer();
  const later = bones();
  later[1].parent = 3;
  assert.equal(upload(s, { 'Left Arm': { ...box(), bones: later } }).ok, false, 'a bone hanging off one after it');
  const ik = bones();
  ik[1].type = 'IK';
  const r = upload(s, { 'Left Arm': { ...box(), bones: ik } });
  assert.equal(r.ok, false);
  assert.match(r.error, /Normal or Jiggle/);
  const many = Array.from({ length: 33 }, (_, i) => ({ name: 'B' + i, parent: -1, pos: [0, 0, 0], rot: [0, 0, 0] }));
  assert.match(upload(s, { 'Left Arm': { ...box(), bones: many } }).error, /too many bones/);
  const short = { ...box(), bones: bones(), w: [[0, 1]] };
  assert.equal(upload(s, { 'Left Arm': short }).ok, false, 'one weight per corner');
});
