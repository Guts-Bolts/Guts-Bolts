// Past trades: every finished trade (accepted, declined, cancelled, expired) is kept for the
// two people in it, newest first, a page at a time, and nobody else can see it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

// A Limited hat with numbered copies, each held by someone.
function limited(s, name, owners) {
  const id = String(s.nextAssetNum + 5000);
  const a = { id, num: s.nextAssetNum++, kind: 'hat', name, description: '', creator: s.staff.id, created: 1, updated: 1, price: 10,
    sales: owners.length, plays: 0, meta: { model: true }, limited: true, stock: 100,
    copies: owners.map((u, i) => ({ serial: i + 1, owner: u.id, price: 0 })) };
  s.assets.set(id, a);
  for (const u of owners) { u.owned.push(id); s.saveUser(u); }
  s.saveAsset(a);
  s.flush();
  return a;
}

function setup() {
  const s = makeServer();
  const ann = s.signUp('ann'), ben = s.signUp('ben'), cat = s.signUp('cat');
  const hat = limited(s, 'Gold Hat', [ann, ann, ann, ann]);
  const cap = limited(s, 'Silver Cap', [ben, ben, ben, ben]);
  return { s, ann, ben, cat, hat, cap };
}
const offer = (s, from, to, mine, theirs) => {
  const r = s.call(from, 'trade.send', { to: to.id, give: [mine], get: [theirs] });
  assert.ok(r.ok, r.error);
  return r.trade.id;
};

test('finished trades show up in both people\'s Past trades, from each side, newest first', () => {
  const { s, ann, ben, hat, cap } = setup();
  const t1 = offer(s, ann, ben, { id: hat.id, serial: 1 }, { id: cap.id, serial: 1 });
  const t2 = offer(s, ann, ben, { id: hat.id, serial: 2 }, { id: cap.id, serial: 2 });
  const t3 = offer(s, ben, ann, { id: cap.id, serial: 3 }, { id: hat.id, serial: 3 });
  const t4 = offer(s, ann, ben, { id: hat.id, serial: 4 }, { id: cap.id, serial: 4 });
  assert.ok(s.call(ben, 'trade.accept', { id: t1 }).ok);
  s.trades.find((x) => x.id === t1).updated -= 30;   // (so the order is clear)
  s.sql.exec('UPDATE pasttrades SET at = at - 30 WHERE id = ?', t1);
  assert.ok(s.call(ben, 'trade.decline', { id: t2 }).ok);
  s.sql.exec('UPDATE pasttrades SET at = at - 20 WHERE id = ?', t2);
  assert.ok(s.call(ben, 'trade.cancel', { id: t3 }).ok);
  s.sql.exec('UPDATE pasttrades SET at = at - 10 WHERE id = ?', t3);
  // An open trade isn't in Past trades.
  const h = s.call(ann, 'trade.history', {});
  assert.ok(h.ok, h.error);
  assert.deepEqual(h.trades.map((x) => x.id), [t3, t2, t1]);
  assert.ok(!h.trades.some((x) => x.id === t4));
  assert.deepEqual(h.trades.map((x) => x.status), ['cancelled', 'declined', 'accepted']);
  const accepted = h.trades[2];
  assert.equal(accepted.with.name, 'ben');
  assert.equal(accepted.with.userId, ben.userId);
  assert.equal(accepted.sent, true);
  assert.deepEqual(accepted.gave.map((i) => [i.name, i.serial]), [['Gold Hat', 1]]);
  assert.deepEqual(accepted.got.map((i) => [i.name, i.serial]), [['Silver Cap', 1]]);
  assert.ok(accepted.finished >= accepted.created);
  // Ben sees the same trade from his side.
  const hb = s.call(ben, 'trade.history', {});
  const fromBen = hb.trades.find((x) => x.id === t1);
  assert.equal(fromBen.sent, false);
  assert.equal(fromBen.with.name, 'ann');
  assert.deepEqual(fromBen.gave.map((i) => i.name), ['Silver Cap']);
  assert.deepEqual(fromBen.got.map((i) => i.name), ['Gold Hat']);
});

