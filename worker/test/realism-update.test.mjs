// The realism update (Sweat, Snow and Ricochets) is on the Updates page and the roadmap,
// and the real sounds it uses are credited.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { BUILT_IN_UPDATES } from '../updates.js';
import update from '../updates/sweat-snow-and-ricochets.js';
import roadmap from '../roadmap/realism.js';

test('the realism update is on the Updates page', () => {
  assert.ok(BUILT_IN_UPDATES.includes(update));
  const text = [update.name, update.summary, ...update.items].join(' ');
  for (const word of ['FireBullet', 'workspace.Stamina', 'workspace.FireSpread', 'Lighting.AutoExposure', 'Lighting.MotionBlur', 'CC0'])
    assert.ok(text.includes(word), `mentions ${word}`);
  assert.ok(!/marketplace/i.test(text));
});

test('the realism work is on the roadmap', () => {
  assert.equal(roadmap.id, 'realism');
  assert.ok(['working', 'done'].includes(roadmap.status));
  assert.ok(roadmap.title.length < 80 && roadmap.text.length < 600);
});

test('every real sound is credited', () => {
  const dir = new URL('../../assets/sounds/real/', import.meta.url);
  const credits = readFileSync(new URL('CREDITS.md', dir), 'utf8');
  const sounds = readdirSync(dir).filter((f) => f.endsWith('.ogg'));
  assert.ok(sounds.length >= 40);
  for (const f of sounds) assert.match(f, /^[a-z]+(_[a-z]+)*(_\d+)?\.ogg$/, `${f}: name it <sound>_<take>.ogg`);
  assert.match(credits, /CC0/);
  const bases = new Set(sounds.map((f) => f.replace(/(_\d+)?\.ogg$/, '')));
  for (const b of bases) assert.ok(credits.includes(b), `CREDITS.md says where ${b} came from`);
});
