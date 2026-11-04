import { test } from 'node:test';
import assert from 'node:assert/strict';
import { knownLength, limitUploadStream } from '../downloads.js';

// Cloudflare's FixedLengthStream, faked: an identity stream that remembers its size.
class FakeFixedLengthStream extends TransformStream {
  constructor(length) { super(); this.readable.expectedLength = length; }
}

test('release uploads reach R2 as a stream with a known length', async () => {
  globalThis.FixedLengthStream = FakeFixedLengthStream;
  try {
    const body = new Response(new Uint8Array([1, 2, 3])).body;
    const out = knownLength(limitUploadStream(body, 10), 3);
    assert.equal(out.expectedLength, 3);
    assert.deepEqual([...new Uint8Array(await new Response(out).arrayBuffer())], [1, 2, 3]);
  } finally {
    delete globalThis.FixedLengthStream;
  }
});

test('without FixedLengthStream (not on Cloudflare) the stream is passed through', () => {
  const body = new Response(new Uint8Array([1])).body;
  assert.equal(knownLength(body, 1), body);
});
