// Packed material pictures (Three In One) are on the Updates page and the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/three-in-one.js';
import item from '../roadmap/photoreal-materials.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['OrmMap', 'occlusion', 'roughness', 'RoughnessMap'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the step is done on the roadmap', () => {
  assert.equal(item.status, 'done');
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
