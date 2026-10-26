import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test('forum search finds matching thread titles and post text', () => {
  const s = makeServer();
  const alice = s.signUp('alice');
  const bob = s.signUp('bob');
  const created = s.call(alice, 'forum.post', {
    board: 'help',
    title: 'Physics question',
    text: 'How do I make a ragdoll bounce correctly?'
  });
  assert.ok(created.ok, created.error);

  const title = s.call(bob, 'forum.search', { q: 'PHYSICS', board: 'all', page: 0 });
  assert.ok(title.ok, title.error);
  assert.equal(title.total, 1);
  assert.equal(title.results[0].match, 'title');
  assert.equal(title.results[0].id, created.thread.id);

  const body = s.call(bob, 'forum.search', { q: 'ragdoll', board: 'all', page: 0 });
  assert.ok(body.ok, body.error);
  assert.equal(body.total, 1);
  assert.equal(body.results[0].match, 'post');
  assert.match(body.results[0].snippet, /ragdoll/i);
});

test('forum search filters by board and rejects invalid boards or short queries', () => {
  const s = makeServer();
  const alice = s.signUp('alice');
  const post = s.call(alice, 'forum.post', {
    board: 'help', title: 'Studio help needed', text: 'Help with my scene.'
  });
  assert.ok(post.ok, post.error);

  const filtered = s.call(alice, 'forum.search', { q: 'help', board: 'scripting', page: 0 });
  assert.ok(filtered.ok, filtered.error);
  assert.equal(filtered.total, 0);

  const invalid = s.call(alice, 'forum.search', { q: 'help', board: 'not-a-board', page: 0 });
  assert.equal(invalid.ok, false);
  const short = s.call(alice, 'forum.search', { q: 'x', board: 'all', page: 0 });
  assert.equal(short.ok, false);
});
