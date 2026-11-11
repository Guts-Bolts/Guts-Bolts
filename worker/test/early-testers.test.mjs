// Early testers: a creator picks a few people to play a game before it's public, and reads their notes
// (worker/testers.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { kMostTesters } from '../testers.js';

function setup() {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob'), cat = s.signUp('cat');
  const obby = s.addGame(ann, 'Obby');
  assert.ok(s.call(ann, 'game.settings', { id: obby.id, access: 'testers' }).ok);
  return { s, ann, bob, cat, obby };
}

test('a testers-only game: only the creator, staff and its testers can play it', () => {
  const { s, ann, bob, cat, obby } = setup();
  const no = s.call(bob, 'servers.play', { game: obby.id });
  assert.equal(no.ok, false);
  assert.match(no.error, /still being tested/);
  const r = s.call(ann, 'game.addTester', { game: obby.id, who: 'bob' });
  assert.ok(r.ok, r.error);
  assert.deepEqual(r.testers.map((p) => p.id), [bob.id]);
  assert.ok(bob.notes.some((n) => n.kind === 'testers' && /picked you to test Obby/.test(n.text)), 'bob is told');
  assert.ok(s.call(bob, 'servers.play', { game: obby.id }).ok, 'a tester can play');
  assert.equal(s.call(cat, 'servers.play', { game: obby.id }).ok, false, 'someone else still can\'t');
  assert.ok(s.call(s.staff, 'servers.play', { game: obby.id }).ok, 'staff can');
  assert.ok(s.call(ann, 'servers.play', { game: obby.id }).ok, 'the creator can');
  assert.equal(s.call(bob, 'game.amTester', { game: obby.id }).tester, true);
  assert.equal(s.call(cat, 'game.amTester', { game: obby.id }).tester, false);
});

test('only the creator picks testers, and the list has limits', () => {
  const { s, ann, bob, cat, obby } = setup();
  assert.match(s.call(bob, 'game.addTester', { game: obby.id, who: 'cat' }).error, /Only the game's creator/);
  assert.match(s.call(ann, 'game.addTester', { game: obby.id, who: 'ann' }).error, /always play it/);
  assert.match(s.call(ann, 'game.addTester', { game: obby.id, who: 'nobody-here' }).error, /nobody with that name/);
  assert.ok(s.call(ann, 'game.addTester', { game: obby.id, who: String(bob.userId) }).ok, 'by user number');
  assert.match(s.call(ann, 'game.addTester', { game: obby.id, who: 'bob' }).error, /already a tester/);
  for (let i = 1; i < kMostTesters; i++) assert.ok(s.call(ann, 'game.addTester', { game: obby.id, who: s.signUp('t' + i).name }).ok);
  assert.match(s.call(ann, 'game.addTester', { game: obby.id, who: 'cat' }).error, /at most/);
  assert.ok(s.call(ann, 'game.removeTester', { game: obby.id, who: 'bob' }).ok);
  assert.equal(s.call(bob, 'servers.play', { game: obby.id }).ok, false, 'taken off: can\'t play any more');
  assert.ok(s.call(s.staff, 'game.testers', { game: obby.id }).ok, 'staff can look');
  assert.match(s.call(cat, 'game.testers', { game: obby.id }).error, /Only the game's creator/);
});

test('testers leave notes only the creator reads; sending again replaces it', () => {
  const { s, ann, bob, cat, obby } = setup();
  s.call(ann, 'game.addTester', { game: obby.id, who: 'bob' });
  assert.match(s.call(cat, 'game.testerNote', { game: obby.id, stars: 4, text: 'hi' }).error, /Only the game's testers/);
  assert.match(s.call(bob, 'game.testerNote', { game: obby.id, stars: 9, text: 'hi' }).error, /1 to 5/);
  assert.match(s.call(bob, 'game.testerNote', { game: obby.id, stars: 3, text: '  ' }).error, /few words/);
  assert.ok(s.call(bob, 'game.testerNote', { game: obby.id, stars: 3, text: 'Level 2 is too hard' }).ok);
  assert.ok(ann.notes.some((n) => n.kind === 'testers' && /bob left a tester note on Obby \(3 stars\)/.test(n.text)));
  const before = ann.notes.length;
  assert.ok(s.call(bob, 'game.testerNote', { game: obby.id, stars: 5, text: 'Fixed, now it\'s great' }).ok);
  assert.equal(ann.notes.length, before, 'changing a note doesn\'t ping again');
  const r = s.call(ann, 'game.testers', { game: obby.id });
  assert.equal(r.notes.length, 1);
  assert.equal(r.notes[0].stars, 5);
  assert.equal(r.notes[0].who.id, bob.id);
  assert.equal(r.testers[0].noted, true);
  assert.equal(s.call(bob, 'game.amTester', { game: obby.id }).note.stars, 5);
  const pub = JSON.stringify([s.call(ann, 'asset.info', { id: obby.id }), s.call(bob, 'asset.info', { id: obby.id })]);
  assert.ok(pub.includes('Obby'), 'the game page loads');
  assert.ok(!pub.includes('Fixed, now'), 'notes never show up for other people');
  s.call(ann, 'game.removeTester', { game: obby.id, who: 'bob' });
  assert.equal(s.call(ann, 'game.testers', { game: obby.id }).notes.length, 0, 'taking a tester off removes their note');
});

test('going public tells every tester it\'s out', () => {
  const { s, ann, bob, cat, obby } = setup();
  s.call(ann, 'game.addTester', { game: obby.id, who: 'bob' });
  assert.ok(s.call(ann, 'game.settings', { id: obby.id, access: 'public' }).ok);
  assert.ok(bob.notes.some((n) => /Obby, the game you tested for ann, is out for everyone now/.test(n.text)));
  assert.ok(s.call(cat, 'servers.play', { game: obby.id }).ok, 'now everyone can play');
  assert.equal(s.call(ann, 'game.testers', { game: obby.id }).testers.length, 1, 'the list is kept for next time');
  assert.match(s.call(ann, 'game.settings', { id: obby.id, access: 'nope' }).error, /testers/);
});
