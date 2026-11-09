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
