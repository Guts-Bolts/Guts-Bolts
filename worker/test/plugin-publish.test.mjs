// Publishing Studio plugins (Studio's PLUGINS > Publish window): a plugin has a version shown in
// the Library, and publishing it again updates the same plugin (free) instead of making a copy.
// (src/server/Server.cpp has the same rules.)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const lua = (text) => Buffer.from(text).toString('base64');
const v1 = lua('plugin:Button("Paint", "", function() end)');
const v2 = lua('plugin:Button("Paint", "", function() end)\nplugin:Button("Erase", "", function() end)');

test('a plugin keeps its version, and an update changes it without a new copy or fee', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  let r = s.call(bob, 'upload', { kind: 'plugin', name: 'Paint Tools', description: 'Paints things.', version: '1.0', data: v1 });
  assert.ok(r.ok, r.error);
  const id = r.asset.id;
  assert.equal(r.asset.meta.version, '1.0');
  const afterUpload = s.balance(bob);
  assert.ok(afterUpload < 100, 'publishing a new plugin costs the fee');

  r = s.call(bob, 'update', { id, name: 'Paint Tools', description: 'Paints and erases.', version: '1.1', data: v2 });
  assert.ok(r.ok, r.error);
  assert.equal(r.asset.id, id);
  assert.equal(r.asset.meta.version, '1.1');
  assert.equal(r.asset.description, 'Paints and erases.');
  assert.equal(s.balance(bob), afterUpload, 'updating is free');

  // Still one plugin, and Studio finds it as yours to update.
  const mine = s.call(bob, 'list', { kind: 'plugin', creator: bob.id });
  assert.equal(mine.assets.length, 1);
  assert.equal(mine.assets[0].meta.version, '1.1');
  // Everyone else gets the new file.
  const info = s.call(s.signUp('amy'), 'asset.info', { id });
  assert.equal(info.asset.meta.version, '1.1');
});

test('only the creator can update a plugin, and versions are kept short', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const r = s.call(bob, 'upload', { kind: 'plugin', name: 'Tools', version: 'x'.repeat(80), data: v1 });
  assert.ok(r.ok, r.error);
  assert.ok(r.asset.meta.version.length <= 20);
  const amy = s.signUp('amy');
  const bad = s.call(amy, 'update', { id: r.asset.id, version: '9.9', data: v2 });
  assert.equal(bad.ok, false);
  assert.match(bad.error, /your own/);
});

test('other kinds of uploads ignore the version', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const game = s.call(bob, 'upload', { kind: 'game', name: 'Obby', version: '2.0', data: lua('{}') });
  assert.ok(game.ok, game.error);
  assert.equal(game.asset.meta.version, undefined);
});
