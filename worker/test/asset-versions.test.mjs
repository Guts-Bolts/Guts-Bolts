// Asset versions: sounds, decals, models, plugins and animations keep their old versions like
// games do, so a bad update can be put back. The asset keeps its ID, so everything using it
// gets the restored one.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const model = (name) => Buffer.from(JSON.stringify({ format: 'gbmodel', nodes: [{ id: 1, name, kind: 'Part' }] })).toString('base64');
const text = (s, id) => new TextDecoder().decode(s.readFile(id));

test('updating a model keeps its old versions, and one can be put back', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const up = s.call(bob, 'upload', { kind: 'model', name: 'Crate', data: model('Crate') });
  assert.ok(up.ok, up.error);
  const id = up.asset.id;
  let r = s.call(bob, 'update', { id, data: model('Crate2') });
  assert.ok(r.ok, r.error);
  assert.equal(r.newVersion, true);
  assert.equal(r.version, 2);
  r = s.call(bob, 'update', { id, data: model('Broken') });
  assert.equal(r.version, 3);
  const list = s.call(bob, 'asset.versions', { id });
  assert.ok(list.ok, list.error);
  assert.deepEqual(list.versions.map((x) => x.v), [3, 2, 1]);
  assert.equal(list.versions[0].live, true);
  const back = s.call(bob, 'asset.restore', { id, version: 2 });
  assert.ok(back.ok, back.error);
  assert.equal(back.version, 4);
  assert.match(text(s, id), /Crate2/);
  assert.equal(back.versions[0].restored, 2);
  assert.equal(s.call(bob, 'asset.versions', { id }).versions.length, 4);
});

test('the same file again adds no version', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const id = s.call(bob, 'upload', { kind: 'model', name: 'Same', data: model('A') }).asset.id;
  const r = s.call(bob, 'update', { id, data: model('A') });
  assert.ok(r.ok, r.error);
  assert.equal(r.newVersion, false);
  assert.equal(s.call(bob, 'asset.versions', { id }).versions.length, 1);
});

test('only the creator can see or restore versions', () => {
  const s = makeServer();
  const bob = s.signUp('bob'), eve = s.signUp('eve');
  const id = s.call(bob, 'upload', { kind: 'model', name: 'Mine', data: model('X') }).asset.id;
  s.call(bob, 'update', { id, data: model('Y') });
  assert.equal(s.call(eve, 'asset.versions', { id }).ok, false);
  assert.equal(s.call(eve, 'asset.restore', { id, version: 1 }).ok, false);
  assert.match(text(s, id), /"Y"/);
});

test('game.* still only means games, and asset.* works for games too', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const m = s.call(bob, 'upload', { kind: 'model', name: 'M', data: model('M') }).asset.id;
  assert.equal(s.call(bob, 'game.versions', { id: m }).ok, false);
  const scene = Buffer.from(JSON.stringify({ format: 'gbscene', info: { title: 'G' }, nodes: [] })).toString('base64');
  const g = s.call(bob, 'upload', { kind: 'game', name: 'G', data: scene }).asset.id;
  assert.ok(s.call(bob, 'asset.versions', { id: g }).ok);
});

test('restoring the live version or a missing one is refused', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const id = s.call(bob, 'upload', { kind: 'model', name: 'M', data: model('1') }).asset.id;
  s.call(bob, 'update', { id, data: model('2') });
  assert.match(s.call(bob, 'asset.restore', { id, version: 2 }).error, /already the live one/);
  assert.match(s.call(bob, 'asset.restore', { id, version: 9 }).error, /isn't kept/);
});

test('a decal put back is checked again, like a new upload', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const png = (n) => Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, n]).toString('base64');
  const up = s.call(bob, 'upload', { kind: 'decal', name: 'Sign', data: png(1) });
  assert.ok(up.ok, up.error);
  const id = up.asset.id;
  s.call(bob, 'update', { id, data: png(2) });
  delete s.assets.get(id).review;   // (staff approved the new picture)
  const r = s.call(bob, 'asset.restore', { id, version: 1 });
  assert.ok(r.ok, r.error);
  assert.equal(s.assets.get(id).review, 'pending');
});
