// Lighter gravity (Softer Landing) is on the Updates page and the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/softer-landing.js';
import item from '../roadmap/softer-landing.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['Gravity', '60', 'Fall damage', 'Roblox'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the gravity change is on the roadmap', () => {
  assert.ok(['working', 'done'].includes(item.status));
  assert.ok(item.text.includes('60'));
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
