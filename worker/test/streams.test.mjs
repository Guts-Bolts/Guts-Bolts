// G&B Live (worker/streams.js): going live, sending pieces, watching them, chat with time outs
// and slow mode, followers hearing about it, reports, and streams ending by themselves.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { isWebm, kKeep, kQuiet, kLeastSegBytes } from '../streams.js';
import { PRODUCTS, activeLink } from '../../website/app/products.js';
import { liveSince } from '../../website/app/streams.js';

// A WebM-looking piece (the EBML mark first), and something that isn't one.
const PIECE = (n = 0) => Buffer.concat([Buffer.from([0x1a, 0x45, 0xdf, 0xa3]), Buffer.alloc(kLeastSegBytes + 100, n)]);
const NOT = Buffer.concat([Buffer.from('ftypisom'), Buffer.alloc(2000, 1)]);
const JPG = Buffer.concat([Buffer.from([0xff, 0xd8, 0xff, 0xe0]), Buffer.alloc(60, 3)]).toString('base64');

function fakeBucket() {
  const files = new Map();
  return {
    files,
    async put(key, body) { files.set(key, Buffer.from(body)); },
    async get(key) { const all = files.get(key); return all ? { size: all.length, body: new Blob([all]).stream() } : null; },
    async delete(key) { files.delete(key); },
  };
}
function setup() {
  const bucket = fakeBucket();
  const s = makeServer({ DOWNLOADS: bucket });
  const ann = s.signUp('ann'), bob = s.signUp('bob'), cat = s.signUp('cat');
  const verified = new Set([ann.id]);
  const was = s.isVerified.bind(s);
  s.isVerified = (u) => verified.has(u.id) || was(u);   // (ann is Verified)
  return { s, ann, bob, cat, bucket };
}
const push = (s, address, n, bytes) => s.fetch(new Request('https://gutsandbolts.net' + address + '&n=' + n, { method: 'PUT', body: bytes,
  headers: { 'content-length': String(bytes.length) }, duplex: 'half' }));
const get = (s, path) => s.fetch(new Request('https://gutsandbolts.net' + path));
const goLive = (s, who, extra = {}) => s.call(who, 'stream.start', Object.assign({ title: 'Building an obby live', cat: 'gaming', thumb: JPG }, extra));

test('pieces are checked by their bytes, and the time label reads well', () => {
  assert.ok(isWebm(PIECE()));
  assert.ok(!isWebm(NOT));
  const t = 1800000000;
  assert.equal(liveSince(t - 45, t), '0:45');
  assert.equal(liveSince(t - 3725, t), '1:02:05');
});

test('only Verified people and staff can go live, with a title', () => {
  const { s, ann, bob } = setup();
  assert.match(goLive(s, bob).error, /Verified/);
  assert.match(goLive(s, ann, { title: ' ' }).error, /title/);
  assert.match(goLive(s, ann, { thumb: 'bm9wZQ==' }).error, /picture/);
  const r = goLive(s, ann);
  assert.ok(r.ok, r.error);
  assert.match(r.push, /^\/liveup\/live-[0-9a-f]+\?t=[0-9a-f]+$/);
  assert.equal(r.stream.state, 'live');
  assert.ok(r.stream.mine && r.stream.canEnd);
  assert.ok(goLive(s, s.staff).ok, 'staff can too');
  // A guest can't start one.
  assert.match(s.call(s.user('c'.repeat(64)), 'stream.start', { title: 'x' }).error, /Sign up/);
});

