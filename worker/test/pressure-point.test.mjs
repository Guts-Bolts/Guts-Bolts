// Arterial bleeding and dead bodies that lie still (Pressure Point).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/pressure-point.js';
import item from '../roadmap/arterial-bleeding.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['artery', 'heartbeat', 'stagger', 'server', 'CutArtery', 'StopBleeding', 'flung', 'still'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the work is on the roadmap', () => {
  assert.equal(item.id, 'arterial-bleeding');
  assert.ok(['working', 'done'].includes(item.status));
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
