// The afterimage fix (No More Afterimages) is on the Updates page and on the roadmap.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/no-more-afterimages.js';
import item from '../roadmap/no-more-afterimages.js';

test('the fix is on the Updates page and the roadmap', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  assert.ok(['working', 'done'].includes(item.status));
  assert.ok(!/marketplace/i.test(JSON.stringify(update)));
});

test('the upscaler checks how far away each pixel was last frame', () => {
  const shader = readFileSync(new URL('../../src/renderer/TemporalShaders.h', import.meta.url), 'utf8');
  assert.match(shader, /uPrevCamPos/);
  assert.match(shader, /FragColor = vec4\(unsquash\(res\), dist\)/);
});
