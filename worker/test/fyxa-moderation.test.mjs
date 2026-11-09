import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp, device } from './helpers.mjs';

test('Fyxa moderation screens group wall posts and edited group descriptions', async () => {
  const AI = {
    async run() {
      return { response: 'unsafe\nS10' };
    },
  };
  const s = makeServer({ AI });
  const d = device(s, 2);
  const owner = signUp(s, 'Alice', d.account);
  const made = s.call(owner, 'groups.create', {
    name: 'Builders United',
    description: 'A group for building games.',
  });
  assert.ok(made.ok, made.error);

  const postArgs = { id: made.group.id, text: 'A wall post that the moderation scanner should inspect.' };
  const posted = d.call('groups.post', postArgs);
  assert.ok(posted.ok, posted.error);
  await s.scanModerationSubmission({ op: 'groups.post', args: postArgs }, posted.moderationActor, posted);

  const editArgs = { id: made.group.id, description: 'An updated group description that must also be screened.' };
  const edited = d.call('groups.edit', editArgs);
  assert.ok(edited.ok, edited.error);
  await s.scanModerationSubmission({ op: 'groups.edit', args: editArgs }, edited.moderationActor, edited);

  const denied = s.call(owner, 'ai.moderation.queue', {});
  assert.equal(denied.ok, false, 'ordinary users cannot inspect moderation cases');

  const queue = s.call(s.staff, 'ai.moderation.queue', {});
  assert.ok(queue.ok, queue.error);
  assert.equal(queue.items.length, 2);
  assert.ok(queue.items.some((item) => item.sourceType === 'group_post' && item.sourceId === made.group.id));
  assert.ok(queue.items.some((item) => item.sourceType === 'group' && item.sourceId === made.group.id));
});


test('Fyxa moderation screens game-setting edits and script text in updated assets', async () => {
  const scanned = [];
  const AI = {
    async run(_model, input) {
      scanned.push(input.messages?.[0]?.content || '');
      return { response: 'unsafe\\nS10' };
    },
  };
  const s = makeServer({ AI });
  const owner = s.signUp('Alice');
  const game = s.addGame(owner, 'Existing game');
  await s.scanModerationSubmission({
    op: 'game.settings',
    args: { id: game.id, description: 'An edited game description.' },
  }, owner, { ok: true });

  const plugin = { id: 'plugin-1', kind: 'plugin', creator: owner.id, name: 'Test plugin', meta: {} };
  s.assets.set(plugin.id, plugin);
  s.saveAsset(plugin);
  const data = Buffer.from(JSON.stringify({ scripts: [{ source: "print('plugin source')" }] })).toString('base64');
  await s.scanModerationSubmission({
    op: 'update',
    args: { id: plugin.id, name: 'Updated plugin', description: 'Updated description', data },
  }, owner, { ok: true });

  assert.ok(scanned.some((text) => text.includes('An edited game description.')));
  assert.ok(scanned.some((text) => text.includes("print('plugin source')")));
  const queue = s.call(s.staff, 'ai.moderation.queue', {});
  assert.ok(queue.ok, queue.error);
  assert.equal(queue.items.length, 2);
  assert.ok(queue.items.some((item) => item.sourceId === game.id));
  assert.ok(queue.items.some((item) => item.sourceId === plugin.id));
});

test('opaque 3D geometry is routed to manual review even when text screening is safe', async () => {
  const AI = { async run() { return { response: 'safe' }; } };
  const s = makeServer({ AI });
  const owner = s.signUp('Bob');
  const model = { id: 'model-1', kind: 'model', creator: owner.id, name: '3D model', meta: {} };
  s.assets.set(model.id, model);
  s.saveAsset(model);
  const data = Buffer.from(JSON.stringify({ geometry: { vertices: [[0, 0, 0]], indices: [0] } })).toString('base64');

  await s.scanModerationSubmission({
    op: 'upload',
    args: { kind: 'model', name: model.name, description: 'A model with geometry.', data },
  }, owner, { ok: true, asset: model });

  const queue = s.call(s.staff, 'ai.moderation.queue', {});
  assert.ok(queue.ok, queue.error);
  assert.ok(queue.items.some((item) => item.sourceId === model.id &&
    item.categories.includes('GEOMETRY_REVIEW_REQUIRED')));
});


test('Fyxa moderation fails closed when the text scanner returns an unstructured response', async () => {
  const AI = { async run() { return {}; } };
  const s = makeServer({ AI });
  const owner = s.signUp('Carol');
  await s.scanModerationSubmission({
    op: 'profile.set',
    args: { blurb: 'A profile update that requires screening.' },
  }, owner, { ok: true });

  const queue = s.call(s.staff, 'ai.moderation.queue', {});
  assert.ok(queue.ok, queue.error);
  assert.ok(queue.items.some((item) => item.sourceId === owner.id &&
    item.categories.includes('SCAN_UNAVAILABLE')));
});


