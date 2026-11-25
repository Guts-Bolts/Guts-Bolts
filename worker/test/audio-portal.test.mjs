// The Audio portal (worker/audio.js): sounds are Library audio assets, with the portal's own
// category, genre, tags, plays, faves and comments; /sound/<id> plays them; Google-readable pages.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { makeServer } from './helpers.mjs';
import { audioTags, soundResponse, AUDIO_CATS, kPageSize } from '../audio.js';
import { build, sitemap, SEO_OPS } from '../seo.js';
import { PRODUCTS, GROUPS, productOf, activeLink } from '../../website/app/products.js';
import { peaksOf, audioTime } from '../../website/app/audio.js';

const SOUND = Buffer.from('ID3 pretend this is an mp3 file with some sound in it').toString('base64');

function setup() {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  return { s, ann, bob };
}
// Upload a sound like the portal's upload page does: 'upload', then 'audio.tag'.
function upload(s, who, extra = {}, tags = {}) {
  const r = s.call(who, 'upload', Object.assign({ kind: 'audio', name: 'Boss Battle', description: 'A loud one', data: SOUND,
    meta: { ext: 'mp3' }, access: 'public' }, extra));
  assert.ok(r.ok, r.error);
  const t = s.call(who, 'audio.tag', Object.assign({ id: r.asset.id, cat: 'music', genre: 'chiptune', tags: 'boss, 8bit #boss' }, tags));
  assert.ok(t.ok, t.error);
  return t.track;
}

test('tags are tidied, and peaks and times for the website', () => {
  assert.deepEqual(audioTags('Boss, 8bit #boss  big_bang'), ['boss', '8bit', 'bigbang']);
  assert.equal(audioTags(Array.from({ length: 20 }, (_, i) => 't' + i)).length, 8);
  assert.deepEqual(peaksOf([Float32Array.from([0, 0.5, 0, -1])], 2), [0.5, 1]);
  assert.deepEqual(peaksOf([], 3), [0, 0, 0]);
  assert.equal(audioTime(75), '1:15');
  assert.ok(AUDIO_CATS.includes('sfx'));
});

test('an uploaded public sound shows up in the portal, with its ID for games', () => {
  const { s, bob } = setup();
  const t = upload(s, s.staff);
  assert.equal(t.cat, 'music');
  assert.equal(t.genre, 'chiptune');
  assert.deepEqual(t.tags, ['boss', '8bit']);
  assert.ok(t.num > 0, 'it has a number games can use');
  const list = s.call(bob, 'audio.list', {});
  assert.deepEqual(list.tracks.map((x) => x.id), [t.id]);
  assert.equal(s.call(bob, 'audio.get', { id: String(t.num) }).track.id, t.id, 'found by its number too');
  // Genres only go with music and loops.
  assert.equal(s.call(s.staff, 'audio.tag', { id: t.id, cat: 'sfx', genre: 'rock' }).track.genre, '');
  assert.match(s.call(bob, 'audio.tag', { id: t.id, cat: 'sfx' }).error, /Only the person/);
});

test('older sounds without portal bits show up too, as "other"', () => {
  const { s, bob } = setup();
  const r = s.call(s.staff, 'upload', { kind: 'audio', name: 'Old Door', data: SOUND, meta: { ext: 'wav' }, access: 'public' });
  const list = s.call(bob, 'audio.list', {});
  assert.equal(list.tracks[0].id, r.asset.id);
  assert.equal(list.tracks[0].cat, 'other');
});

