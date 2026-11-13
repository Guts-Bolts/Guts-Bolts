// Pictures players draw in games (AssetService:PromptPublishImageAsync): published from the
// player's own account as a decal, free (no upload fee), remembering the game they came from.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const png = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0]).toString('base64');

test('a drawing from a game is free to publish, and keeps only its own notes', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const before = s.balance(bob);
  const r = s.call(bob, 'upload', { kind: 'decal', name: 'My cat', data: png, access: 'public',
    meta: { drawing: true, game: 'abc123', price: 99, sneaky: 'x' } });
  assert.ok(r.ok, r.error);
  assert.equal(r.fee || 0, 0, 'no fee');
  assert.equal(s.balance(bob), before, 'their Bolts are untouched');
  const a = s.assets.get(r.asset.id);
  assert.equal(a.creator, bob.id, 'it is theirs');
  assert.deepEqual(a.meta, { ext: 'png', drawing: true, game: 'abc123' });

  const odd = s.call(bob, 'upload', { kind: 'decal', name: 'Dog', data: png, meta: { drawing: true, game: '<script>' } });
  assert.ok(odd.ok, odd.error);
  assert.equal(s.assets.get(odd.asset.id).meta.game, undefined, 'a strange game id is dropped');
});

test('an ordinary decal still costs the usual fee, and can not pretend with drawing: "yes"', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const before = s.balance(bob);
  const r = s.call(bob, 'upload', { kind: 'decal', name: 'Sign', data: png, meta: { drawing: 'yes' } });
  assert.ok(r.ok, r.error);
  assert.ok(s.balance(bob) < before, 'the usual fee was taken');
  assert.equal(s.assets.get(r.asset.id).meta.drawing, undefined);
});
