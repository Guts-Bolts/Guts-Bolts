// Roblox-style character footing: the update log says what changed, and the
// roadmap item is finished in the same version.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import gathered from '../roadmap-gathered.js';

test('the Roblox-style footing update is on the Updates page', () => {
  const update = BUILT_IN_UPDATES.find((u) => u.id === 'running-start');
  assert.ok(update, 'running-start must be visible on the website update log');
  assert.equal(update.tag, 'Engine');
  assert.match(update.version, /^\d+\.\d+\.\d+$/);
  const text = [update.name, update.summary, ...update.items].join(' ');
  assert.match(text, /WalkSpeed/);
  assert.match(text, /momentum/);
  assert.match(text, /steer/);
});

test('the character-footing roadmap item is done in the same version', () => {
  const update = BUILT_IN_UPDATES.find((u) => u.id === 'running-start');
  const item = gathered.find((x) => x.id === 'character-footing');
  assert.ok(item, 'character-footing is on the roadmap');
  assert.equal(item.status, 'done');
  assert.equal(item.version, update.version);
});
