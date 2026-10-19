// Lyreto links on games and groups: only real lyreto.com links are kept, and the
// server reads the invite page's preview tags into the card the page shows.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { lyretoLink, lyretoCardFrom } from '../server.js';

test('Lyreto links are cleaned up, and other sites are refused', () => {
  assert.equal(lyretoLink('https://lyreto.com/invite/abc123'), 'https://lyreto.com/invite/abc123');
  assert.equal(lyretoLink('  lyreto.com/myserver/ '), 'https://lyreto.com/myserver');
  assert.equal(lyretoLink('https://www.LYRETO.com/i/Xy-9'), 'https://lyreto.com/i/Xy-9');
  assert.equal(lyretoLink('https://lyreto.com/join?code=abc'), 'https://lyreto.com/join?code=abc');
  assert.equal(lyretoLink(''), '');
  for (const bad of ['https://lyreto.com/', 'https://evil.com/lyreto.com/x', 'https://lyreto.com.evil.com/x',
    'https://me@lyreto.com/x', 'https://lyreto.com:8080/x', 'javascript:alert(1)', 'ftp://lyreto.com/x',
    'https://lyreto.com/<script>', 'https://lyreto.com/x?a="b"', 'https://discord.gg/abc'])
    assert.equal(lyretoLink(bad), null, bad);
});

test('a game\'s creator can set its Lyreto link; others can\'t', () => {
  const s = makeServer();
  const alice = s.signUp('alice'), bob = s.signUp('bob');
  const game = s.addGame(alice, 'Arena');
  let r = s.call(alice, 'game.settings', { id: game.id, lyreto: 'lyreto.com/arena-fans' });
  assert.ok(r.ok, r.error);
  assert.deepEqual(r.asset.lyreto, { link: 'https://lyreto.com/arena-fans' });
  r = s.call(alice, 'game.settings', { id: game.id, lyreto: 'https://discord.gg/arena' });
  assert.ok(!r.ok);
  assert.match(r.error, /Lyreto/);
  assert.ok(!s.call(bob, 'game.settings', { id: game.id, lyreto: 'lyreto.com/mine' }).ok);
  r = s.call(alice, 'game.settings', { id: game.id, lyreto: '' });
  assert.ok(r.ok, r.error);
  assert.equal(r.asset.lyreto, undefined);
});

test('a group\'s managers can set its Lyreto link', () => {
  const s = makeServer();
  const alice = s.signUp('alice'), bob = s.signUp('bob');
  alice.verified = true;   // (groups are free for Verified people)
  const made = s.call(alice, 'groups.create', { name: 'Builders' });
  assert.ok(made.ok, made.error);
  const id = made.group.id;
  assert.ok(s.call(bob, 'groups.join', { id }).ok);
  assert.ok(!s.call(bob, 'groups.edit', { id, lyreto: 'lyreto.com/builders' }).ok, 'a Member can\'t');
  const r = s.call(alice, 'groups.edit', { id, lyreto: 'lyreto.com/builders' });
  assert.ok(r.ok, r.error);
  assert.equal(r.group.lyreto.link, 'https://lyreto.com/builders');
  assert.equal(s.call(bob, 'groups.get', { id }).group.lyreto.link, 'https://lyreto.com/builders');
});

test('the preview card comes from the invite page\'s tags', async () => {
  const page = `<html><head><title>x</title>
    <meta property="og:title" content="Arena Fans &amp; Friends">
    <meta content="Come hang out! 120 members" property="og:description">
    <meta property="og:image" content="https://cdn.lyreto.com/icons/arena.png"></head></html>`;
  assert.deepEqual(lyretoCardFrom(page),
    { title: 'Arena Fans & Friends', description: 'Come hang out! 120 members', image: 'https://cdn.lyreto.com/icons/arena.png' });
  // A picture from some other site isn't shown; a page with no title gives no card.
  assert.equal(lyretoCardFrom('<meta property="og:title" content="A"><meta property="og:image" content="https://evil.com/a.png">').image, '');
  assert.equal(lyretoCardFrom('<p>nothing</p>'), null);

  const s = makeServer();
  const alice = s.signUp('alice');
  const game = s.addGame(alice, 'Arena');
  s.call(alice, 'game.settings', { id: game.id, lyreto: 'lyreto.com/arena-fans' });
  const realFetch = globalThis.fetch;
  globalThis.fetch = async (url) => ({ ok: true, url, text: async () => (url === 'https://lyreto.com/arena-fans' ? page : '') });
  try { await s.fetchLyretoCards(); } finally { globalThis.fetch = realFetch; }
  const info = s.call(alice, 'asset.info', { id: game.id });
  assert.equal(info.asset.lyreto.title, 'Arena Fans & Friends');
  assert.equal(info.asset.lyreto.link, 'https://lyreto.com/arena-fans');
});
