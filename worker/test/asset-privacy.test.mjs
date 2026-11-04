// Asset privacy: uploads start private (the creator ticks "Public"), private sounds and decals
// stay out of the Library, and the inventory is sorted like the catalog (hats, hair, faces,
// heads, body parts, bundles, games, audio, models...).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const sound = Buffer.from('ID3 some sound').toString('base64');
const png = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0]).toString('base64');
const upAudio = (s, me, extra = {}) => s.call(me, 'upload', Object.assign({ kind: 'audio', name: 'Boss Theme', data: sound, meta: { ext: 'mp3' } }, extra));
const inLibrary = (s, who, kind, id) => s.call(who, 'list', { kind, limit: 100 }).assets.some((a) => a.id === id);

test('a new sound is private unless "Public" is ticked', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const quiet = upAudio(s, s.staff);
  assert.ok(quiet.ok, quiet.error);
  assert.equal(quiet.asset.access, 'private');
  const loud = upAudio(s, s.staff, { name: 'Lobby Music', access: 'public' });
  assert.equal(loud.asset.access, 'public');
  assert.ok(!inLibrary(s, bob, 'audio', quiet.asset.id), 'private sounds are not in the Library');
  assert.ok(inLibrary(s, bob, 'audio', loud.asset.id), 'public ones are');
  assert.ok(inLibrary(s, s.staff, 'audio', quiet.asset.id), 'the creator still sees their own');
  assert.equal(s.call(bob, 'asset.info', { id: quiet.asset.id }).ok, false, 'its page is hidden from others');
  const got = s.call(bob, 'get', { id: quiet.asset.id });
  assert.ok(got.ok, 'a game that uses its ID can still play it: ' + got.error);
});

test('decals start private too, and models and animations follow the same rule', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const d = s.call(s.staff, 'upload', { kind: 'decal', name: 'Sign', data: png });
  assert.ok(d.ok, d.error);
  assert.equal(d.asset.access, 'private');
  assert.ok(!inLibrary(s, bob, 'decal', d.asset.id));
  const m = s.call(s.staff, 'upload', { kind: 'model', name: 'Crate', data: Buffer.from('{"nodes":[]}').toString('base64') });
  assert.ok(m.ok, m.error);
  assert.equal(m.asset.access, 'private', 'no tick means private');
});

test('the creator can make a sound public later, and back', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const a = upAudio(s, s.staff).asset;
  assert.equal(s.call(bob, 'model.access', { id: a.id, access: 'public' }).ok, false, 'only the creator can');
  let r = s.call(s.staff, 'model.access', { id: a.id, access: 'public' });
  assert.ok(r.ok, r.error);
  assert.ok(inLibrary(s, bob, 'audio', a.id));
  r = s.call(s.staff, 'model.access', { id: a.id, access: 'private' });
  assert.ok(r.ok, r.error);
  assert.ok(!inLibrary(s, bob, 'audio', a.id));
});

test('sounds uploaded before privacy existed stay public', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const old = s.addGame(s.staff, 'Old Song', { kind: 'audio', access: undefined });
  delete old.access;
  assert.ok(inLibrary(s, bob, 'audio', old.id));
  assert.equal(s.call(bob, 'asset.info', { id: old.id }).asset.access, 'public');
});

test('the inventory is sorted into hats, hair, heads, body parts, games, audio and more', () => {
  const s = makeServer();
  const bob = s.signUp('bob'), amy = s.signUp('amy');
  const give = (extra) => { const a = s.addGame(s.staff, extra.name || 'x', extra); bob.owned.push(a.id); return a; };
  const hat = give({ kind: 'hat', name: 'Top Hat' }), hair = give({ kind: 'hair', name: 'Spiky' });
  const head = give({ kind: 'bodypart', name: 'Big Head', meta: { bodyPart: 'Head' } });
  const arm = give({ kind: 'bodypart', name: 'Robot Arm', meta: { bodyPart: 'Left Arm' } });
  const game = s.addGame(bob, 'Obby'); bob.owned.push(game.id);
  const pub = upAudio(s, bob, { access: 'public' }), priv = upAudio(s, bob, { name: 'Secret' });
  assert.ok(pub.ok && priv.ok, pub.error || priv.error);
  for (const r of [pub, priv]) delete s.assets.get(r.asset.id).review;   // (as if staff passed their check)
  s.saveUser(bob); s.flush();
  const inv = (who, cat) => s.call(who, 'profile.inventory', { id: String(bob.userId), cat });
  const ids = (r) => r.items.map((a) => a.id);
  assert.deepEqual(ids(inv(amy, 'hats')), [hat.id]);
  assert.deepEqual(ids(inv(amy, 'hair')), [hair.id]);
  assert.deepEqual(ids(inv(amy, 'heads')), [head.id]);
  assert.deepEqual(ids(inv(amy, 'bodyparts')), [arm.id]);
  assert.deepEqual(ids(inv(amy, 'games')), [game.id]);
  assert.equal(inv(amy, 'accessories').total, 2, 'hats and hair are both accessories');
  assert.equal(inv(amy, 'body').total, 2);
  // Their private sound only shows to them.
  assert.deepEqual(ids(inv(amy, 'audio')), [pub.asset.id]);
  assert.deepEqual(new Set(ids(inv(bob, 'audio'))), new Set([pub.asset.id, priv.asset.id]));
  const all = inv(amy, 'all');
  assert.equal(all.counts.audio, 1);
  assert.equal(all.counts.creations, 2, 'a game and a public sound');
  assert.ok(!ids(all).includes(priv.asset.id));
});
