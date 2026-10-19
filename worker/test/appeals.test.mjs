// Ban appeals: a banned account can ask staff, once per ban, to be unbanned; staff accept or deny.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, device } from './helpers.mjs';

const plea = 'I am sorry, it was my little brother. It will not happen again!';

// bob, banned by staff, on a device that sends real signed requests.
function bannedBob(s) {
  const d = device(s, 7), bob = s.signUp('bob', d.account);
  const ban = s.call(s.staff, 'admin.ban', { to: String(bob.userId), reason: 'spam', note: 'Stop posting links', days: 3 });
  assert.ok(ban.ok, ban.error);
  return { d, bob };
}

test('a banned account can appeal (and do nothing else), once per ban', () => {
  const s = makeServer();
  const { d, bob } = bannedBob(s);
  assert.match(d.call('friends.list').error, /^This account has been banned/);
  const hello = d.call('hello', { name: 'bob' });
  assert.ok(hello.ok, hello.error);
  assert.equal(hello.me.ban.canAppeal, true);
  assert.equal(hello.me.ban.appeal, null);
  assert.equal(d.call('appeal.send', { text: 'pls' }).ok, false, 'too short');
  const r = d.call('appeal.send', { text: plea });
  assert.ok(r.ok, r.error);
  assert.equal(r.me.ban.appeal.status, 'open');
  assert.equal(r.me.ban.appeal.text, plea);
  assert.equal(r.me.ban.canAppeal, false);
  const again = d.call('appeal.send', { text: plea + ' Please!' });
  assert.equal(again.ok, false);
  assert.match(again.error, /already appealed/);
  assert.equal(bob.appeal.text, plea, 'the first one stays');
});

test('appeals are capped at 500 letters, and someone who isn\'t banned can\'t appeal', () => {
  const s = makeServer();
  const { d, bob } = bannedBob(s);
  assert.ok(d.call('appeal.send', { text: 'x'.repeat(2000) }).ok);
  assert.equal(bob.appeal.text.length, 500);
  const carol = s.signUp('carol');
  assert.equal(s.call(carol, 'appeal.send', { text: plea }).error, 'Your account isn\'t banned.');
});

test('staff see appeals with the ban\'s reason, note and who banned; players can\'t', () => {
  const s = makeServer();
  const { d, bob } = bannedBob(s);
  d.call('appeal.send', { text: plea });
  assert.equal(s.call(s.signUp('carol'), 'admin.appeals').ok, false);
  const r = s.call(s.staff, 'admin.appeals');
  assert.ok(r.ok, r.error);
  assert.equal(r.appeals.length, 1);
  const a = r.appeals[0];
  assert.equal(a.user.id, bob.id);
  assert.equal(a.text, plea);
  assert.equal(a.ban.reason, 'spam');
  assert.equal(a.ban.title, 'Spam');
  assert.equal(a.ban.note, 'Stop posting links');
  assert.ok(a.ban.at > 0 && a.ban.until > a.ban.at);
  assert.equal(a.ban.by.id, s.staff.id, 'who banned them, from the staff log');
});

test('staff deny with a note: the person sees it, and can appeal again', () => {
  const s = makeServer();
  const { d, bob } = bannedBob(s);
  d.call('appeal.send', { text: plea });
  const id = s.call(s.staff, 'admin.appeals').appeals[0].id;
  const r = s.call(s.staff, 'admin.answerAppeal', { id, accept: false, note: 'Three days is fair.' });
  assert.ok(r.ok, r.error);
  assert.equal(r.appeals.length, 0, 'not open any more');
  assert.equal(bob.banned, true);
  assert.equal(s.call(s.staff, 'admin.answerAppeal', { id, accept: false }).ok, false, 'already answered');
  const me = d.call('hello', { name: 'bob' }).me;
  assert.equal(me.ban.appeal.status, 'denied');
  assert.equal(me.ban.appeal.note, 'Three days is fair.');
  assert.equal(me.ban.canAppeal, true);
  const closed = s.call(s.staff, 'admin.appeals', { status: 'closed' }).appeals;
  assert.equal(closed.length, 1);
  assert.equal(closed[0].answeredBy.id, s.staff.id);
  const log = s.call(s.staff, 'admin.log', { user: String(bob.userId) }).log;
  assert.ok(log.some((x) => x.action === 'appeal' && /Denied/.test(x.text)));
  assert.ok(d.call('appeal.send', { text: plea + ' I really mean it.' }).ok, 'a new appeal after the answer');
});

test('accepting an appeal unbans through the normal unban (and the staff log says so)', () => {
  const s = makeServer();
  const { d, bob } = bannedBob(s);
  d.call('appeal.send', { text: plea });
  const id = s.call(s.staff, 'admin.appeals').appeals[0].id;
  const r = s.call(s.staff, 'admin.answerAppeal', { id, accept: true, note: 'OK, one more chance.' });
  assert.ok(r.ok, r.error);
  assert.equal(bob.banned, false);
  assert.equal(bob.banReason, undefined);
  const me = d.call('hello', { name: 'bob' }).me;
  assert.equal(me.ban, null);
  assert.ok(d.call('friends.list').ok, 'can use the site again');
  const log = s.call(s.staff, 'admin.log', { user: String(bob.userId) }).log;
  assert.ok(log.some((x) => x.action === 'unban'));
  assert.ok(log.some((x) => x.action === 'appeal' && /Accepted/.test(x.text)));
  assert.ok((bob.notes || []).some((n) => n.kind === 'appeal'));
});

test('a new ban means a new appeal; a player can\'t answer appeals', () => {
  const s = makeServer();
  const { d, bob } = bannedBob(s);
  d.call('appeal.send', { text: plea });
  const id = bob.appeal.id;
  assert.equal(s.call(s.signUp('carol'), 'admin.answerAppeal', { id, accept: true }).ok, false);
  assert.equal(bob.banned, true);
  bob.appeal.banAt -= 100;   // (as if it was sent for an earlier ban)
  assert.equal(s.call(s.staff, 'admin.appeals').appeals.length, 0, 'an appeal for an old ban isn\'t open any more');
  assert.equal(s.call(s.staff, 'admin.answerAppeal', { id, accept: true }).ok, false);
  const r = d.call('appeal.send', { text: plea });
  assert.ok(r.ok, r.error);
  assert.equal(r.me.ban.appeal.status, 'open');
});
