import { test } from 'node:test';
import assert from 'node:assert/strict';
import { isTrustedReleaseClaims, limitUploadStream, normalizeReleaseManifest } from '../downloads.js';

const REPO = 'Guts-Bolts/Guts-Bolts';
const workflow = REPO + '/.github/workflows/build.yml';
const claims = (ref = 'refs/heads/main', extra = {}) => ({
  repository: REPO,
  repository_id: '1266919304',
  event_name: 'push',
  ref,
  workflow_ref: workflow + '@' + ref,
  ...extra,
});

test('download publishing accepts only the designated release workflow on main or version tags', () => {
  assert.equal(isTrustedReleaseClaims(claims()), true);
  assert.equal(isTrustedReleaseClaims(claims('refs/tags/v1.2.3')), true);
  assert.equal(isTrustedReleaseClaims(claims('refs/heads/feature/test')), false);
});

test('download publishing rejects tokens from other workflows and event types', () => {
  assert.equal(isTrustedReleaseClaims(claims('refs/heads/main', {
    workflow_ref: REPO + '/.github/workflows/other.yml@refs/heads/main',
  })), false);
  assert.equal(isTrustedReleaseClaims(claims('refs/heads/main', { event_name: 'pull_request' })), false);
  assert.equal(isTrustedReleaseClaims(claims('refs/heads/main', { repository_id: '999' })), false);
  assert.equal(isTrustedReleaseClaims(claims('refs/heads/main', { repository: 'attacker/Guts-Bolts' })), false);
  assert.equal(isTrustedReleaseClaims(claims('refs/tags/vbeta')), false);
});

test('release manifests accept only known platform keys and safe filenames', () => {
  assert.deepEqual(normalizeReleaseManifest({
    version: 'v1.2.3',
    name: 'Release 1.2.3',
    notes: 'Release notes',
    files: { windows: 'GutsAndBolts-Windows.zip', linux: 'GutsAndBolts-Linux.tar.gz' },
  }), {
    version: '1.2.3',
    name: 'Release 1.2.3',
    notes: 'Release notes',
    files: { windows: 'GutsAndBolts-Windows.zip', linux: 'GutsAndBolts-Linux.tar.gz' },
  });
});

test('release manifests reject path traversal, unknown platforms and malformed metadata', () => {
  assert.equal(normalizeReleaseManifest({ version: '../latest', files: { windows: 'app.zip' } }), null);
  assert.equal(normalizeReleaseManifest({ version: '1.0', files: { unknown: 'app.zip' } }), null);
  assert.equal(normalizeReleaseManifest({ version: '1.0', files: { windows: '../app.zip' } }), null);
  assert.equal(normalizeReleaseManifest({ version: '1.0', name: 'x'.repeat(161), files: { windows: 'app.zip' } }), null);
  assert.equal(normalizeReleaseManifest({ version: '1.0', files: {} }), null);
});

test('release upload stream passes a body within its byte limit', async () => {
  const stream = new ReadableStream({ start(controller) { controller.enqueue(new Uint8Array([1, 2])); controller.close(); } });
  const bytes = new Uint8Array(await new Response(limitUploadStream(stream, 2)).arrayBuffer());
  assert.deepEqual([...bytes], [1, 2]);
});

test('release upload stream rejects a body above its byte limit', async () => {
  const stream = new ReadableStream({ start(controller) { controller.enqueue(new Uint8Array([1, 2, 3])); controller.close(); } });
  await assert.rejects(() => new Response(limitUploadStream(stream, 2)).arrayBuffer(), /Release upload is too large/);
});
