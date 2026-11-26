// The graphics overhaul (Crystal Clear) is on the Updates page and the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/crystal-clear.js';
import roadmap from '../roadmap/graphics-overhaul.js';

test('the graphics overhaul is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  assert.equal(update.id, 'crystal-clear');
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['Lighting.ToneMapper', 'AgX', 'ACES', 'Neutral', 'Lighting.PhysicalSky'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace|roblox/i.test(text));
});

test('the graphics overhaul is on the roadmap', () => {
  assert.equal(roadmap.id, 'graphics-overhaul');
  assert.ok(['working', 'done'].includes(roadmap.status));
  assert.ok(roadmap.title.length < 80 && roadmap.text.length < 600);
});
