// Pictures packed ahead of time (Pre-Squeezed), which finishes the speed step.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/packed-pictures.js';
import item from '../roadmap/photoreal-speed.js';

test('the update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['packed', 'gbpack', 'Mac'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the speed step on the roadmap is done', () => {
  assert.equal(item.status, 'done');
  assert.ok(item.text.includes('pictures packed ahead of time'));
  assert.ok(item.title.length < 80 && item.text.length < 600);
});
