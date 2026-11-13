// Four sun shadow maps (Long Shadows) are on the Updates page and the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/long-shadows.js';
import item from '../roadmap/unreal-graphics-shadows.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['four', 'shadow maps', 'ShadowDistance', 'phones'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the step is done on the roadmap', () => {
  assert.equal(item.status, 'done');
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
