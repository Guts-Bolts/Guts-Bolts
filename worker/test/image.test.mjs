// G&B Image (worker/image.js): uploading pictures, the staff check, public and unlisted links,
// renaming, deleting, reports, the /imgpic address, and Image being its own product on the site.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { makeServer } from './helpers.mjs';
import { kMostBytes, kPerDay } from '../image.js';
import { PRODUCTS, GROUPS, productOf, activeLink } from '../../website/app/products.js';
import { filterText, fitCrop } from '../../website/app/image.js';

const PNG = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==';
const TEXT = Buffer.from('hello, I am not a picture at all').toString('base64');

function setup() {
  const s = makeServer();
  return { s, ann: s.signUp('ann'), bob: s.signUp('bob') };
}
const up = (s, who, extra = {}) => s.call(who, 'image.upload', Object.assign({ title: 'My screenshot', data: PNG, w: 1, h: 1 }, extra));

test('uploading: a real picture is needed, guests can\'t, odd choices are tidied', () => {
  const { s, ann } = setup();
  assert.match(up(s, ann, { data: TEXT }).error, /\.png, \.jpg, \.gif or \.webp/);
  const big = Buffer.alloc(kMostBytes + 10); Buffer.from(PNG, 'base64').copy(big);
  assert.match(up(s, ann, { data: big.toString('base64') }).error, /too big/);
  assert.match(s.call(s.user('c'.repeat(64)), 'image.upload', { data: PNG }).error, /Sign up/);
  const r = up(s, ann, { title: '', visibility: 'secret', w: -5, h: 99999999 });
  assert.ok(r.ok, r.error);
  assert.equal(r.image.title, 'Untitled');
  assert.equal(r.image.visibility, 'public');
  assert.equal(r.image.type, 'png');
  assert.equal(r.image.w, 0);
  assert.equal(r.image.h, 20000);
  assert.match(r.image.id, /^im-[0-9a-f]{12}$/, 'long random ids, so unlisted links can\'t be guessed');
});