test('only the two people in a trade can see it', () => {
  const { s, ann, ben, cat, hat, cap } = setup();
  const t1 = offer(s, ann, ben, { id: hat.id, serial: 1 }, { id: cap.id, serial: 1 });
  assert.ok(s.call(ben, 'trade.decline', { id: t1 }).ok);
  const theirs = s.call(cat, 'trade.history', {});
  assert.ok(theirs.ok);
  assert.equal(theirs.trades.length, 0);
  assert.equal(theirs.total, 0);
  // Not even by asking for someone else's.
  const sneaky = s.call(cat, 'trade.history', { user: ann.id });
  assert.equal(sneaky.trades.length, 0);
  // Staff don't get other people's Past trades from it either.
  assert.equal(s.call(s.staff, 'trade.history', {}).trades.length, 0);
});

test('Past trades come a page (20) at a time', () => {
  const { s, ann, ben, hat, cap } = setup();
  for (let i = 0; i < 23; i++) {
    const id = offer(s, ann, ben, { id: hat.id, serial: 1 }, { id: cap.id, serial: 1 });
    assert.ok(s.call(ann, 'trade.cancel', { id }).ok);
    s.sql.exec('UPDATE pasttrades SET at = ? WHERE id = ?', Math.floor(Date.now() / 1000) - 1000 + i, id);
  }
  const p1 = s.call(ann, 'trade.history', { page: 1 });
  assert.equal(p1.total, 23);
  assert.equal(p1.pages, 2);
  assert.equal(p1.trades.length, 20);
  const p2 = s.call(ann, 'trade.history', { page: 2 });
  assert.equal(p2.page, 2);
  assert.equal(p2.trades.length, 3);
  assert.equal(new Set([...p1.trades, ...p2.trades].map((x) => x.id)).size, 23);   // no trade on two pages
  assert.equal(s.call(ann, 'trade.history', { page: 99 }).page, 2);   // past the end: the last page
  assert.equal(s.call(ann, 'trade.history', { page: -3 }).page, 1);
});

test('a trade nobody answers expires after 30 days and goes in Past trades', () => {
  const { s, ann, ben, hat, cap } = setup();
  const id = offer(s, ann, ben, { id: hat.id, serial: 1 }, { id: cap.id, serial: 1 });
  s.trades.find((x) => x.id === id).created -= 31 * 86400;
  const list = s.call(ben, 'trade.list', {});
  assert.equal(list.trades.find((x) => x.id === id).status, 'expired');
  assert.equal(s.call(ben, 'trade.accept', { id }).ok, false);
  const h = s.call(ann, 'trade.history', {});
  assert.equal(h.trades[0].id, id);
  assert.equal(h.trades[0].status, 'expired');
  assert.ok((ann.notes || []).some((n) => n.kind === 'trade' && /expired/.test(n.text)));
});

test('blocking someone cancels open trades, and they go in Past trades', () => {
  const { s, ann, ben, hat, cap } = setup();
  const id = offer(s, ann, ben, { id: hat.id, serial: 1 }, { id: cap.id, serial: 1 });
  assert.ok(s.call(ben, 'block.add', { user: ann.id }).ok);
  assert.equal(s.call(ben, 'trade.history', {}).trades[0].status, 'cancelled');
  assert.equal(s.call(ann, 'trade.history', {}).trades[0].id, id);
});

test('Past trades keep the item names after an item is gone', () => {
  const { s, ann, ben, hat, cap } = setup();
  const id = offer(s, ann, ben, { id: hat.id, serial: 1 }, { id: cap.id, serial: 1 });
  assert.ok(s.call(ben, 'trade.decline', { id }).ok);
  s.assets.delete(cap.id);
  const h = s.call(ann, 'trade.history', {});
  assert.equal(h.trades[0].got[0].name, 'Silver Cap');
});

test('finished trades the server already had are added to Past trades when it starts', () => {
  const { s, ann, ben, hat, cap } = setup();
  s.trades.push({ id: 'tr-old1', from: ann.id, to: ben.id, give: [{ id: hat.id, serial: 2 }], get: [{ id: cap.id, serial: 2 }],
    status: 'declined', created: Math.floor(Date.now() / 1000) - 2000, updated: Math.floor(Date.now() / 1000) - 1000 });
  s.dirty.trades = true;
  s.flush();
  // A new server object over the same database, like after a restart.
  const again = new s.constructor({ storage: { sql: s.sql } }, { OFFICIAL: s.official });
  const h = again.op('trade.history', again.users.get(ann.id), {});
  assert.ok(h.trades.some((x) => x.id === 'tr-old1' && x.status === 'declined' && x.gave[0].name === 'Gold Hat'));
});
