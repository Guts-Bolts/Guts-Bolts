// Ban lists: a creator bans someone once and it works in every game that uses the list
// (worker/banlists.js). Shared with chosen editors only, a reason on every ban, appeals, a log.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

function setup() {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob'), cat = s.signUp('cat'), dan = s.signUp('dan');
  const obby = s.addGame(ann, 'Obby'), tycoon = s.addGame(ann, 'Tycoon');
  return { s, ann, bob, cat, dan, obby, tycoon };
}

test('one list, two games: a ban keeps them out of both, with the reason', () => {
  const { s, ann, bob, obby, tycoon } = setup();
  const made = s.call(ann, 'banlist.create', { name: 'Cheaters' });
  assert.ok(made.ok, made.error);
  const id = made.list.id;
  assert.ok(s.call(ann, 'game.banLists', { game: obby.id, lists: [id] }).ok);
  assert.ok(s.call(ann, 'game.banLists', { game: tycoon.id, lists: [id] }).ok);
  assert.ok(s.call(bob, 'servers.play', { game: obby.id }).ok, 'not banned yet');

  assert.match(s.call(ann, 'banlist.add', { id, who: 'bob', days: 0 }).error, /needs a reason/);
  const r = s.call(ann, 'banlist.add', { id, who: 'bob', reason: 'Speed hacking', days: 7 });
  assert.ok(r.ok, r.error);
  assert.equal(r.list.entries[0].who.id, bob.id);
  assert.equal(r.list.entries[0].by.id, ann.id, 'who banned them is kept');
  for (const g of [obby, tycoon]) {
    const play = s.call(bob, 'servers.play', { game: g.id });
    assert.equal(play.ok, false);
    assert.match(play.error, /banned from .*Reason: Speed hacking/);
    assert.equal(s.call(bob, 'servers.private', { game: g.id }).ok, false);
  }
  assert.ok(s.call(bob, 'servers.list', { game: obby.id }).ok, 'they can still see the game');
  const check = s.call(bob, 'banlist.check', { game: obby.id });
  assert.equal(check.banned, true);
  assert.equal(check.ban.reason, 'Speed hacking');
  assert.ok(check.ban.until > check.ban.at);
  assert.equal(check.ban.canAppeal, true);
});

