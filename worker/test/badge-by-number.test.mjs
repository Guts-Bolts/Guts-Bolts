// Giving badges by the user ID on someone's profile (like 5), not the 64-letter account ID.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signer } from './helpers.mjs';

function setup() {
  const s = makeServer();
  const official = signer(51);
  s.official = official.account;
  const guts = s.signUp('Guts2', official.account);
  return { s, official, guts };
}

test('Staff search tells the Staff page which badges each person has', () => {
  const { s, official, guts } = setup();
  const bob = s.signUp('Bob');
  bob.grants = { tester: official.sign('gb-badge:tester:' + bob.id), bughunter: 'made-up' };
  s.saveUser(bob);
  const r = s.call(guts, 'admin.find', { query: String(bob.userId) });
  assert.ok(r.ok, r.error);
  const found = r.users.find((u) => u.userId === bob.userId);
  assert.deepEqual(found.badges, ['tester']);   // the made-up one doesn't count
});

test('the official account can give Bug Hunter by user ID, and it shows on the profile', () => {
  const { s, official, guts } = setup();
  const bob = s.signUp('Bob');
  const r = s.call(guts, 'admin.grant', { to: String(bob.userId), key: 'bughunter', sig: official.sign('gb-badge:bughunter:' + bob.id) });
  assert.ok(r.ok, r.error);
  assert.ok(s.badgesOf(bob).includes('bughunter'));
  const p = s.call(bob, 'profile', { id: String(bob.userId) });
  assert.ok(p.user.badges.includes('bughunter'));
  // ...and can take it away the same way.
  const off = s.call(guts, 'admin.revoke', { to: String(bob.userId), key: 'bughunter' });
  assert.ok(off.ok, off.error);
  assert.ok(!s.badgesOf(bob).includes('bughunter'));
});

test('badges given the old way (by the long account ID) still show', () => {
  const { s, official } = setup();
  const old = s.signUp('OldTimer');
  old.grants = { featured: official.sign('gb-badge:featured:' + old.id) };
  s.saveUser(old);
  const p = s.call(old, 'profile', { id: String(old.userId) });
  assert.ok(p.user.badges.includes('featured'));
});
