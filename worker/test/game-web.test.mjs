// HttpService web requests from games (http.request) and game secrets (game.secret*): game-web.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { GUEST_OK } from '../server.js';
import { webUrl } from '../game-web.js';

function hostServer(s, host, game, dedicated = false) {
  const id = 's-' + Math.random().toString(16).slice(2, 10);
  const sess = { id, game: game.id, title: game.name, host: host.id, code: '', priv: false, max: 12,
    created: Math.floor(Date.now() / 1000), control: 'c-' + id, players: new Set(), dedicated };
  s.sessions.set(id, sess);
  return sess;
}
// Stand in for the internet: remember what was asked, answer with `reply`.
function fakeFetch(reply = () => new Response('hello', { status: 200, headers: { 'content-type': 'text/plain' } })) {
  const seen = [];
  const old = globalThis.fetch;
  globalThis.fetch = async (url, init) => { seen.push({ url, init }); return reply(url, init); };
  return { seen, restore: () => { globalThis.fetch = old; } };
}
const KEY = 'lyr_live_0123456789abcdef';

test('only lyreto.com over https is on the list', () => {
  assert.equal(webUrl('https://lyreto.com/api/x').host, 'lyreto.com');
  assert.equal(webUrl('https://api.lyreto.com/v1').host, 'api.lyreto.com');
  for (const bad of ['http://lyreto.com/', 'https://lyreto.com.evil.com/', 'https://evillyreto.com/', 'https://user:pw@lyreto.com/',
    'https://lyreto.com:8443/', 'https://192.168.1.1/', 'https://localhost/', 'ftp://lyreto.com/', 'lyreto.com', ''])
    assert.ok(webUrl(bad).error, bad + ' should be refused');
});

test('http.request needs web requests on, and a live server (or the creator)', async () => {
  const s = makeServer();
  const ann = s.signUp('ann'), ben = s.signUp('ben'), cat = s.signUp('cat');
  const game = s.addGame(ann, 'Bridge');
  const ask = (who) => s.call(who, 'http.request', { game: game.id, url: 'https://lyreto.com/api/ping' });
  assert.match(ask(ann).error, /web requests are off/);
  assert.equal(s.call(ben, 'game.settings', { id: game.id, http: true }).ok, false);   // not theirs
  assert.ok(s.call(ann, 'game.settings', { id: game.id, http: true }).ok);
  assert.ok(ask(ann).ok, 'the creator can test from Studio');
  assert.match(ask(cat).error, /running server/);
  hostServer(s, ben, game);
  const r = ask(ben);
  assert.ok(r.ok, r.error);
  assert.equal(JSON.stringify(r), '{"ok":true}', 'the prepared request never shows in the answer');
  assert.ok(GUEST_OK.has('http.request'), 'guests can host games too');
});

