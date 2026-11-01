import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test('Studio Learn progress is saved to the signed-in account', () => {
  const s = makeServer();
  const a = s.signUp('LearnerA');
  const saved = s.call(a, 'learn.progress.save', { completed: ['first-build', 'tiny-obby', 'first-build'], current: 'first-script', step: 2 });
  assert.equal(saved.ok, true);
  assert.deepEqual(saved.progress.completed, ['first-build', 'tiny-obby']);
  assert.equal(saved.progress.current, 'first-script');
  assert.equal(saved.progress.step, 2);
  assert.deepEqual(s.call(a, 'learn.progress.get').progress, saved.progress);
  const second = s.call(a, 'learn.progress.save', { completed: ['first-script'], current: 'tiny-obby', step: 1 });
  assert.deepEqual(second.progress.completed, ['first-build', 'tiny-obby', 'first-script']);
  assert.equal(second.progress.current, 'tiny-obby');
  assert.equal(second.progress.step, 1);
  const row = s.sql.exec('SELECT data FROM users WHERE id = ?', a.id)[0];
  assert.ok(row, 'account row was written to persistent storage');
  assert.deepEqual(JSON.parse(row.data).learnProgress, second.progress);
});

test('Studio Learn progress is isolated between accounts', () => {
  const s = makeServer(), a = s.signUp('LearnerA'), b = s.signUp('LearnerB');
  assert.equal(s.call(a, 'learn.progress.save', { completed: ['first-build'], current: 'first-build' }).ok, true);
  assert.deepEqual(s.call(b, 'learn.progress.get').progress, { completed: [], current: '', step: 0 });
});

test('Studio Learn progress rejects unknown IDs and malformed records', () => {
  const s = makeServer(), a = s.signUp('LearnerA');
  assert.equal(s.call(a, 'learn.progress.save', { completed: ['not-a-lesson'], current: '' }).ok, false);
  assert.equal(s.call(a, 'learn.progress.save', { completed: ['first-build'], current: '../outside' }).ok, false);
  assert.equal(s.call(a, 'learn.progress.save', { completed: ['first-build'], current: 42 }).ok, false);
  assert.equal(s.call(a, 'learn.progress.save', { completed: ['first-build'], current: 'first-build', step: 3 }).ok, false);
  assert.equal(s.call(a, 'learn.progress.save', { completed: Array(21).fill('first-build'), current: '' }).ok, false);
});

test('Studio Learn progress requires a signed-up account', () => {
  const s = makeServer(), guest = s.user('c'.repeat(64));
  assert.equal(s.call(guest, 'learn.progress.get').ok, false);
  assert.equal(s.call(guest, 'learn.progress.save', { completed: [], current: '' }).ok, false);
});
