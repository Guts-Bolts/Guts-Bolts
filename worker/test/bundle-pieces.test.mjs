// Bundle pieces: each part of a body bundle can be worn on its own (the Avatar page's Body
// parts section). A piece's id is "<bundle id>-<part>", like "137-leftarm".
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const cube = () => ({
  v: [-0.5, -0.5, -0.5, 0.5, -0.5, -0.5, 0.5, 0.5, -0.5, -0.5, 0.5, -0.5, -0.5, -0.5, 0.5, 0.5, -0.5, 0.5, 0.5, 0.5, 0.5, -0.5, 0.5, 0.5],
  f: [[0, 3, 2, 1], [4, 5, 6, 7], [0, 1, 5, 4], [2, 3, 7, 6], [1, 2, 6, 5], [0, 4, 7, 3]],
  size: [1, 1, 0.5], pos: [0, 1.5, 0],
});
const bodyBundle = (s, name, body) => s.call(s.staff, 'upload', { kind: 'bundle', name, price: 0,
  data: Buffer.from(JSON.stringify({ format: 'gbbundle', body })).toString('base64'), meta: { body: true } });
const look = (wearing) => ({ head: [1, 2, 3], torso: [1, 2, 3], leftArm: [1, 2, 3], rightArm: [1, 2, 3], leftLeg: [1, 2, 3], rightLeg: [1, 2, 3], wearing });

test('you can wear one part of a body bundle you own, and it shows like a body part', () => {
  const s = makeServer(), bob = s.signUp('bob');
  const robot = bodyBundle(s, 'Robot Body', { Torso: cube(), 'Left Arm': cube() });
  assert.ok(robot.ok, robot.error);
  const id = robot.asset.id;
  assert.ok(s.call(bob, 'buy', { id }).ok);
  const set = s.call(bob, 'avatar.set', { avatar: look([id + '-leftarm']) });
  assert.ok(set.ok, set.error);
  assert.deepEqual(bob.avatar.wearing, [id + '-leftarm']);
  const p = s.call(bob, 'profile', { id: String(bob.userId) });
  const worn = p.wearing.find((w) => w.id === id + '-leftarm');
  assert.ok(worn, 'the profile lists the piece');
  assert.equal(worn.kind, 'bodypart');
  assert.equal(worn.meta.bodyPart, 'Left Arm');
  assert.equal(worn.meta.bundle, id, 'its body comes from the bundle (matched by ID)');
  assert.equal(worn.name, 'Robot Body: Left Arm');
});

test('pieces of bundles you don\'t own, or that aren\'t real, are left off', () => {
  const s = makeServer(), bob = s.signUp('bob');
  const robot = bodyBundle(s, 'Robot Body', { Torso: cube() });
  const id = robot.asset.id;
  assert.ok(s.call(bob, 'avatar.set', { avatar: look([id + '-torso', id + '-tail', 'nope-torso', 'item-headless-head-torso']) }).ok);
  assert.deepEqual(bob.avatar.wearing, [], 'not bought yet, and the others aren\'t pieces');
  // An items-only bundle has no body, so it has no pieces.
  s.add(bob, 5000, 'test', 'test');
  assert.ok(s.call(bob, 'buy', { id: 'item-headless-head' }).ok);
  assert.ok(s.call(bob, 'avatar.set', { avatar: look(['item-headless-head-head']) }).ok);
  assert.deepEqual(bob.avatar.wearing, []);
});

test('/wear/ sends just the one part of a piece', async () => {
  const s = makeServer();
  const robot = bodyBundle(s, 'Robot Body', { Torso: cube(), Head: cube() });
  const id = robot.asset.id;
  const res = await s.fetch(new Request('https://gutsandbolts.net/wear/' + id + '-head'));
  assert.equal(res.status, 200);
  const j = await res.json();
  assert.equal(j.format, 'gbbundle');
  assert.deepEqual(Object.keys(j.body), ['Head']);
  // A part the bundle doesn't change has no mesh to send.
  assert.equal((await s.fetch(new Request('https://gutsandbolts.net/wear/' + id + '-leftleg'))).status, 404);
});

test('outfits keep bundle pieces', () => {
  const s = makeServer(), bob = s.signUp('bob');
  const robot = bodyBundle(s, 'Robot Body', { Torso: cube() });
  const id = robot.asset.id;
  assert.ok(s.call(bob, 'buy', { id }).ok);
  assert.ok(s.call(bob, 'avatar.set', { avatar: look([id + '-torso']) }).ok);
  const saved = s.call(bob, 'outfit.save', { name: 'Robo' });
  assert.ok(saved.ok, saved.error);
  assert.ok(saved.outfits[0].wearing.some((w) => w.id === id + '-torso' && w.meta.bodyPart === 'Torso'));
  assert.ok(s.call(bob, 'avatar.set', { avatar: look([]) }).ok);
  const worn = s.call(bob, 'outfit.wear', { id: saved.outfits[0].id });
  assert.ok(worn.ok, worn.error);
  assert.deepEqual(bob.avatar.wearing, [id + '-torso']);
});
