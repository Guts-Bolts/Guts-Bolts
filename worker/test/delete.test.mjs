// Deleting your account: personal things go, games and items others got stay, the name stays taken.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, device } from './helpers.mjs';

const auth = 'ab'.repeat(32), wrong = 'cd'.repeat(32);

// bob signs up with a password on his own device, makes things and makes friends.
function busyBob(s) {
  const d = device(s, 9);
  const up = d.call('account.signup', { username: 'bob', salt: '1'.repeat(32), auth, key: '2'.repeat(208) });
  assert.ok(up.ok, up.error);
  const bob = s.users.get(d.account), carol = s.signUp('carol');
  assert.ok(d.call('profile.set', { blurb: 'I like trains', status: 'Building a railway' }).ok);
  assert.ok(d.call('avatar.set', { avatar: { head: [1, 2, 3], torso: [1, 2, 3], leftArm: [1, 2, 3], rightArm: [1, 2, 3], leftLeg: [1, 2, 3], rightLeg: [1, 2, 3] } }).ok);
  assert.ok(d.call('outfit.save', { name: 'Trains' }).ok);
  bob.email = 'bob@example.com'; bob.emailVerified = true; bob.twoStep = true; bob.totpSecret = 'ABCDEFGH';
  assert.ok(d.call('friends.add', { user: String(carol.userId) }).ok);
  assert.ok(s.call(carol, 'friends.accept', { user: String(bob.userId) }).ok);
  assert.ok(s.call(carol, 'follow.add', { user: String(bob.userId) }).ok);
  assert.ok(d.call('follow.add', { user: String(carol.userId) }).ok);
  assert.ok(s.call(carol, 'message.send', { to: String(bob.userId), subject: 'Hi', body: 'Hello bob' }).ok);
  assert.ok(d.call('message.send', { to: String(carol.userId), subject: 'Hey', body: 'Hello carol' }).ok);
  const game = s.addGame(bob, 'Bob\'s Railway');
  const fav = s.addGame(carol, 'Carol\'s Castle');
  assert.ok(d.call('game.favorite', { id: fav.id }).ok);
  const shirt = s.addGame(bob, 'Bob\'s Shirt', { kind: 'shirt', price: 5 });
  carol.owned.push(shirt.id);
  const hat = s.addGame(s.staff, 'Golden Hat', { kind: 'hat', limited: true, stock: 2, sales: 2,
    copies: [{ serial: 1, owner: bob.id, price: 500 }, { serial: 2, owner: carol.id, price: 0 }] });
  bob.owned.push(hat.id); carol.owned.push(hat.id);
  s.flush();
  return { d, bob, carol, game, fav, shirt, hat };
}

test('a wrong password deletes nothing, and the staff account can\'t delete itself', () => {
  const s = makeServer();
  const { d, bob } = busyBob(s);
  const r = d.call('account.delete', { auth: wrong });
  assert.equal(r.ok, false);
  assert.equal(r.error, 'Wrong password.');
  assert.equal(bob.username, 'bob');
  assert.ok(d.call('hello', {}).ok);
  const staff = s.call(s.staff, 'account.delete', { auth });
  assert.equal(staff.ok, false);
  assert.equal(staff.error, 'The staff account can\'t be deleted.');
  assert.equal(s.staff.userId, 1);
});

test('deleting removes what\'s personal and logs the device out', () => {
  const s = makeServer();
  const { d, bob, carol, fav } = busyBob(s);
  const number = bob.userId;
  const r = d.call('account.delete', { auth });
  assert.ok(r.ok, r.error);
  for (const k of ['email', 'twoStep', 'totpSecret', 'avatar', 'blurb', 'posts', 'outfits', 'inbox', 'sent', 'favorites', 'notes', 'appeal', 'warnings'])
    assert.equal(bob[k], undefined, k + ' is gone');
  assert.equal(bob.keyBlob, '');
  assert.equal(bob.pwHash, '');
  assert.equal(bob.username, '');
  assert.equal(bob.name, 'Deleted user');
  assert.deepEqual([bob.friends, bob.following, bob.followers], [[], [], []]);
  for (const k of ['friends', 'friendIn', 'friendOut', 'following', 'followers']) assert.ok(!carol[k].includes(bob.id), 'carol\'s ' + k);
  assert.equal(fav.favorites, 0, 'the favourite is taken back');
  assert.equal(d.call('hello', {}).error, 'This account was deleted.');
  assert.equal(s.call(carol, 'profile', { id: String(number) }).ok, false);
  assert.equal(s.call(carol, 'account.salt', { username: 'bob' }).ok, false, 'nobody can log in to it');
  // The staff log has it as a system event, without the name.
  const log = s.call(s.staff, 'admin.log').log;
  const entry = log.find((x) => x.action === 'account');
  assert.ok(entry);
  assert.equal(entry.by.name, 'System');
  assert.match(entry.text, new RegExp('#' + number));
  assert.ok(!/bob/i.test(entry.text));
});

test('games and items stay (by "Deleted user"), and the username and number are never reused', () => {
  const s = makeServer();
  const { d, bob, carol, game, shirt } = busyBob(s);
  const number = bob.userId;
  assert.ok(d.call('account.delete', { auth }).ok);
  const info = s.call(carol, 'asset.info', { id: game.id });
  assert.ok(info.ok, info.error);
  assert.equal(info.asset.creatorName, 'Deleted user');
  assert.ok(s.assets.get(shirt.id) && carol.owned.includes(shirt.id), 'what carol bought is still hers');
  assert.equal(s.call(carol, 'message.list').messages.length, 1, 'carol keeps the message bob sent her');
  const fresh = device(s, 10);
  assert.equal(fresh.call('account.check', { username: 'bob' }).available, false);
  const up = fresh.call('account.signup', { username: 'Bob', salt: '1'.repeat(32), auth, key: '2'.repeat(208) });
  assert.equal(up.ok, false);
  assert.match(up.error, /taken/);
  const dan = fresh.call('account.signup', { username: 'dan', salt: '1'.repeat(32), auth, key: '2'.repeat(208) });
  assert.ok(dan.ok, dan.error);
  assert.ok(dan.me.userId > number, 'a new number, not the deleted one');
});

test('Limited copies stay with the deleted account, off resale and out of trades', () => {
  const s = makeServer();
  const { d, bob, carol, hat } = busyBob(s);
  const offer = s.call(carol, 'trade.send', { to: String(bob.userId), give: [{ id: hat.id, serial: 2 }], get: [{ id: hat.id, serial: 1 }] });
  assert.ok(offer.ok, offer.error);
  assert.ok(d.call('account.delete', { auth }).ok);
  const copy = hat.copies.find((c) => c.serial === 1);
  assert.equal(copy.owner, bob.id);
  assert.equal(copy.price, 0, 'not for sale any more');
  assert.ok(bob.owned.includes(hat.id));
  assert.notEqual(s.trades.find((x) => x.id === offer.trade.id).status, 'open', 'open trades are cancelled');
  assert.equal(s.call(carol, 'trade.send', { to: String(bob.userId), give: [{ id: hat.id, serial: 2 }], get: [{ id: hat.id, serial: 1 }] }).ok, false);
});
