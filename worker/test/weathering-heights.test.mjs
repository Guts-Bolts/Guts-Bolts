// Weathering Heights (real Studio playtests, weather that rolls in, better terrain) is
// on the Updates page and on the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/weathering-heights.js';
import item from '../roadmap/real-playtest-weather-terrain.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  assert.equal(update.id, 'weathering-heights');
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['Stop', 'snow', 'Fog', 'Blizzard', 'Sandstorm', 'Erode', 'Auto Paint', 'Mountains', 'Islands'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the work is on the roadmap', () => {
  assert.equal(item.id, 'real-playtest-weather-terrain');
  assert.ok(['working', 'done'].includes(item.status));
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
