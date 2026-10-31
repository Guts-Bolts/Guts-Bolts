import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

function setup() {
  const s = makeServer();
  const alice = s.signUp('Alice');
  const bob = s.signUp('Bob');
  const carol = s.signUp('Carol');
  for (const [a, b] of [[alice, bob], [alice, carol], [bob, carol]]) {
    a.friends.push(b.id);
    b.friends.push(a.id);
    s.saveUser(a, b);
  }
  return { s, alice, bob, carol };
}

test('party creation invites friends without adding them before they accept', () => {
  const { s, alice, bob, carol } = setup();
  const made = s.call(alice, 'party.create', { title: 'Weekend build', members: [bob.id, carol.id] });
  assert.ok(made.ok, made.error);
  assert.equal(made.chat.members.length, 1);
  assert.equal(made.chat.members[0].id, alice.id);
  assert.equal(s.call(bob, 'party.list').invites.length, 1);
  assert.equal(s.call(carol, 'party.list').invites.length, 1);
});

test('accepted party members receive the same persistent chat history', () => {
  const { s, alice, bob, carol } = setup();
  const made = s.call(alice, 'party.create', { members: [bob.id, carol.id] });
  assert.ok(made.ok, made.error);
  const joined = s.call(bob, 'party.respond', { id: made.chat.id, accept: true });
  assert.ok(joined.ok, joined.error);
  const sent = s.call(alice, 'party.send', { id: made.chat.id, body: 'Anyone online?' });
  assert.ok(sent.ok, sent.error);
  const bobChat = s.call(bob, 'party.list').chats.find((x) => x.id === made.chat.id);
  assert.equal(bobChat.members.length, 2);
  assert.equal(bobChat.messages.at(-1).body, 'Anyone online?');
  assert.equal(s.call(carol, 'party.list').chats.length, 0);
  assert.equal(s.call(carol, 'party.list').invites.length, 1);
});

test('party owner can invite and remove members; members can leave', () => {
  const { s, alice, bob, carol } = setup();
  const made = s.call(alice, 'party.create', { members: [bob.id] });
  assert.ok(made.ok, made.error);
  assert.ok(s.call(bob, 'party.respond', { id: made.chat.id, accept: true }).ok);
  assert.ok(s.call(alice, 'party.invite', { id: made.chat.id, user: carol.id }).ok);
  assert.equal(s.call(carol, 'party.list').invites.length, 1);
  assert.ok(s.call(carol, 'party.respond', { id: made.chat.id, accept: true }).ok);
  assert.ok(s.call(alice, 'party.kick', { id: made.chat.id, user: bob.id }).ok);
  assert.equal(s.call(bob, 'party.list').chats.length, 0);
  assert.ok(s.call(carol, 'party.leave', { id: made.chat.id }).ok);
  assert.equal(s.call(carol, 'party.list').chats.length, 0);
});

test('party creation rejects non-friends and parties cannot exceed 20 members', () => {
  const { s, alice, bob } = setup();
  const stranger = s.signUp('Stranger');
  const rejected = s.call(alice, 'party.create', { members: [stranger.id] });
  assert.equal(rejected.ok, false);
  const tooMany = s.call(alice, 'party.create', { members: Array.from({ length: 20 }, (_, i) => 'missing-' + i) });
  assert.equal(tooMany.ok, false);
  assert.equal(s.call(bob, 'party.list').chats.length, 0);
});
