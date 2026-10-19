// Emotes and animation packs in the catalog: the starter ones are always there, owners
// equip up to 8 emotes into slots and one pack, and only staff make new ones.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const b64 = (obj) => Buffer.from(JSON.stringify(obj)).toString('base64');
const clip = (loop = false) => ({ loop, priority: 'Action', keys: [
  { t: 0, poses: { 'Right Arm': { pos: [0, 0, 0], rot: [0, 0, 0] } } },
  { t: 0.5, poses: { 'Right Arm': { pos: [0, 0, 0], rot: [-150, 0, 30] } } }] });

test('the starter emotes and animation packs are in the catalog, with pictures', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const r = s.call(bob, 'list', { kind: 'clothing', limit: 100 });
  assert.ok(r.ok, r.error);
  const byId = Object.fromEntries(r.assets.map((a) => [a.id, a]));
  for (const [id, kind, price] of [['emote-salute', 'emote', 0], ['emote-shrug', 'emote', 0], ['emote-robot', 'emote', 0],
    ['emote-jumping-jacks', 'emote', 15], ['emote-sprinkler', 'emote', 25], ['pack-bouncy', 'animpack', 0], ['pack-robot', 'animpack', 40]]) {
    const a = byId[id];
    assert.ok(a, id + ' is in the catalog');
    assert.equal(a.kind, kind);
    assert.equal(a.price, price, id + ' costs ' + price);
    assert.equal(a.creator, s.staff.id, 'Guts made it');
    assert.ok(a.thumb > 0, id + ' has a picture');
    const pic = s.call(bob, 'thumb.get', { id });
    assert.ok(pic.ok && Buffer.from(pic.data, 'base64').subarray(1, 4).toString() === 'PNG', id + "'s picture is a PNG");
  }
  // Their files are real keyframes.
  const got = s.call(bob, 'get', { id: 'emote-salute' });
  assert.ok(got.ok, got.error);
  const file = JSON.parse(Buffer.from(got.data, 'base64').toString());
  assert.equal(file.format, 'gbanim');
  assert.ok(file.clip.keys.length >= 2);
  const pack = JSON.parse(Buffer.from(s.call(bob, 'get', { id: 'pack-bouncy' }).data, 'base64').toString());
  assert.equal(pack.format, 'gbanimpack');
  assert.deepEqual(Object.keys(pack.clips).sort(), ['fall', 'idle', 'jump', 'run', 'walk']);
});

test('equipping emotes into slots and an animation pack', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  assert.deepEqual(s.meJson(bob).emotes, ['', '', '', '', '', '', '', '']);
  assert.equal(s.meJson(bob).animPack, '');
  // Not yours yet.
  let r = s.call(bob, 'emotes.equip', { slot: 1, id: 'emote-salute' });
  assert.equal(r.ok, false);
  assert.match(r.error, /catalog first/);
  assert.ok(s.call(bob, 'buy', { id: 'emote-salute' }).ok);
  assert.ok(s.call(bob, 'buy', { id: 'emote-jumping-jacks' }).ok);
  assert.equal(s.balance(bob), 85, 'Jumping Jacks cost 15 Bolts');
  r = s.call(bob, 'emotes.equip', { slot: 3, id: 'emote-salute' });
  assert.ok(r.ok, r.error);
  assert.equal(r.me.emotes[2], 'emote-salute');
  r = s.call(bob, 'emotes.equip', { slot: 1, id: 'emote-jumping-jacks' });
  assert.deepEqual(r.me.emotes.slice(0, 3), ['emote-jumping-jacks', '', 'emote-salute']);
  // The same emote in another slot moves there.
  r = s.call(bob, 'emotes.equip', { slot: 8, id: 'emote-salute' });
  assert.deepEqual(r.me.emotes, ['emote-jumping-jacks', '', '', '', '', '', '', 'emote-salute']);
  // Emptying a slot, and slots that don't exist.
  r = s.call(bob, 'emotes.equip', { slot: 1, id: '' });
  assert.equal(r.me.emotes[0], '');
  for (const slot of [0, 9, -1]) assert.equal(s.call(bob, 'emotes.equip', { slot, id: 'emote-salute' }).ok, false, 'slot ' + slot);
  // Only emotes go in emote slots.
  assert.ok(s.call(bob, 'buy', { id: 'pack-bouncy' }).ok);
  assert.equal(s.call(bob, 'emotes.equip', { slot: 2, id: 'pack-bouncy' }).ok, false);
  // One animation pack at a time.
  assert.equal(s.call(bob, 'animpack.equip', { id: 'pack-robot' }).ok, false, "you don't own the Robot pack");
  assert.equal(s.call(bob, 'animpack.equip', { id: 'emote-salute' }).ok, false, "an emote isn't a pack");
  r = s.call(bob, 'animpack.equip', { id: 'pack-bouncy' });
  assert.ok(r.ok, r.error);
  assert.equal(r.me.animPack, 'pack-bouncy');
  r = s.call(bob, 'animpack.equip', { id: '' });
  assert.equal(r.me.animPack, '');
  // They show up in the Emotes group of the inventory.
  const inv = s.call(bob, 'profile.inventory', { id: String(bob.userId), cat: 'emotes' });
  assert.ok(inv.ok, inv.error);
  assert.equal(inv.total, 3);
  assert.equal(inv.counts.emotes, 3);
});