test('sending pieces, watching them, and old pieces letting go', async () => {
  const { s, ann, bob, bucket } = setup();
  const r = goLive(s, ann);
  const id = r.stream.id;
  // Nothing sent yet: not in the list.
  assert.equal(s.call(bob, 'stream.list').live.length, 0);
  let res = await push(s, r.push, 0, PIECE(0));
  assert.equal(res.status, 200, await res.clone().text());
  assert.equal(s.call(bob, 'stream.list').live[0].id, id);
  // Wrong token, out of order, not WebM, too small: all refused.
  assert.equal((await push(s, r.push.replace(/t=\w+/, 't=nope'), 1, PIECE())).status, 404);
  assert.equal((await push(s, r.push, 0, PIECE())).status, 409);
  assert.equal((await push(s, r.push, 1, NOT)).status, 400);
  assert.equal((await push(s, r.push, 1, PIECE().subarray(0, 100))).status, 400);
  // Watching: the piece comes back as it went.
  const w = s.call(bob, 'stream.get', { id });
  assert.equal(w.stream.last, 0);
  assert.equal(w.stream.viewers, 1);
  res = await get(s, w.stream.src + '0');
  assert.equal(res.status, 200);
  assert.equal(res.headers.get('content-type'), 'video/webm');
  assert.deepEqual(Buffer.from(await res.arrayBuffer()), PIECE(0));
  assert.equal((await get(s, w.stream.src + '1')).status, 404);
  assert.equal((await get(s, '/live/' + id + '/pic')).status, 200);
  // Only the newest kKeep pieces stay in R2.
  for (let n = 1; n < kKeep + 5; n++) assert.equal((await push(s, r.push, n, PIECE(n))).status, 200);
  const kept = [...bucket.files.keys()].filter((k) => k.includes(id));
  assert.equal(kept.length, kKeep);
  const now = s.call(bob, 'stream.get', { id }).stream;
  assert.equal(now.last, kKeep + 4);
  assert.equal(now.first, 5);
  assert.equal((await get(s, now.src + '0')).status, 404);
  // Guests can watch too, and each counts once.
  s.call(s.user('d'.repeat(64)), 'stream.get', { id, viewer: 'abc' });
  s.call(s.user('d'.repeat(64)), 'stream.get', { id, viewer: 'abc' });
  assert.equal(s.call(bob, 'stream.get', { id }).stream.viewers, 2);
  // Ending deletes every piece.
  assert.match(s.call(bob, 'stream.end', { id }).error, /Only the streamer/);
  assert.equal(s.call(ann, 'stream.end', { id }).stream.state, 'ended');
  assert.equal([...bucket.files.keys()].filter((k) => k.includes(id)).length, 0);
  assert.equal((await push(s, r.push, kKeep + 5, PIECE())).status, 404);
  const list = s.call(bob, 'stream.list');
  assert.equal(list.live.length, 0);
  assert.equal(list.recent[0].id, id);
});

test('a stream with no new pieces for a while ends by itself, and starting again ends the old one', async () => {
  const { s, ann } = setup();
  const first = goLive(s, ann);
  await push(s, first.push, 0, PIECE());
  const second = goLive(s, ann, { title: 'Take two' });
  assert.equal(s.streams.get(first.stream.id).state, 'ended');
  await push(s, second.push, 0, PIECE());
  s.streams.get(second.stream.id).seen -= kQuiet + 1;   // (the tab closed a while ago)
  assert.equal(s.call(ann, 'stream.list').live.length, 0);
  assert.equal(s.streams.get(second.stream.id).why, 'quiet');
});

