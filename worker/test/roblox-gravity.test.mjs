// Roblox-strength gravity: the update log says what changed, and it's on the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import gathered from '../roadmap-gathered.js';

test('the Heavy Landing update is on the Updates page', () => {
  const update = BUILT_IN_UPDATES.find((u) => u.id === 'heavy-landing');
  assert.ok(update, 'heavy-landing must be visible on the website update log');
  assert.equal(update.tag, 'Engine');
  const text = [update.name, update.summary, ...update.items].join(' ');
  assert.match(text, /98\.1/);
  assert.match(text, /Roblox/);
});

test('the roblox-gravity roadmap item is there', () => {
  const item = gathered.find((x) => x.id === 'roblox-gravity');
  assert.ok(item, 'roblox-gravity is on the roadmap');
  assert.ok(['working', 'done'].includes(item.status));
});
