// Promo codes: the official account makes codes (like GUTSTOBER2026) that give Bolts,
// for a limited time if it likes, and everyone can redeem each one once.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const now = () => Math.floor(Date.now() / 1000);

test('a promo code gives its Bolts once per account, and any capitals or spaces work', () => {
  const s = makeServer();
  const made = s.call(s.staff, 'admin.promoMake', { code: 'gutstober2026', bolts: 1000 });
  assert.ok(made.ok, made.error);
  assert.equal(made.promos[0].code, 'GUTSTOBER2026');
  assert.equal(made.promos[0].status, 'live');
  const bob = s.signUp('bob'), carol = s.signUp('carol');
  const r = s.call(bob, 'bolts.redeem', { code: ' GutsTober 2026 ' });
  assert.ok(r.ok, r.error);
  assert.equal(r.got, 1000);
  assert.equal(s.balance(bob), 1100);
  assert.equal(s.call(bob, 'bolts.redeem', { code: 'GUTSTOBER2026' }).error, 'You already used that code.');
  assert.equal(s.balance(bob), 1100);
  assert.ok(s.call(carol, 'bolts.redeem', { code: 'GUTSTOBER2026' }).ok, 'someone else can still use it');
  assert.equal(s.call(s.staff, 'admin.promos').promos[0].uses, 2);
  assert.equal(s.call(bob, 'bolts.history').history.at(-1).reason, 'Promo code GUTSTOBER2026');
});

test('codes that are wrong, not started, ended or stopped don\'t give anything', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  assert.match(s.call(bob, 'bolts.redeem', { code: 'NOPE123' }).error, /doesn't exist/);
  assert.ok(s.call(s.staff, 'admin.promoMake', { code: 'LATER', bolts: 50, start: now() + 3600 }).ok);
  assert.match(s.call(bob, 'bolts.redeem', { code: 'LATER' }).error, /doesn't work yet/);
  assert.ok(s.call(s.staff, 'admin.promoMake', { code: 'QUICK', bolts: 50, end: now() + 3600 }).ok);
  s.promos.find((p) => p.code === 'QUICK').end = now() - 1;   // (time goes by)
  assert.match(s.call(bob, 'bolts.redeem', { code: 'QUICK' }).error, /run out/);
  assert.equal(s.call(s.staff, 'admin.promos').promos.find((p) => p.code === 'QUICK').status, 'ended');
  assert.ok(s.call(s.staff, 'admin.promoMake', { code: 'OOPS', bolts: 50 }).ok);
  const stop = s.call(s.staff, 'admin.promoStop', { code: 'oops' });
  assert.ok(stop.ok, stop.error);
  assert.equal(stop.promos.find((p) => p.code === 'OOPS').status, 'stopped');
  assert.match(s.call(bob, 'bolts.redeem', { code: 'OOPS' }).error, /doesn't exist/);
  assert.equal(s.balance(bob), 100);
});

test('only the official account can make codes, and bad codes are refused', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  assert.equal(s.call(bob, 'admin.promoMake', { code: 'FREE', bolts: 99999 }).ok, false);
  assert.equal(s.call(bob, 'admin.promos').ok, false);
  const make = (args) => s.call(s.staff, 'admin.promoMake', args);
  assert.match(make({ code: 'A!', bolts: 10 }).error, /3-30 letters/);
  assert.match(make({ code: 'BOLTSFREE', bolts: 10 }).error, /BOLTS/);
  assert.match(make({ code: 'TOOMUCH', bolts: 2000000 }).error, /between 1 and/);
  assert.match(make({ code: 'BACKWARDS', bolts: 10, start: now() + 7200, end: now() + 3600 }).error, /after the start/);
  assert.match(make({ code: 'GONE', bolts: 10, start: now() - 7200, end: now() - 3600 }).error, /already gone by/);
  assert.ok(make({ code: 'TWICE', bolts: 10 }).ok);
  assert.match(make({ code: 'twice', bolts: 10 }).error, /already a code/);
});

test('promo codes and who used them are still there after a restart', async () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  s.call(s.staff, 'admin.promoMake', { code: 'KEEPME', bolts: 10 });
  s.call(bob, 'bolts.redeem', { code: 'KEEPME' });
  const { GbServerObject } = await import('../server.js');
  const again = new GbServerObject({ storage: { sql: s.sql } }, { OFFICIAL: s.official });
  assert.equal(again.promos[0].uses, 1);
  assert.equal(again.op('bolts.redeem', again.users.get(bob.id), { code: 'KEEPME' }).error, 'You already used that code.');
});
