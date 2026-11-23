// Shared Reactions: GIFs that survive the text filter, emoji reactions on forum posts and chat
// messages, forum votes and sorting, and the link cards in messages (both servers have the same).
import test from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { filterText } from '../textfilter.js';
import { renderInline, embedCards, previewText } from '../../website/app/embeds.js';

const GIF = '[media:https://static.klipy.com/ii/d7aec6f6f171607374b2065c836f92f4/c6/64/QLYkF4M7.gif]';

test('a GIF from the picker gets through the text filter, other links still do not', () => {
  assert.equal(filterText('look ' + GIF), 'look ' + GIF);
  assert.equal(filterText(GIF + '\n' + GIF), GIF + '\n' + GIF);
  assert.equal(filterText('[media:https://evil.example/a.gif]'), '[ Content Deleted ]');
  assert.equal(filterText('[media:https://static.klipy.com/a.gif] go to evil.com'), '[ Content Deleted ]');
  assert.equal(filterText('[media:https://static.klipy.com/a.gif?x=evil.com]'), '[ Content Deleted ]');
  assert.equal(filterText('see https://gutsandbolts.net/#/game/12'), 'see https://gutsandbolts.net/#/game/12');
});

test('forum posts keep their GIFs, and the card shows a preview and the first GIF', () => {
  const s = makeServer(), a = s.signUp('alice');
  const r = s.call(a, 'forum.post', { board: 'help', title: 'my build', text: 'Here it is\n' + GIF });
  assert.ok(r.ok, r.error);
  assert.equal(r.posts[0].text, 'Here it is\n' + GIF);
  const list = s.call(a, 'forum.list', { board: 'help' });
  assert.equal(list.threads[0].preview, 'Here it is');
  assert.equal(list.threads[0].image, 'https://static.klipy.com/ii/d7aec6f6f171607374b2065c836f92f4/c6/64/QLYkF4M7.gif');
  assert.equal(list.threads[0].boardName, 'Help');
});

test('emoji reactions on forum posts go on and off, one per person per emoji', () => {
  const s = makeServer(), a = s.signUp('alice'), b = s.signUp('bobby');
  const th = s.call(a, 'forum.post', { board: 'help', title: 'hi', text: 'hello' }).thread;
  const post = s.call(a, 'forum.thread', { thread: th.id }).posts[0].id;
  let r = s.call(b, 'forum.react', { thread: th.id, post, emoji: '🔥' });
  assert.ok(r.ok, r.error);
  assert.deepEqual(r.reactions, [{ emoji: '🔥', count: 1, mine: true }]);
  r = s.call(a, 'forum.react', { thread: th.id, post, emoji: '🔥' });
  assert.deepEqual(r.reactions, [{ emoji: '🔥', count: 2, mine: true }]);
  r = s.call(b, 'forum.react', { thread: th.id, post, emoji: '🔥' });
  assert.deepEqual(r.reactions, [{ emoji: '🔥', count: 1, mine: false }]);
  assert.equal(s.call(b, 'forum.react', { thread: th.id, post, emoji: 'lol' }).ok, false);
  const guest = s.user('c'.repeat(64));
  assert.equal(s.call(guest, 'forum.react', { thread: th.id, post, emoji: '👍' }).ok, false);
  assert.deepEqual(s.call(b, 'forum.thread', { thread: th.id }).posts[0].reactions, [{ emoji: '🔥', count: 1, mine: false }]);
});

