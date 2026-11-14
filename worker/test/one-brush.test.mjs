// Drawing blood, gibs and sparks many at once (One Brush Stroke) is on the Updates page.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/one-brush.js';
import item from '../roadmap/photoreal-speed.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['stain', 'gib', 'spark', 'one go'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the speed step is on the roadmap', () => {
  assert.ok(item.text.includes('blood'));
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
