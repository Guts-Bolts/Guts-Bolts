// Profile backgrounds: any decal that passed its staff check can go behind your avatar on your
// profile (worker/backgrounds.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test('pick a decal as your profile background; others see it; take it off', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  const sky = s.addGame(ann, 'Sunset Sky', { kind: 'decal' });
  const game = s.addGame(ann, 'A Game');
  assert.match(s.call(bob, 'profile.background', { decal: game.id }).error, /isn't a decal/);
  sky.review = 'pending';
  assert.match(s.call(bob, 'profile.background', { decal: sky.id }).error, /staff check/);
  delete sky.review;
  const r = s.call(bob, 'profile.background', { decal: 'gb:' + sky.id });
  assert.ok(r.ok, r.error);
  assert.equal(r.background.name, 'Sunset Sky');
  assert.equal(s.call(ann, 'profile', { id: bob.id }).background.id, sky.id, 'others see it');
  sky.review = 'rejected';
  assert.equal(s.call(ann, 'profile', { id: bob.id }).background, null, 'a decal that fails its check stops showing');
  delete sky.review;
  assert.equal(s.call(bob, 'profile.background', { decal: '' }).background, null);
  assert.equal(s.call(ann, 'profile', { id: bob.id }).background, null, 'taken off');
  const guest = s.signUp('guest');
  guest.userId = 0;
  assert.match(s.call(guest, 'profile.background', { decal: sky.id }).error, /Sign up/);
});
