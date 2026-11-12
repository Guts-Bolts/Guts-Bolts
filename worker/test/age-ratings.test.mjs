// Age ratings: a creator answers a few questions and the answers decide the game's rating
// (worker/ratings.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { QUESTIONS, ratingFor } from '../ratings.js';

const allNone = () => Object.fromEntries(QUESTIONS.map((q) => [q.id, 'none']));

test('the highest age any answer needs is the rating', () => {
  assert.equal(ratingFor(allNone()), 'all');
  assert.equal(ratingFor(Object.assign(allNone(), { scary: 'mild' })), '9');
  assert.equal(ratingFor(Object.assign(allNone(), { scary: 'mild', violence: 'realistic' })), '13');
  assert.equal(ratingFor(Object.assign(allNone(), { violence: 'gore', scary: 'mild' })), '17');
  assert.equal(ratingFor({ violence: 'none' }), '', 'a missing answer');
  assert.equal(ratingFor(Object.assign(allNone(), { scary: 'very' })), '', 'a made-up answer');
});

test('the creator rates a game; it shows on the game and in its history', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  const g = s.addGame(ann, 'Spooky House');
  assert.equal(s.call(bob, 'asset.info', { id: g.id }).asset.rating, '', 'not rated yet');
  assert.match(s.call(bob, 'game.rate', { game: g.id, answers: allNone() }).error, /Only the game's creator/);
  assert.match(s.call(ann, 'game.rate', { game: g.id, answers: { violence: 'none' } }).error, /every question/);
  const r = s.call(ann, 'game.rate', { game: g.id, answers: Object.assign(allNone(), { scary: 'intense' }) });
  assert.ok(r.ok, r.error);
  assert.equal(r.rating, '13');
  assert.equal(s.call(bob, 'asset.info', { id: g.id }).asset.rating, '13');
  const info = s.call(bob, 'game.rating', { game: g.id });
  assert.equal(info.answers.scary, 'intense');
  assert.equal(info.history.length, 1);
  assert.equal(info.history[0].to, '13');
  assert.equal(info.history[0].by.name, 'ann');
  // Same rating again: no new history line. Staff fixing it: the creator is told.
  s.call(ann, 'game.rate', { game: g.id, answers: Object.assign(allNone(), { scary: 'intense', romance: 'mild' }) });
  assert.equal(s.call(bob, 'game.rating', { game: g.id }).history.length, 1);
  const fixed = s.call(s.staff, 'game.rate', { game: g.id, answers: Object.assign(allNone(), { violence: 'gore' }) });
  assert.equal(fixed.rating, '17');
  assert.equal(fixed.history[0].staff, true);
  assert.ok(ann.notes.some((n) => n.kind === 'rating' && /Staff changed the age rating of Spooky House to 17\+/.test(n.text)));
});

test('a player\'s rating limit leaves higher-rated games out of lists (but not their own)', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), kid = s.signUp('kid');
  const calm = s.addGame(ann, 'Calm Farm'), scary = s.addGame(ann, 'Night Terror'), fresh = s.addGame(ann, 'Unrated');
  s.call(ann, 'game.rate', { game: calm.id, answers: allNone() });
  s.call(ann, 'game.rate', { game: scary.id, answers: Object.assign(allNone(), { violence: 'gore' }) });
  const names = (who) => s.call(who, 'list', { kind: 'game' }).assets.map((a) => a.name).sort();
  assert.deepEqual(names(kid), ['Calm Farm', 'Night Terror', 'Unrated']);
  assert.match(s.call(kid, 'rating.limit', { limit: '21' }).error, /Pick/);
  assert.equal(s.call(kid, 'rating.limit', { limit: '13' }).limit, '13');
  assert.equal(s.call(kid, 'rating.limit', {}).limit, '13');
  assert.deepEqual(names(kid), ['Calm Farm', 'Unrated']);
  s.call(ann, 'rating.limit', { limit: 'all' });
  assert.ok(names(ann).includes('Night Terror'), 'your own games always show');
  s.call(kid, 'rating.limit', { limit: '' });
  assert.equal(names(kid).length, 3, 'back to everything');
  void fresh;
});
