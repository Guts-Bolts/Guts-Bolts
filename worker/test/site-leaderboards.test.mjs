// Leaderboards (worker/leaderboards.js): top creators, most played games and most faved art,
// this week and all time; plus the new upload achievements on profiles (extras.js).
// src/server/ServerLeaderboards.cpp has the same rules (tests/ServerLeaderboardsTests.cpp).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { lastDays, playsIn, kTopSize } from '../leaderboards.js';
import { leaderboardPages } from '../../website/app/leaderboards.js';
import { PRODUCTS } from '../../website/app/products.js';

const PNG = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==';
const DAY = 86400;

// Visits on `daysAgo` (0 = today), the way server.js counts a play.
function visit(s, g, n, daysAgo = 0) {
  const key = lastDays(Math.floor(Date.now() / 1000) - daysAgo * DAY, 1)[0];
  g.days = g.days || {};
  g.days[key] = { plays: ((g.days[key] || {}).plays || 0) + n };
  g.plays = (g.plays || 0) + n;
  s.saveAsset(g);
}
const fresh = (s) => { s.leadersCache = {}; };

test('a week is the last 7 days, today first', () => {
  const t = Date.UTC(2026, 10, 24, 12) / 1000;
  assert.deepEqual(lastDays(t), ['2026-11-24', '2026-11-23', '2026-11-22', '2026-11-21', '2026-11-20', '2026-11-19', '2026-11-18']);
  assert.equal(playsIn({ days: { '2026-11-24': { plays: 3 }, '2026-11-10': { plays: 50 } } }, lastDays(t)), 3);
});

test('most played games: this week counts only the last 7 days, all time counts everything', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  const old = s.addGame(ann, 'Old Hit'), fresh1 = s.addGame(bob, 'New Thing');
  visit(s, old, 500, 20);   // big, but weeks ago
  visit(s, old, 2, 1);
  visit(s, fresh1, 40, 0);
  let r = s.call(bob, 'leaders.get', { period: 'week' });
  assert.ok(r.ok, r.error);
  assert.equal(r.period, 'week');
  assert.deepEqual(r.games.map((g) => [g.name, g.visits]), [['New Thing', 40], ['Old Hit', 2]]);
  assert.equal(r.games[0].by.name, 'bob');
  r = s.call(bob, 'leaders.get', { period: 'all' });
  assert.deepEqual(r.games.map((g) => [g.name, g.visits]), [['Old Hit', 502], ['New Thing', 40]]);
});

test('only public things by people who aren\'t banned make the lists', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), cat = s.signUp('cat');
  const priv = s.addGame(ann, 'Secret', { access: 'private' });
  const waiting = s.addGame(ann, 'Waiting', { review: 'pending' });
  const banned = s.addGame(cat, 'Banned Game');
  const ok = s.addGame(ann, 'Fine Game');
  for (const g of [priv, waiting, banned, ok]) visit(s, g, 10);
  cat.banned = true; s.saveUser(cat);
  const r = s.call(ann, 'leaders.get', {});
  assert.deepEqual(r.games.map((g) => g.name), ['Fine Game']);
  assert.deepEqual(r.creators.map((c) => c.name), ['ann']);
  assert.equal(r.creators[0].visits, 10, 'the private and waiting games don\'t add points either');
});

test('most faved art and top creators (visits + faves)', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob'), cy = s.signUp('cy');
  const post = (who, title) => s.call(s.staff, 'art.post', { title, cat: 'drawing', data: PNG }).piece.id;
  // Staff posts go straight to the gallery; hand them to the artists we want.
  const a1 = post(s.staff, 'Sunset'), a2 = post(s.staff, 'Robot'), a3 = post(s.staff, 'Old one');
  s.artInit();
  s.art.get(a1).by = ann.id; s.art.get(a2).by = bob.id; s.art.get(a3).by = bob.id;
  s.art.get(a3).at -= 30 * DAY;   // posted a month ago
  for (const who of [bob, cy]) s.call(who, 'art.fave', { id: a1, on: true });
  for (const who of [ann, cy, s.staff]) s.call(who, 'art.fave', { id: a3, on: true });
  s.call(cy, 'art.fave', { id: a2, on: true });
  const g = s.addGame(cy, 'Cy Land');
  visit(s, g, 4);
  fresh(s);
  let r = s.call(ann, 'leaders.get', { period: 'week' });
  assert.deepEqual(r.art.map((p) => [p.title, p.faves]), [['Sunset', 2], ['Robot', 1]], 'the old piece isn\'t in this week');
  assert.deepEqual(r.creators.map((c) => [c.name, c.points]), [['cy', 4], ['ann', 2], ['bob', 1]]);
  assert.equal(r.creators[0].visits, 4);
  fresh(s);
  r = s.call(ann, 'leaders.get', { period: 'all' });
  assert.deepEqual(r.art.map((p) => p.title), ['Old one', 'Sunset', 'Robot']);
  assert.deepEqual(r.creators[0], Object.assign({}, r.creators[0], { name: 'bob', points: 4, faves: 4, visits: 0 }));
});

test('guests can look; the lists are short and kept for a few minutes', () => {
  const s = makeServer();
  const ann = s.signUp('ann');
  const games = Array.from({ length: kTopSize + 3 }, (_, i) => s.addGame(ann, 'Game ' + i));
  games.forEach((g, i) => visit(s, g, i + 1));
  const guest = s.user('c'.repeat(64));
  let r = s.call(guest, 'leaders.get', {});
  assert.ok(r.ok, r.error);
  assert.equal(r.games.length, kTopSize);
  assert.equal(r.games[0].name, 'Game ' + (kTopSize + 2));
  visit(s, games[0], 1000);
  r = s.call(guest, 'leaders.get', {});
  assert.notEqual(r.games[0].name, 'Game 0', 'still the saved list');
  fresh(s);
  assert.equal(s.call(guest, 'leaders.get', {}).games[0].name, 'Game 0');
});

test('new achievements: first upload, art, faves and visits', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  const ids = (who) => s.call(bob, 'profile', { id: String(who.userId) }).achievements.filter((x) => x.earned).map((x) => x.id);
  assert.deepEqual(ids(ann), []);
  const id = s.call(s.staff, 'art.post', { title: 'Mine', cat: 'drawing', data: PNG }).piece.id;
  s.art.get(id).by = ann.id;
  assert.deepEqual(ids(ann).sort(), ['artist', 'first-upload']);
  s.art.get(id).faves = Array.from({ length: 25 }, (_, i) => 'x' + i);
  assert.ok(ids(ann).includes('fan-favorite'));
  const g = s.addGame(ann, 'Ann World');
  visit(s, g, 1000);
  const got = ids(ann);
  for (const want of ['game-maker', 'crowd', 'hit-maker']) assert.ok(got.includes(want), want);
  assert.ok(!got.includes('superstar'));
  const sup = s.call(bob, 'profile', { id: String(ann.userId) }).achievements.find((x) => x.id === 'superstar');
  assert.deepEqual([sup.have, sup.need], [1000, 10000]);
});

test('the page is on the site: Communities links to it', () => {
  const communities = PRODUCTS.find((p) => p.id === 'communities');
  assert.ok(communities.pages.includes('leaderboards'));
  assert.ok(communities.links.some(([, href]) => href === '#/leaderboards'));
  const pages = leaderboardPages({ html: () => '', raw: (x) => x, show() {}, pageCall: async () => ({ ok: false }), signedIn: () => false,
    verified: () => '', avatarSvg: () => '', gameIcon: () => '', me: () => null });
  assert.equal(typeof pages.leaderboards, 'function');
});
