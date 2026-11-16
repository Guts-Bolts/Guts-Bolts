import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp } from './helpers.mjs';

// The home server page (website/server/) signs in with staff.check, a plain "is this staff?" question.
test('staff.check says yes for staff and no for players', () => {
  const s = makeServer();
  const yes = s.op('staff.check', s.staff, {});
  assert.equal(yes.ok, true);
  assert.equal(yes.staff, true);

  const player = signUp(s, 'NotStaffPlayer');
  const no = s.op('staff.check', player, {});
  assert.equal(no.ok, false);
  assert.match(no.error, /Only staff/);
});
