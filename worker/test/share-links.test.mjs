// Share links: a creator makes links to their game, item, profile or group, and sees clicks,
// different people, plays, buys (and what they spent), favourites, follows and joins (worker/sharelinks.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { kMostLinks } from '../sharelinks.js';

function setup() {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob'), cat = s.signUp('cat');
  const obby = s.addGame(ann, 'Obby');
  s.writeFile(obby.id, new TextEncoder().encode('{"nodes":[]}'));
  const cap = s.addGame(ann, 'Cool Cap', { kind: 'hat', price: 10 });
  const guest = s.user('c'.repeat(64));   // someone without an account
  return { s, ann, bob, cat, obby, cap, guest };
}

test('only the owner makes links, up to the limit, and they can be removed', () => {
  const { s, ann, bob, obby } = setup();
  assert.match(s.call(bob, 'share.create', { kind: 'game', id: obby.id }).error, /Only its owner/);
  assert.match(s.call(ann, 'share.create', { kind: 'video', id: obby.id }).error, /games, items, profiles and groups/);
  const r = s.call(ann, 'share.create', { kind: 'game', id: obby.id, label: 'YouTube <b>video</b>' });
  assert.ok(r.ok, r.error);
  assert.match(r.link.path, /^\/s\/[A-Za-z0-9]{8}$/);
  assert.equal(r.link.label, 'YouTube bvideo/b', 'no HTML in labels');
  for (let i = 1; i < kMostLinks; i++) assert.ok(s.call(ann, 'share.create', { kind: 'game', id: obby.id }).ok);
  assert.match(s.call(ann, 'share.create', { kind: 'game', id: obby.id }).error, /Remove an old one/);
  assert.equal(s.call(ann, 'share.list', { kind: 'game', id: obby.id }).links.length, kMostLinks);
  assert.match(s.call(bob, 'share.list', { kind: 'game', id: obby.id }).error, /Only its owner/);
  assert.match(s.call(bob, 'share.remove', { code: r.link.code }).error, /Only its owner/);
  assert.ok(s.call(ann, 'share.remove', { code: r.link.code }).ok);
  assert.match(s.call(bob, 'share.open', { code: r.link.code }).error, /doesn't go anywhere/);
  assert.ok(s.assets.get(obby.id), 'removing a link leaves the game alone');
  assert.match(s.call(ann, 'share.create', { kind: 'profile', id: bob.userId }).error, /your own profile/);
  assert.ok(s.call(ann, 'share.create', { kind: 'profile' }).ok, 'your own profile');
});

test('clicks, different people and plays are counted, once each', () => {
  const { s, ann, bob, cat, obby, guest } = setup();
  const code = s.call(ann, 'share.create', { kind: 'game', id: obby.id, label: 'Discord' }).link.code;
  const open = s.call(guest, 'share.open', { code, visitor: 'abc123' });
  assert.ok(open.ok, open.error);
  assert.equal(open.path, '#/game/' + obby.id);
  s.call(guest, 'share.open', { code, visitor: 'abc123' });   // the same browser again
  s.call(bob, 'share.open', { code, visitor: 'zzz' });
  assert.deepEqual(s.call(bob, 'share.done', { code }).counted, [], 'nothing done yet');
  assert.ok(s.call(bob, 'get', { id: obby.id }).ok, 'bob plays');
  assert.deepEqual(s.call(bob, 'share.done', { code }).counted, ['play']);
  assert.deepEqual(s.call(bob, 'share.done', { code }).counted, [], 'counted once');
  // cat had played before opening the link: that isn't the link's doing
  s.call(cat, 'get', { id: obby.id });
  s.call(cat, 'share.open', { code, visitor: 'q' });
  assert.deepEqual(s.call(cat, 'share.done', { code }).counted, []);
  s.call(ann, 'share.open', { code, visitor: 'me' });
  assert.deepEqual(s.call(ann, 'share.done', { code }).counted, [], 'the owner\'s own clicks don\'t make plays');
  const l = s.call(ann, 'share.list', { kind: 'game', id: obby.id }).links[0];
  assert.equal(l.total.clicks, 5);
  assert.equal(l.total.people, 4);
  assert.equal(l.total.play, 1);
  assert.equal(l.conversion, 25);
  assert.equal(l.days.length, 1);
});

test('buys add what was spent; favourites count too', () => {
  const { s, ann, bob, cap } = setup();
  const code = s.call(ann, 'share.create', { kind: 'item', id: cap.id }).link.code;
  s.call(bob, 'share.open', { code, visitor: 'b' });
  assert.ok(s.call(bob, 'buy', { id: cap.id }).ok, 'bob buys the cap');
  assert.ok(s.call(bob, 'item.favorite', { id: cap.id }).ok);
  assert.deepEqual(s.call(bob, 'share.done', { code }).counted.sort(), ['buy', 'favorite']);
  const l = s.call(ann, 'share.list', { kind: 'item', id: cap.id }).links[0];
  assert.equal(l.total.buy, 1);
  assert.equal(l.total.spent, 10);
  assert.equal(l.conversion, 100);
});

test('profile links count follows', () => {
  const { s, ann, bob } = setup();
  const code = s.call(ann, 'share.create', { kind: 'profile' }).link.code;
  const open = s.call(bob, 'share.open', { code, visitor: 'x' });
  assert.equal(open.path, '#/user/' + ann.userId);
  assert.ok(s.call(bob, 'follow.add', { user: ann.userId }).ok);
  assert.deepEqual(s.call(bob, 'share.done', { code }).counted, ['follow']);
});
