// Someone banned for good shows "[ Content Deleted ]" on their profile; a timed ban doesn't hide it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test('a profile banned for good answers USER_DELETED like any other failed request', () => {
  const s = makeServer();
  const alice = s.signUp('alice'), bob = s.signUp('bob');
  bob.banned = true; s.saveUser(bob);
  const r = s.call(alice, 'profile', { id: String(bob.userId) });
  assert.equal(r.ok, false);
  assert.equal(r.code, 'USER_DELETED');
  assert.match(r.error, /USER_DELETED/);
});

test('a timed ban still shows the profile', () => {
  const s = makeServer();
  const alice = s.signUp('alice'), bob = s.signUp('bob');
  bob.banned = true; bob.bannedUntil = Math.floor(Date.now() / 1000) + 86400; s.saveUser(bob);
  assert.equal(s.call(alice, 'profile', { id: String(bob.userId) }).ok, true);
});

test('guests don\'t fill the staff "find a person" list', () => {
  const s = makeServer();
  s.user('e'.repeat(64));   // a guest: no user number
  s.signUp('carol');
  const r = s.call(s.staff, 'admin.find', { query: '' });
  assert.ok(r.ok, r.error);
  assert.ok(r.users.every((u) => u.userId > 0));
});
