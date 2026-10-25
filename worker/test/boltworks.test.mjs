import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test('Boltworks Club early registration is saved once per account', () => {
  const s = makeServer();
  const alice = s.signUp('boltworksfan');
  const status = s.call(alice, 'boltworks.status');
  assert.equal(status.ok, true);
  assert.equal(status.registered, false);
  const registered = s.call(alice, 'boltworks.register');
  assert.equal(registered.ok, true);
  assert.equal(registered.registered, true);
  assert.equal(registered.already, false);
  const repeated = s.call(alice, 'boltworks.register');
  assert.equal(repeated.ok, true);
  assert.equal(repeated.registered, true);
  assert.equal(repeated.already, true);
  assert.equal(s.call(alice, 'boltworks.status').registered, true);
});
