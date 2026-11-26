// Group pages (group-pages.js): icons and banners from decals, announcements, members by rank,
// and the People page's cards and tabs.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { kNewsKept } from '../group-pages.js';

const png = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0]).toString('base64');
function setup() {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob'), cat = s.signUp('cat');
  s.add(ann, 500, 'test', 'test:ann');
  const g = s.call(ann, 'groups.create', { name: 'Builders', description: 'We build' }).group;
  assert.ok(s.call(bob, 'groups.join', { id: g.id }).ok);
  assert.ok(s.call(cat, 'groups.join', { id: g.id }).ok);
  return { s, ann, bob, cat, g };
}

test('the icon and banner are decals; a decal waiting for its staff check shows once it passes', () => {
  const { s, ann, bob, g } = setup();
  const staffDecal = s.call(s.staff, 'upload', { kind: 'decal', name: 'Logo', data: png, access: 'public' }).asset;
  let r = s.call(ann, 'groups.pictures', { id: g.id, icon: String(staffDecal.num), banner: staffDecal.id });
  assert.ok(r.ok, r.error);
  assert.equal(r.group.icon.id, staffDecal.id);
  assert.equal(r.group.banner.id, staffDecal.id);
  assert.equal(s.call(bob, 'groups.list', {}).groups[0].icon.id, staffDecal.id, 'the group list shows the icon');
  assert.match(s.call(bob, 'groups.pictures', { id: g.id, icon: '' }).error, /rank/, 'members can\'t change it');
  assert.match(s.call(ann, 'groups.pictures', { id: g.id, icon: 'nope' }).error, /decal/);

  // Bob isn't Verified, so his new decal waits for staff before it shows.
  const waiting = s.call(bob, 'upload', { kind: 'decal', name: 'Mine', data: png }).asset;
  assert.ok(waiting.review, 'it needs a staff check');
  r = s.call(ann, 'groups.pictures', { id: g.id, icon: waiting.id });
  assert.ok(r.ok && r.waiting, 'saved, but waiting');
  assert.equal(r.group.icon, null, 'not shown yet');
  r = s.call(ann, 'groups.pictures', { id: g.id, icon: '', banner: '' });
  assert.equal(r.group.icon, null);
  assert.equal(r.group.banner, null);
});

test('announcements: ranks that can shout post them, everyone hears, the newest stay', () => {
  const { s, ann, bob, g } = setup();
  assert.match(s.call(bob, 'groups.announce', { id: g.id, title: 'Hi', text: 'x' }).error, /rank/);
  assert.match(s.call(ann, 'groups.announce', { id: g.id, title: '', text: 'x' }).error, /title/);
  let r = s.call(ann, 'groups.announce', { id: g.id, title: 'Game night', text: 'Friday at 8!\nBring friends.' });
  assert.ok(r.ok, r.error);
  assert.equal(r.news[0].title, 'Game night');
  assert.ok(bob.notes.some((n) => n.text.includes('Game night')), 'members get told');
  const got = s.call(bob, 'groups.get', { id: g.id });
  assert.equal(got.news.length, 1);
  assert.equal(got.news[0].text, 'Friday at 8!\nBring friends.');
  assert.match(s.call(bob, 'groups.announceDelete', { id: g.id, post: got.news[0].id }).error, /admin/);
  for (let i = 0; i < kNewsKept + 3; i++) s.call(ann, 'groups.announce', { id: g.id, title: 'News ' + i, text: 'x' });
  r = s.call(bob, 'groups.get', { id: g.id });
  assert.equal(r.news.length, kNewsKept);
  assert.equal(r.news[0].title, 'News ' + (kNewsKept + 2), 'newest first');
  r = s.call(ann, 'groups.announceDelete', { id: g.id, post: r.news[0].id });
  assert.ok(r.ok);
  assert.equal(r.news.length, kNewsKept - 1);
});

test('members by rank, with avatars, and how many have each rank', () => {
  const { s, ann, bob, cat, g } = setup();
  assert.ok(s.call(ann, 'groups.member', { id: g.id, user: bob.id, action: 'rank', rank: 'Admin' }).ok);
  const got = s.call(cat, 'groups.get', { id: g.id });
  assert.deepEqual(got.rankCounts, { Owner: 1, Admin: 1, Member: 1 });
  let r = s.call(cat, 'groups.members', { id: g.id, rank: 'Admin' });
  assert.equal(r.total, 1);
  assert.equal(r.people[0].username, 'bob');
  assert.equal(r.people[0].role, 'Admin');
  assert.ok('avatar' in r.people[0] && 'level' in r.people[0] && 'online' in r.people[0]);
  r = s.call(cat, 'groups.members', { id: g.id, rank: '' });
  assert.deepEqual(r.people.map((p) => p.username), ['ann', 'bob', 'cat'], 'everyone, most in charge first');
  r = s.call(cat, 'groups.members', { id: g.id, rank: '', offset: 1, limit: 1 });
  assert.deepEqual(r.people.map((p) => p.username), ['bob']);
  assert.match(s.call(cat, 'groups.members', { id: g.id, rank: 'r-gone' }).error, /gone/);
});

test('People: cards with levels and group counts, and the Verified / Club tabs', () => {
  const { s, ann, bob } = setup();
  const r = s.call(bob, 'users.search', { query: 'ann' });
  const card = r.users.find((u) => u.username === 'ann');
  assert.equal(card.groups, 1);
  assert.equal(card.level, 1);
  assert.equal(typeof card.online, 'boolean');
  const verified = s.call(bob, 'users.search', { show: 'verified' }).users;
  assert.ok(verified.every((u) => u.verified));
  assert.ok(!verified.some((u) => u.username === 'bob'));
  assert.equal(s.call(bob, 'users.search', { show: 'club' }).users.length, 0);
  void ann;
});
