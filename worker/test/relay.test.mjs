// Game servers that stay up whoever leaves: game server machines run private servers too,
// and take over a server whose host left (relay.host / relay.join / servers.private).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

// A pretend WebSocket connection (what /ws makes), remembering what the server sent it.
function connect(s) {
  const sent = [];
  const c = { id: Math.random().toString(16).slice(2, 10), mode: 'request', account: '', session: '', ticket: '', peer: '',
    since: Math.floor(Date.now() / 1000), lastActive: Math.floor(Date.now() / 1000), ip: '',
    ws: { send: (m) => sent.push(JSON.parse(m)), close() {} } };
  c.sent = sent;
  s.conns.set(c.id, c);
  return c;
}

// Signed requests without the keys: who sent it is passed straight in.
function relaxed(s) {
  s.checkRequest = (req) => ({ me: s.users.get(req.account), args: req.args || {}, opName: req.op });
  s.relay = (c, who, op, args = {}) => s.onMessage(c, JSON.stringify({ op, account: who.id, args }));
  return s;
}

// A game server machine (GutsAndBoltsGameServer) waiting for games.
function machine(s) {
  const c = connect(s);
  s.relay(c, s.staff, 'relay.pool', { slots: 4 });
  assert.equal(c.mode, 'pool');
  return c;
}

// Someone joins `sessionId` and its host lets them in.
function join(s, who, args) {
  const c = connect(s);
  s.relay(c, who, 'relay.join', args);
  if (c.mode !== 'pending') return c;
  const sess = s.sessions.get(c.session);
  const pipe = connect(s);
  s.onMessage(pipe, JSON.stringify({ t: 'accept', ticket: c.ticket }));
  assert.equal(c.mode, 'pipe');
  assert.ok(sess.players.has(c.id));
  return c;
}

test('a private server runs on a game server machine, with its owner\'s code', () => {
  const s = relaxed(makeServer());
  const ann = s.signUp('ann'), ben = s.signUp('ben');
  const game = s.addGame(ann, 'Obby');
  const pool = machine(s);

  const r = s.call(ben, 'servers.private', { game: game.id });
  assert.ok(r.ok, r.error);
  assert.equal(r.wait, 2);
  assert.match(r.code, /^[A-Z2-9]{6}$/);
  const start = pool.sent.find((m) => m.t === 'start');
  assert.deepEqual([start.game, start.private, start.code, start.owner], [game.id, true, r.code, ben.id]);
  // Still starting: asking again with the code says wait.
  assert.equal(s.call(ben, 'servers.private', { game: game.id, code: r.code }).wait, 2);

  // The machine starts it.
  const ctl = connect(s);
  s.relay(ctl, s.staff, 'relay.host', { game: game.id, title: 'Obby', dedicated: true, private: true, code: r.code, owner: ben.id });
  const sess = s.sessions.get(ctl.session);
  assert.ok(sess.dedicated && sess.priv);
  assert.equal(sess.code, r.code);
  assert.equal(sess.owner, ben.id);
  assert.equal(s.pools.get(pool.id).starting.size, 0);

  const again = s.call(ben, 'servers.private', { game: game.id, code: r.code });
  assert.deepEqual([again.join, again.code], [sess.id, r.code]);
  // Ben gets in; it's his, so the friends rule doesn't stop him even without the code.
  join(s, ben, { session: sess.id });
  assert.equal(sess.players.size, 1);
  // Others need the code (or to be Ben's friend), and don't see it in the list.
  const cal = s.signUp('cal');
  assert.equal(join(s, cal, { session: sess.id }).sent[0].ok, false);
  assert.equal(s.call(cal, 'servers.list', { game: game.id }).servers.length, 0);
  assert.equal(s.call(ben, 'servers.list', { game: game.id }).servers.length, 1);
  join(s, cal, { code: r.code });
  assert.equal(sess.players.size, 2);
});

test('with no game server machine, you host your private server yourself', () => {
  const s = relaxed(makeServer());
  const ann = s.signUp('ann');
  const game = s.addGame(ann, 'Obby');
  const r = s.call(ann, 'servers.private', { game: game.id });
  assert.ok(r.ok && r.host, JSON.stringify(r));
  // A code the machine never started falls back to hosting too.
  assert.ok(s.call(ann, 'servers.private', { game: game.id, code: 'ABCDEF' }).host);
});

