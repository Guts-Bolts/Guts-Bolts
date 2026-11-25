import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test('the Boltworks Club early list is closed now; people already on it stay on it', () => {
  const s = makeServer();
  const alice = s.signUp('boltworksfan'), bob = s.signUp('latecomer');
  const status = s.call(alice, 'boltworks.status');
  assert.equal(status.ok, true);
  assert.equal(status.registered, false);
  assert.equal(status.listClosed, true);
  assert.match(s.call(bob, 'boltworks.register').error, /closed/);
  // Someone who registered before launch.
  s.sql.exec('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)', 'boltworksWaitlist', JSON.stringify([{ id: alice.id, username: alice.name, at: 1795000000 }]));
  const repeated = s.call(alice, 'boltworks.register');
  assert.equal(repeated.ok, true);
  assert.equal(repeated.already, true);
  assert.equal(s.call(alice, 'boltworks.status').registered, true);
});