test('embedded texture payloads are routed to manual review', async () => {
  const AI = { async run() { return { response: 'safe' }; } };
  const s = makeServer({ AI });
  const owner = s.signUp('Drew');
  const model = { id: 'model-texture-1', kind: 'model', creator: owner.id, name: 'Textured model', meta: {} };
  s.assets.set(model.id, model);
  s.saveAsset(model);
  const data = Buffer.from(JSON.stringify({ normalTextureMap: 'A'.repeat(256) })).toString('base64');

  await s.scanModerationSubmission({
    op: 'upload',
    args: { kind: 'model', name: model.name, description: 'A textured model.', data },
  }, owner, { ok: true, asset: model });

  const queue = s.call(s.staff, 'ai.moderation.queue', {});
  assert.ok(queue.ok, queue.error);
  assert.ok(queue.items.some((item) => item.sourceId === model.id &&
    item.categories.includes('GEOMETRY_REVIEW_REQUIRED')));
});


test('payload-only model updates still create manual-review cases without text fields', async () => {
  const AI = { async run() { return { response: 'safe' }; } };
  const s = makeServer({ AI });
  const owner = s.signUp('Payload reviewer');
  const model = { id: 'model-payload-only', kind: 'model', creator: owner.id, name: '', meta: {} };
  s.assets.set(model.id, model);
  s.saveAsset(model);
  const data = Buffer.from(JSON.stringify({ geometry: { vertices: [[0, 0, 0]], indices: [0] } })).toString('base64');

  await s.scanModerationSubmission({
    op: 'update',
    args: { id: model.id, data },
  }, owner, { ok: true });

  const queue = s.call(s.staff, 'ai.moderation.queue', {});
  assert.ok(queue.ok, queue.error);
  assert.ok(queue.items.some((item) => item.sourceId === model.id &&
    item.categories.includes('GEOMETRY_REVIEW_REQUIRED')));
});


test('repeated identical moderation submissions increment occurrence counts and retain strongest signals', async () => {
  let scans = 0;
  const AI = {
    async run() {
      scans += 1;
      return { response: scans === 1 ? 'unsafe\\nS1' : 'unsafe\\nS4' };
    },
  };
  const s = makeServer({ AI });
  const owner = s.signUp('Repeat reporter');
  const request = { op: 'profile.set', args: { blurb: 'The same flagged profile text.' } };
  await s.scanModerationSubmission(request, owner, { ok: true });
  const firstQueue = s.call(s.staff, 'ai.moderation.queue', {});
  assert.ok(firstQueue.ok && firstQueue.items.length === 1);
  const dismissed = s.call(s.staff, 'ai.moderation.review', {
    caseId: firstQueue.items[0].id, outcome: 'dismissed', note: 'Reviewed as allowed in context.',
  });
  assert.ok(dismissed.ok, dismissed.error);
  await s.scanModerationSubmission(request, owner, { ok: true });

  const queue = s.call(s.staff, 'ai.moderation.queue', {});
  assert.ok(queue.ok, queue.error);
  assert.equal(queue.items.length, 1, 'identical submissions remain grouped into one case');
  assert.equal(queue.items[0].occurrences, 2);
  assert.ok(queue.items[0].lastSeenAt >= queue.items[0].createdAt);
  assert.equal(queue.items[0].severity, 'critical', 'a later higher-severity signal is retained');
  assert.ok(queue.items[0].categories.includes('S1'));
  assert.ok(queue.items[0].categories.includes('S4'));
});


test('accepted HTTP and WebSocket submissions both schedule tracked moderation scans', async () => {
  const AI = { async run() { return { response: 'unsafe\\nS10' }; } };
  const s = makeServer({ AI });
  const d = device(s, 31);
  const owner = signUp(s, 'Transport sender', d.account);
  const group = s.call(owner, 'groups.create', {
    name: 'Transport moderation group',
    description: 'A group for testing moderation dispatch.',
  });
  assert.ok(group.ok, group.error);

  const httpReq = d.request('groups.post', {
    id: group.group.id,
    text: 'HTTP route moderation integration test content.',
  });
  const httpResponse = await s.fetch(new Request('https://gb.test/api', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(httpReq),
  }));
  const httpAnswer = await httpResponse.json();
  assert.ok(httpAnswer.ok, httpAnswer.error);
  await Promise.all([...s.backgroundTasks]);

  const sent = [];
  const connection = {
    id: 'moderation-integration-ws',
    mode: 'request',
    ip: '127.0.0.1',
    window: 0,
    sent: 0,
    ws: { send(data) { sent.push(JSON.parse(data)); } },
  };
  s.conns.set(connection.id, connection);
  const wsReq = d.request('groups.post', {
    id: group.group.id,
    text: 'WebSocket route moderation integration test content.',
  });
  s.onMessage(connection, JSON.stringify(wsReq));
  await Promise.all([...s.backgroundTasks]);
  assert.ok(sent[0] && sent[0].ok, sent[0] && sent[0].error);

  const queue = s.call(s.staff, 'ai.moderation.queue', {});
  assert.ok(queue.ok, queue.error);
  assert.ok(queue.items.some((item) => item.excerpt.includes('HTTP route moderation integration test content.')));
  assert.ok(queue.items.some((item) => item.excerpt.includes('WebSocket route moderation integration test content.')));
});
