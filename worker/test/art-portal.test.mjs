// The Art portal (worker/art.js): post a picture, staff check, gallery sorts, faves,
// comments, reports, and the /artpic picture address.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { makeServer } from './helpers.mjs';
import { artType, artTags, kPostsPerDay, kMostBytes } from '../art.js';
import { PRODUCTS, productOf, activeLink } from '../../website/app/products.js';

// A tiny real PNG (1x1), and a fake "picture" that is really text.
const PNG = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==';
const TEXT = Buffer.from('hello, I am not a picture at all').toString('base64');

function setup() {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  return { s, ann, bob };
}
const post = (s, who, extra = {}) => s.call(who, 'art.post', Object.assign({ title: 'Robot at sunset', cat: 'drawing', tags: 'robot, Sunset #robot', data: PNG }, extra));

test('pictures are checked by their bytes, and tags are tidied', () => {
  assert.equal(artType(Buffer.from(PNG, 'base64')), 'png');
  assert.equal(artType(Buffer.from(TEXT, 'base64')), '');
  assert.equal(artType(Buffer.from('GIF89a......', 'latin1')), 'gif');
  assert.deepEqual(artTags('robot, Sunset #robot  big_cat'), ['robot', 'sunset', 'bigcat']);
  assert.equal(artTags(Array.from({ length: 20 }, (_, i) => 't' + i)).length, 8);
});

test('posting: a real picture and a title are needed; guests can\'t post', () => {
  const { s, ann } = setup();
  assert.match(post(s, ann, { data: TEXT }).error, /\.png, \.jpg, \.gif or \.webp/);
  assert.match(post(s, ann, { title: '   ' }).error, /title/);
  const guest = s.user('c'.repeat(64));
  assert.match(s.call(guest, 'art.post', { title: 'x', data: PNG }).error, /Sign up/);
  const big = Buffer.alloc(kMostBytes + 10); Buffer.from(PNG, 'base64').copy(big);
  assert.match(post(s, ann, { data: big.toString('base64') }).error, /too big/);
  const r = post(s, ann);
  assert.ok(r.ok, r.error);
  assert.deepEqual(r.piece.tags, ['robot', 'sunset']);
  assert.equal(r.piece.type, 'png');
});

