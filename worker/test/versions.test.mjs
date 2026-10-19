// Version history: publishing a game keeps its old versions, and its creator can put one back.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const scene = (title) => Buffer.from(JSON.stringify({ format: 'gbscene', info: { title }, nodes: [] })).toString('base64');
const text = (s, id) => new TextDecoder().decode(s.readFile(id));

test('publishing keeps every version, newest first', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const up = s.call(bob, 'upload', { kind: 'game', name: 'Obby', data: scene('one') });
  assert.ok(up.ok, up.error);
  const id = up.asset.id;
  let r = s.call(bob, 'update', { id, data: scene('two') });
  assert.ok(r.ok, r.error);
  assert.equal(r.version, 2);
  r = s.call(bob, 'update', { id, data: scene('three') });
  assert.equal(r.version, 3);
  const list = s.call(bob, 'game.versions', { id });
  assert.ok(list.ok, list.error);
  assert.deepEqual(list.versions.map((x) => x.v), [3, 2, 1]);
  assert.equal(list.versions[0].live, true);
  assert.equal(list.versions[1].live, false);
  assert.equal(list.versions[2].byName, 'bob');
  assert.ok(list.versions[2].size > 0);
  assert.equal(list.most, 50);
  assert.match(text(s, id), /three/);
});

test('saving the same file again adds no new version', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const id = s.call(bob, 'upload', { kind: 'game', name: 'Same', data: scene('a') }).asset.id;
  const r = s.call(bob, 'update', { id, data: scene('a'), name: 'Same Name' });
  assert.ok(r.ok, r.error);
  assert.equal(r.newVersion, false);
  assert.equal(r.version, 1);
  assert.equal(s.call(bob, 'game.versions', { id }).versions.length, 1);
});

test('restoring puts an old version back as a new newest version, keeping the rest', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const id = s.call(bob, 'upload', { kind: 'game', name: 'Back', data: scene('first') }).asset.id;
  s.call(bob, 'update', { id, data: scene('second, broken') });
  const r = s.call(bob, 'game.restore', { id, version: 1 });
  assert.ok(r.ok, r.error);
  assert.equal(r.version, 3);
  assert.deepEqual(r.versions.map((x) => x.v), [3, 2, 1]);
  assert.equal(r.versions[0].restored, 1);
  assert.match(text(s, id), /first/);
  assert.equal(s.assets.get(id).size, s.readFile(id).length);
  // What people download is the restored one.
  const got = s.call(s.signUp('amy'), 'get', { id });
  assert.match(Buffer.from(got.data, 'base64').toString(), /first/);
  // The live one can't be "restored" again.
  const again = s.call(bob, 'game.restore', { id, version: 3 });
  assert.equal(again.ok, false);
  assert.match(again.error, /already the live one/);
});

test('only the creator can see or restore versions', () => {
  const s = makeServer();
  const bob = s.signUp('bob'), eve = s.signUp('eve');
  const id = s.call(bob, 'upload', { kind: 'game', name: 'Mine', data: scene('x') }).asset.id;
  s.call(bob, 'update', { id, data: scene('y') });
  const see = s.call(eve, 'game.versions', { id });
  assert.equal(see.ok, false);
  const restore = s.call(eve, 'game.restore', { id, version: 1 });
  assert.equal(restore.ok, false);
  assert.match(text(s, id), /"y"/);
});

test('games published before version history: the current file becomes version 1', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const g = s.addGame(bob, 'Old Game');   // made before history existed: no versions kept
  s.writeFile(g.id, new TextEncoder().encode(JSON.stringify({ format: 'gbscene', info: { title: 'old' } })));
  g.size = s.readFile(g.id).length;
  let list = s.call(bob, 'game.versions', { id: g.id });
  assert.ok(list.ok, list.error);
  assert.deepEqual(list.versions.map((x) => [x.v, x.live]), [[1, true]]);
  const r = s.call(bob, 'update', { id: g.id, data: scene('new') });
  assert.equal(r.version, 2);
  list = s.call(bob, 'game.versions', { id: g.id });
  assert.deepEqual(list.versions.map((x) => x.v), [2, 1]);
  assert.ok(s.call(bob, 'game.restore', { id: g.id, version: 1 }).ok);
  assert.match(text(s, g.id), /"old"/);
});

test('only the newest 50 versions are kept', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const id = s.call(bob, 'upload', { kind: 'game', name: 'Busy', data: scene('v1') }).asset.id;
  for (let i = 2; i <= 55; i++) assert.ok(s.call(bob, 'update', { id, data: scene('v' + i) }).ok);
  const list = s.call(bob, 'game.versions', { id }).versions;
  assert.equal(list.length, 50);
  assert.equal(list[0].v, 55);
  assert.equal(list[49].v, 6);
  assert.equal(s.readFile('ver:' + id + ':5'), null);   // pruned files are gone
  assert.ok(s.readFile('ver:' + id + ':6'));
  assert.equal(s.call(bob, 'game.restore', { id, version: 3 }).ok, false);
});

test('deleting a game deletes its version history', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const id = s.call(bob, 'upload', { kind: 'game', name: 'Gone', data: scene('a') }).asset.id;
  s.call(bob, 'update', { id, data: scene('b') });
  assert.ok(s.call(bob, 'delete', { id }).ok);
  assert.equal(s.readFile('ver:' + id + ':1'), null);
  assert.equal(s.readFile('ver:' + id + ':2'), null);
});
