// The first Unreal-style graphics update (Light That Bounces) is on the Updates page, and
// it and the parts still to come are on the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/light-that-bounces.js';
import part1 from '../roadmap/unreal-graphics-1.js';
import shadows from '../roadmap/unreal-graphics-shadows.js';
import reflections from '../roadmap/unreal-graphics-reflections.js';
import detail from '../roadmap/unreal-graphics-detail.js';
import weather from '../roadmap/unreal-weather.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['Epic', 'Lumen', 'TSR', 'Low'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('every part is on the roadmap', () => {
  assert.ok(['working', 'done'].includes(part1.status));
  for (const item of [part1, shadows, reflections, detail, weather]) {
    assert.ok(['planned', 'working', 'done'].includes(item.status), item.id);
    assert.ok(item.title.length < 80 && item.text.length < 600, item.id);
  }
});
