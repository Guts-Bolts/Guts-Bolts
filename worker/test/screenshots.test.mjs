// Screenshots on game pages: up to 5 pictures (.png, .jpg or .webp, 2 MB each) that the
// game's creator adds, deletes and puts in order; anyone can see them.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const b64 = (bytes) => Buffer.from(bytes).toString('base64');
function png(extra = 64) {
  const d = new Uint8Array(24 + extra);
  d.set([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a], 0);
  new DataView(d.buffer).setUint32(16, 1280);
  new DataView(d.buffer).setUint32(20, 720);
  return d;
}
const jpg = () => new Uint8Array([0xff, 0xd8, 0xff, 0xe0, 1, 2, 3, 4, 5, 6]);
const webp = () => new Uint8Array([...Buffer.from('RIFF'), 20, 0, 0, 0, ...Buffer.from('WEBPVP8 '), 1, 2, 3, 4]);

test('a creator can add up to 5 screenshots, in .png, .jpg or .webp', () => {
  const s = makeServer();
  const ann = s.signUp('ann');
  const g = s.addGame(ann, 'Sky Race');
  for (const pic of [png(), jpg(), webp(), png(), jpg()]) {
    const r = s.call(ann, 'shots.add', { id: g.id, data: b64(pic) });
    assert.ok(r.ok, r.error);
  }
  const sixth = s.call(ann, 'shots.add', { id: g.id, data: b64(png()) });
  assert.equal(sixth.ok, false);
  assert.match(sixth.error, /5 screenshots/);
  const info = s.call(ann, 'list', { kind: 'game', limit: 100 }).assets.find((a) => a.id === g.id);
  assert.equal(info.shots.length, 5);
  assert.deepEqual(s.assets.get(g.id).shots.map((x) => x.type), ['png', 'jpg', 'webp', 'png', 'jpg']);
});

test('screenshots must be pictures and not too big, with clear messages', () => {
  const s = makeServer();
  const ann = s.signUp('ann');
  const g = s.addGame(ann, 'Sky Race');
  const gif = s.call(ann, 'shots.add', { id: g.id, data: b64(Buffer.from('GIF89a......')) });
  assert.equal(gif.ok, false);
  assert.match(gif.error, /\.png, \.jpg or \.webp/);
  const huge = s.call(ann, 'shots.add', { id: g.id, data: b64(png(2 * 1024 * 1024)) });
  assert.equal(huge.ok, false);
  assert.match(huge.error, /too big.*2 MB/);
  const scrambled = s.call(ann, 'shots.add', { id: g.id, data: '!!!not base64!!!' });
  assert.equal(scrambled.ok, false);
  assert.equal(s.assets.get(g.id).shots.length, 0);
});

test('only the creator (or staff) can change a game\'s screenshots', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), ben = s.signUp('ben');
  const g = s.addGame(ann, 'Sky Race');
  const r = s.call(ben, 'shots.add', { id: g.id, data: b64(png()) });
  assert.equal(r.ok, false);
  assert.match(r.error, /your own games/);
  const mine = s.call(ann, 'shots.add', { id: g.id, data: b64(png()) });
  assert.equal(s.call(ben, 'shots.delete', { id: g.id, shot: mine.shots[0] }).ok, false);
  // Staff can take one down (and it's in the staff log).
  assert.ok(s.call(s.staff, 'shots.delete', { id: g.id, shot: mine.shots[0] }).ok);
  assert.equal(s.assets.get(g.id).shots.length, 0);
  assert.ok(s.staffLog.some((x) => /screenshot/.test(x.text || '')));
  // Only games have screenshots.
  const model = s.addGame(ann, 'A Model', { kind: 'model' });
  assert.equal(s.call(ann, 'shots.add', { id: model.id, data: b64(png()) }).ok, false);
});

test('screenshots can be put in order and deleted', () => {
  const s = makeServer();
  const ann = s.signUp('ann');
  const g = s.addGame(ann, 'Sky Race');
  const ids = [png(), jpg(), webp()].map((p) => s.call(ann, 'shots.add', { id: g.id, data: b64(p) }).shots.at(-1));
  const moved = s.call(ann, 'shots.order', { id: g.id, order: [ids[2], ids[0], ids[1]] });
  assert.ok(moved.ok, moved.error);
  assert.deepEqual(moved.shots, [ids[2], ids[0], ids[1]]);
  // The order has to be exactly the screenshots it has.
  assert.equal(s.call(ann, 'shots.order', { id: g.id, order: [ids[0], ids[1]] }).ok, false);
  assert.equal(s.call(ann, 'shots.order', { id: g.id, order: [ids[0], ids[0], ids[1]] }).ok, false);
  assert.equal(s.call(ann, 'shots.order', { id: g.id, order: [ids[0], ids[1], 'nope'] }).ok, false);
  const del = s.call(ann, 'shots.delete', { id: g.id, shot: ids[0] });
  assert.ok(del.ok, del.error);
  assert.deepEqual(del.shots, [ids[2], ids[1]]);
  assert.equal(s.readFile('shot:' + g.id + ':' + ids[0]), null);   // the picture is gone too
  assert.match(s.call(ann, 'shots.delete', { id: g.id, shot: ids[0] }).error, /already gone/);
});

test('anyone can see screenshots, and deleting the game deletes them', async () => {
  const s = makeServer();
  const ann = s.signUp('ann');
  const g = s.addGame(ann, 'Sky Race');
  const pic = jpg();
  const id = s.call(ann, 'shots.add', { id: g.id, data: b64(pic) }).shots[0];
  const res = await s.fetch(new Request('https://gutsandbolts.net/shot/' + g.id + '/' + id));
  assert.equal(res.status, 200);
  assert.equal(res.headers.get('content-type'), 'image/jpeg');
  assert.deepEqual(new Uint8Array(await res.arrayBuffer()), pic);
  assert.equal((await s.fetch(new Request('https://gutsandbolts.net/shot/' + g.id + '/nope'))).status, 404);
  // And through a request (for the apps), even for someone who isn't signed up.
  const guest = s.user('c'.repeat(64));
  const got = s.call(guest, 'shots.get', { id: g.id, shot: id });
  assert.ok(got.ok, got.error);
  assert.equal(got.data, b64(pic));
  assert.ok(s.call(ann, 'delete', { id: g.id }).ok);
  assert.equal(s.readFile('shot:' + g.id + ':' + id), null);
  assert.equal((await s.fetch(new Request('https://gutsandbolts.net/shot/' + g.id + '/' + id))).status, 404);
});
