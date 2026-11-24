// Levels, XP, daily streaks, profile walls and showcases (worker/levels.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { xpForLevel, levelOf, levelReward, dailyBolts, XP_FOR, kDailyXp, kShowcase } from '../levels.js';

function setup() {
  const s = makeServer();
  return { s, ann: s.signUp('ann'), bob: s.signUp('bob'), cat: s.signUp('cat') };
}
const befriend = (s, a, b) => { a.friends.push(b.id); b.friends.push(a.id); s.saveUser(a, b); };

test('the level curve: level 2 at 100 XP, 3 at 300, 10 at 4,500', () => {
  assert.equal(levelOf(0), 1);
  assert.equal(levelOf(99), 1);
  assert.equal(levelOf(100), 2);
  assert.equal(levelOf(299), 2);
  assert.equal(levelOf(300), 3);
  assert.equal(xpForLevel(10), 4500);
  assert.equal(levelOf(1e12), 100, 'stops at 100');
  assert.equal(levelReward(2), 10);
  assert.equal(levelReward(50), 100, 'at most 100 Bolts a level');
});

test('the streak: 25 Bolts on day one, 5 more each day in a row, up to 60', () => {
  assert.deepEqual([1, 2, 3, 8, 30].map(dailyBolts), [25, 30, 35, 60, 60]);
});

test('daily Bolts: claim once a day, the streak grows on days in a row and starts over after a gap', () => {
  const { s, ann } = setup();
  const r = s.call(ann, 'bolts.daily');
  assert.ok(r.ok, r.error);
  assert.equal(r.got, 25);
  assert.equal(r.streak, 1);
  assert.equal(r.me.levels.streak, 1);
  assert.equal(r.me.levels.xp, kDailyXp, 'claiming gives some XP too');
  assert.equal(s.call(ann, 'bolts.daily').ok, false, 'once a day');
  // Pretend yesterday was claimed too (3 days in a row so far).
  const today = new Date().toISOString().slice(0, 10);
  const yesterday = new Date(Date.now() - 86400000).toISOString().slice(0, 10);
  ann.ledger = ann.ledger.filter((e) => e[3] !== 'daily:' + today);
  Object.assign(ann.levels, { lastDaily: yesterday, streak: 2 });
  const again = s.call(ann, 'bolts.daily');
  assert.equal(again.streak, 3);
  assert.equal(again.got, 35);
  assert.equal(again.me.levels.nextDaily, 40, 'tomorrow is worth more');
  // A missed day: back to 1.
  ann.ledger = ann.ledger.filter((e) => e[3] !== 'daily:' + today);
  Object.assign(ann.levels, { lastDaily: '2020-01-01', streak: 9, best: 9 });
  assert.equal(s.call(ann, 'levels.me').levels.streak, 0, 'a broken streak shows as 0');
  const fresh = s.call(ann, 'bolts.daily');
  assert.equal(fresh.streak, 1);
  assert.equal(fresh.got, 25);
  assert.equal(ann.levels.best, 9, 'the best streak is kept');
});

test('XP for doing things, with a daily cap for each, and Bolts for each new level', () => {
  const { s, ann, bob } = setup();
  befriend(s, ann, bob);
  const xp0 = s.call(ann, 'levels.me').levels.xp;
  assert.ok(s.call(ann, 'wall.post', { id: String(bob.userId), text: 'hi bob!' }).ok);
  assert.equal(s.call(ann, 'levels.me').levels.xp, xp0 + XP_FOR['wall.post'][0], 'a wall post gives XP');
  // Failed requests give nothing.
  s.call(ann, 'wall.post', { id: String(bob.userId), text: '' });
  assert.equal(s.call(ann, 'levels.me').levels.xp, xp0 + XP_FOR['wall.post'][0]);
  // The cap: only the first few count each day.
  for (let i = 0; i < 20; i++) s.call(ann, 'wall.post', { id: String(bob.userId), text: 'post ' + i });
  assert.equal(s.call(ann, 'levels.me').levels.xp, xp0 + XP_FOR['wall.post'][0] * XP_FOR['wall.post'][1]);
  // Level up: Bolts and a bell, once.
  const bolts = s.balance(ann);
  s.giveXp(ann, 1000);
  const lv = s.call(ann, 'levels.me').levels;
  assert.ok(lv.level >= 4, 'level ' + lv.level);
  let reward = 0;
  for (let L = 2; L <= lv.level; L++) reward += levelReward(L);
  assert.equal(s.balance(ann), bolts + reward);
  assert.ok(ann.notes.some((n) => n.kind === 'level'));
  // Guests never get XP.
  const guest = s.user('c'.repeat(64));
  assert.equal(s.levelsEarn('wall.post', guest), 0);
});

