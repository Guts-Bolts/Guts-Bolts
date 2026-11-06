import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { createHash, randomBytes } from 'node:crypto';
import { RemoteControlObject } from '../remote-control.js';

const OWNER = 'a'.repeat(64);
const sha256 = (value) => createHash('sha256').update(value).digest('hex');
function fakeSql() {
  const db = new DatabaseSync(':memory:');
  return {
    exec(query, ...bind) {
      if (!bind.length && query.split(';').filter((x) => x.trim()).length > 1) { db.exec(query); return []; }
      const statement = db.prepare(query);
      if (/^\s*(select|pragma)/i.test(query)) return statement.all(...bind);
      statement.run(...bind);
      return [];
    },
  };
}
function world() {
  const sql = fakeSql();
  const waits = [];
  const object = new RemoteControlObject({ storage: { sql }, waitUntil: (promise) => waits.push(promise) }, { OFFICIAL: OWNER });
  const request = (body, actor = OWNER, ip = '127.0.0.1') => object.control(actor, body, ip);
  return { object, sql, request, waits };
}
function pairDevice(w, token = randomBytes(32).toString('hex')) {
  w.sql.exec("INSERT INTO remote_devices (id, owner, token_hash, created, revoked) VALUES ('home', ?, ?, ?, 0)",
    OWNER, sha256(token), Math.floor(Date.now() / 1000));
  return token;
}

test('pairing consumes a one-time code and stores only the hash of the device credential', async () => {
  const w = world();
  const code = randomBytes(32).toString('hex');
  const conn = { id: 'connection-1', pending: true, authed: false, ws: { sent: null, send(text) { this.sent = JSON.parse(text); }, close() {} } };
  w.object.connections.set(conn.id, conn);
  w.sql.exec('INSERT INTO remote_pairings (code_hash, expires, conn_id) VALUES (?, ?, ?)',
    sha256(code), Math.floor(Date.now() / 1000) + 300, conn.id);
  const response = await w.object.control(w.request({ action: 'pair', code }));
  assert.equal(response.status, 200);
  assert.equal(conn.ws.sent.t, 'pair.complete');
  assert.match(conn.ws.sent.token, /^[a-f0-9]{64}$/);
  const stored = [...w.sql.exec("SELECT token_hash, revoked FROM remote_devices WHERE id = 'home'")][0];
  assert.equal(stored.token_hash, sha256(conn.ws.sent.token));
  assert.equal(stored.token_hash.includes(conn.ws.sent.token), false);
  assert.equal(stored.revoked, 0);
  assert.equal(conn.authed, true);
  assert.equal([...w.sql.exec('SELECT * FROM remote_pairings')].length, 0);
});

test('pairing refuses expired codes and disconnected pending sockets', async () => {
  const w = world();
  const code = randomBytes(32).toString('hex');
  w.sql.exec('INSERT INTO remote_pairings (code_hash, expires, conn_id, ip) VALUES (?, ?, ?, ?)',
    sha256(code), Math.floor(Date.now() / 1000) - 1, 'gone');
  const response = await w.object.control(w.request({ action: 'pair', code }));
  assert.equal(response.status, 400);
  assert.match(response.body.error, /invalid or expired/i);
});

test('only allowlisted commands with bounded arguments reach the paired agent', async () => {
  const w = world();
  pairDevice(w);
  const sent = [];
  w.object.agent = { authed: true, ws: { send(text) { sent.push(JSON.parse(text)); }, close() {} } };
  w.sql.exec('INSERT INTO remote_status (id, data, seen) VALUES (1, ?, ?)', JSON.stringify({ serverRunning: true }), Math.floor(Date.now() / 1000));
  const accepted = await w.object.control(w.request({ action: 'command', command: 'set-slots', args: { slots: 12 } }));
  assert.equal(accepted.status, 202);
  assert.equal(sent.length, 1);
  assert.equal(sent[0].command, 'set-slots');
  assert.deepEqual(sent[0].args, { slots: 12 });
  assert.ok(sent[0].expires > Math.floor(Date.now() / 1000));
  for (const [command, args] of [
    ['powershell', { command: 'whoami' }],
    ['set-slots', { slots: 51 }],
    ['set-priority', { priority: 'Realtime' }],
    ['start', { extra: true }],
  ]) {
    const response = await w.object.control(w.request({ action: 'command', command, args }));
    assert.equal(response.status, 400);
  }
  assert.equal(sent.length, 1);
});

test('commands are not sent when the device is offline or stale', async () => {
  const w = world();
  pairDevice(w);
  const sent = [];
  w.object.agent = { authed: true, ws: { send(text) { sent.push(text); }, close() {} } };
  w.sql.exec('INSERT INTO remote_status (id, data, seen) VALUES (1, ?, ?)', '{}', Math.floor(Date.now() / 1000) - 60);
  const response = await w.object.control(w.request({ action: 'command', command: 'restart', args: {} }));
  assert.equal(response.status, 503);
  assert.equal(sent.length, 0);
});

