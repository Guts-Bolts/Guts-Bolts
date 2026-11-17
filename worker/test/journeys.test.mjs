// Journeys: branching player paths through a game, and where players leave (worker/journeys.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { stepPath, journeyResults, cleanStages, kMostRunningJourneys } from '../journeys.js';

const STAGES = [
  { name: 'Pick a class', nodes: ['ChoseWarrior', 'ChoseMage', 'SkippedClass'] },
  { name: 'First fight', nodes: ['WonFight', 'LostFight'] },
  { name: 'Shop', nodes: ['VisitedShop'] },
];

function setup() {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  const game = s.addGame(ann, 'Dungeon');
  return { s, ann, bob, game };
}

test('a path only moves forward, one stage at a time', () => {
  assert.equal(stepPath(STAGES, '', 'ChoseMage'), '1');
  assert.equal(stepPath(STAGES, '1', 'ChoseWarrior'), null, 'the first choice at a stage counts');
  assert.equal(stepPath(STAGES, '1', 'VisitedShop'), null, 'stages can\'t be skipped');
  assert.equal(stepPath(STAGES, '1', 'LostFight'), '1,1');
  assert.equal(stepPath(STAGES, '1,1,0', 'VisitedShop'), null, 'nothing after the last stage');
});

test('the chart adds up: flows, players at each choice, and who left where', () => {
  const r = journeyResults({ id: 'x', name: 'J', stages: STAGES }, ['', '', '0', '0,0', '1,1,0', '2,0,0', '0,0']);
  assert.equal(r.players, 7);
  assert.equal(r.leftAtJoin, 2);
  assert.deepEqual(r.stages[0].nodes.map((n) => n.players), [3, 1, 1]);
  assert.equal(r.stages[0].nodes[0].left, 1, 'one warrior never fought');
  assert.equal(r.stages[1].nodes[0].left, 2, 'two winners never went to the shop');
  assert.equal(r.stages[2].nodes[0].left, 0, 'finishing isn\'t leaving');
  const flow = (stage, from, to) => (r.flows.find((f) => f.stage === stage && f.from === from && f.to === to) || {}).players || 0;
  assert.equal(flow(0, -1, 0), 3, 'Joined -> warrior');
  assert.equal(flow(1, 0, 0), 2, 'warrior -> won');
  assert.equal(flow(2, 1, 0), 1, 'lost -> shop');
});

test('stages are checked', () => {
  assert.match(cleanStages([]).error, /at least one stage/);
  assert.match(cleanStages([{ name: '', nodes: ['A'] }]).error, /name/);
  assert.match(cleanStages([{ name: 'S', nodes: [] }]).error, /at least one event/);
  assert.match(cleanStages([{ name: 'S', nodes: ['__join'] }]).error, /two underscores/);
  assert.match(cleanStages([{ name: 'S', nodes: Array.from({ length: 11 }, (_, i) => 'E' + i) }]).error, /up to 10/);
  assert.match(cleanStages(Array.from({ length: 11 }, (_, i) => ({ name: 'S' + i, nodes: ['E'] }))).error, /up to 10 stages/);
  assert.deepEqual(cleanStages([{ name: 'S', nodes: ['A', 'A', 'B'] }]).stages, [{ name: 'S', nodes: ['A', 'B'] }]);
});

test('the game\'s events build players\' journeys, and only the creator sees them', () => {
  const { s, ann, bob, game } = setup();
  const made = s.call(ann, 'journeys.create', { game: game.id, name: 'First ten minutes', stages: STAGES });
  assert.ok(made.ok, made.error);
  assert.match(s.call(bob, 'journeys.list', { game: game.id }).error, /Only the game's creator/);
  assert.match(s.call(bob, 'journeys.create', { game: game.id, name: 'Mine', stages: STAGES }).error, /Only the game's creator/);
  assert.ok(s.call(s.staff, 'journeys.list', { game: game.id }).ok, 'staff can look');
  // The game hears that something is counting, even without experiments.
  assert.equal(s.call(bob, 'experiments.forGame', { game: game.id }).journeys, 1);
  const ev = (player, name) => ({ player, name });
  const r = s.call(ann, 'analytics.log', { game: game.id, events: [
    ev(10, '__join'), ev(10, 'ChoseMage'), ev(10, 'WonFight'), ev(10, 'VisitedShop'),
    ev(11, '__join'), ev(11, 'ChoseWarrior'), ev(11, 'ChoseMage'),
    ev(12, '__join'),
    ev(13, 'ChoseMage'),   // never joined: not counted
    ev(11, '__join'),      // joining again doesn't start over
  ] });
  assert.ok(r.ok, r.error);
  const j = s.call(ann, 'journeys.list', { game: game.id }).journeys[0];
  assert.equal(j.players, 3);
  assert.equal(j.leftAtJoin, 1);
  assert.deepEqual(j.stages[0].nodes.map((n) => n.players), [1, 1, 0]);
  assert.equal(j.stages[0].nodes[0].left, 1, 'the warrior left before fighting');
  assert.equal(j.stages[2].nodes[0].players, 1);
  // Someone who isn't the game's server can't send events.
  assert.match(s.call(bob, 'analytics.log', { game: game.id, events: [ev(20, '__join')] }).error, /own servers/);
  // Stop, limits, remove.
  assert.ok(s.call(ann, 'journeys.stop', { game: game.id, id: j.id }).ok);
  assert.equal(s.call(bob, 'experiments.forGame', { game: game.id }).journeys, 0);
  for (let i = 0; i < kMostRunningJourneys; i++) assert.ok(s.call(ann, 'journeys.create', { game: game.id, name: 'J' + i, stages: STAGES }).ok);
  assert.match(s.call(ann, 'journeys.create', { game: game.id, name: 'One more', stages: STAGES }).error, /at once/);
  assert.ok(s.call(ann, 'journeys.remove', { game: game.id, id: j.id }).ok);
  assert.equal(s.call(ann, 'journeys.list', { game: game.id }).journeys.length, kMostRunningJourneys);
  assert.ok(!JSON.stringify(s.call(bob, 'asset.info', { id: game.id })).includes('ChoseMage'), 'journeys never show on the public game info');
});
