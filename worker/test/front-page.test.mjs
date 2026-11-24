// The busy front page (website/app/frontpage.js): Home shows what people are doing
// across the site, using list calls guests are allowed to make.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { pickOfTheDay, dayNumber, FRONT_CALLS, loadFront } from '../../website/app/frontpage.js';

const read = (p) => readFileSync(new URL('../../' + p, import.meta.url), 'utf8');

test('pick of the day is the same all day and changes the next day', () => {
  const list = ['a', 'b', 'c'];
  const day = dayNumber(Date.UTC(2026, 10, 24, 1));
  assert.equal(day, dayNumber(Date.UTC(2026, 10, 24, 23)));
  assert.equal(pickOfTheDay(list, day), pickOfTheDay(list, day));
  assert.notEqual(pickOfTheDay(list, day), pickOfTheDay(list, day + 1));
  assert.equal(pickOfTheDay([], day), null);
  assert.equal(pickOfTheDay([null, 'x'], day), 'x');
});

test('every front page call is one guests may make', () => {
  const server = read('worker/server.js');
  const lookOnly = server.slice(server.indexOf('const LOOK_ONLY = new Set('));
  const products = ['worker/videos.js', 'worker/art.js', 'worker/streams.js', 'worker/academy.js'].map(read).join('\n');
  for (const [, op] of FRONT_CALLS) {
    const ok = lookOnly.slice(0, lookOnly.indexOf(']);')).includes(`'${op}'`) || new RegExp(`LOOK_ONLY = \\[[^\\]]*'${op.replace('.', '\\.')}'`).test(products);
    assert.ok(ok, op + ' is look-only (guests can see the front page)');
  }
});

test('a failed shelf does not break the page', async () => {
  const data = await loadFront(async (op) => ({ ok: false, error: 'nope' }));
  for (const [key] of FRONT_CALLS) assert.equal(data[key], null, key);
});

test('leaving the page mid-load still stops it (the stale signal is not swallowed)', async () => {
  class Stale extends Error {}
  await assert.rejects(loadFront(async () => { throw new Stale(); }), Stale);
});

test('Home uses the front page and no longer says videos are coming soon', () => {
  const app = read('website/app/app.js');
  const home = app.slice(app.indexOf('pages.home = async'), app.indexOf('pages.soon = async'));
  assert.match(home, /loadFront\(pageCall\)/);
  assert.match(home, /frontBox/);
  assert.ok(!/soon share videos/.test(home));
  assert.match(read('website/app/index.html'), /href="frontpage\.css"/);
});
