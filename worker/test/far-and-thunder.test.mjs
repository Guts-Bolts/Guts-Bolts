// Far and Thunder (detail levels for far meshes + thunderstorms) is on the Updates page
// and on the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/far-and-thunder.js';
import item from '../roadmap/storms-and-detail-levels.js';
import detail from '../roadmap/unreal-graphics-detail.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  assert.equal(update.id, 'far-and-thunder');
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['Detail levels', 'Thunderstorm', 'Lightning', 'Low', 'Cinematic', 'Lighting.Weather'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the work is on the roadmap', () => {
  for (const r of [item, detail]) {
    assert.ok(['working', 'done'].includes(r.status), r.id);
    assert.ok(r.title.length < 80 && r.text.length < 600, r.id);
  }
});
