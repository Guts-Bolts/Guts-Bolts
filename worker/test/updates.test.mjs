// The update log (worker/updates.js) must load and every entry must be filled in.
// (A broken bracket here once stopped the whole website server from starting.)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BUILT_IN_UPDATES } from '../updates.js';

test('Publish readiness is documented in the website update log', () => {
  const update = BUILT_IN_UPDATES.find((u) => u.id === 'publish-readiness');
  assert.ok(update, 'Publish readiness must be visible on the website update log');
  assert.equal(update.tag, 'Studio');
  const text = [update.name, update.summary, ...update.items].join(' ');
  assert.match(text, /scene structure/);
  assert.match(text, /local assets/);
  assert.match(text, /security review/);
  assert.match(text, /never executed/);
});

test('Scene Validator is published in the website update log', () => {
  const update = BUILT_IN_UPDATES.find((u) => u.id === 'scene-validator');
  assert.ok(update, 'Scene Validator must be visible on the website update log');
  assert.equal(update.tag, 'Studio');
  const text = [update.name, update.summary, ...update.items].join(' ');
  assert.match(text, /broken references/);
  assert.match(text, /constraints/);
  assert.match(text, /validation report/);
  assert.match(text, /does not run scripts/);
});

test('Multiplayer Session Recorder is published in the website update log', () => {
  const update = BUILT_IN_UPDATES.find((u) => u.id === 'multiplayer-session-recorder');
  assert.ok(update, 'Multiplayer Session Recorder must be visible on the website update log');
  assert.equal(update.tag, 'Studio');
  const text = [update.name, update.summary, ...update.items].join(' ');
  assert.match(text, /scene snapshot/);
  assert.match(text, /timestamped/);
  assert.match(text, /not deterministic/);
  assert.match(text, /review before sharing/);
});

test('the update log loads and has updates', () => {
  assert.ok(Array.isArray(BUILT_IN_UPDATES));
  assert.ok(BUILT_IN_UPDATES.length > 10);
});

test('every update has an id, a name, a time, a tag, a summary and what changed', () => {
  const tags = new Set(['Engine', 'Studio', 'Website', 'Player', 'Server', 'Fix', 'Release']);
  const ids = new Set();
  for (const u of BUILT_IN_UPDATES) {
    const where = `update "${u.id}"`;
    assert.match(u.id, /^[a-z0-9-]+$/, `${where}: id is letters, numbers and dashes`);
    assert.ok(!ids.has(u.id), `${where}: the id is used twice`);
    ids.add(u.id);
    assert.ok(typeof u.name === 'string' && u.name.trim(), `${where}: needs a name`);
    assert.ok(Number.isInteger(u.time) && u.time > 1.6e9, `${where}: time is seconds since 1970`);
    assert.ok(tags.has(u.tag), `${where}: tag "${u.tag}" isn't one of ${[...tags].join(', ')}`);
    assert.ok(typeof u.summary === 'string' && u.summary.trim(), `${where}: needs a summary`);
    assert.ok(Array.isArray(u.items) && u.items.length > 0, `${where}: needs a list of what changed`);
    for (const line of u.items) assert.ok(typeof line === 'string' && line.trim(), `${where}: an empty line in items`);
  }
});

test('Studio workflow update is published in the website update log', () => {
  const update = BUILT_IN_UPDATES.find((u) => u.id === 'studio-under-the-hood');
  assert.ok(update, 'the Studio improvements must be visible on the website');
  assert.equal(update.tag, 'Studio');
  const text = [update.name, update.summary, ...update.items].join(' ');
  assert.match(text, /Performance Monitor/);
  assert.match(text, /Accessory maker/);
  assert.match(text, /Bundle maker/);
  assert.match(text, /Properties panel/);
  assert.match(text, /Script Analysis/);
});

test('Project Dependency Map is published in the website update log', () => {
  const update = BUILT_IN_UPDATES.find((u) => u.id === 'project-dependency-map');
  assert.ok(update, 'the Project Dependency Map must be visible on the website');
  assert.equal(update.tag, 'Studio');
  const text = [update.name, update.summary, ...update.items].join(' ');
  assert.match(text, /incoming relationship counts/);
  assert.match(text, /require\(\)/);
  assert.match(text, /code editor/);
  assert.match(text, /static code patterns/);
});

test('Performance Doctor is published in the website update log', () => {
  const update = BUILT_IN_UPDATES.find((u) => u.id === 'performance-doctor');
  assert.ok(update, 'Performance Doctor must be visible on the website update log');
  assert.equal(update.tag, 'Studio');
  const text = [update.name, update.summary, ...update.items].join(' ');
  assert.match(text, /frame timings/);
  assert.match(text, /viewport and simulation/);
  assert.match(text, /diagnosis report/);
  assert.match(text, /heuristics/);
  assert.match(text, /separate/);
});

test('says "Library", never "Marketplace"', () => {
  // (Roblox's MarketplaceService is a script name, so that one's fine.)
  for (const u of BUILT_IN_UPDATES)
    assert.ok(!/marketplace(?!service)/i.test(JSON.stringify(u)), `update "${u.id}" says Marketplace`);
});


test('Project Time Machine is published in the website update log', () => {
  const update = BUILT_IN_UPDATES.find((u) => u.id === 'project-time-machine');
  assert.ok(update, 'Project Time Machine must be visible on the website update log');
  assert.equal(update.tag, 'Studio');
  const text = [update.name, update.summary, ...update.items].join(' ');
  assert.match(text, /checkpoints/);
  assert.match(text, /compares/);
  assert.match(text, /Restor/i);
  assert.match(text, /not cloud-synced/);
});

test('Catalog Repair Shop is published in the website update log', () => {
  const update = BUILT_IN_UPDATES.find((u) => u.id === 'catalog-item-workshop');
  assert.ok(update, 'Catalog Repair Shop must be visible on the website update log');
  assert.equal(update.tag, 'Studio');
  const text = [update.name, update.summary, ...update.items].join(' ');
  assert.match(text, /existing listing/);
  assert.match(text, /position/);
  assert.match(text, /mesh/);
  assert.match(text, /ownership checks/);
});
