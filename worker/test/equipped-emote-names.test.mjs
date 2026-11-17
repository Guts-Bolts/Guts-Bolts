// The Player app names your equipped emotes from the server: a fresh download ('get')
// brings the name, and an emote file already on the computer asks 'asset.info' for it
// (src/player/PlayerFriends.cpp syncEmotes). Both must give the real name, for every
// emote you can equip, or the emote menu fills up with "Emote" placeholders.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test('every equipped emote\'s name comes back from both get and asset.info', () => {
  const s = makeServer();
  const bob = s.signUp('bob');
  const names = { 'emote-salute': 'Salute', 'emote-shrug': 'Shrug', 'emote-robot': 'Robot' };
  let slot = 1;
  for (const id of Object.keys(names)) {
    const bought = s.call(bob, 'buy', { id });
    assert.ok(bought.ok, bought.error);
    const r = s.call(bob, 'emotes.equip', { slot: slot++, id });
    assert.ok(r.ok, r.error);
  }
  const slots = s.meJson(bob).emotes;
  assert.deepEqual(slots.slice(0, 3), Object.keys(names));
  for (const id of slots.filter(Boolean)) {
    const got = s.call(bob, 'get', { id });
    assert.ok(got.ok, got.error);
    assert.equal(got.asset.name, names[id], 'a fresh download names ' + id);
    const info = s.call(bob, 'asset.info', { id });
    assert.ok(info.ok, info.error);
    assert.equal(info.asset.name, names[id], 'asset.info names ' + id + ' (for one already downloaded)');
  }
});
