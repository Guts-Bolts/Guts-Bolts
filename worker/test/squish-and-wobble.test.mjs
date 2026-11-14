// Soft parts (Squish and Wobble) are on the Updates page and the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/squish-and-wobble.js';
import item from '../roadmap/photoreal-gore.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['Soft', 'Softness', 'TearAt', 'Poke', 'Flesh'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the step is on the roadmap', () => {
  assert.ok(['working', 'done'].includes(item.status));
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
