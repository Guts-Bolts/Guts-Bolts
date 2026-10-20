// Item pictures (thumb.get): a T-shirt or decal waiting for a staff check, and a private
// model, only show their picture to their creator and staff, like the item itself.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

function withThumb(s, maker, name, extra) {
  const a = s.addGame(maker, name, Object.assign({ kind: 'tshirt', thumb: 1 }, extra));
  s.writeFile('thumb:' + a.id, new Uint8Array([0x89, 0x50, 0x4e, 0x47, 1, 2, 3, 4, 5]));
  s.flush();
  return a;
}

test('a picture waiting for a staff check is only shown to its creator and staff', () => {
  const s = makeServer();
  const maker = s.signUp('Maker'), other = s.signUp('Other');
  const tee = withThumb(s, maker, 'Unchecked Tee', { review: 'pending' });
  assert.equal(s.call(other, 'thumb.get', { id: tee.id }).ok, false);
  assert.ok(s.call(maker, 'thumb.get', { id: tee.id }).data);
  assert.ok(s.call(s.staff, 'thumb.get', { id: tee.id }).data);
  // Passed: everyone sees it.
  assert.ok(s.call(s.staff, 'admin.review', { id: tee.id, ok: true }).ok);
  assert.ok(s.call(other, 'thumb.get', { id: tee.id }).data);
});

test('a private model\'s picture is only shown to its creator', () => {
  const s = makeServer();
  const maker = s.signUp('Builder'), other = s.signUp('Peeker');
  const model = withThumb(s, maker, 'Secret Base', { kind: 'model', access: 'private' });
  assert.equal(s.call(other, 'thumb.get', { id: model.id }).ok, false);
  assert.ok(s.call(maker, 'thumb.get', { id: model.id }).data);
  assert.ok(s.call(maker, 'model.access', { id: model.id, access: 'public' }).ok);
  assert.ok(s.call(other, 'thumb.get', { id: model.id }).data);
});
