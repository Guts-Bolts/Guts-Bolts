// Promotional text: a short line on a game's listing, separate from its name (worker/promotext.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { cleanPromo, kPromoLength, kPromoChangesPerDay } from '../promotext.js';

test('promotional text shows on the game, ends by itself, and only the creator sets it', () => {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  const game = s.addGame(ann, 'Candy Rush');
  const r = s.call(ann, 'game.promo', { game: game.id, text: '  Halloween event:\n2x candy!  ', days: 3 });
  assert.ok(r.ok, r.error);
  assert.equal(r.promo.text, 'Halloween event: 2x candy!', 'one line, spaces squeezed');
  const until = r.promo.until - Math.floor(Date.now() / 1000);
  assert.ok(until > 3 * 86400 - 10 && until <= 3 * 86400, 'three days');
  const info = s.call(bob, 'asset.info', { id: game.id });
  assert.equal((info.asset || info).promo.text, 'Halloween event: 2x candy!', 'everyone sees it');
  assert.match(s.call(bob, 'game.promo', { game: game.id, text: 'Mine now' }).error, /Only the game's creator/);
  assert.match(s.call(ann, 'game.promo', { game: game.id, text: 'candy rush' }).error, /already the game's name/);
  // Days are kept between 1 and 30.
  assert.ok(s.call(ann, 'game.promo', { game: game.id, text: 'Long one', days: 400 }).promo.until - Math.floor(Date.now() / 1000) <= 30 * 86400);
  // Old news goes away by itself.
  s.assets.get(game.id).promo.until = Math.floor(Date.now() / 1000) - 1;
  const after = s.call(bob, 'asset.info', { id: game.id });
  assert.equal((after.asset || after).promo, undefined);
  // Clearing takes it off.
  assert.equal(s.call(ann, 'game.promo', { game: game.id, text: '' }).promo, null);
});

test('promotional text is short, filtered, and can\'t flicker', () => {
  assert.equal(cleanPromo('x'.repeat(200)).length, kPromoLength);
  const s = makeServer();
  const ann = s.signUp('ann');
  const game = s.addGame(ann, 'Obby');
  assert.match(s.call(ann, 'game.promo', { game: game.id, text: 'join at http://example.com now' }).error, /aren't allowed/);
  let last;
  for (let i = 0; i < kPromoChangesPerDay + 1; i++) last = s.call(ann, 'game.promo', { game: game.id, text: 'Update ' + i });
  assert.match(last.error, /changes today/);
  assert.ok(s.call(s.staff, 'game.promo', { game: game.id, text: 'Staff fix' }).ok, 'staff aren\'t limited');
});
