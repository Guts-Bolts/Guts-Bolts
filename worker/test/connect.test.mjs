// "Log in with Guts&Bolts": partner apps (like Lyreto) linking people's accounts (OAuth 2.0 codes and tokens).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const BACK = 'https://lyreto.com/api/connections/gutsandbolts/callback';
// A tiny real PNG header (1 x 1), enough for the server's size check.
const PNG = Buffer.from('89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489', 'hex').toString('base64');

function setup() {
  const s = makeServer();
  const made = s.call(s.staff, 'admin.connectSecret', { id: 'lyreto' });
  assert.ok(made.ok, made.error);
  return { s, secret: made.secret };
}
const swap = (s, args) => s.handle({ op: 'connect.token', args });
const whoIs = (s, token) => s.handle({ op: 'connect.userinfo', args: { token, origin: 'https://gutsandbolts.net' } });

test('approve, swap the code for a token, then the token says who you are', () => {
  const { s, secret } = setup();
  const kermit = s.signUp('Kermit');
  const app = s.call(kermit, 'connect.app', { client_id: 'lyreto', redirect_uri: BACK });
  assert.ok(app.ok, app.error);
  assert.equal(app.app.name, 'Lyreto');
  const ok = s.call(kermit, 'connect.approve', { client_id: 'lyreto', redirect_uri: BACK, picture: PNG });
  assert.ok(ok.ok, ok.error);
  const t = swap(s, { grant_type: 'authorization_code', code: ok.code, redirect_uri: BACK, client_id: 'lyreto', client_secret: secret });
  assert.ok(t.ok, t.error);
  assert.equal(t.token_type, 'Bearer');
  const u = whoIs(s, t.access_token);
  assert.ok(u.ok, u.error);
  assert.equal(u.user.id, String(kermit.userId));
  assert.equal(u.user.username, 'Kermit');
  assert.match(u.user.avatar, new RegExp('^https://gutsandbolts.net/avatar/' + kermit.userId + '\\.png'));
  const pic = s.handle({ op: 'connect.avatarPicture', args: { id: String(kermit.userId) } });
  assert.equal(pic.data, PNG);
  assert.deepEqual(s.call(kermit, 'connect.list').apps.map((a) => a.id), ['lyreto']);
});

test('only Lyreto\'s own return address is allowed', () => {
  const { s } = setup();
  const bob = s.signUp('bob');
  assert.equal(s.call(bob, 'connect.app', { client_id: 'lyreto', redirect_uri: 'https://evil.example/steal' }).ok, false);
  assert.equal(s.call(bob, 'connect.approve', { client_id: 'lyreto', redirect_uri: 'https://lyreto.com/other' }).ok, false);
  assert.equal(s.call(bob, 'connect.app', { client_id: 'someoneelse', redirect_uri: BACK }).ok, false);
});

test('a code works once, only with the right secret', () => {
  const { s, secret } = setup();
  const bob = s.signUp('bob');
  const code = s.call(bob, 'connect.approve', { client_id: 'lyreto', redirect_uri: BACK }).code;
  const wrong = swap(s, { code, client_id: 'lyreto', client_secret: 'gbs_nope' });
  assert.equal(wrong.oauth, 'invalid_client');
  // (a wrong secret doesn't use the code up)
  assert.ok(swap(s, { code, redirect_uri: BACK, client_id: 'lyreto', client_secret: secret }).ok);
  assert.equal(swap(s, { code, redirect_uri: BACK, client_id: 'lyreto', client_secret: secret }).oauth, 'invalid_grant');
});

test('a new secret replaces the old one, and only the official account can make one', () => {
  const { s, secret } = setup();
  const bob = s.signUp('bob');
  assert.equal(s.call(bob, 'admin.connectSecret', { id: 'lyreto' }).ok, false);
  const again = s.call(s.staff, 'admin.connectSecret', { id: 'lyreto' });
  const code = s.call(bob, 'connect.approve', { client_id: 'lyreto', redirect_uri: BACK }).code;
  assert.equal(swap(s, { code, client_id: 'lyreto', client_secret: secret }).oauth, 'invalid_client');
  assert.ok(swap(s, { code, client_id: 'lyreto', client_secret: again.secret }).ok);
});

test('disconnecting (or deleting the account) stops the token working', () => {
  const { s, secret } = setup();
  const bob = s.signUp('bob');
  const token = () => swap(s, { code: s.call(bob, 'connect.approve', { client_id: 'lyreto', redirect_uri: BACK, picture: PNG }).code,
    client_id: 'lyreto', client_secret: secret }).access_token;
  const first = token();
  assert.ok(whoIs(s, first).ok);
  s.call(bob, 'connect.remove', { id: 'lyreto' });
  assert.equal(whoIs(s, first).oauth, 'invalid_token');
  assert.equal(s.handle({ op: 'connect.avatarPicture', args: { id: String(bob.userId) } }).ok, false);
  const second = token();
  assert.ok(whoIs(s, second).ok);
  s.deleteAccount(bob);
  s.flush();
  assert.equal(whoIs(s, second).oauth, 'invalid_token');
});

test('nothing works before staff make the secret', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  assert.equal(s.call(bob, 'connect.approve', { client_id: 'lyreto', redirect_uri: BACK }).ok, false);
  assert.equal(whoIs(s, 'gbt_made_up').oauth, 'invalid_token');
});
