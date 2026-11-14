// Cutting parts in two (Clean Cut) is on the Updates page and the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/clean-cut.js';
import item from '../roadmap/photoreal-slice.js';
import rest from '../roadmap/photoreal-gore.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['Slice', 'Flesh', 'InsideColor', 'bone', 'Gore'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the step is on the roadmap', () => {
  assert.equal(item.status, 'done');
  assert.ok(item.pr > 0);
  assert.ok(item.title.length < 80 && item.text.length < 600);
  assert.ok(['planned', 'working', 'done'].includes(rest.status));
});
