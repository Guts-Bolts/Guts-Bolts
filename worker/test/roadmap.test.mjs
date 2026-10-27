// The public roadmap: staff say what's planned, being worked on and done, developers
// suggest ideas and vote, and staff answer the ideas (on the roadmap, or no with a note).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { GUEST_OK } from '../server.js';

test('staff add, change and remove roadmap items, and everyone can see them', () => {
  const s = makeServer();
  const add = s.call(s.staff, 'admin.roadmapSave', { title: 'Weather', text: 'Rain and snow', status: 'working', version: '0.7.8' });
  assert.ok(add.ok, add.error);
  assert.equal(add.items[0].title, 'Weather');
  const id = add.items[0].id;
  assert.ok(s.call(s.staff, 'admin.roadmapSave', { id, title: 'Weather', text: 'Rain and snow', status: 'done', version: '0.7.8' }).ok);
  const visitor = s.user('c'.repeat(64));
  assert.ok(GUEST_OK.has('roadmap.get'), 'visitors can look');
  const seen = s.call(visitor, 'roadmap.get');
  assert.equal(seen.items[0].status, 'done');
  assert.match(s.call(s.staff, 'admin.roadmapSave', { title: 'X', status: 'someday' }).error, /Planned/);
  assert.match(s.call(s.staff, 'admin.roadmapSave', { status: 'planned' }).error, /title/);
  assert.ok(s.call(s.staff, 'admin.roadmapRemove', { id }).ok);
  assert.equal(s.call(visitor, 'roadmap.get').items.length, 0);
  assert.equal(s.staffLog.at(-1).action, 'roadmap');
});

test('people suggest ideas and vote, and the most-voted come first', () => {
  const s = makeServer();
  const bob = s.signUp('bob'), carol = s.signUp('carol'), dan = s.signUp('dan');
  assert.ok(s.call(bob, 'roadmap.suggest', { title: 'Mounts', text: 'Horses to ride' }).ok);
  const r = s.call(carol, 'roadmap.suggest', { title: 'Boats', text: '' });
  assert.ok(r.ok, r.error);
  const boats = r.ideas.find((x) => x.title === 'Boats');
  assert.equal(boats.votes, 1, 'your own idea starts with your vote');
  assert.ok(boats.mine);
  assert.match(s.call(dan, 'roadmap.suggest', { title: 'boats' }).error, /Vote for it/);
  s.call(bob, 'roadmap.vote', { id: boats.id, on: true });
  const after = s.call(dan, 'roadmap.vote', { id: boats.id, on: true });
  assert.equal(after.ideas[0].title, 'Boats');
  assert.equal(after.ideas[0].votes, 3);
  assert.ok(after.ideas[0].voted);
  assert.equal(s.call(dan, 'roadmap.vote', { id: boats.id, on: false }).ideas[0].votes, 2);
  assert.equal(s.call(dan, 'roadmap.vote', { id: boats.id, on: true }).ideas[0].votes, 3, 'voting twice counts once');
  assert.equal(s.call(dan, 'roadmap.vote', { id: boats.id, on: true }).ideas[0].votes, 3);
  const visitor = s.user('c'.repeat(64));
  assert.match(s.call(visitor, 'roadmap.suggest', { title: 'Hi' }).error, /Sign up/);
  assert.ok(!GUEST_OK.has('roadmap.suggest') && !GUEST_OK.has('roadmap.vote'));
});

test('three open ideas each, and an idea needs a title', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  for (const t of ['One', 'Two', 'Three']) assert.ok(s.call(bob, 'roadmap.suggest', { title: t }).ok);
  assert.match(s.call(bob, 'roadmap.suggest', { title: 'Four' }).error, /3 ideas waiting/);
  assert.match(s.call(bob, 'roadmap.suggest', { title: '   ' }).error, /title/);
});

test('staff answer ideas: planned goes on the roadmap, and the person gets told', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const id = s.call(bob, 'roadmap.suggest', { title: 'Mounts', text: 'Horses' }).ideas[0].id;
  assert.equal(s.call(bob, 'admin.ideaAnswer', { id, answer: 'planned' }).ok, false, 'only staff');
  const r = s.call(s.staff, 'admin.ideaAnswer', { id, answer: 'planned', note: 'Coming in 0.8!' });
  assert.ok(r.ok, r.error);
  assert.equal(r.items[0].title, 'Mounts');
  assert.equal(r.items[0].status, 'planned');
  assert.equal(r.ideas.length, 0);
  assert.equal(r.answered[0].note, 'Coming in 0.8!');
  assert.match(s.users.get(bob.id).notes[0].text, /on the roadmap/);
  assert.match(s.call(bob, 'roadmap.vote', { id, on: true }).error, /already answered/);
  const no = s.call(bob, 'roadmap.suggest', { title: 'Free Bolts' }).ideas[0].id;
  s.call(s.staff, 'admin.ideaAnswer', { id: no, answer: 'declined', note: 'Sorry, no.' });
  assert.match(s.users.get(bob.id).notes[0].text, /Sorry, no/);
  const spam = s.call(bob, 'roadmap.suggest', { title: 'spam spam' }).ideas[0].id;
  assert.ok(s.call(s.staff, 'admin.ideaRemove', { id: spam }).ok);
  assert.equal(s.call(bob, 'roadmap.get').ideas.length, 0);
});

test('the roadmap and ideas are still there after a restart', async () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  s.call(s.staff, 'admin.roadmapSave', { title: 'Weather', status: 'done' });
  s.call(bob, 'roadmap.suggest', { title: 'Mounts' });
  const { GbServerObject } = await import('../server.js');
  const again = new GbServerObject({ storage: { sql: s.sql } }, { OFFICIAL: s.official });
  const r = again.op('roadmap.get', again.users.get(bob.id), {});
  assert.equal(r.items[0].title, 'Weather');
  assert.equal(r.ideas[0].title, 'Mounts');
  assert.ok(r.ideas[0].voted);
});