test('chat: lines arrive in order, time outs, slow mode, and deleting', async () => {
  const { s, ann, bob, cat } = setup();
  const r = goLive(s, ann);
  const id = r.stream.id;
  await push(s, r.push, 0, PIECE());
  assert.match(s.call(bob, 'stream.chat', { id, text: '  ' }).error, /Write something/);
  const a = s.call(bob, 'stream.chat', { id, text: 'first!' });
  assert.ok(a.ok, a.error);
  s.call(cat, 'stream.chat', { id, text: 'hi ann' });
  let w = s.call(bob, 'stream.get', { id });
  assert.deepEqual(w.chat.map((c) => c.text), ['first!', 'hi ann']);
  // Only what's new after the last line you saw.
  w = s.call(bob, 'stream.get', { id, after: w.chat[0].n });
  assert.deepEqual(w.chat.map((c) => c.text), ['hi ann']);
  // Only the streamer (or staff) gives time outs; the timed out person's lines go.
  assert.match(s.call(bob, 'stream.timeout', { id, user: cat.id }).error, /Only the streamer/);
  assert.ok(s.call(ann, 'stream.timeout', { id, user: cat.id }).ok);
  assert.match(s.call(cat, 'stream.chat', { id, text: 'let me back' }).error, /time out/);
  assert.equal(s.call(cat, 'stream.get', { id }).timeoutUntil > 0, true);
  assert.deepEqual(s.call(bob, 'stream.get', { id }).chat.map((c) => c.text), ['first!']);
  assert.match(s.call(ann, 'stream.timeout', { id, user: s.staff.id }).error, /staff/);
  // Slow mode: one line every 15 seconds (not for the streamer).
  assert.equal(s.call(ann, 'stream.slow', { id, secs: 15 }).stream.slow, 15);
  assert.match(s.call(bob, 'stream.chat', { id, text: 'again' }).error, /Slow mode/);
  assert.ok(s.call(ann, 'stream.chat', { id, text: 'streamer talks' }).ok);
  // Deleting a line: who wrote it, or the streamer.
  const line = s.call(bob, 'stream.get', { id }).chat[0];
  assert.ok(line.canDelete);
  assert.ok(!s.call(cat, 'stream.get', { id }).chat[0].canDelete);
  assert.ok(s.call(ann, 'stream.unchat', { id, chat: line.id }).ok);
  // Chat closes when the stream ends.
  s.call(ann, 'stream.end', { id });
  assert.match(s.call(bob, 'stream.chat', { id, text: 'bye' }).error, /ended/);
});

test('followers hear when someone goes live (not on every restart), staff can end streams, and reports work', async () => {
  const { s, ann, bob, cat } = setup();
  assert.ok(s.call(bob, 'follow.add', { user: ann.id }).ok);
  const r = goLive(s, ann);
  const id = r.stream.id;
  assert.ok(bob.notes.some((n) => n.kind === 'stream' && n.about === id && /ann is live now/.test(n.text)));
  assert.ok(!(cat.notes || []).some((n) => n.kind === 'stream'));
  goLive(s, ann, { title: 'Again' });
  assert.equal(bob.notes.filter((n) => n.kind === 'stream').length, 1, 'restarting right away doesn\'t tell them twice');
  const again = [...s.streams.values()].find((x) => x.state === 'live').id;
  await push(s, '/liveup/' + again + '?t=' + s.streams.get(again).token, 0, PIECE());
  // Reports.
  assert.match(s.call(ann, 'report.send', { kind: 'stream', id: again, reason: 'spam' }).error, /yourself/);
  assert.ok(s.call(bob, 'report.send', { kind: 'stream', id: again, reason: 'spam' }).ok);
  const c = s.call(cat, 'stream.chat', { id: again, text: 'rude words here' }).chat.id;
  assert.ok(s.call(bob, 'report.send', { kind: 'stream', id: again + ':' + c, reason: 'harassment' }).ok);
  // Staff end it, and the streamer is told.
  assert.equal(s.call(s.staff, 'stream.end', { id: again }).stream.state, 'ended');
  assert.ok(ann.notes.some((n) => n.kind === 'stream' && /Staff ended/.test(n.text)));
});

test('Live is an open product inside Media, with its own pages', () => {
  const live = PRODUCTS.find((p) => p.id === 'live');
  assert.equal(live.status, 'open');
  assert.deepEqual(live.pages, ['live', 'golive']);
  assert.equal(activeLink('live', '#/live'), 'live');
  assert.equal(activeLink('live', '#/live/live-abc'), 'live');
  assert.equal(activeLink('golive', '#/golive'), 'golive');
});