test('paid private servers still have to be bought first', () => {
  const s = relaxed(makeServer());
  const ann = s.signUp('ann'), ben = s.signUp('ben');
  const game = s.addGame(ann, 'Obby', { privatePrice: 50 });
  machine(s);
  const r = s.call(ben, 'servers.private', { game: game.id });
  assert.equal(r.ok, false);
  assert.match(r.error, /cost 50 Bolts/);
});

test('when a player host leaves, a game server machine takes over and everyone follows', () => {
  const s = relaxed(makeServer());
  const ann = s.signUp('ann'), ben = s.signUp('ben'), cal = s.signUp('cal');
  const game = s.addGame(ann, 'Obby');

  // Nobody runs a machine yet: Ann hosts.
  const host = connect(s);
  s.relay(host, ann, 'relay.host', { game: game.id, title: 'Obby' });
  const old = s.sessions.get(host.session);
  join(s, ben, { session: old.id });
  join(s, cal, { session: old.id });
  const pool = machine(s);

  // Ann leaves. The machine is asked to carry on her server.
  s.drop(host.id);
  assert.ok(!s.sessions.has(old.id));
  const start = pool.sent.find((m) => m.t === 'start');
  assert.equal(start.continues, old.id);
  assert.ok(s.moved.get(old.id).machineUntil > 0);

  // Ben and Cal are told to wait (neither is asked to host it).
  for (const who of [ben, cal]) {
    const c = connect(s);
    s.relay(c, who, 'relay.join', { session: old.id });
    assert.deepEqual([c.sent[0].hostLeft, c.sent[0].you], [true, false]);
  }

  // The machine starts it; they follow to it.
  const ctl = connect(s);
  s.relay(ctl, s.staff, 'relay.host', { game: game.id, title: 'Obby', dedicated: true, continues: old.id });
  const fresh = s.sessions.get(ctl.session);
  assert.ok(fresh.dedicated);
  assert.equal(s.moved.get(old.id).newId, fresh.id);
  join(s, ben, { session: old.id });
  join(s, cal, { session: old.id });
  assert.equal(fresh.players.size, 2);

  // Now nobody is the host: Ben leaving doesn't close it.
  const benPipe = [...fresh.players].map((id) => s.conns.get(id)).find((c) => c.account === ben.id);
  s.drop(benPipe.id);
  assert.ok(s.sessions.has(fresh.id));
  assert.equal(fresh.players.size, 1);
});

test('a private server keeps its code when a machine takes it over', () => {
  const s = relaxed(makeServer());
  const ann = s.signUp('ann'), ben = s.signUp('ben');
  const game = s.addGame(ann, 'Obby');
  const host = connect(s);
  s.relay(host, ann, 'relay.host', { game: game.id, title: 'Obby', private: true });
  const old = s.sessions.get(host.session);
  join(s, ben, { code: old.code });
  machine(s);
  s.drop(host.id);
  const ctl = connect(s);
  s.relay(ctl, s.staff, 'relay.host', { game: game.id, dedicated: true, continues: old.id });
  const fresh = s.sessions.get(ctl.session);
  assert.deepEqual([fresh.priv, fresh.code, fresh.owner], [true, old.code, ann.id]);
});

test('if the machine never starts it, one of the players hosts it like before', () => {
  const s = relaxed(makeServer());
  const ann = s.signUp('ann'), ben = s.signUp('ben');
  const game = s.addGame(ann, 'Obby');
  const host = connect(s);
  s.relay(host, ann, 'relay.host', { game: game.id, title: 'Obby' });
  const old = s.sessions.get(host.session);
  join(s, ben, { session: old.id });
  machine(s);
  s.drop(host.id);
  s.moved.get(old.id).machineUntil = 1;   // (long ago)
  const c = connect(s);
  s.relay(c, ben, 'relay.join', { session: old.id });
  assert.deepEqual([c.sent[0].hostLeft, c.sent[0].you], [true, true]);
});
