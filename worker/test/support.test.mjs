// Support tickets: someone who forgot their password (and has no email) asks staff for help.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const hex = (n) => 'c'.repeat(n);
const guest = (s) => s.user('d'.repeat(64));   // a browser that isn't logged in

test('a guest can send a ticket, and only staff see the list', () => {
  const s = makeServer();
  const bob = s.signUp('bob'), device = guest(s);
  const r = s.call(device, 'support.send', { topic: 'password', username: 'bob', message: 'I forgot my password, sorry!' });
  assert.ok(r.ok, r.error);
  assert.equal(s.call(bob, 'admin.tickets').ok, false);
  const list = s.call(s.staff, 'admin.tickets');
  assert.ok(list.ok, list.error);
  assert.equal(list.tickets.length, 1);
  assert.equal(list.tickets[0].about.username, 'bob');
  assert.equal(list.tickets[0].from, null, 'sent by a guest');
  assert.equal(s.call(device, 'support.mine').tickets.length, 1);
  assert.equal(s.call(bob, 'support.mine').tickets.length, 0, 'other devices don\'t see it');
});

test('tickets need a real account and a real message', () => {
  const s = makeServer();
  const device = guest(s);
  assert.equal(s.call(device, 'support.send', { topic: 'password', username: 'nobody', message: 'I forgot my password' }).ok, false);
  assert.equal(s.call(device, 'support.send', { topic: 'other', message: 'help' }).ok, false);
  assert.equal(s.call(device, 'support.send', { topic: 'password', username: 'Guts', message: 'I forgot my password' }).ok, false);
});

test('staff send a reset code; it works once, only on the device that asked', () => {
  const s = makeServer();
  const bob = s.signUp('bob'), device = guest(s), other = s.user('e'.repeat(64));
  const id = s.call(device, 'support.send', { topic: 'password', username: 'bob', message: 'I forgot my password, sorry!' }).ticket.id;
  assert.ok(s.call(s.staff, 'admin.ticketReset', { id }).ok);
  const mine = s.call(device, 'support.mine').tickets[0];
  assert.match(mine.resetCode, /^[0-9]{6}$/);
  assert.equal(mine.resetUsername, 'bob');
  const reset = (who, code) => s.call(who, 'account.reset', { username: 'bob', code, salt: hex(32), auth: hex(64), key: hex(208) });
  assert.equal(reset(other, mine.resetCode).ok, false, 'another device can\'t use it');
  const r = reset(device, mine.resetCode);
  assert.ok(r.ok, r.error);
  assert.equal(bob.keys[0], device.id, 'the device that asked takes over the account');
  assert.equal(reset(device, mine.resetCode).ok, false, 'used up');
  const after = s.call(device, 'support.mine').tickets[0];
  assert.equal(after.resetCode, undefined);
  assert.equal(after.status, 'closed');
});

test('staff answers reach the sender, and the log', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const id = s.call(bob, 'support.send', { topic: 'other', message: 'My game won\'t load, what do I do?' }).ticket.id;
  assert.ok(s.call(s.staff, 'admin.replyTicket', { id, text: 'Try updating the Player.' }).ok);
  assert.equal(s.call(bob, 'support.mine').tickets[0].replies[0].text, 'Try updating the Player.');
  assert.ok(bob.notes.some((n) => n.kind === 'support'));
  assert.ok(s.call(s.staff, 'admin.closeTicket', { id }).ok);
  assert.equal(s.call(s.staff, 'admin.tickets').tickets.length, 0);
  assert.equal(s.call(s.staff, 'admin.tickets', { status: 'closed' }).tickets.length, 1);
  assert.ok(s.call(s.staff, 'admin.log').log.some((x) => x.action === 'ticket'));
});

test('a device can only have a few open tickets', () => {
  const s = makeServer();
  const device = guest(s);
  for (let i = 0; i < 3; i++) assert.ok(s.call(device, 'support.send', { topic: 'other', message: 'Question number ' + i }).ok);
  assert.equal(s.call(device, 'support.send', { topic: 'other', message: 'One question too many' }).ok, false);
});

test('the Staff page says what email needs', () => {
  const s = makeServer();
  const m = s.call(s.staff, 'admin.mailStatus').mail;
  assert.equal(m.on, false);
  assert.match(m.problem, /BREVO_API_KEY/);
});

test('the friends list has user numbers, so "Friends online" links open the right profile', () => {
  const s = makeServer();
  const bob = s.signUp('bob'), carol = s.signUp('carol');
  bob.friends.push(carol.id); carol.friends.push(bob.id);
  const f = s.call(bob, 'friends.list').friends[0];
  assert.equal(f.userId, carol.userId);
  assert.equal(f.username, 'carol');
  assert.ok(s.call(bob, 'profile', { id: String(f.userId) }).ok);
});
