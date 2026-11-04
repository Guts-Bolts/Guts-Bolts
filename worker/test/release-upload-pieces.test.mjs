import { test } from 'node:test';
import assert from 'node:assert/strict';
import { handleMultipartStep } from '../downloads.js';

// A tiny stand-in for the R2 bucket: enough multipart upload to join pieces back together.
function fakeBucket() {
  const objects = new Map(), uploads = new Map();
  let next = 0;
  const resume = (key, id) => ({
    async uploadPart(n, body) {
      const bytes = new Uint8Array(await new Response(body).arrayBuffer());
      uploads.get(id).set(n, bytes);
      return { partNumber: n, etag: 'e' + n };
    },
    async complete(parts) {
      const pieces = parts.map((p) => uploads.get(id).get(p.partNumber));
      const all = new Uint8Array(pieces.reduce((a, b) => a + b.length, 0));
      let at = 0;
      for (const b of pieces) { all.set(b, at); at += b.length; }
      objects.set(key, all);
    },
  });
  return {
    objects,
    async createMultipartUpload(key) { const id = 'up' + (++next); uploads.set(id, new Map()); return { uploadId: id, key }; },
    resumeMultipartUpload: resume,
    async put(key, body) { objects.set(key, new Uint8Array(await new Response(body).arrayBuffer())); },
    async get(key) {
      const b = objects.get(key);
      return b && { size: b.length, body: new Response(b).body };
    },
  };
}

const meta = { httpMetadata: {} };
const req = (method, body) => new Request('https://x/', {
  method, body, headers: body ? { 'content-length': String(body.length) } : {},
});

test('a big release file goes up in pieces and comes out whole', async () => {
  const env = { DOWNLOADS: fakeBucket() };
  const url = (q) => new URL('https://x/?version=0.7.9' + q);
  const call = (step, r, q = '') => handleMultipartStep(r, env, url(q), step, 'Big.zip', '0.7.9/Big.zip', meta).then((x) => x.json());
  const { uploadId } = await call('start', req('POST'));
  const a = await call('part', req('PUT', new Uint8Array([1, 2])), `&uploadId=${uploadId}&n=1`);
  const b = await call('part', req('PUT', new Uint8Array([3])), `&uploadId=${uploadId}&n=2`);
  const done = await call('finish', req('POST', JSON.stringify({ parts: [a.part, b.part] })), `&uploadId=${uploadId}`);
  assert.equal(done.ok, true);
  assert.equal(done.size, 3);
  assert.deepEqual([...env.DOWNLOADS.objects.get('latest/Big.zip')], [1, 2, 3]);
});

test('a piece needs a real part number and an upload id', async () => {
  const env = { DOWNLOADS: fakeBucket() };
  const r1 = await handleMultipartStep(req('PUT', new Uint8Array([1])), env, new URL('https://x/?uploadId=u&n=0'), 'part', 'A.zip', '1/A.zip', meta);
  assert.equal(r1.status, 400);
  const r2 = await handleMultipartStep(req('PUT', new Uint8Array([1])), env, new URL('https://x/?n=1'), 'part', 'A.zip', '1/A.zip', meta);
  assert.equal(r2.status, 400);
});
