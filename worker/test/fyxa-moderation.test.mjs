import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp, device } from './helpers.mjs';

test('Fyxa moderation screens group wall posts and edited group descriptions', () => {
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

  const posted = d.call('groups.post', {
    id: made.group.id,
    text: 'A wall post that the moderation scanner should inspect.',
  });
  assert.ok(posted.ok, posted.error);

  const edited = d.call('groups.edit', {
    id: made.group.id,
    description: 'An updated group description that must also be screened.',
  });
  assert.ok(edited.ok, edited.error);

  const denied = s.call(owner, 'ai.moderation.queue', {});
  assert.equal(denied.ok, false, 'ordinary users cannot inspect moderation cases');

  const queue = s.call(s.staff, 'ai.moderation.queue', {});
  assert.ok(queue.ok, queue.error);
  assert.equal(queue.items.length, 2);
  assert.ok(queue.items.some((item) => item.sourceType === 'group_post' && item.sourceId === made.group.id));
  assert.ok(queue.items.some((item) => item.sourceType === 'group' && item.sourceId === made.group.id));
});
