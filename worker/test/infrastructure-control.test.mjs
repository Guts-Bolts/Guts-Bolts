import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { InfrastructureObject } from '../infrastructure-control.js';

function fakeSql() {
  const db = new DatabaseSync(':memory:');
  return {
    exec(query, ...bind) {
      if (!bind.length && query.split(';').filter((part) => part.trim()).length > 1) {
        db.exec(query);
        return [];
      }
      const statement = db.prepare(query);
      if (/^\s*(select|pragma)/i.test(query)) return statement.all(...bind);
      statement.run(...bind);
      return [];
    },
  };
}

function makeNode({ models = ['qwen3:8b'], toolModels = ['qwen3:8b'] } = {}) {
  const sql = fakeSql();
  const object = new InfrastructureObject({
    storage: { sql },
    blockConcurrencyWhile(fn) { fn(); },
  }, { OFFICIAL: 'a'.repeat(64) });
  const id = 'b'.repeat(32);
  const now = Math.floor(Date.now() / 1000);
  sql.exec(
    'INSERT INTO infra_devices (id, owner, name, platform, token_hash, created, revoked, last_seen, status) VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?)',
    id, 'a'.repeat(64), 'Test node', 'Windows', 'c'.repeat(64), now, now,
    JSON.stringify({ models, toolModels, cpuPercent: 10, gpuPercent: 0 }),
  );
  const sent = [];
  const connection = {
    id: 'test-connection', nodeId: id, authed: true,
    ws: { send(value) { sent.push(JSON.parse(value)); } },
  };
  object.connections.set(connection.id, connection);
  object.nodes.set(id, connection);
  return { object, connection, sent };
}

const job = {
  account: 'd'.repeat(64),
  model: 'qwen3:8b',
  messages: [{ role: 'user', content: 'Hello' }],
  tools: [],
};

test('Nexus dispatches an Ollama-compatible inference request to a paired online node', async () => {
  const { object, connection, sent } = makeNode();
  const pending = object.infer(job);
  assert.equal(sent.length, 1);
  assert.equal(sent[0].t, 'node.infer');
  assert.equal(sent[0].model, 'qwen3:8b');
  assert.equal(sent[0].messages[0].content, 'Hello');

  await object.onNodeMessage(connection, {
    t: 'node.infer.result',
    requestId: sent[0].requestId,
    message: { role: 'assistant', content: 'Hello from Ollama.' },
  });
  const result = await pending;
  assert.equal(result.body.ok, true);
  assert.equal(result.body.message.content, 'Hello from Ollama.');
});

test('Nexus requires tool-capable models when Fyxa sends tools', async () => {
  const { object } = makeNode({ models: ['qwen3:8b'], toolModels: [] });
  const result = await object.infer({ ...job, tools: [{ type: 'function', function: { name: 'get_game_tree' } }] });
  assert.equal(result.status, 503);
  assert.match(result.body.error, /required capabilities/);
});

test('Nexus allows only one active inference per node', async () => {
  const { object, connection, sent } = makeNode();
  const pending = object.infer(job);
  const second = await object.infer(job);
  assert.equal(second.status, 503);
  assert.equal(sent.length, 1);

  await object.onNodeMessage(connection, {
    t: 'node.infer.result',
    requestId: sent[0].requestId,
    message: { role: 'assistant', content: 'done' },
  });
  assert.equal((await pending).body.ok, true);
});

test('Nexus rejects malformed conversation roles before dispatch', async () => {
  const { object, sent } = makeNode();
  const result = await object.infer({ ...job, messages: [{ role: 'system-shell', content: 'run command' }] });
  assert.equal(result.status, 400);
  assert.equal(sent.length, 0);
});
