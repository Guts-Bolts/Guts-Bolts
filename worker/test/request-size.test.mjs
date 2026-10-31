import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readLimitedText } from '../server.js';

const post = (body) => new Request('https://gutsandbolts.net/api', { method: 'POST', body });

test('API body reader accepts a request at the configured byte limit', async () => {
  const request = post(new Uint8Array([123, 125, 32, 32]));
  assert.equal(await readLimitedText(request, 4), '{}  ');
});

test('API body reader rejects a request that exceeds the byte limit', async () => {
  const request = post(new Uint8Array([123, 125, 32, 32, 32]));
  assert.equal(await readLimitedText(request, 4), null);
});

test('API body reader handles requests without a body', async () => {
  const request = new Request('https://gutsandbolts.net/api', { method: 'POST' });
  assert.equal(await readLimitedText(request, 4), '');
});