test('private sounds and ones waiting for a staff check stay out of the lists', () => {
  const { s, ann, bob } = setup();
  const waiting = upload(s, ann);
  assert.equal(waiting.review, 'pending');
  const hidden = upload(s, s.staff, { name: 'Secret', access: 'private' });
  assert.equal(s.call(bob, 'audio.list', {}).total, 0);
  assert.equal(s.call(bob, 'audio.get', { id: waiting.id }).ok, false);
  assert.equal(s.call(bob, 'audio.get', { id: hidden.id }).ok, false);
  assert.match(s.call(bob, 'audio.fave', { id: waiting.id }).error, /isn't here/);
  // The maker sees their own in "My audio".
  assert.equal(s.call(ann, 'audio.list', { by: ann.userId }).total, 1);
  assert.equal(s.call(bob, 'audio.list', { by: ann.userId }).total, 0);
  // Waiting sounds don't play by /sound/ yet; private ones do (games that use them keep working).
  assert.equal(s.call(bob, 'audio.file', { id: waiting.id }).ok, false);
  assert.ok(s.audioOp('audio.file', null, { id: hidden.id }).ok);
});

test('lists: categories, genres, tags, search, sorts and pages', () => {
  const { s, ann, bob } = setup();
  const a = upload(s, s.staff, { name: 'Laser zap' }, { cat: 'sfx', tags: 'laser' });
  const b = upload(s, s.staff, { name: 'Night drive' }, { cat: 'music', genre: 'electronic', tags: 'synth' });
  const c = upload(s, s.staff, { name: 'Castle theme' }, { cat: 'music', genre: 'orchestral' });
  assert.deepEqual(s.call(bob, 'audio.list', { cat: 'sfx' }).tracks.map((x) => x.id), [a.id]);
  assert.deepEqual(s.call(bob, 'audio.list', { genre: 'electronic' }).tracks.map((x) => x.id), [b.id]);
  assert.deepEqual(s.call(bob, 'audio.list', { tag: 'synth' }).tracks.map((x) => x.id), [b.id]);
  assert.deepEqual(s.call(bob, 'audio.list', { q: 'castle' }).tracks.map((x) => x.id), [c.id]);
  s.call(ann, 'audio.fave', { id: a.id });
  s.call(bob, 'audio.fave', { id: a.id });
  s.call(bob, 'audio.fave', { id: b.id });
  assert.deepEqual(s.call(bob, 'audio.list', { sort: 'top' }).tracks.map((x) => x.id), [a.id, b.id, c.id]);
  s.call(ann, 'audio.listen', { id: c.id });
  s.call(bob, 'audio.listen', { id: c.id });
  assert.equal(s.call(bob, 'audio.list', { sort: 'plays' }).tracks[0].id, c.id);
  assert.match(s.call(bob, 'audio.list', { by: 'nobody-like-this' }).error, /nobody/);
  assert.ok(kPageSize >= 10);
});

test('plays: once per person per sound a day, and not the maker', () => {
  const { s, ann, bob } = setup();
  const t = upload(s, s.staff);
  assert.equal(s.call(bob, 'audio.listen', { id: t.id }).plays, 1);
  assert.equal(s.call(bob, 'audio.listen', { id: t.id }).plays, 1, 'again the same day doesn\'t count');
  assert.equal(s.call(ann, 'audio.listen', { id: t.id }).plays, 2);
  assert.equal(s.call(s.staff, 'audio.listen', { id: t.id }).plays, 2, 'the maker\'s own don\'t count');
  bob.audioHeard.day -= 1;   // (tomorrow)
  assert.equal(s.call(bob, 'audio.listen', { id: t.id }).plays, 3);
  const guest = s.user('c'.repeat(64));
  assert.equal(s.call(guest, 'audio.listen', { id: t.id }).plays, 4, 'guests can listen too');
  assert.match(s.call(guest, 'audio.fave', { id: t.id }).error, /Sign up/);
});

test('faves, comments (the maker is told), deleting comments, reports', () => {
  const { s, ann, bob } = setup();
  const t = upload(s, s.staff);
  assert.deepEqual(s.call(ann, 'audio.fave', { id: t.id }), { ok: true, faves: 1, faved: true });
  assert.equal(s.call(ann, 'audio.get', { id: t.id }).track.faved, true);
  assert.deepEqual(s.call(ann, 'audio.fave', { id: t.id, on: false }), { ok: true, faves: 0, faved: false });
  assert.match(s.call(ann, 'audio.comment', { id: t.id, text: '   ' }).error, /Write something/);
  const r = s.call(ann, 'audio.comment', { id: t.id, text: 'Great loop!' });
  assert.ok(r.ok, r.error);
  assert.ok(s.staff.notes.some((n) => n.kind === 'audio' && n.about === t.id && /ann commented on your sound/.test(n.text)));
  const cid = r.track.commentList[0].id;
  assert.equal(r.track.commentList[0].canDelete, true);
  assert.match(s.call(bob, 'audio.uncomment', { id: t.id, comment: cid }).error, /only delete your own/);
  // Reports: the sound, and a comment under it.
  const rep = s.call(bob, 'report.send', { kind: 'audio', id: t.id + ':' + cid, reason: 'spam', text: 'meh' });
  assert.ok(rep.ok, rep.error);
  assert.match(s.call(ann, 'report.send', { kind: 'audio', id: t.id + ':' + cid, reason: 'spam' }).error, /yourself/);
  assert.ok(s.call(bob, 'report.send', { kind: 'audio', id: t.id, reason: 'spam' }).ok);
  assert.ok(s.call(s.staff, 'audio.uncomment', { id: t.id, comment: cid }).ok, 'the maker (or staff) can delete it');
  assert.equal(s.call(bob, 'audio.get', { id: t.id }).track.commentList.length, 0);
});

test('/sound/<id> plays the file, and jumps around with Range', async () => {
  const { s, ann } = setup();
  const t = upload(s, s.staff);
  const whole = await s.fetch(new Request('https://x/sound/' + t.id));
  assert.equal(whole.status, 200);
  assert.equal(whole.headers.get('content-type'), 'audio/mpeg');
  assert.equal(whole.headers.get('accept-ranges'), 'bytes');
  assert.equal(Buffer.from(await whole.arrayBuffer()).toString('base64'), SOUND);
  const part = await s.fetch(new Request('https://x/sound/' + t.num, { headers: { range: 'bytes=0-2' } }));
  assert.equal(part.status, 206);
  assert.equal(Buffer.from(await part.arrayBuffer()).toString(), 'ID3');
  assert.match(part.headers.get('content-range'), /^bytes 0-2\/\d+$/);
  const waiting = upload(s, ann);
  assert.equal((await s.fetch(new Request('https://x/sound/' + waiting.id))).status, 404);
  assert.equal(soundResponse(new Request('https://x', { headers: { range: 'bytes=99-' } }), new Uint8Array(5), 'mp3').status, 416);
});

test('Google-readable pages: /audio, /audio/<id>, and the sitemap', async () => {
  const { s } = setup();
  const t = upload(s, s.staff, { name: 'Night Drive', description: 'Synthwave for racing games' });
  const ask = async (req) => s.seoAsk(req);
  assert.ok(SEO_OPS.has('audio.get') && SEO_OPS.has('audio.list'));
  const list = await build(ask, '/audio');
  assert.equal(list.status, 200);
  assert.match(list.html, /Night Drive/);
  const page = await build(ask, '/audio/' + t.id);
  assert.equal(page.status, 200);
  assert.match(page.html, /<audio[^>]+src="\/sound\//);
  assert.match(page.html, /MusicRecording/);
  assert.match(page.html, /Synthwave for racing games/);
  assert.match(page.html, new RegExp('Sound ID</dt><dd>' + t.num));
  const hidden = upload(s, s.staff, { name: 'Secret', access: 'private' });
  assert.equal((await build(ask, '/audio/' + hidden.id)).status, 404);
  assert.match(await sitemap(ask), new RegExp('/audio/' + t.id));
});

test('Audio is an open product, with its own pages and second bar', () => {
  const p = PRODUCTS.find((x) => x.id === 'audio');
  assert.equal(p.status, 'open');
  assert.equal(productOf('track').id, 'audio');
  assert.equal(productOf('audioupload').id, 'audio');
  assert.ok(GROUPS.find((g) => g.id === 'media').products.includes('audio'));
  assert.ok(!GROUPS.find((g) => g.id === 'media').later.includes('Audio'));
  assert.equal(activeLink('audio', '#/audio?sort=plays'), 'audio-plays');
  assert.equal(activeLink('audio', '#/audio?cat=sfx'), 'audio-sfx');
  assert.equal(activeLink('track', '#/track/x'), 'audio');
  const html = readFileSync(new URL('../../website/app/index.html', import.meta.url), 'utf8');
  assert.match(html, /href="#\/audio" data-product="audio"/);
  assert.match(html, /audio\.css/);
});
