// The built-in roadmap: one file per item in worker/roadmap/, shown next to the items staff
// add on the website. Staff can't change or remove them there (a pull request does).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync } from 'node:fs';
import { makeServer } from './helpers.mjs';
import { builtInItems, ROAD_STATUS, kRoadTitle, kRoadText } from '../roadmap.js';
import gathered from '../roadmap-gathered.js';

const folder = new URL('../roadmap/', import.meta.url);

test('every file in worker/roadmap/ is a proper roadmap item named after its id', async () => {
  const files = readdirSync(folder).filter((f) => f.endsWith('.js'));
  assert.equal(gathered.length, files.length, 'run node worker/gather-updates.mjs');
  const ids = new Set();
  for (const f of files) {
    const x = (await import(new URL(f, folder))).default;
    assert.equal(x.id + '.js', f, f + ': name the file after the id');
    assert.ok(!ids.has(x.id), f + ': the id is used twice');
    ids.add(x.id);
    assert.ok(ROAD_STATUS.includes(x.status), f + ': status is planned, working or done');
    assert.ok(x.title && x.title.length <= kRoadTitle, f + ': title');
    assert.ok(typeof x.text === 'string' && x.text.length <= kRoadText, f + ': text');
    assert.ok(Number.isInteger(x.pr) && x.pr >= 0, f + ': pr is a number (0 if none)');
    assert.ok(Number.isInteger(x.time) && x.time > 1.7e9, f + ': time');
  }
});

test('everyone sees the built-in items, with staff ones too', () => {
  const s = makeServer();
  const visitor = s.user('c'.repeat(64));
  const before = s.call(visitor, 'roadmap.get').items;
  assert.equal(before.length, gathered.length);
  assert.ok(before.every((x) => x.builtIn && x.id.startsWith('b-')));
  assert.ok(s.call(s.staff, 'admin.roadmapSave', { title: 'Horses', text: '', status: 'planned', version: '' }).ok);
  const after = s.call(visitor, 'roadmap.get').items;
  assert.equal(after.length, gathered.length + 1);
  assert.ok(after.some((x) => x.title === 'Horses' && !x.builtIn));
});

test('staff can\'t change or remove a built-in item from the website', () => {
  const s = makeServer();
  const id = s.call(s.staff, 'roadmap.get').items.find((x) => x.builtIn).id;
  assert.match(s.call(s.staff, 'admin.roadmapSave', { id, title: 'X', text: '', status: 'done', version: '' }).error, /pull request/);
  assert.match(s.call(s.staff, 'admin.roadmapRemove', { id }).error, /pull request/);
});

test('broken files are left out instead of breaking the page', () => {
  const list = builtInItems([{ id: 'ok', title: 'Fine', status: 'done', pr: 3 }, { id: 'bad', title: 'No status' }, null, { title: 'No id', status: 'done' }]);
  assert.deepEqual(list.map((x) => x.id), ['b-ok']);
  assert.equal(list[0].pr, 3);
  assert.equal(list[0].text, '');
});
