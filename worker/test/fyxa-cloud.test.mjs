// Fyxa in the cloud (worker/fyxa-cloud.js): daily steps per account (more for Boltworks Club),
// a site-wide daily budget, and Studio's Ollama-style messages in and out.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';
import { toWorkersAi, fromWorkersAi, neuronsFor, DEFAULT_LIMITS, kFyxaModel } from '../fyxa-cloud.js';

// A pretend Workers AI that remembers what it was asked.
function withAi(s, answer = { response: 'Hi! I made the ball red.', usage: { prompt_tokens: 1000, completion_tokens: 100 } }) {
  s.asked = [];
  s.env.AI = { run: async (model, input) => { s.asked.push({ model, input }); if (answer instanceof Error) throw answer; return answer; } };
  return s;
}
const hello = { messages: [{ role: 'user', content: 'Make the ball red' }],
  tools: [{ type: 'function', function: { name: 'set_property', description: 'Set a property', parameters: { type: 'object', properties: {} } } }] };
async function chat(s, me) {
  const r = s.call(me, 'fyxacloud.chat', hello);
  return r.ok && r.fyxaCloud ? s.fyxaCloudAnswer(r) : r;
}

test('a cloud answer comes back in the shape Studio reads, and counts a step and neurons', async () => {
  const s = withAi(makeServer());
  const ann = s.signUp('ann');
  const r = await chat(s, ann);
  assert.ok(r.ok, r.error);
  assert.deepEqual(r.message, { role: 'assistant', content: 'Hi! I made the ball red.' });
  assert.equal(s.asked[0].model, kFyxaModel);
  assert.equal(s.asked[0].input.tools[0].function.name, 'set_property');
  const u = s.call(ann, 'fyxacloud.usage');
  assert.equal(u.used, 1);
  assert.equal(u.limit, DEFAULT_LIMITS.free);
  assert.equal(u.budgetLeft, 10000 - neuronsFor({ prompt_tokens: 1000, completion_tokens: 100 }));
  assert.equal('account' in r, false, 'nothing private goes back');
});

test('the daily limit stops the cloud (and says the own-GPU way is free); Boltworks Club gets more', async () => {
  const s = withAi(makeServer());
  const ann = s.signUp('ann');
  ann.fyxaCloud = { day: new Date().toISOString().slice(0, 10), steps: DEFAULT_LIMITS.free };
  const r = s.call(ann, 'fyxacloud.chat', hello);
  assert.equal(r.ok, false);
  assert.match(r.error, /20 Fyxa cloud steps.*Boltworks Club.*Ollama/);
  s.clubOf = () => 'bwc';
  assert.equal(s.call(ann, 'fyxacloud.usage').limit, DEFAULT_LIMITS.bwc);
  assert.ok((await chat(s, ann)).ok, 'a club member can keep going');
});

test('guests can\'t use it, and it says when the server has no AI', () => {
  const s = makeServer();
  const guest = s.user('c'.repeat(64));
  assert.match(s.call(guest, 'fyxacloud.chat', hello).error, /Sign up/);
  assert.match(s.call(s.signUp('ann'), 'fyxacloud.chat', hello).error, /isn't set up/);
});

test('the site-wide budget runs out for everyone; staff can raise it, change limits or switch it off', async () => {
  const s = withAi(makeServer(), { response: 'ok', usage: { prompt_tokens: 400000, completion_tokens: 10 } });
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  assert.ok((await chat(s, ann)).ok);   // ~9,800 neurons
  assert.ok((await chat(s, ann)).ok);   // over 10,000 now
  assert.match(s.call(bob, 'fyxacloud.chat', hello).error, /whole site/);
  assert.equal(s.call(ann, 'admin.fyxaCloud', { budget: 1 }).ok, false, 'only staff');
  assert.ok(s.call(s.staff, 'admin.fyxaCloud', { budget: 100000, limits: { free: 1 } }).ok);
  assert.ok((await chat(s, bob)).ok);
  assert.match(s.call(bob, 'fyxacloud.chat', hello).error, /your 1 Fyxa cloud steps/);
  s.call(s.staff, 'admin.fyxaCloud', { off: true });
  assert.match(s.call(s.signUp('cat'), 'fyxacloud.chat', hello).error, /switched off/);
});

test('when Cloudflare\'s AI fails, the step is given back', async () => {
  const s = withAi(makeServer(), new Error('busy'));
  const ann = s.signUp('ann');
  const r = await chat(s, ann);
  assert.match(r.error, /didn't answer/);
  assert.equal(s.call(ann, 'fyxacloud.usage').used, 0);
});

test('Studio\'s tool calls and tool results go through, and tool calls come back', () => {
  const msgs = toWorkersAi([
    { role: 'user', content: 'hi' },
    { role: 'assistant', content: '', tool_calls: [{ function: { name: 'get_game_tree', arguments: {} } }] },
    { role: 'tool', name: 'get_game_tree', content: '{"tree":""}', images: ['abc'] },
  ]);
  assert.equal(msgs[1].tool_calls[0].function.arguments, '{}');
  assert.equal(msgs[2].role, 'tool');
  assert.equal('images' in msgs[2], false, 'pictures are dropped');
  const m = fromWorkersAi({ response: null, tool_calls: [{ name: 'insert_object', arguments: { class: 'Part' } }] });
  assert.deepEqual(m.tool_calls, [{ function: { name: 'insert_object', arguments: { class: 'Part' } } }]);
  const o = fromWorkersAi({ choices: [{ message: { content: 'x', tool_calls: [{ function: { name: 'undo', arguments: '{"steps":2}' } }] } }] });
  assert.deepEqual(o.tool_calls[0].function.arguments, { steps: 2 });
});
