// The photoreal upgrade's groundwork (Know Your Limits) is on the Updates page, and it
// and the steps still to come are on the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/know-your-limits.js';
import groundwork from '../roadmap/photoreal-groundwork.js';
import materials from '../roadmap/photoreal-materials.js';
import post from '../roadmap/photoreal-post.js';
import ocean from '../roadmap/photoreal-ocean.js';
import gore from '../roadmap/photoreal-gore.js';
import speed from '../roadmap/photoreal-speed.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['Mac', 'iPhone', 'web Player', 'Settings', 'memory'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('every step is on the roadmap', () => {
  assert.equal(groundwork.status, 'done');
  for (const item of [groundwork, materials, post, ocean, gore, speed]) {
    assert.ok(['planned', 'working', 'done'].includes(item.status), item.id);
    assert.ok(item.title.length < 80 && item.text.length < 600, item.id);
  }
});
