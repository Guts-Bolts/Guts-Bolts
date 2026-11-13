// "Mirrors, Mist and Rolling Barrels" is on the Updates page, and the work is on the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/mirrors-mist-and-rolling-barrels.js';
import item from '../roadmap/mirrors-mist-rolling.js';
import reflections from '../roadmap/unreal-graphics-reflections.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['mirror', 'fog', 'roll', 'wind', 'AirDensity', 'FogFalloff', 'shafts', 'FogWisps'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the work is on the roadmap', () => {
  for (const it of [item, reflections]) {
    assert.ok(['working', 'done'].includes(it.status), it.id);
    assert.ok(it.title.length < 80 && it.text.length < 600, it.id);
  }
});