test('art from people who aren\'t Verified waits for a staff check', () => {
  const { s, ann, bob } = setup();
  const r = post(s, ann);
  assert.equal(r.piece.review, 'pending');
  const id = r.piece.id;
  assert.equal(s.call(bob, 'art.list', {}).pieces.length, 0, 'not in the gallery yet');
  assert.equal(s.call(bob, 'art.get', { id }).ok, false, 'others can\'t open it');
  assert.ok(s.call(ann, 'art.get', { id }).ok, 'the artist can');
  assert.equal(s.call(ann, 'art.list', { by: ann.userId }).pieces.length, 1, 'and sees it in their own list');
  assert.equal(s.call(bob, 'art.list', { by: ann.userId }).pieces.length, 0);
  assert.match(s.call(bob, 'art.fave', { id }).error, /isn't here/);
  assert.match(s.call(ann, 'art.review', { id, ok: true }).error, /Only staff/);
  assert.deepEqual(s.call(s.staff, 'art.pending', {}).pieces.map((p) => p.id), [id]);
  assert.ok(s.call(s.staff, 'art.review', { id, ok: true }).ok);
  assert.ok(ann.notes.some((n) => n.kind === 'art' && /passed the staff check/.test(n.text)));
  assert.equal(s.call(bob, 'art.list', {}).pieces.length, 1, 'now everyone sees it');
  // Turning one down tells the artist why.
  const no = post(s, ann, { title: 'Second' }).piece.id;
  assert.ok(s.call(s.staff, 'art.review', { id: no, ok: false, note: 'Not your own work' }).ok);
  assert.ok(ann.notes.some((n) => /didn't pass the staff check: Not your own work/.test(n.text)));
  assert.equal(s.call(ann, 'art.get', { id: no }).piece.reviewNote, 'Not your own work');
});

test('staff and Verified creators go straight to the gallery', () => {
  const { s } = setup();
  const r = post(s, s.staff);
  assert.equal(r.piece.review, undefined);
});

test('a daily limit for people who aren\'t Verified', () => {
  const { s, ann } = setup();
  for (let i = 0; i < kPostsPerDay; i++) assert.ok(post(s, ann, { title: 'n' + i }).ok);
  assert.match(post(s, ann).error, /Come back tomorrow/);
});

test('gallery: newest, most faved, categories, tags and search', () => {
  const { s, ann, bob } = setup();
  const a = post(s, s.staff, { title: 'Old castle', cat: 'pixel', tags: 'castle' }).piece.id;
  const b = post(s, s.staff, { title: 'New robot', cat: 'drawing', tags: 'robot' }).piece.id;
  s.art.get(a).at -= 100;   // (a is older)
  assert.deepEqual(s.call(bob, 'art.list', {}).pieces.map((p) => p.id), [b, a]);
  assert.ok(s.call(bob, 'art.fave', { id: a }).ok);
  assert.ok(s.call(ann, 'art.fave', { id: a }).ok);
  assert.ok(s.call(ann, 'art.fave', { id: a, on: true }).ok, 'faving twice counts once');
  assert.deepEqual(s.call(bob, 'art.list', { sort: 'top' }).pieces.map((p) => p.id), [a, b]);
  assert.equal(s.call(bob, 'art.list', { sort: 'top' }).pieces[0].faves, 2);
  assert.deepEqual(s.call(bob, 'art.list', { cat: 'pixel' }).pieces.map((p) => p.id), [a]);
  assert.deepEqual(s.call(bob, 'art.list', { tag: 'robot' }).pieces.map((p) => p.id), [b]);
  assert.deepEqual(s.call(bob, 'art.list', { q: 'castle' }).pieces.map((p) => p.id), [a]);
  s.art.get(a).at -= 8 * 86400;
  assert.deepEqual(s.call(bob, 'art.list', { sort: 'week' }).pieces.map((p) => p.id), [b], 'best this week is only this week');
  assert.ok(s.call(bob, 'art.fave', { id: a, on: false }).ok);
  assert.equal(s.call(bob, 'art.get', { id: a }).piece.faved, false);
});

test('comments tell the artist; the writer, artist or staff can delete them', () => {
  const { s, ann, bob } = setup();
  const id = post(s, s.staff).piece.id;
  assert.match(s.call(bob, 'art.comment', { id, text: '  ' }).error, /Write something/);
  const r = s.call(bob, 'art.comment', { id, text: 'Love the colours' });
  assert.ok(r.ok, r.error);
  assert.ok(s.staff.notes.some((n) => n.kind === 'art' && /bob commented on your art/.test(n.text)));
  const c = r.piece.commentList[0];
  assert.equal(c.text, 'Love the colours');
  assert.match(s.call(ann, 'art.uncomment', { id, comment: c.id }).error, /your own comments/);
  assert.ok(s.call(s.staff, 'art.uncomment', { id, comment: c.id }).ok, 'the artist (here staff) can');
  assert.equal(s.call(bob, 'art.get', { id }).piece.commentList.length, 0);
});

test('only the artist edits; the artist or staff delete', () => {
  const { s, ann, bob } = setup();
  const id = post(s, ann).piece.id;
  s.call(s.staff, 'art.review', { id, ok: true });
  assert.match(s.call(bob, 'art.edit', { id, title: 'Mine now' }).error, /Only the artist/);
  const e = s.call(ann, 'art.edit', { id, title: 'Robot at dawn', cat: 'painting', tags: 'robot dawn' });
  assert.ok(e.ok, e.error);
  assert.equal(e.piece.title, 'Robot at dawn');
  assert.match(s.call(bob, 'art.delete', { id }).error, /artist or staff/);
  assert.ok(s.call(ann, 'art.delete', { id }).ok);
  assert.equal(s.call(bob, 'art.get', { id }).ok, false);
  assert.equal(s.readFile('art:' + id), null, 'the picture is gone too');
});

test('art and its comments can be reported', () => {
  const { s, ann, bob } = setup();
  const id = post(s, s.staff).piece.id;
  const c = s.call(ann, 'art.comment', { id, text: 'meh' }).piece.commentList[0];
  const r = s.call(bob, 'report.send', { kind: 'art', id, reason: 'harassment', note: 'stolen art' });
  assert.ok(r.ok, r.error);
  assert.ok(s.call(bob, 'report.send', { kind: 'art', id: id + ':' + c.id, reason: 'harassment' }).ok, 'a comment too');
  assert.match(s.call(ann, 'report.send', { kind: 'art', id: id + ':' + c.id, reason: 'harassment' }).error, /yourself/);
  const list = s.call(s.staff, 'admin.reports', {});
  if (list.ok) {
    const mine = list.reports.filter((x) => x.kind === 'art');
    assert.ok(mine.length >= 1);
    assert.equal(mine[0].piece, id);
  }
});

test('/artpic serves the picture (and only once it passed the check)', async () => {
  const { s, ann } = setup();
  const id = post(s, ann).piece.id;
  const miss = s.artPictureResponse(id);
  assert.equal(miss.status, 404, 'waiting for a check: no picture for anyone');
  s.call(s.staff, 'art.review', { id, ok: true });
  const r = s.artPictureResponse(id);
  assert.equal(r.status, 200);
  assert.equal(r.headers.get('content-type'), 'image/png');
  assert.deepEqual(new Uint8Array(await r.arrayBuffer()), new Uint8Array(Buffer.from(PNG, 'base64')));
  assert.equal(s.artPictureResponse(id + '/thumb').status, 200, 'no preview sent: the full picture stands in');
});

test('Art is its own product on the site', () => {
  const art = PRODUCTS.find((p) => p.id === 'art');
  assert.equal(art.status, 'open');
  assert.equal(productOf('artpiece').id, 'art');
  assert.equal(activeLink('art', '#/art?sort=week'), 'art-week');
  assert.equal(activeLink('art', '#/art?by=me'), 'art-mine');
  assert.equal(activeLink('artpiece', '#/artpiece/x'), 'art');
  const page = readFileSync(new URL('../../website/app/index.html', import.meta.url), 'utf8');
  assert.match(page, /href="art\.css"/);
  const server = readFileSync(new URL('../server.js', import.meta.url), 'utf8');
  assert.match(server, /op === 'art\.post'/, 'Fyxa looks at new art');
});

test('every product sits in exactly one Home group', async () => {
  const { GROUPS } = await import('../../website/app/products.js');
  for (const p of PRODUCTS) assert.equal(GROUPS.filter((g) => g.products.includes(p.id)).length, 1, p.id);
  for (const g of GROUPS) for (const id of g.products) assert.ok(PRODUCTS.some((p) => p.id === id), id);
  assert.ok(GROUPS.find((g) => g.id === 'creation').products.includes('art'));
});
