// Boltworks Club tiers (BWC / TBC / OBC): a signed badge from the official account that shows
// a little icon next to the member's name. One tier at a time; staff members can't hand them out.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signer } from './helpers.mjs';

function setup() {
  const s = makeServer();
  const official = signer(61), helper = signer(62);
  s.official = official.account;
  const guts = s.signUp('Guts2', official.account), staff = s.signUp('helper', helper.account);
  staff.grants = { staff: official.sign('gb-badge:staff:' + staff.id) };
  s.saveUser(staff);
  return { s, official, helper, guts, staff };
}

test('the official account gives a Boltworks Club tier and it shows on the profile', () => {
  const { s, official, guts } = setup();
  const bob = s.signUp('Bob');
  assert.equal(s.publicUser(bob).club, '');
  const r = s.call(guts, 'admin.grant', { to: String(bob.userId), key: 'tbc', sig: official.sign('gb-badge:tbc:' + bob.id) });
  assert.ok(r.ok, r.error);
  assert.equal(r.user.club, 'tbc');
  assert.equal(s.call(bob, 'profile', { id: String(bob.userId) }).user.club, 'tbc');
});

test('a new tier replaces the old one, and taking it away clears it', () => {
  const { s, official, guts } = setup();
  const bob = s.signUp('Bob');
  s.call(guts, 'admin.grant', { to: String(bob.userId), key: 'obc', sig: official.sign('gb-badge:obc:' + bob.id) });
  const r = s.call(guts, 'admin.grant', { to: String(bob.userId), key: 'bwc', sig: official.sign('gb-badge:bwc:' + bob.id) });
  assert.equal(r.user.club, 'bwc');
  assert.equal(bob.grants.obc, undefined);
  const gone = s.call(guts, 'admin.revoke', { to: String(bob.userId), key: 'bwc' });
  assert.equal(gone.user.club, '');
});

test("staff members can't give tiers, and made-up tiers don't count", () => {
  const { s, helper, staff } = setup();
  const bob = s.signUp('Bob');
  const sig = 's:' + staff.id + ':' + staff.grants.staff + ':' + helper.sign('gb-badge:obc:' + bob.id);
  assert.equal(s.call(staff, 'admin.grant', { to: String(bob.userId), key: 'obc', sig }).ok, false);
  bob.grants = { obc: helper.sign('gb-badge:obc:' + bob.id), tbc: 'made-up' };
  assert.equal(s.clubOf(bob), '');
});

test('the best tier wins if someone somehow has two', () => {
  const { s, official } = setup();
  const bob = s.signUp('Bob');
  bob.grants = { bwc: official.sign('gb-badge:bwc:' + bob.id), obc: official.sign('gb-badge:obc:' + bob.id) };
  assert.equal(s.clubOf(bob), 'obc');
});