test('revoked device credentials cannot control the server', async () => {
  const w = world();
  pairDevice(w);
  w.sql.exec("UPDATE remote_devices SET revoked = 1 WHERE id = 'home'");
  const response = await w.object.control(w.request({ action: 'status' }));
  assert.equal(response.status, 403);
});

test('push subscriptions are encrypted at rest and restricted to known push providers', async () => {
  const key = randomBytes(32).toString('base64url');
  const w = world();
  w.object.env.REMOTE_PUSH_ENCRYPTION_KEY = key;
  pairDevice(w);
  const endpoint = 'https://fcm.googleapis.com/fcm/send/fake-endpoint-token';
  const subscription = { endpoint, keys: { p256dh: randomBytes(65).toString('base64url'), auth: randomBytes(16).toString('base64url') } };
  const response = await w.object.control(w.request({ action: 'push.subscribe', subscription }));
  assert.equal(response.status, 200);
  const row = [...w.sql.exec('SELECT encrypted FROM remote_push_subscriptions')][0];
  assert.ok(row.encrypted);
  assert.equal(row.encrypted.includes(endpoint), false);
  const bad = await w.object.control(w.request({ action: 'push.subscribe', subscription: {
    endpoint: 'https://attacker.example/collect', keys: subscription.keys,
  } }));
  assert.equal(bad.status, 400);
});

test('durable heartbeat alarm records a missed-agent alert only once', async () => {
  const w = world();
  w.sql.exec('INSERT INTO remote_status (id, data, seen) VALUES (1, ?, ?)', '{}', Math.floor(Date.now() / 1000) - 60);
  await w.object.alarm();
  await w.object.alarm();
  const rows = [...w.sql.exec("SELECT action FROM remote_audit WHERE action = 'alert.offline'")];
  assert.equal(rows.length, 1);
});

test('device authentication rejects incorrect credentials and accepts the paired token', async () => {
  const w = world();
  const token = randomBytes(32).toString('hex');
  pairDevice(w, token);
  const makeConnection = (id) => ({
    id, pending: false, authed: false, attempts: 0,
    ws: { sent: [], send(text) { this.sent.push(JSON.parse(text)); }, close() {} },
  });
  const bad = makeConnection('bad-connection');
  w.object.connections.set(bad.id, bad);
  await w.object.onAgentMessage(bad, { t: 'device.auth', token: randomBytes(32).toString('hex') });
  assert.equal(bad.authed, false);
  assert.equal(bad.ws.sent[0].t, 'device.rejected');
  const good = makeConnection('good-connection');
  w.object.connections.set(good.id, good);
  await w.object.onAgentMessage(good, { t: 'device.auth', token });
  assert.equal(good.authed, true);
  assert.equal(w.object.agent, good);
});

test('only the configured official account can invoke remote-control RPC', async () => {
  const w = world();
  const other = 'b'.repeat(64);
  const response = await w.object.control(other, { action: 'pair', code: randomBytes(32).toString('hex') });
  assert.equal(response.status, 401);
});

test('a second valid-token connection cannot replace a live authenticated agent', async () => {
  const w = world();
  const token = randomBytes(32).toString('hex');
  pairDevice(w, token);
  const makeConnection = (id) => ({
    id, pending: false, authed: false, attempts: 0,
    ws: { sent: [], send(text) { this.sent.push(JSON.parse(text)); }, close() {} },
  });
  const active = makeConnection('active');
  active.authed = true;
  active.owner = OWNER;
  w.object.connections.set(active.id, active);
  w.object.agent = active;
  w.sql.exec('INSERT INTO remote_status (id, data, seen) VALUES (1, ?, ?)', '{}', Math.floor(Date.now() / 1000));
  const second = makeConnection('second');
  w.object.connections.set(second.id, second);
  await w.object.onAgentMessage(second, { t: 'device.auth', token });
  assert.equal(w.object.agent, active);
  assert.equal(second.authed, false);
  assert.equal(second.ws.sent[0].t, 'device.busy');
});

test('phone pairing submissions are rate-limited per official account and client IP', async () => {
  const w = world();
  for (let i = 0; i < 10; i++) {
    const response = await w.request({ action: 'pair', code: randomBytes(32).toString('hex') }, OWNER, '192.0.2.55');
    assert.equal(response.status, 400);
  }
  const limited = await w.request({ action: 'pair', code: randomBytes(32).toString('hex') }, OWNER, '192.0.2.55');
  assert.equal(limited.status, 429);
  const otherIp = await w.request({ action: 'pair', code: randomBytes(32).toString('hex') }, OWNER, '192.0.2.56');
  assert.equal(otherIp.status, 400);
});
