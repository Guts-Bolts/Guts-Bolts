// Launchpad: creators apply with a game, staff pick some, and picked games get a shelf spot, a help
// thread and more Fyxa cloud steps (worker/launchpad.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { DEFAULT_LIMITS } from '../fyxa-cloud.js';

const PITCH = 'A big co-op heist game where four players plan and pull off robberies in a living city.';

function setup() {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  const heist = s.addGame(ann, 'Heist City');
  return { s, ann, bob, heist };
}

test('applying: your own playable game, a real pitch, something to help with, one at a time', () => {
  const { s, ann, bob, heist } = setup();
  assert.match(s.call(bob, 'launchpad.apply', { game: heist.id, pitch: PITCH, help: ['speed'] }).error, /your own games/);
  assert.match(s.call(ann, 'launchpad.apply', { game: heist.id, pitch: 'fun game', help: ['speed'] }).error, /a bit more/);
  assert.match(s.call(ann, 'launchpad.apply', { game: heist.id, pitch: PITCH, help: ['money'] }).error, /at least one/);
  s.call(ann, 'game.settings', { id: heist.id, access: 'private' });
  assert.match(s.call(ann, 'launchpad.apply', { game: heist.id, pitch: PITCH, help: ['speed'] }).error, /public/);
  s.call(ann, 'game.settings', { id: heist.id, access: 'testers' });
  const r = s.call(ann, 'launchpad.apply', { game: heist.id, pitch: PITCH, help: ['speed', 'security', 'speed'] });
  assert.ok(r.ok, r.error);
  assert.deepEqual(r.app.help, ['speed', 'security']);
  assert.equal(r.app.status, 'waiting');
  const other = s.addGame(ann, 'Second Game');
  assert.match(s.call(ann, 'launchpad.apply', { game: other.id, pitch: PITCH, help: ['art'] }).error, /One at a time/);
  assert.match(s.call(ann, 'launchpad.apply', { game: heist.id, pitch: PITCH, help: ['art'] }).error, /already has an application/);
  assert.equal(s.call(ann, 'launchpad.info').mine.length, 1);
  assert.equal(s.call(bob, 'launchpad.info').mine.length, 0);
  assert.ok(s.call(ann, 'launchpad.withdraw', { id: r.app.id }).ok);
  assert.ok(s.call(ann, 'launchpad.apply', { game: other.id, pitch: PITCH, help: ['art'] }).ok, 'withdrawn: can apply again');
});

test('staff pick a game: shelf, label, help thread and more Fyxa steps', () => {
  const { s, ann, bob, heist } = setup();
  const app = s.call(ann, 'launchpad.apply', { game: heist.id, pitch: PITCH, help: ['networking'] }).app;
  assert.match(s.call(ann, 'launchpad.decide', { id: app.id, accept: true }).error, /Only staff/);
  assert.equal(s.call(s.staff, 'launchpad.list', { status: 'waiting' }).apps.length, 1);
  assert.equal(s.call(ann, 'fyxacloud.usage').limit, DEFAULT_LIMITS.free, 'no perk before');
  const d = s.call(s.staff, 'launchpad.decide', { id: app.id, accept: true, note: 'Love it' });
  assert.ok(d.ok, d.error);
  assert.equal(d.app.stage, 'build');
  assert.ok(ann.notes.some((n) => n.kind === 'launchpad' && /Heist City is in Launchpad/.test(n.text)));
  assert.equal(s.call(ann, 'fyxacloud.usage').limit, DEFAULT_LIMITS.tbc, 'more Fyxa steps while in it');
  assert.equal(s.call(bob, 'asset.info', { id: heist.id }).asset.launchpad, 'build');
  assert.deepEqual(s.call(bob, 'launchpad.games').games.map((g) => g.name), ['Heist City'], 'on the shelf');
  // The help thread: both sides can write, each is told; others can't read it.
  assert.ok(s.call(ann, 'launchpad.note', { id: app.id, text: 'How do I make my server faster with 30 players?' }).ok);
  assert.ok(s.staff.notes.some((n) => n.kind === 'launchpad' && /ann wrote/.test(n.text)));
  assert.ok(s.call(s.staff, 'launchpad.note', { id: app.id, text: 'Use streaming and fewer unanchored parts.' }).ok);
  assert.ok(ann.notes.some((n) => n.kind === 'launchpad' && /Staff wrote/.test(n.text)));
  assert.match(s.call(bob, 'launchpad.note', { id: app.id, text: 'hi' }).error, /doesn't exist/);
  assert.equal(s.call(ann, 'launchpad.info').mine[0].notes.length, 2);
  // Stages; graduating ends the extra steps.
  assert.match(s.call(s.staff, 'launchpad.stage', { id: app.id, stage: 'done' }).error, /Pick/);
  assert.ok(s.call(s.staff, 'launchpad.stage', { id: app.id, stage: 'launch' }).ok);
  assert.ok(s.call(s.staff, 'launchpad.stage', { id: app.id, stage: 'graduated' }).ok);
  assert.equal(s.call(ann, 'fyxacloud.usage').limit, DEFAULT_LIMITS.free, 'graduated: back to normal');
  assert.equal(s.call(bob, 'asset.info', { id: heist.id }).asset.launchpad, 'graduated', 'the label stays');
});

test('turning down tells the creator why; a closed round takes no applications', () => {
  const { s, ann, heist } = setup();
  const app = s.call(ann, 'launchpad.apply', { game: heist.id, pitch: PITCH, help: ['design'] }).app;
  assert.ok(s.call(s.staff, 'launchpad.decide', { id: app.id, accept: false, note: 'Needs more levels first' }).ok);
  assert.ok(ann.notes.some((n) => /wasn't picked.*Needs more levels first/.test(n.text)));
  assert.match(s.call(s.staff, 'launchpad.decide', { id: app.id, accept: true }).error, /already answered/);
  assert.ok(s.call(s.staff, 'launchpad.round', { name: '2027 Round 1', open: false }).ok);
  assert.match(s.call(ann, 'launchpad.apply', { game: heist.id, pitch: PITCH, help: ['design'] }).error, /isn't taking/);
  assert.equal(s.call(ann, 'launchpad.info').round.name, '2027 Round 1');
  s.call(s.staff, 'launchpad.round', { name: '2027 Round 1', open: true, closes: Math.floor(Date.now() / 1000) - 5 });
  assert.equal(s.call(ann, 'launchpad.info').round.open, false, 'past its closing time');
});
