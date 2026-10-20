// The Partner badge: friends of Guts&Bolts (like Lyreto). Staff members can give it, like Verified.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signer } from './helpers.mjs';

// A server whose official account is a real key pair, with a Staff member who has a real Staff badge.
function setup() {
  const s = makeServer();
  const official = signer(41), helper = signer(42);
  s.official = official.account;
  const guts = s.signUp('Guts2', official.account), staff = s.signUp('helper', helper.account);
  staff.grants = { staff: official.sign('gb-badge:staff:' + staff.id) };
  s.saveUser(staff);
  return { s, official, helper, guts, staff };
}
const fromStaff = (helper, staff, s, key, to) => 's:' + staff.id + ':' + staff.grants.staff + ':' + helper.sign('gb-badge:' + key + ':' + to.id);

test('staff can make someone a Partner, and it shows next to their name', () => {
  const { s, helper, staff } = setup();
  const kermit = s.signUp('Kermit');
  assert.equal(s.publicUser(kermit).partner, false);
  const r = s.call(staff, 'admin.grant', { to: String(kermit.userId), key: 'partner', sig: fromStaff(helper, staff, s, 'partner', kermit) });
  assert.ok(r.ok, r.error);
  assert.equal(r.user.partner, true);
  assert.ok(s.badgesOf(kermit).includes('partner'));
  const p = s.call(kermit, 'profile', { id: String(kermit.userId) });
  assert.equal(p.user.partner, true);
});

test('staff can take the Partner badge away', () => {
  const { s, official, staff } = setup();
  const kermit = s.signUp('Kermit');
  kermit.grants = { partner: official.sign('gb-badge:partner:' + kermit.id) };
  assert.equal(s.isPartner(kermit), true);
  const r = s.call(staff, 'admin.revoke', { to: String(kermit.userId), key: 'partner' });
  assert.ok(r.ok, r.error);
  assert.equal(r.user.partner, false);
});

test("staff still can't hand out other badges, and a made-up Partner badge doesn't count", () => {
  const { s, helper, staff } = setup();
  const bob = s.signUp('bob');
  const r = s.call(staff, 'admin.grant', { to: String(bob.userId), key: 'tester', sig: fromStaff(helper, staff, s, 'tester', bob) });
  assert.equal(r.ok, false);
  bob.grants = { partner: helper.sign('gb-badge:partner:' + bob.id) };   // signed by someone who isn't the official account
  assert.equal(s.isPartner(bob), false);
});