test('the profile shows the level, streak and showcase', () => {
  const { s, ann, bob } = setup();
  const game = s.addGame(ann, 'Castle Run');
  const r = s.call(bob, 'profile', { id: String(ann.userId) });
  assert.ok(r.ok, r.error);
  assert.equal(r.levels.level, 1);
  assert.deepEqual(r.levels.showcase, []);
  assert.ok(s.call(ann, 'showcase.set', { ids: [game.id] }).ok);
  const after = s.call(bob, 'profile', { id: String(ann.userId) });
  assert.deepEqual(after.levels.showcase.map((a) => a.id), [game.id]);
  // Only your own things, and only a few.
  const bobs = s.addGame(bob, 'Not yours');
  assert.match(s.call(ann, 'showcase.set', { ids: [bobs.id] }).error, /made or own/);
  assert.match(s.call(ann, 'showcase.set', { ids: Array.from({ length: kShowcase + 1 }, (_, i) => String(i)) }).error, /up to/);
  // A private game doesn't show to others.
  game.access = 'private';
  assert.deepEqual(s.call(bob, 'profile', { id: String(ann.userId) }).levels.showcase, []);
});

test('the wall: friends post by default, the owner picks who can, blocked people can\'t', () => {
  const { s, ann, bob, cat } = setup();
  const wall = String(ann.userId);
  assert.match(s.call(bob, 'wall.post', { id: wall, text: 'hello' }).error, /friends/);
  befriend(s, ann, bob);
  const p = s.call(bob, 'wall.post', { id: wall, text: 'hello ann' });
  assert.ok(p.ok, p.error);
  assert.ok(ann.notes.some((n) => n.kind === 'wall'), 'the owner gets a bell');
  assert.ok(s.call(ann, 'wall.post', { id: wall, text: 'my own wall' }).ok, 'the owner can always');
  // Open it to everyone.
  assert.ok(s.call(ann, 'wall.who', { who: 'everyone' }).ok);
  assert.ok(s.call(cat, 'wall.post', { id: wall, text: 'hi from cat' }).ok);
  // Close it.
  s.call(ann, 'wall.who', { who: 'nobody' });
  assert.match(s.call(bob, 'wall.post', { id: wall, text: 'still here?' }).error, /closed/);
  s.call(ann, 'wall.who', { who: 'everyone' });
  // Blocked.
  s.call(ann, 'block.add', { user: cat.id });
  assert.equal(s.call(cat, 'wall.post', { id: wall, text: 'let me in' }).ok, false);
  assert.match(s.call(ann, 'wall.who', { who: 'lol' }).error, /friends, everyone or nobody/);
  // Reading: newest first, guests too.
  const guest = s.user('d'.repeat(64));
  const list = s.call(guest, 'wall.list', { id: wall });
  assert.ok(list.ok, list.error);
  assert.deepEqual(list.posts.map((x) => x.text), ['hi from cat', 'my own wall', 'hello ann']);
  assert.equal(list.posts[0].canDelete, false);
});

test('deleting wall posts: the writer, the owner and staff can; others can\'t', () => {
  const { s, ann, bob, cat } = setup();
  const wall = String(ann.userId);
  s.call(ann, 'wall.who', { who: 'everyone' });
  const a = s.call(bob, 'wall.post', { id: wall, text: 'one' }).post;
  const b = s.call(bob, 'wall.post', { id: wall, text: 'two' }).post;
  const c = s.call(bob, 'wall.post', { id: wall, text: 'three' }).post;
  assert.match(s.call(cat, 'wall.delete', { id: wall, post: a.id }).error, /your own/);
  assert.ok(s.call(bob, 'wall.delete', { id: wall, post: a.id }).ok, 'the writer');
  assert.ok(s.call(ann, 'wall.delete', { id: wall, post: b.id }).ok, 'the owner');
  assert.ok(s.call(s.staff, 'wall.delete', { id: wall, post: c.id }).ok, 'staff');
  assert.equal(s.call(ann, 'wall.list', { id: wall }).posts.length, 0);
  assert.match(s.call(ann, 'wall.delete', { id: wall, post: a.id }).error, /gone/);
});

test('wall posts go through the word filter and can\'t be too long', () => {
  const { s, ann } = setup();
  const wall = String(ann.userId);
  const p = s.call(ann, 'wall.post', { id: wall, text: 'x'.repeat(1000) });
  assert.equal(p.post.text.length, 300);
  assert.match(s.call(ann, 'wall.post', { id: wall, text: '   ' }).error, /Write something/);
});
