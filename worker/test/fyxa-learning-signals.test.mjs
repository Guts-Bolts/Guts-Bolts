import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const signal = (s, user, extra = {}) => s.call(user, 'ai.learning.capture', Object.assign({
  provider: 'ollama',
  model: 'qwen3:8b',
  category: 'scripting',
  tools: [{ name: 'get_script_source', outcome: 'success' }],
}, extra));

test('Fyxa captures bounded quality metadata without storing conversation content', () => {
  const s = makeServer();
  const user = s.signUp('alice');
  const captured = signal(s, user);
  assert.ok(captured.ok);
  assert.equal(captured.recorded, true);
  assert.equal(captured.qualityLabel, 'awaiting_feedback');

  const columns = [...s.sql.exec('PRAGMA table_info(ai_learning_signals)')].map((row) => row.name);
  assert.ok(columns.includes('tool_summary'));
  assert.ok(!columns.includes('prompt'));
  assert.ok(!columns.includes('response'));
  assert.ok(!columns.includes('tool_arguments'));
  assert.ok(!columns.includes('tool_output'));

  const history = s.call(user, 'ai.learning.history');
  assert.ok(history.ok);
  assert.equal(history.events.length, 1);
  assert.deepEqual(history.events[0].tools, [{ name: 'get_script_source', outcome: 'success' }]);
});

test('tool failures are labelled separately and user feedback records why', () => {
  const s = makeServer();
  const user = s.signUp('alice');
  const captured = signal(s, user, {
    tools: [
      { name: 'get_script_source', outcome: 'success' },
      { name: 'set_script_source', outcome: 'failure' },
    ],
  });
  assert.ok(captured.ok);
  assert.equal(captured.qualityLabel, 'tool_failure');
  assert.equal(captured.toolFailures, 1);

  const feedback = s.call(user, 'ai.learning.feedback', {
    signalId: captured.signalId, rating: -1, reason: 'tool_failure',
  });
  assert.ok(feedback.ok, feedback.error);
  assert.equal(feedback.qualityLabel, 'user_not_helpful');

  const history = s.call(user, 'ai.learning.history');
  assert.equal(history.events[0].feedbackRating, -1);
  assert.equal(history.events[0].feedbackReason, 'tool_failure');
});

test('opt-out immediately deletes learning signals and blocks subsequent capture', () => {
  const s = makeServer();
  const user = s.signUp('alice');
  const captured = signal(s, user);
  assert.ok(captured.ok && captured.recorded);
  assert.equal([...s.sql.exec('SELECT COUNT(*) AS count FROM ai_learning_signals WHERE account_id = ?', user.id)][0].count, 1);

  const off = s.call(user, 'ai.preferences.update', { trainingEnabled: false });
  assert.ok(off.ok, off.error);
  assert.equal([...s.sql.exec('SELECT COUNT(*) AS count FROM ai_learning_signals WHERE account_id = ?', user.id)][0].count, 0);

  const blocked = signal(s, user);
  assert.ok(blocked.ok);
  assert.equal(blocked.recorded, false);
  assert.equal(blocked.reason, 'opted_out');
});

test('guests cannot capture learning signals and feedback cannot cross account boundaries', () => {
  const s = makeServer();
  const guest = s.user('c'.repeat(63) + 'd');
  s.saveUser(guest);
  const guestCapture = signal(s, guest);
  assert.equal(guestCapture.ok, false);

  const owner = s.signUp('alice');
  const other = s.signUp('bob');
  const captured = signal(s, owner);
  assert.ok(captured.ok && captured.recorded);
  const stolen = s.call(other, 'ai.learning.feedback', {
    signalId: captured.signalId, rating: 1, reason: 'helpful',
  });
  assert.equal(stolen.ok, false);
});

test('invalid tool metadata and feedback reasons are rejected or discarded', () => {
  const s = makeServer();
  const user = s.signUp('alice');
  const captured = signal(s, user, {
    tools: [
      { name: 'get_tree', outcome: 'success' },
      { name: 'bad tool name', outcome: 'success' },
      { name: 'write_file', outcome: 'unexpected' },
    ],
  });
  assert.ok(captured.ok && captured.recorded);
  assert.equal(s.call(user, 'ai.learning.feedback', {
    signalId: captured.signalId, rating: -1, reason: 'made_up_reason',
  }).ok, false);
  const history = s.call(user, 'ai.learning.history');
  assert.deepEqual(history.events[0].tools, [{ name: 'get_tree', outcome: 'success' }]);
});

test('account deletion removes all learning signals', () => {
  const s = makeServer();
  const user = s.signUp('alice');
  assert.ok(signal(s, user).recorded);
  const id = user.id;
  s.deleteAccount(user);
  assert.equal([...s.sql.exec('SELECT COUNT(*) AS count FROM ai_learning_signals WHERE account_id = ?', id)][0].count, 0);
});
