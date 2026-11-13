// Experiments (A/B tests): a creator tries versions of their game and counts what players do
// in each (worker/experiments.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { variantOf, zScore, kMostRunning } from '../experiments.js';

function setup() {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  const obby = s.addGame(ann, 'Obby');
  return { s, ann, bob, obby };
}

test('the version a player gets is fixed, spread out, and matches the C++ numbers', () => {
  assert.equal(variantOf('abc', 7, 2), variantOf('abc', 7, 2));
  const counts = [0, 0, 0];
  for (let p = 1; p <= 3000; p++) counts[variantOf('5f0e2a9c1b7d', p, 3)]++;
  for (const c of counts) assert.ok(c > 850 && c < 1150, 'about a third each: ' + counts);
  // FNV-1a of "x:1" (the same sum src/scripting/Experiments.cpp and src/server do)
  assert.equal(variantOf('x', 1, 1000000007), 0x87017982 % 1000000007);
});

test('a creator makes, stops and removes experiments; nobody else can', () => {
  const { s, ann, bob, obby } = setup();
  const bad = (args, re) => assert.match(s.call(ann, 'experiments.create', Object.assign({ game: obby.id }, args)).error, re);
  bad({ name: '', variants: ['A', 'B'] }, /name/);
  bad({ name: 'Shop', variants: ['A'] }, /2 to 4 versions/);
  bad({ name: 'Shop', variants: ['A', 'a'] }, /different name/);
  bad({ name: 'Shop', variants: ['A', 'B'], metrics: ['a', 'b', 'c', 'd'] }, /up to 3/);
  bad({ name: 'Shop', variants: ['A', 'B'], metrics: ['funnel:Tutorial'] }, /funnel:Tutorial:3/);
  bad({ name: 'Shop', variants: ['A', 'B'], metrics: ['spent:'] }, /currency/);
  const r = s.call(ann, 'experiments.create', { game: obby.id, name: 'Shop', variants: ['Old', 'New'], metrics: ['BoughtSword', 'spent:Coins', 'funnel:Tutorial:3'] });
  assert.ok(r.ok, r.error);
  assert.equal(r.experiments.length, 1);
  assert.deepEqual(r.experiments[0].results.map((v) => v.name), ['Old', 'New']);
  bad({ name: 'shop', variants: ['A', 'B'] }, /already a running experiment/);
  assert.match(s.call(bob, 'experiments.create', { game: obby.id, name: 'Mine', variants: ['A', 'B'] }).error, /Only the game's creator/);
  assert.match(s.call(bob, 'experiments.list', { game: obby.id }).error, /Only the game's creator/);
  assert.ok(s.call(s.staff, 'experiments.list', { game: obby.id }).ok, 'staff can look');
  const id = r.experiments[0].id;
  // the game asks which experiments are running
  const g = s.call(bob, 'experiments.forGame', { game: obby.id });
  assert.deepEqual(g.experiments, [{ id, name: 'Shop', variants: ['Old', 'New'] }]);
  assert.ok(s.call(ann, 'experiments.stop', { game: obby.id, id }).ok);
  assert.equal(s.call(bob, 'experiments.forGame', { game: obby.id }).experiments.length, 0, 'stopped ones aren\'t running');
  assert.ok(s.call(ann, 'experiments.remove', { game: obby.id, id }).ok);
  assert.equal(s.call(ann, 'experiments.list', { game: obby.id }).experiments.length, 0);
  for (let i = 0; i < kMostRunning; i++) assert.ok(s.call(ann, 'experiments.create', { game: obby.id, name: 'E' + i, variants: ['A', 'B'] }).ok);
  bad({ name: 'One more', variants: ['A', 'B'] }, /at once/);
  const pub = JSON.stringify(s.call(bob, 'asset.info', { id: obby.id }));
  assert.ok(!pub.includes('"E0"'), 'experiments never show on the public game info');
});

test('events from the game add up per version, with players, minutes and coming back', () => {
  const { s, ann, bob, obby } = setup();
  const id = s.call(ann, 'experiments.create', { game: obby.id, name: 'Shop', variants: ['Old', 'New'], metrics: ['BoughtSword', 'spent:Coins'] }).experiments[0].id;
  // 200 players: those on "New" buy a sword far more often
  const events = [];
  for (let p = 1000; p < 1200; p++) {
    const isNew = variantOf(id, p, 2) === 1;
    events.push({ player: p, name: '__join' }, { player: p, name: '__minutes', value: 10 });
    if (isNew ? p % 10 < 6 : p % 10 < 2) events.push({ player: p, name: 'BoughtSword', value: 1 }, { player: p, name: 'spent:Coins', value: 50 });
    events.push({ player: p, name: 'NotCounted', value: 1 });
  }
  for (let i = 0; i < events.length; i += 200) assert.ok(s.call(ann, 'analytics.log', { game: obby.id, events: events.slice(i, i + 200) }).ok);
  // the same day again doesn't count as coming back
  s.call(ann, 'analytics.log', { game: obby.id, events: [{ player: 1000, name: '__join' }] });
  const e = s.call(ann, 'experiments.list', { game: obby.id }).experiments[0];
  const [oldV, newV] = e.results;
  assert.equal(oldV.players + newV.players, 200);
  assert.equal(oldV.minutes, oldV.players * 10);
  assert.equal(oldV.cameBack + newV.cameBack, 0);
  const sword = (v) => v.metrics.find((m) => m.metric === 'BoughtSword');
  assert.ok(sword(newV).did > sword(oldV).did * 2);
  assert.equal(sword(newV).sure, true, 'a big difference is marked as real');
  assert.equal(sword(oldV).sure, false, 'the first version is what the others are compared with');
  assert.equal(newV.metrics.find((m) => m.metric === 'spent:Coins').total, sword(newV).did * 50);
  assert.ok(!JSON.stringify(e).includes('NotCounted'), 'only the chosen metrics are kept');
});

test('only the game\'s own servers can send analytics', () => {
  const { s, bob, obby } = setup();
  s.call(obby.creator === bob.id ? bob : s.users.get(obby.creator), 'experiments.create', { game: obby.id, name: 'Shop', variants: ['A', 'B'] });
  const r = s.call(bob, 'analytics.log', { game: obby.id, events: [{ player: 5, name: '__join' }] });
  assert.equal(r.ok, false);
  assert.match(r.error, /own servers/);
});

test('the z-test says when a difference is luck', () => {
  assert.ok(Math.abs(zScore(10, 100, 12, 100)) < 1.96, '10% vs 12% of 100 is luck');
  assert.ok(Math.abs(zScore(100, 1000, 200, 1000)) > 1.96, '10% vs 20% of 1000 is real');
  assert.equal(zScore(0, 0, 1, 1), 0);
});
