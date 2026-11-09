import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

test('Fyxa sessions persist and are isolated by account and project', () => {
  const s = makeServer();
  const alice = s.signUp('alice');
  const bob = s.signUp('bob');
  const created = s.call(alice, 'ai.sessions.create', { title: 'Vehicle system', projectId: 'project-42' });
  assert.ok(created.ok, created.error);
  const id = created.session.id;
  assert.equal(s.call(alice, 'ai.sessions.get', { sessionId: id }).session.title, 'Vehicle system');
  assert.equal(s.call(bob, 'ai.sessions.get', { sessionId: id }).ok, false);
  assert.equal(s.call(alice, 'ai.sessions.list', { projectId: 'other-project' }).sessions.length, 0);
  assert.equal(s.call(alice, 'ai.sessions.list', { projectId: 'project-42' }).sessions.length, 1);
});

test('Fyxa memory is account-scoped, project-scoped, and supports user controls', () => {
  const s = makeServer();
  const alice = s.signUp('alice');
  const bob = s.signUp('bob');
  const created = s.call(alice, 'ai.memory.create', {
    content: 'The vehicle system uses the engine physics API.',
    memoryType: 'project', projectId: 'project-42', tags: ['vehicles', 'physics'], importance: 4,
  });
  assert.ok(created.ok, created.error);
  const id = created.memory.id;
  assert.equal(s.call(bob, 'ai.memory.list').memories.length, 0);
  assert.equal(s.call(bob, 'ai.memory.update', { memoryId: id, content: 'stolen' }).ok, false);
  const listed = s.call(alice, 'ai.memory.list', { projectId: 'project-42', memoryType: 'project' });
  assert.equal(listed.memories.length, 1);
  assert.equal(listed.memories[0].content, 'The vehicle system uses the engine physics API.');
  const updated = s.call(alice, 'ai.memory.update', { memoryId: id, content: 'Use the current vehicle physics API.', importance: 5 });
  assert.ok(updated.ok, updated.error);
  assert.equal(updated.memory.importance, 5);
  assert.equal(s.call(alice, 'ai.memory.delete', { memoryId: id }).deleted, true);
  assert.equal(s.call(alice, 'ai.memory.list').memories.length, 0);
});

test('Fyxa memory validates types, bounds, and source-session ownership', () => {
  const s = makeServer();
  const alice = s.signUp('alice');
  const bob = s.signUp('bob');
  const session = s.call(alice, 'ai.sessions.create').session;
  assert.equal(s.call(alice, 'ai.memory.create', { content: 'x', memoryType: 'unknown' }).ok, false);
  assert.equal(s.call(alice, 'ai.memory.create', { content: 'x', sourceSessionId: 'missing' }).ok, false);
  assert.equal(s.call(bob, 'ai.memory.create', { content: 'x', sourceSessionId: session.id }).ok, false);
  const created = s.call(alice, 'ai.memory.create', {
    content: 'Remember this.', sourceSessionId: session.id, tags: ['a', 'a', 'b'], importance: 99,
  });
  assert.ok(created.ok, created.error);
  assert.deepEqual(created.memory.tags, ['a', 'b']);
  assert.equal(created.memory.importance, 5);
});

test('deleting a Fyxa session removes memories explicitly sourced from that session', () => {
  const s = makeServer();
  const alice = s.signUp('alice');
  const session = s.call(alice, 'ai.sessions.create').session;
  assert.ok(s.call(alice, 'ai.memory.create', {
    content: 'Session-derived memory.', sourceSessionId: session.id,
  }).ok);
  assert.equal(s.call(alice, 'ai.memory.list').memories.length, 1);
  assert.equal(s.call(alice, 'ai.sessions.delete', { sessionId: session.id }).deleted, true);
  assert.equal(s.call(alice, 'ai.memory.list').memories.length, 0);
  assert.equal(s.call(alice, 'ai.sessions.get', { sessionId: session.id }).ok, false);
});

test('Fyxa memory audit records metadata without storing memory content', () => {
  const s = makeServer();
  const alice = s.signUp('alice');
  const secretText = 'distinctive private project note';
  const created = s.call(alice, 'ai.memory.create', { content: secretText });
  assert.ok(created.ok, created.error);
  const rows = s.sql.exec('SELECT event_type, details FROM ai_memory_audit WHERE account_id = ?', alice.id);
  assert.ok(rows.some((r) => r.event_type === 'memory.created'));
  assert.equal(rows.some((r) => r.details.includes(secretText)), false);
});
