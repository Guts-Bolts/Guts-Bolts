// Cars with real suspension (Spring Loaded).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/spring-loaded.js';
import item from '../roadmap/car-suspension.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['spring', 'Wheel', 'VehicleSeat', 'Buggy', 'seat.Stiffness'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the work is on the roadmap', () => {
  assert.equal(item.id, 'car-suspension');
  assert.ok(['working', 'done'].includes(item.status));
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