test('pictures from people who aren\'t Verified wait for a staff check', () => {
  const { s, ann, bob } = setup();
  const r = up(s, ann);
  assert.equal(r.image.review, 'pending');
  const id = r.image.id;
  assert.equal(s.call(bob, 'image.list', {}).images.length, 0, 'not in the list yet');
  assert.equal(s.call(bob, 'image.get', { id }).ok, false, 'others can\'t open it');
  assert.equal(s.imagePictureResponse(id).status, 404, 'and its link shows nothing yet');
  assert.ok(s.call(ann, 'image.get', { id }).ok, 'the uploader can');
  assert.ok(s.call(ann, 'image.picture', { id }).data, 'and get the picture through a signed request');
  assert.match(s.call(ann, 'image.review', { id, ok: true }).error, /Only staff/);
  assert.deepEqual(s.call(s.staff, 'image.pending', {}).images.map((m) => m.id), [id]);
  assert.ok(s.call(s.staff, 'image.review', { id, ok: true }).ok);
  assert.ok(ann.notes.some((n) => n.kind === 'image' && /passed the staff check/.test(n.text)));
  assert.equal(s.call(bob, 'image.list', {}).images.length, 1, 'now everyone sees it');
  const no = up(s, ann, { title: 'Second' }).image.id;
  assert.ok(s.call(s.staff, 'image.review', { id: no, ok: false, note: 'Not yours to share' }).ok);
  assert.ok(ann.notes.some((n) => /didn't pass the staff check: Not yours to share/.test(n.text)));
  assert.equal(s.call(ann, 'image.get', { id: no }).image.reviewNote, 'Not yours to share');
});

test('unlisted pictures open with their link but stay out of the list; renaming and deleting', () => {
  const { s, ann, bob } = setup();
  const id = up(s, s.staff, { visibility: 'unlisted' }).image.id;
  assert.equal(s.call(bob, 'image.list', {}).images.length, 0);
  const r = s.call(bob, 'image.get', { id });
  assert.ok(r.ok);
  assert.equal(r.image.views, 1);
  assert.equal(r.image.canDelete, false);
  assert.match(s.call(bob, 'image.edit', { id, title: 'mine now' }).error, /Only the person who uploaded/);
  const e = s.call(s.staff, 'image.edit', { id, title: 'Logo', visibility: 'public' });
  assert.equal(e.image.title, 'Logo');
  assert.equal(s.call(bob, 'image.list', {}).images.length, 1, 'public now');
  assert.equal(s.call(bob, 'image.list', { by: 'ann' }).images.length, 0, 'by person');
  assert.match(s.call(bob, 'image.delete', { id }).error, /Only the person who uploaded/);
  up(s, ann);
  assert.equal(s.call(ann, 'image.mine', {}).images.length, 1, 'your own list has waiting ones too');
  assert.ok(s.call(s.staff, 'image.delete', { id }).ok);
  assert.equal(s.readFile('image:' + id), null, 'the picture is gone too');
});

test('/imgpic serves the picture with safe headers; a missing preview falls back to the picture', async () => {
  const { s } = setup();
  const id = up(s, s.staff).image.id;
  const r = s.imagePictureResponse(id);
  assert.equal(r.status, 200);
  assert.equal(r.headers.get('content-type'), 'image/png');
  assert.equal(r.headers.get('x-content-type-options'), 'nosniff');
  assert.deepEqual(new Uint8Array(await r.arrayBuffer()), new Uint8Array(Buffer.from(PNG, 'base64')));
  assert.equal(s.imagePictureResponse(id + '.png').status, 200, 'links ending in .png work too');
  assert.equal(s.imagePictureResponse(id + '/thumb').status, 200);
  assert.equal(s.imagePictureResponse('im-nope').status, 404);
});

test('reports, and a daily limit', () => {
  const { s, ann, bob } = setup();
  const id = up(s, s.staff).image.id;
  assert.ok(s.call(bob, 'report.send', { kind: 'image', id, reason: 'harassment' }).ok);
  assert.match(s.call(s.staff, 'report.send', { kind: 'image', id, reason: 'harassment' }).error, /yourself/);
  for (let i = 0; i < kPerDay; i++) assert.ok(up(s, ann).ok);
  assert.match(up(s, ann).error, /Come back tomorrow/);
});

test('the editor\'s sums: colour filters and crop boxes', () => {
  assert.equal(filterText({ brightness: 100, contrast: 100, saturate: 100, blur: 0 }), 'none');
  assert.equal(filterText({ brightness: 120, contrast: 100, saturate: 100, blur: 0, gray: true }), 'brightness(120%) grayscale(100%)');
  assert.deepEqual(fitCrop({ x: 50, y: 50, w: -40, h: -20 }, 0, 100, 100), { x: 10, y: 30, w: 40, h: 20 }, 'dragging up and left works');
  assert.deepEqual(fitCrop({ x: 0, y: 0, w: 200, h: 50 }, 1, 100, 100), { x: 0, y: 0, w: 50, h: 50 }, 'square, inside the picture');
  const wide = fitCrop({ x: 0, y: 0, w: 160, h: 160 }, 16 / 9, 160, 90);
  assert.ok(Math.abs(wide.w / wide.h - 16 / 9) < 0.05 && wide.w <= 160 && wide.h <= 90);
});

test('Image is its own product on the site, in the Creation group', () => {
  const p = PRODUCTS.find((x) => x.id === 'image');
  assert.equal(p.status, 'open');
  assert.equal(productOf('imageview').id, 'image');
  assert.equal(activeLink('image', '#/image?by=me'), 'image-mine');
  assert.equal(activeLink('imageview', '#/imageview/x'), 'image');
  const g = GROUPS.find((x) => x.id === 'creation');
  assert.ok(g.products.includes('image'));
  assert.ok(!g.later.includes('Image'), 'not "on the way" any more');
  const page = readFileSync(new URL('../../website/app/index.html', import.meta.url), 'utf8');
  assert.match(page, /href="image\.css"/);
  const server = readFileSync(new URL('../server.js', import.meta.url), 'utf8');
  assert.match(server, /op === 'image\.upload'/, 'Fyxa looks at new pictures');
  const index = readFileSync(new URL('../index.js', import.meta.url), 'utf8');
  assert.match(index, /imgpic/, 'the C++ server\'s pictures are passed through too');
});
