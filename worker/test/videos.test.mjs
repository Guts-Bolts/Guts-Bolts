// G&B Videos (worker/videos.js): start an upload, send the file, the staff check, the feed,
// likes, comments, followers hearing about new videos, reports, and watching with byte ranges.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { videoType, videoTags, videoRange, kPerDay, kMostBytes } from '../videos.js';
import { PRODUCTS, activeLink } from '../../website/app/products.js';
import { videoTime, videoCount } from '../../website/app/videos.js';

// The first bytes of a real MP4 ("ftyp" at byte 4), a MOV, a WebM, and something that isn't a video.
const MP4 = Buffer.concat([Buffer.from([0, 0, 0, 0x18]), Buffer.from('ftypisom'), Buffer.alloc(200, 7)]);
const MOV = Buffer.concat([Buffer.from([0, 0, 0, 0x14]), Buffer.from('ftypqt  '), Buffer.alloc(40, 1)]);
const WEBM = Buffer.concat([Buffer.from([0x1a, 0x45, 0xdf, 0xa3]), Buffer.alloc(40, 2)]);
const TEXT = Buffer.from('hello, I am not a video at all, just words');
// A tiny JPEG-looking picture for the thumbnail.
const JPG = Buffer.concat([Buffer.from([0xff, 0xd8, 0xff, 0xe0]), Buffer.alloc(60, 3)]).toString('base64');

// Cloudflare R2, in memory.
function fakeBucket() {
  const files = new Map();
  return {
    files,
    async put(key, body) {
      const chunks = [];
      for await (const c of body) chunks.push(Buffer.from(c));
      files.set(key, Buffer.concat(chunks));
    },
    async get(key, opts) {
      const all = files.get(key);
      if (!all) return null;
      const r = opts && opts.range;
      const part = r ? all.subarray(r.offset, r.offset + r.length) : all;
      return { size: all.length, body: new Blob([part]).stream(), arrayBuffer: async () => part.buffer.slice(part.byteOffset, part.byteOffset + part.length) };
    },
    async head(key) { const all = files.get(key); return all ? { size: all.length } : null; },
    async delete(key) { files.delete(key); },
  };
}

function setup() {
  const bucket = fakeBucket();
  const s = makeServer({ DOWNLOADS: bucket });
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  return { s, ann, bob, bucket };
}
const start = (s, who, extra = {}) => s.call(who, 'video.start', Object.assign({ title: 'My first speedrun', cat: 'gaming', tags: 'speedrun, Obby #speedrun',
  size: MP4.length, secs: 75.4, w: 1280, h: 720, thumb: JPG }, extra));
const putFile = (s, address, bytes) => s.fetch(new Request('https://gutsandbolts.net' + address, { method: 'PUT', body: bytes,
  headers: { 'content-length': String(bytes.length) }, duplex: 'half' }));
async function upload(s, who, extra = {}, bytes = MP4) {
  const r = start(s, who, Object.assign({ size: bytes.length }, extra));
  assert.ok(r.ok, r.error);
  const res = await putFile(s, r.upload, bytes);
  return { start: r, res, done: await res.json() };
}
const get = (s, path, headers = {}) => s.fetch(new Request('https://gutsandbolts.net' + path, { headers }));

test('videos are checked by their bytes, and tags and ranges are tidied', () => {
  assert.equal(videoType(MP4), 'mp4');
  assert.equal(videoType(MOV), 'mov');
  assert.equal(videoType(WEBM), 'webm');
  assert.equal(videoType(TEXT), '');
  assert.deepEqual(videoTags('speedrun, Obby #speedrun big_jump'), ['speedrun', 'obby', 'bigjump']);
  assert.deepEqual(videoRange('bytes=0-', 100), { offset: 0, length: 100 });
  assert.deepEqual(videoRange('bytes=10-19', 100), { offset: 10, length: 10 });
  assert.deepEqual(videoRange('bytes=-30', 100), { offset: 70, length: 30 });
  assert.equal(videoRange('bytes=200-', 100), 'bad');
  assert.equal(videoRange('', 100), null);
  assert.equal(videoTime(75), '1:15');
  assert.equal(videoTime(3725), '1:02:05');
  assert.equal(videoCount(1234), '1.2K');
  assert.equal(videoCount(56000), '56K');
});