test('game.ban makes the game its own list the first time', () => {
  const { s, ann, bob, cat, obby } = setup();
  const r = s.call(ann, 'game.ban', { game: obby.id, who: String(bob.userId), reason: 'Spawn killing' });
  assert.ok(r.ok, r.error);
  assert.equal(r.list.name, 'Obby bans');
  assert.deepEqual(obby.banLists, [r.list.id]);
  assert.equal(s.call(bob, 'servers.play', { game: obby.id }).ok, false);
  assert.ok(s.call(ann, 'game.ban', { game: obby.id, who: 'cat', reason: 'Spam' }).ok);
  assert.equal(obby.banLists.length, 1, 'the second ban goes on the same list');
  assert.match(s.call(bob, 'game.ban', { game: obby.id, who: 'cat', reason: 'x' }).error, /Only the game's creator/);
});

test('nobody can ban the game\'s creator, staff, the list\'s team or themselves', () => {
  const { s, ann, bob, obby } = setup();
  const id = s.call(ann, 'banlist.create', { name: 'Team list' }).list.id;
  assert.ok(s.call(ann, 'banlist.share', { id, who: 'bob' }).ok);
  assert.match(s.call(ann, 'banlist.add', { id, who: 'ann', reason: 'x' }).error, /yourself/);
  assert.match(s.call(ann, 'banlist.add', { id, who: 'bob', reason: 'x' }).error, /looks after this list/);
  assert.match(s.call(ann, 'banlist.add', { id, who: 'Guts', reason: 'x' }).error, /staff/);
  // Bob (an editor) uses the list on his own game, and ann can't be kept out of hers by it.
  const bobGame = s.addGame(bob, 'Bob World');
  assert.ok(s.call(bob, 'game.banLists', { game: bobGame.id, lists: [id] }).ok);
  assert.ok(s.call(ann, 'game.banLists', { game: obby.id, lists: [id] }).ok);
  assert.ok(s.call(ann, 'servers.play', { game: obby.id }).ok);
});

test('only lists you own or edit can go on your game; editors can\'t rename, delete or share', () => {
  const { s, ann, bob, cat, obby } = setup();
  const catList = s.call(cat, 'banlist.create', { name: 'Cat\'s list' }).list.id;
  assert.match(s.call(ann, 'game.banLists', { game: obby.id, lists: [catList] }).error, /own or were added to/);
  assert.match(s.call(bob, 'game.banLists', { game: obby.id, lists: [] }).error, /Only the game's creator/);
  assert.equal(s.call(ann, 'banlist.get', { id: catList }).ok, false, 'other people\'s lists are private');
  assert.ok(s.call(cat, 'banlist.share', { id: catList, who: 'ann' }).ok);
  assert.ok(s.call(ann, 'game.banLists', { game: obby.id, lists: [catList] }).ok, 'shared with her now');
  assert.match(s.call(ann, 'banlist.rename', { id: catList, name: 'Mine' }).error, /Only the list's owner/);
  assert.match(s.call(ann, 'banlist.share', { id: catList, who: 'bob' }).error, /Only the list's owner/);
  assert.ok(s.call(ann, 'banlist.add', { id: catList, who: 'bob', reason: 'Exploiting' }).ok, 'editors can ban');
  assert.equal(s.call(bob, 'servers.play', { game: obby.id }).ok, false);
  const mine = s.call(ann, 'banlist.mine').lists;
  assert.equal(mine.length, 1);
  assert.equal(mine[0].role, 'editor');
  assert.deepEqual(mine[0].games.map((g) => g.id), [obby.id]);
});

test('appeal once per ban; accepting lets them back in, and the log says who did what', () => {
  const { s, ann, bob, obby } = setup();
  const id = s.call(ann, 'game.ban', { game: obby.id, who: 'bob', reason: 'Griefing' }).list.id;
  assert.match(s.call(bob, 'banlist.appeal', { game: obby.id, text: 'pls' }).error, /Write a little more/);
  assert.ok(s.call(bob, 'banlist.appeal', { game: obby.id, text: 'Sorry, it was an accident with the tool.' }).ok);
  assert.match(s.call(bob, 'banlist.appeal', { game: obby.id, text: 'Sorry again, please unban me.' }).error, /already appealed/);
  assert.ok(ann.notes.some((n) => n.kind === 'banlist' && n.about === id), 'the list\'s team hears about it');
  const list = s.call(ann, 'banlist.get', { id }).list;
  assert.equal(list.openAppeals, 1);
  const r = s.call(ann, 'banlist.answer', { id, appeal: list.appeals[0].id, accept: true, note: 'OK, be nice' });
  assert.ok(r.ok, r.error);
  assert.equal(r.list.entries.length, 0);
  assert.ok(s.call(bob, 'servers.play', { game: obby.id }).ok);
  assert.ok(bob.notes.some((n) => n.kind === 'banappeal' && /accepted/.test(n.text)));
  assert.deepEqual(r.list.log.map((x) => x.did).slice(0, 3), ['accept', 'appeal', 'add']);
  assert.equal(r.list.log[0].by.id, ann.id);
});

test('bans run out, and a ban that ran out stops counting', () => {
  const { s, ann, bob, obby } = setup();
  const id = s.call(ann, 'game.ban', { game: obby.id, who: 'bob', reason: 'Spam', days: 1 }).list.id;
  assert.equal(s.call(bob, 'servers.play', { game: obby.id }).ok, false);
  s.banLists.get(id).entries[0].until = Math.floor(Date.now() / 1000) - 1;
  assert.ok(s.call(bob, 'servers.play', { game: obby.id }).ok);
  assert.match(s.call(ann, 'banlist.add', { id, who: 'bob', reason: 'x', days: 99999 }).error, /days/);
});

test('deleting a list takes it off its games; lists are kept after a restart', () => {
  const { s, ann, bob, obby } = setup();
  const id = s.call(ann, 'game.ban', { game: obby.id, who: 'bob', reason: 'Spam' }).list.id;
  s.banLists = null;   // as if the server started again: read back from the database
  assert.equal(s.call(bob, 'servers.play', { game: obby.id }).ok, false);
  assert.ok(s.call(ann, 'banlist.delete', { id }).ok);
  assert.deepEqual(obby.banLists, []);
  assert.ok(s.call(bob, 'servers.play', { game: obby.id }).ok);
});

test('a banned player can\'t join someone\'s server of the game, or host one', () => {
  const { s, ann, bob, cat, obby } = setup();
  s.checkRequest = (req) => ({ me: s.users.get(req.account), args: req.args || {}, opName: req.op });
  const conn = () => {
    const sent = [];
    const c = { id: Math.random().toString(16).slice(2, 10), mode: 'request', ws: { send: (m) => sent.push(JSON.parse(m)), close() {} }, sent };
    s.conns.set(c.id, c);
    return c;
  };
  const host = conn();
  s.onMessage(host, JSON.stringify({ op: 'relay.host', account: cat.id, args: { game: obby.id } }));
  assert.equal(host.mode, 'host');
  s.call(ann, 'game.ban', { game: obby.id, who: 'bob', reason: 'Spam' });
  const j = conn();
  s.onMessage(j, JSON.stringify({ op: 'relay.join', account: bob.id, args: { session: host.session } }));
  assert.notEqual(j.mode, 'pending');
  assert.match(j.sent.at(-1).error, /banned/);
  const h = conn();
  s.onMessage(h, JSON.stringify({ op: 'relay.host', account: bob.id, args: { game: obby.id } }));
  assert.notEqual(h.mode, 'host');
});
