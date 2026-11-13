// Twister Season (3D clouds, supercells, downbursts, tornadoes) is on the Updates page
// and on the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/twister-season.js';
import item from '../roadmap/severe-weather.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  assert.equal(update.id, 'twister-season');
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['Cumulus', 'Cumulonimbus', 'Supercell', 'Tornadoes', 'Downbursts', 'Lighting:SpawnTornado', 'Lighting.CloudType', 'Low and Medium'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the work is on the roadmap', () => {
  assert.ok(['working', 'done'].includes(item.status));
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
