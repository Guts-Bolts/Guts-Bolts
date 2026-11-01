import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readLimitedText } from '../http.js';

const post = (body) => new Request('https://gutsandbolts.net/api', { method: 'POST', body });

test('bounded body reader accepts a request exactly at its byte limit', async () => {
  assert.equal(await readLimitedText(post(new Uint8Array([123, 125, 32, 32])), 4), '{}  ');
});

test('bounded body reader rejects a request larger than its byte limit', async () => {
  assert.equal(await readLimitedText(post(new Uint8Array([123, 125, 32, 32, 32])), 4), null);
});

test('bounded body reader measures UTF-8 bytes, not JavaScript characters', async () => {
  assert.equal(await readLimitedText(post('éé'), 3), null);
  assert.equal(await readLimitedText(post('éé'), 4), 'éé');
});

test('bounded body reader handles a request without a body', async () => {
  const request = new Request('https://gutsandbolts.net/api', { method: 'POST' });
  assert.equal(await readLimitedText(request, 4), '');
});

test('bounded body reader rejects invalid limits', async () => {
  await assert.rejects(() => readLimitedText(post('x'), -1), /Invalid body limit/);
});
