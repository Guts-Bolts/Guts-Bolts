// Blood that shows up on PC and is the same for every player (Everybody Bleeds the Same).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/everybody-bleeds.js';
import item from '../roadmap/server-blood.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['PC', 'server', 'pools', 'join'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the work is on the roadmap', () => {
  assert.equal(item.id, 'server-blood');
  assert.ok(['working', 'done'].includes(item.status));
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
