// Achievements on profiles, game update logs (in My Feed too) and timed events with a free
// item for playing. (worker/extras.js; src/server/ServerExtras.cpp has the same rules.)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const hatFile = Buffer.from(JSON.stringify({ format: 'gbaccessory', version: 1, kind: 'hat',
  node: { id: 1, name: 'Hat', kind: 'Model', children: [] } })).toString('base64');

test('profiles show achievements, earned ones first', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  let r = s.call(bob, 'profile', { id: String(bob.userId) });
  assert.ok(r.ok, r.error);
  assert.ok(r.achievements.length >= 10);
  assert.ok(r.achievements.every((x) => !x.earned), 'a brand new account has none');
  // Publish a game and play it: two achievements.
  const g = s.addGame(bob, 'Bob World');
  bob.played = [g.id];
  s.saveUser(bob);
  r = s.call(bob, 'profile', { id: String(bob.userId) });
  const earned = r.achievements.filter((x) => x.earned).map((x) => x.id);
  assert.deepEqual(earned.sort(), ['first-play', 'game-maker']);
  assert.equal(r.achievements[0].earned, true, 'earned ones come first');
  const explorer = r.achievements.find((x) => x.id === 'explorer');
  assert.equal(explorer.have, 1);
  assert.equal(explorer.need, 10);
});

test('a game\'s creator posts update notes; people who favourited it see them in My Feed', () => {
  const s = makeServer();
  const bob = s.signUp('bob'), amy = s.signUp('amy');
  const g = s.addGame(bob, 'Bob World');
  let r = s.call(amy, 'gamelog.post', { id: g.id, title: 'Hacked', text: 'nope' });
  assert.equal(r.ok, false, 'only the creator can post');
  r = s.call(bob, 'gamelog.post', { id: g.id, title: '', text: 'no title' });
  assert.equal(r.ok, false);
  r = s.call(bob, 'gamelog.post', { id: g.id, title: 'Winter map', text: 'Snow everywhere and a new sled.' });
  assert.ok(r.ok, r.error);
  assert.equal(r.entries.length, 1);
  assert.equal(r.entries[0].title, 'Winter map');
  // Anyone can read them.
  r = s.call(amy, 'gamelog.list', { id: g.id });
  assert.equal(r.entries[0].text, 'Snow everywhere and a new sled.');
  // Not in Amy's feed until she favourites the game.
  assert.ok(!s.call(amy, 'feed.list').feed.some((x) => x.kind === 'gamelog'));
  s.call(amy, 'game.favorite', { id: g.id, on: true });
  const feed = s.call(amy, 'feed.list').feed;
  const note = feed.find((x) => x.kind === 'gamelog');
  assert.ok(note, 'the note is in her feed');
  assert.equal(note.game.name, 'Bob World');
  // Five a day per game, then wait.
  for (let i = 0; i < 4; i++) assert.ok(s.call(bob, 'gamelog.post', { id: g.id, title: 'Fix ' + i }).ok);
  assert.equal(s.call(bob, 'gamelog.post', { id: g.id, title: 'One too many' }).ok, false);
  // Deleting one.
  const id = s.call(bob, 'gamelog.list', { id: g.id }).entries[0].id;
  r = s.call(bob, 'gamelog.delete', { id: g.id, entry: id });
  assert.ok(r.ok, r.error);
  assert.equal(r.entries.length, 4);
});

test('staff make an event; playing during it earns its free item', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const up = s.call(s.staff, 'upload', { kind: 'hat', name: 'Snow Hat', data: hatFile, price: 500 });
  assert.ok(up.ok, up.error);
  const t = Math.floor(Date.now() / 1000);
  assert.equal(s.call(bob, 'admin.eventSave', { name: 'Winter', start: t - 60, end: t + 3600, minutes: 10 }).ok, false, 'staff only');
  let r = s.call(s.staff, 'admin.eventSave', { name: 'Winter Fest', text: 'Play for a hat!', start: t - 60, end: t + 86400, item: up.asset.id, minutes: 10 });
  assert.ok(r.ok, r.error);
  const ev = r.events[0];
  assert.equal(ev.state, 'live');
  assert.equal(ev.item.name, 'Snow Hat');
  // Not enough playing yet.
  assert.equal(s.call(bob, 'events.claim', { id: ev.id }).ok, false);
  // Two "still playing" reports, 5 minutes each (the 280 second gap is checked: pretend it passed).
  for (let i = 0; i < 2; i++) {
    bob.lastPlay = 0;
    s.call(bob, 'bolts.play');
  }
  r = s.call(bob, 'events.list');
  assert.equal(r.events[0].played, 10);
  r = s.call(bob, 'events.claim', { id: ev.id });
  assert.ok(r.ok, r.error);
  assert.equal(r.got.name, 'Snow Hat');
  assert.ok(bob.owned.includes(up.asset.id), 'the hat is his, for free');
  assert.equal(r.events[0].claimed, true);
  assert.equal(s.call(bob, 'events.claim', { id: ev.id }).ok, false, 'only once');
  // And it shows as an achievement.
  const prof = s.call(bob, 'profile', { id: String(bob.userId) });
  assert.ok(prof.achievements.find((x) => x.id === 'party-goer').earned);
  // Staff can remove it.
  r = s.call(s.staff, 'admin.eventRemove', { id: ev.id });
  assert.ok(r.ok, r.error);
  assert.equal(r.events.length, 0);
});

test('events check their times', () => {
  const s = makeServer();
  const t = Math.floor(Date.now() / 1000);
  assert.equal(s.call(s.staff, 'admin.eventSave', { name: 'Backwards', start: t + 100, end: t }).ok, false);
  assert.equal(s.call(s.staff, 'admin.eventSave', { name: 'Forever', start: t, end: t + 400 * 86400 }).ok, false);
  const r = s.call(s.staff, 'admin.eventSave', { name: 'Soon', start: t + 3600, end: t + 7200, minutes: 0 });
  assert.ok(r.ok, r.error);
  assert.equal(r.events[0].state, 'soon');
  const bob = s.signUp('bob');
  assert.equal(s.call(bob, 'events.claim', { id: r.events[0].id }).ok, false, 'not started yet');
});