test('an emote you no longer own leaves its slot', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  s.call(bob, 'buy', { id: 'emote-robot' });
  s.call(bob, 'emotes.equip', { slot: 2, id: 'emote-robot' });
  s.call(bob, 'buy', { id: 'pack-bouncy' });
  s.call(bob, 'animpack.equip', { id: 'pack-bouncy' });
  bob.owned = bob.owned.filter((id) => id !== 'emote-robot' && id !== 'pack-bouncy');
  assert.equal(s.meJson(bob).emotes[1], '');
  assert.equal(s.meJson(bob).animPack, '');
});

test('only staff make emotes and animation packs, and they must be real keyframes', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const emote = b64({ format: 'gbanim', clip: clip(true) });
  let r = s.call(bob, 'upload', { kind: 'emote', name: 'Floss', data: emote });
  assert.equal(r.ok, false);
  assert.match(r.error, /staff/);
  for (const bad of [{ format: 'gbmodel' }, { format: 'gbanim', clip: { keys: [] } }, { format: 'gbanim', clip: { keys: [{ t: 99, poses: {} }] } }]) {
    r = s.call(s.staff, 'upload', { kind: 'emote', name: 'Bad', data: b64(bad) });
    assert.equal(r.ok, false, JSON.stringify(bad));
  }
  r = s.call(s.staff, 'upload', { kind: 'emote', name: 'Big Wave', price: 10, data: emote });
  assert.ok(r.ok, r.error);
  assert.equal(r.asset.kind, 'emote');
  assert.equal(r.asset.price, 10);
  // Packs: only the five animations they replace.
  r = s.call(s.staff, 'upload', { kind: 'animpack', name: 'Odd Pack', data: b64({ format: 'gbanimpack', clips: { swim: clip() } }) });
  assert.equal(r.ok, false);
  assert.match(r.error, /idle, walk, run, jump and fall/);
  r = s.call(s.staff, 'upload', { kind: 'animpack', name: 'Wavy Pack', data: b64({ format: 'gbanimpack', clips: { idle: clip(true), walk: clip(true) } }) });
  assert.ok(r.ok, r.error);
  // Updating checks the keyframes again.
  assert.equal(s.call(s.staff, 'update', { id: r.asset.id, data: b64({ format: 'gbanimpack', clips: {} }) }).ok, false);
  // Staff give them a picture (a render of the move); nobody else can.
  const png = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0]).toString('base64');
  assert.ok(s.call(s.staff, 'thumb.set', { id: r.asset.id, data: png }).ok);
  assert.equal(s.call(bob, 'thumb.set', { id: r.asset.id, data: png }).ok, false);
  // ...but other catalog items still can't take pictures.
  assert.equal(s.call(s.staff, 'thumb.set', { id: 'item-verified-hat', data: png }).ok, false);
});