test('starting an upload: a title, a size that fits, and an account are needed', () => {
  const { s, ann } = setup();
  assert.match(start(s, ann, { title: '  ' }).error, /title/);
  assert.match(start(s, ann, { size: 0 }).error, /Pick a video/);
  assert.match(start(s, ann, { size: kMostBytes + 1 }).error, /too big/);
  assert.match(start(s, ann, { thumb: Buffer.from('nope').toString('base64') }).error, /picture/);
  const guest = s.user('c'.repeat(64));
  assert.match(s.call(guest, 'video.start', { title: 'x', size: 10 }).error, /Sign up/);
  const r = start(s, ann);
  assert.ok(r.ok, r.error);
  assert.match(r.upload, /^\/vidup\/vid-[0-9a-f]{12}\?t=[0-9a-f]{32}$/);
  assert.equal(r.video.state, 'uploading');
  assert.deepEqual(r.video.tags, ['speedrun', 'obby']);
  assert.equal(r.video.secs, 75);
});

test('sending the file: it must be a real video of the size given, and the address works once', async () => {
  const { s, ann, bucket } = setup();
  const bad = start(s, ann, { size: TEXT.length });
  const no = await putFile(s, bad.upload, TEXT);
  assert.equal(no.status, 400);
  assert.match((await no.json()).error, /isn't a video/);
  assert.equal(s.call(ann, 'video.get', { id: bad.video.id }).ok, false, 'it was let go');
  assert.equal(bucket.files.size, 0, 'and the file is gone too');

  const wrong = start(s, ann);
  assert.equal((await putFile(s, wrong.upload, Buffer.concat([MP4, MP4]))).status, 400, 'not the size it said');
  const r = start(s, ann);
  assert.equal((await putFile(s, r.upload.replace(/t=.*/, 't=' + '0'.repeat(32)), MP4)).status, 404, 'a wrong token');
  const ok = await putFile(s, r.upload, MP4);
  const done = await ok.json();
  assert.ok(done.ok, done.error);
  assert.equal(done.video.state, 'ready');
  assert.ok(bucket.files.has('videos/' + r.video.id));
  assert.equal((await putFile(s, r.upload, MP4)).status, 404, 'the address only works once');
});

test('videos from people who aren\'t Verified wait for a staff check', async () => {
  const { s, ann, bob } = setup();
  const { done } = await upload(s, ann);
  const id = done.video.id;
  assert.equal(done.video.review, 'pending');
  assert.match(done.video.src, /\?k=[0-9a-f]{24}$/, 'the uploader watches it with its key');
  assert.equal(s.call(bob, 'video.list', {}).videos.length, 0, 'not in the feed yet');
  assert.equal(s.call(bob, 'video.get', { id }).ok, false, 'others can\'t open it');
  assert.equal((await get(s, '/vid/' + id)).status, 404, 'or watch it without the key');
  assert.equal((await get(s, done.video.src)).status, 200, 'the key works');
  assert.equal(s.call(ann, 'video.list', { by: ann.userId }).videos.length, 1, 'the uploader sees it in their own list');
  assert.equal(s.call(bob, 'video.list', { by: ann.userId }).videos.length, 0);
  assert.match(s.call(bob, 'video.like', { id }).error, /isn't here/);
  assert.match(s.call(ann, 'video.review', { id, ok: true }).error, /Only staff/);
  assert.deepEqual(s.call(s.staff, 'video.pending', {}).videos.map((v) => v.id), [id]);
  assert.ok(s.call(s.staff, 'video.review', { id, ok: true }).ok);
  assert.ok(ann.notes.some((n) => n.kind === 'video' && /passed the staff check/.test(n.text)));
  assert.equal(s.call(bob, 'video.list', {}).videos.length, 1, 'now everyone sees it');
  assert.equal(s.call(bob, 'video.get', { id }).video.src, '/vid/' + id, 'with a plain address');
  // Turning one down tells the uploader why.
  const no = (await upload(s, ann, { title: 'Second' })).done.video.id;
  assert.ok(s.call(s.staff, 'video.review', { id: no, ok: false, note: 'Not your own video' }).ok);
  assert.ok(ann.notes.some((n) => /didn't pass the staff check: Not your own video/.test(n.text)));
  assert.equal(s.call(ann, 'video.get', { id: no }).video.reviewNote, 'Not your own video');
});

test('followers hear about new videos (once), and staff and Verified go straight to the feed', async () => {
  const { s, ann, bob } = setup();
  assert.ok(s.call(bob, 'follow.add', { user: s.staff.id }).ok);
  const { done } = await upload(s, s.staff, { title: 'Big news' });
  assert.equal(done.video.review, undefined);
  assert.ok(bob.notes.some((n) => n.kind === 'video' && n.about === done.video.id && /Big news/.test(n.text)));
  assert.ok(!(ann.notes || []).some((n) => n.kind === 'video'), 'people who don\'t follow aren\'t told');
  // Not-Verified: followers hear when it passes the check.
  assert.ok(s.call(bob, 'follow.add', { user: ann.id }).ok);
  const later = (await upload(s, ann, { title: 'Waits first' })).done.video.id;
  assert.ok(!bob.notes.some((n) => n.about === later));
  s.call(s.staff, 'video.review', { id: later, ok: true });
  assert.equal(bob.notes.filter((n) => n.about === later).length, 1);
});

test('a daily limit for people who aren\'t Verified', () => {
  const { s, ann } = setup();
  for (let i = 0; i < kPerDay; i++) assert.ok(start(s, ann, { title: 'n' + i }).ok);
  assert.match(start(s, ann).error, /Come back tomorrow/);
});

test('videos together must fit in the free space', () => {
  const s = makeServer({ DOWNLOADS: fakeBucket(), VIDEO_STORAGE_GB: 0.0000001 });   // about 107 bytes
  const ann = s.signUp('ann');
  assert.match(start(s, ann, { size: 500 }).error, /full for now/);
});

test('the feed: newest, most liked, categories, tags, search and someone\'s channel', async () => {
  const { s, ann, bob } = setup();
  const a = (await upload(s, s.staff, { title: 'Old obby run', cat: 'gaming', tags: 'obby' })).done.video.id;
  const b = (await upload(s, s.staff, { title: 'Guitar cover', cat: 'music', tags: 'guitar' })).done.video.id;
  assert.ok(s.call(ann, 'video.like', { id: a }).ok);
  assert.ok(s.call(bob, 'video.like', { id: a }).ok);
  assert.deepEqual(s.call(bob, 'video.list', { sort: 'top' }).videos.map((v) => v.id), [a, b]);
  assert.deepEqual(s.call(bob, 'video.list', { cat: 'music' }).videos.map((v) => v.id), [b]);
  assert.deepEqual(s.call(bob, 'video.list', { tag: 'obby' }).videos.map((v) => v.id), [a]);
  assert.deepEqual(s.call(bob, 'video.list', { q: 'guitar' }).videos.map((v) => v.id), [b]);
  assert.equal(s.call(bob, 'video.list', { by: 1 }).videos.length, 2);
  const guest = s.user('d'.repeat(64));
  assert.equal(s.call(guest, 'video.list', {}).videos.length, 2, 'guests can look');
  const seen = s.call(guest, 'video.get', { id: b });
  assert.ok(seen.ok, 'and watch');
  assert.equal(seen.video.views, 1);
  assert.match(s.call(guest, 'video.like', { id: b }).error, /Sign up/);
  // Liking twice counts once; taking it back works.
  assert.equal(s.call(ann, 'video.like', { id: a }).likes, 2);
  assert.equal(s.call(ann, 'video.like', { id: a, on: false }).likes, 1);
});

test('comments: the uploader is told; the writer, the uploader or staff can delete one', async () => {
  const { s, ann, bob } = setup();
  const id = (await upload(s, s.staff)).done.video.id;
  const c = s.call(ann, 'video.comment', { id, text: 'So fast!' });
  assert.ok(c.ok);
  assert.ok(s.staff.notes.some((n) => n.kind === 'video' && /ann commented/.test(n.text)));
  const cid = c.video.commentList[0].id;
  assert.match(s.call(bob, 'video.uncomment', { id, comment: cid }).error, /only delete your own/);
  assert.ok(s.call(s.staff, 'video.uncomment', { id, comment: cid }).ok);
  assert.match(s.call(ann, 'video.comment', { id, text: '   ' }).error, /Write something/);
});

test('editing, deleting (the file goes too) and reporting', async () => {
  const { s, ann, bob, bucket } = setup();
  const id = (await upload(s, s.staff)).done.video.id;
  assert.match(s.call(ann, 'video.edit', { id, title: 'Mine now' }).error, /Only the uploader/);
  assert.equal(s.call(s.staff, 'video.edit', { id, title: 'New name', tags: 'a b' }).video.title, 'New name');
  assert.ok(s.call(ann, 'report.send', { kind: 'video', id, reason: 'spam' }).ok);
  const c = s.call(bob, 'video.comment', { id, text: 'hi' }).video.commentList[0].id;
  assert.ok(s.call(ann, 'report.send', { kind: 'video', id: id + ':' + c, reason: 'spam' }).ok);
  const reports = s.call(s.staff, 'admin.reports', {});
  assert.ok(reports.reports.some((r) => r.kind === 'video' && r.name === 'New name' && r.video === id));
  assert.match(s.call(bob, 'video.delete', { id }).error, /Only the uploader or staff/);
  assert.ok(s.call(s.staff, 'video.delete', { id }).ok);
  await new Promise((r) => setTimeout(r, 5));
  assert.equal(bucket.files.size, 0);
  assert.equal((await get(s, '/vid/' + id)).status, 404);
});

test('watching: whole file, byte ranges for skipping ahead, and the picture', async () => {
  const { s } = setup();
  const id = (await upload(s, s.staff)).done.video.id;
  const all = await get(s, '/vid/' + id);
  assert.equal(all.status, 200);
  assert.equal(all.headers.get('content-type'), 'video/mp4');
  assert.equal(all.headers.get('accept-ranges'), 'bytes');
  assert.equal(Buffer.from(await all.arrayBuffer()).length, MP4.length);
  const part = await get(s, '/vid/' + id, { range: 'bytes=4-11' });
  assert.equal(part.status, 206);
  assert.equal(part.headers.get('content-range'), 'bytes 4-11/' + MP4.length);
  assert.equal(Buffer.from(await part.arrayBuffer()).toString(), 'ftypisom');
  assert.equal((await get(s, '/vid/' + id, { range: 'bytes=99999-' })).status, 416);
  const pic = await get(s, '/vidthumb/' + id);
  assert.equal(pic.status, 200);
  assert.equal(pic.headers.get('content-type'), 'image/jpeg');
});

test('Videos is an open product with its own pages and second bar', () => {
  const p = PRODUCTS.find((x) => x.id === 'videos');
  assert.equal(p.status, 'open');
  assert.equal(p.href, '#/videos');
  assert.deepEqual(p.pages, ['videos', 'watch', 'videoupload']);
  assert.equal(activeLink('videos', '#/videos?sort=week'), 'videos-week');
  assert.equal(activeLink('videos', '#/videos?by=me'), 'videos-mine');
  assert.equal(activeLink('watch', '#/watch/vid-1'), 'videos');
});