test('the request is made by the server, with trusted headers and no redirects', async () => {
  const s = makeServer();
  const ann = s.signUp('ann'), ben = s.signUp('ben');
  const game = s.addGame(ann, 'Bridge');
  s.call(ann, 'game.settings', { id: game.id, http: true });
  hostServer(s, ben, game);
  const net = fakeFetch();
  try {
    const a = s.call(ben, 'http.request', { game: game.id, url: 'https://lyreto.com/api/send', method: 'post',
      headers: { 'Content-Type': 'application/json', 'X-Note': 5 }, body: '{"text":"hi"}' });
    assert.ok(a.ok, a.error);
    const r = await s.webFetch(a);
    assert.equal(r.status, 200);
    assert.equal(r.body, 'hello');
    const { url, init } = net.seen[0];
    assert.equal(url, 'https://lyreto.com/api/send');
    assert.equal(init.method, 'POST');
    assert.equal(init.redirect, 'manual');
    assert.equal(init.headers['X-GutsAndBolts-Game'], String(game.num));
    assert.equal(init.headers['X-GutsAndBolts-Server'], 'player');
    assert.equal(init.headers['X-GutsAndBolts-Host'], String(ben.userId));
    assert.equal(init.headers['X-Note'], '5');
  } finally { net.restore(); }
  // Scripts can't pretend to be someone else, or set what the site trusts.
  for (const h of ['X-GutsAndBolts-Game', 'Host', 'Cookie', 'CF-Connecting-IP', 'X-Forwarded-For'])
    assert.match(s.call(ben, 'http.request', { game: game.id, url: 'https://lyreto.com/', headers: { [h]: '1' } }).error, /can't set/);
  assert.match(s.call(ben, 'http.request', { game: game.id, url: 'https://lyreto.com/', headers: { A: 'x\r\nB: y' } }).error, /line break/);
  assert.match(s.call(ben, 'http.request', { game: game.id, url: 'https://lyreto.com/', method: 'GET', body: 'x' }).error, /GET/);
  assert.match(s.call(ben, 'http.request', { game: game.id, url: 'https://lyreto.com/', method: 'CONNECT' }).error, /Method/);
  assert.match(s.call(ben, 'http.request', { game: game.id, url: 'https://example.com/' }).error, /isn't on/);
});

test('secrets: write-only, put in by the server, only to their site, blanked if echoed', async () => {
  const s = makeServer();
  const ann = s.signUp('ann'), ben = s.signUp('ben');
  const game = s.addGame(ann, 'Bridge');
  s.call(ann, 'game.settings', { id: game.id, http: true });
  assert.match(s.call(ben, 'game.secretSet', { id: game.id, name: 'LyretoKey', value: KEY, host: 'lyreto.com' }).error, /creator/);
  assert.match(s.call(ann, 'game.secretSet', { id: game.id, name: 'bad name', value: KEY, host: 'lyreto.com' }).error, /letters/);
  assert.match(s.call(ann, 'game.secretSet', { id: game.id, name: 'K', value: KEY, host: 'example.com' }).error, /Pick the site/);
  assert.match(s.call(ann, 'game.secretSet', { id: game.id, name: 'K', value: 'short', host: 'lyreto.com' }).error, /too short/);
  const set = s.call(ann, 'game.secretSet', { id: game.id, name: 'LyretoKey', value: KEY, host: 'lyreto.com' });
  assert.ok(set.ok, set.error);
  assert.deepEqual(set.secrets.map((x) => [x.name, x.host, x.dedicatedOnly]), [['LyretoKey', 'lyreto.com', false]]);
  // The value never comes back: not in the list, not on the game's page.
  assert.ok(!JSON.stringify(s.call(ann, 'game.secrets', { id: game.id })).includes(KEY));
  assert.ok(!JSON.stringify(s.call(ann, 'asset.info', { id: game.id })).includes(KEY));
  assert.ok(!JSON.stringify(s.call(ann, 'list', { kind: 'game' })).includes(KEY));

  hostServer(s, ben, game);
  const net = fakeFetch((url, init) => new Response('you sent ' + init.headers.Authorization, { status: 200, headers: { 'x-echo': init.headers.Authorization } }));
  try {
    const a = s.call(ben, 'http.request', { game: game.id, url: 'https://api.lyreto.com/bridge',
      headers: { Authorization: { secret: 'LyretoKey', prefix: 'Bearer ', suffix: '' } } });
    assert.ok(a.ok, a.error);
    assert.ok(!JSON.stringify(a).includes(KEY), 'the host never gets the value');
    const r = await s.webFetch(a);
    assert.equal(net.seen[0].init.headers.Authorization, 'Bearer ' + KEY, 'the site gets the real key');
    assert.equal(r.body, 'you sent Bearer [secret]');
    assert.equal(r.headers['x-echo'], 'Bearer [secret]');
  } finally { net.restore(); }
  assert.match(s.call(ben, 'http.request', { game: game.id, url: 'https://lyreto.com/', headers: { A: { secret: 'Nope' } } }).error, /no secret called/);

  // Guts&Bolts game servers only: a player hosting can't use it.
  s.call(ann, 'game.secretSet', { id: game.id, name: 'ServerOnly', value: KEY, host: 'lyreto.com', dedicatedOnly: true });
  const use = { game: game.id, url: 'https://lyreto.com/', headers: { A: { secret: 'ServerOnly' } } };
  assert.match(s.call(ben, 'http.request', use).error, /game servers/);
  hostServer(s, s.staff, game, true);
  s.call(ann, 'game.settings', { id: game.id, http: true });
  assert.ok(s.call(s.staff, 'http.request', use).ok);

  assert.ok(s.call(ann, 'game.secretRemove', { id: game.id, name: 'LyretoKey' }).ok);
  assert.deepEqual(s.call(ann, 'game.secrets', { id: game.id }).secrets.map((x) => x.name), ['ServerOnly']);
});

test('a server gets 120 web requests a minute', () => {
  const s = makeServer();
  const ann = s.signUp('ann');
  const game = s.addGame(ann, 'Busy');
  s.call(ann, 'game.settings', { id: game.id, http: true });
  hostServer(s, ann, game);
  for (let i = 0; i < 120; i++) assert.ok(s.call(ann, 'http.request', { game: game.id, url: 'https://lyreto.com/' }).ok);
  assert.match(s.call(ann, 'http.request', { game: game.id, url: 'https://lyreto.com/' }).error, /too many/);
});

test('big answers are refused', async () => {
  const s = makeServer();
  const ann = s.signUp('ann');
  const game = s.addGame(ann, 'Big');
  s.call(ann, 'game.settings', { id: game.id, http: true });
  const net = fakeFetch(() => new Response('x'.repeat(1024 * 1024 + 1)));
  try {
    const r = await s.webFetch(s.call(ann, 'http.request', { game: game.id, url: 'https://lyreto.com/' }));
    assert.match(r.error, /1 MB/);
  } finally { net.restore(); }
});