test('forum votes: up, down, back to nothing, and the Top and New sorts', () => {
  const s = makeServer(), a = s.signUp('alice'), b = s.signUp('bobby'), c = s.signUp('carla');
  const one = s.call(a, 'forum.post', { board: 'help', title: 'first', text: 'one' }).thread;
  s.lastPost.clear();
  const two = s.call(a, 'forum.post', { board: 'help', title: 'second', text: 'two' }).thread;
  s.forum.get(two.id).at += 10;
  assert.equal(s.call(b, 'forum.vote', { thread: one.id, vote: 1 }).thread.score, 1);
  assert.equal(s.call(c, 'forum.vote', { thread: one.id, vote: 1 }).thread.score, 2);
  let r = s.call(c, 'forum.vote', { thread: one.id, vote: -1 });
  assert.equal(r.thread.score, 0); assert.equal(r.thread.myVote, -1);
  r = s.call(c, 'forum.vote', { thread: one.id, vote: 0 });
  assert.equal(r.thread.score, 1); assert.equal(r.thread.myVote, 0);
  assert.equal(s.call(c, 'forum.vote', { thread: one.id, vote: 5 }).ok, false);
  assert.equal(s.call(b, 'forum.list', { board: 'help', sort: 'top' }).threads[0].id, one.id);
  assert.equal(s.call(b, 'forum.list', { board: 'help', sort: 'new' }).threads[0].id, two.id);
  const all = s.call(b, 'forum.list', { board: 'all', sort: 'hot' });
  assert.ok(all.ok); assert.equal(all.threads.length, 2); assert.equal(all.canPost, false);
});

test('peeking at a thread for a link card is not a view', () => {
  const s = makeServer(), a = s.signUp('alice'), b = s.signUp('bobby');
  const th = s.call(a, 'forum.post', { board: 'help', title: 'hi', text: 'hello' }).thread;
  s.call(b, 'forum.thread', { thread: th.id, peek: true });
  assert.equal(s.forum.get(th.id).views, 0);
  s.call(b, 'forum.thread', { thread: th.id });
  assert.equal(s.forum.get(th.id).views, 1);
});

test('chat messages: GIFs get through and everyone in the chat sees the reactions', () => {
  const s = makeServer(), a = s.signUp('alice'), b = s.signUp('bobby');
  a.friends.push(b.id); b.friends.push(a.id);
  const chat = s.call(a, 'party.create', { members: [b.id] }).chat;
  s.call(b, 'party.respond', { id: chat.id, accept: true });
  let r = s.call(a, 'party.send', { id: chat.id, body: 'gg\n' + GIF });
  assert.ok(r.ok, r.error);
  const msg = r.chat.messages[r.chat.messages.length - 1];
  assert.equal(msg.body, 'gg\n' + GIF);
  r = s.call(b, 'party.react', { id: chat.id, message: msg.id, emoji: '😂' });
  assert.ok(r.ok, r.error);
  const seen = s.call(a, 'party.read', { id: chat.id }).chat.messages.find((m) => m.id === msg.id);
  assert.deepEqual(seen.reacts, { '😂': [b.id] });
  s.call(b, 'party.react', { id: chat.id, message: msg.id, emoji: '😂' });
  assert.equal(s.call(a, 'party.read', { id: chat.id }).chat.messages.find((m) => m.id === msg.id).reacts, undefined);
  assert.equal(s.call(b, 'party.react', { id: chat.id, message: 'nope', emoji: '😂' }).ok, false);
});

test('messages show GIFs and site links safely, with cards for games and threads', () => {
  const cards = [];
  const out = renderInline('<b>hi</b> gutsandbolts.net/#/game/12 and https://gutsandbolts.net/#/forum/t/fabc123.\n' + GIF, { media: 'm' }, cards);
  assert.ok(out.includes('&lt;b&gt;hi&lt;/b&gt;'));
  assert.ok(out.includes('<a class="gb-link" href="#/game/12">gutsandbolts.net/#/game/12</a>'));
  assert.ok(out.includes('href="#/forum/t/fabc123">gutsandbolts.net/#/forum/t/fabc123</a>.'));
  assert.ok(out.includes('<img class="m" src="https://static.klipy.com/'));
  assert.deepEqual(cards, [{ kind: 'game', id: '12' }, { kind: 'forum/t', id: 'fabc123' }]);
  assert.ok(embedCards(cards).includes('data-embed="game:12"'));
  assert.ok(!renderInline('[media:https://evil.example/a.gif]').includes('<img'));
  assert.equal(previewText('gg\n' + GIF), 'gg [GIF]');
});
