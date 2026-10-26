// Staff can reset a username that isn't allowed: it becomes "User <their number>".
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signer } from './helpers.mjs';

// A server whose official account is a real key pair, with a Staff member who has a real Staff badge.
function setup() {
  const s = makeServer();
  const official = signer(51), helper = signer(52);
  s.official = official.account;
  const guts = s.signUp('Guts2', official.account), staff = s.signUp('helper', helper.account);
  staff.grants = { staff: official.sign('gb-badge:staff:' + staff.id) };
  s.saveUser(staff);
  return { s, official, guts, staff };
}

test('staff can reset a bad username to "User <number>"', () => {
  const { s, staff } = setup();
  const bob = s.signUp('BadName');
  const r = s.call(staff, 'admin.resetName', { to: String(bob.userId) });
  assert.ok(r.ok, r.error);
  assert.equal(bob.username, 'User ' + bob.userId);
  assert.equal(bob.name, 'User ' + bob.userId);
  assert.equal(r.user.username, 'User ' + bob.userId);
  // The bad name isn't one of their past names (those show on the profile)...
  assert.ok(!(bob.pastNames || []).includes('BadName'));
  // ...but nobody can take it, not even them.
  const back = s.call(bob, 'account.rename', { username: 'BadName' });
  assert.equal(back.ok, false);
  // They get told, and it's in the staff log.
  assert.equal(bob.notes[0].kind, 'rename');
  assert.ok(s.staffLog.some((x) => x.action === 'rename' && x.about === bob.id));
});

test("regular players can't reset usernames", () => {
  const { s } = setup();
  const bob = s.signUp('bob'), carol = s.signUp('carol');
  assert.equal(s.call(bob, 'admin.resetName', { to: String(carol.userId) }).ok, false);
  assert.equal(carol.username, 'carol');
});

test("staff can't reset Guts or another staff member, but Guts can reset staff", () => {
  const { s, official, guts, staff } = setup();
  const other = s.signUp('helper2', signer(53).account);
  other.grants = { staff: official.sign('gb-badge:staff:' + other.id) };
  s.saveUser(other);
  assert.ok(s.isStaff(other));
  assert.equal(s.call(staff, 'admin.resetName', { to: guts.id }).ok, false);
  assert.equal(s.call(staff, 'admin.resetName', { to: String(other.userId) }).ok, false);
  assert.equal(other.username, 'helper2');
  const r = s.call(guts, 'admin.resetName', { to: String(other.userId) });
  assert.ok(r.ok, r.error);
  assert.equal(other.username, 'User ' + other.userId);
});
