// The "Less Is More" update (Studio's Simple layout, Roblox-style Properties, Search Studio)
// is in the update log and on the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import update from '../updates/less-is-more.js';
import roadmap from '../roadmap/studio-simple-layout.js';

test('the update entry is complete', () => {
  assert.equal(update.id, 'less-is-more');
  assert.equal(update.tag, 'Studio');
  assert.ok(update.summary.length > 20);
  assert.ok(update.items.length >= 5);
  for (const line of update.items) assert.ok(!/marketplace/i.test(line), 'say Library, never Marketplace');
});

test('the roadmap item fits the limits', () => {
  assert.equal(roadmap.id, 'studio-simple-layout');
  assert.ok(['planned', 'working', 'done'].includes(roadmap.status));
  assert.ok(roadmap.title.length < 80);
  assert.ok(roadmap.text.length < 600);
});
