// The graphics card picks what to draw (The Card Decides).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/card-picks.js';
import item from '../roadmap/photoreal-speed.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['graphics card', 'on screen', 'Macs'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the speed step on the roadmap counts it as done so far', () => {
  assert.ok(item.text.includes('choosing what to draw by itself)'));
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
