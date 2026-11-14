import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  handleDownload, mayPublishNightly, normalizeNightlyManifest, normalizeReleaseManifest, nightlyKey, publishNightly, pruneNightlies,
} from '../downloads.js';

const A = 'a'.repeat(40), B = 'b'.repeat(40), C = 'c'.repeat(40);
const REPO = 'Guts-Bolts/Guts-Bolts';
const claims = (ref) => ({ repository: REPO, repository_id: '1266919304', event_name: 'push', ref,
  workflow_ref: REPO + '/.github/workflows/build.yml@' + ref });

// Just enough of an R2 bucket for these checks.
function bucket() {
  const objects = new Map();
  return {
    objects,
    async put(key, body) { objects.set(key, typeof body === 'string' ? body : new Uint8Array(await new Response(body).arrayBuffer())); },
    async get(key) {
      if (!objects.has(key)) return null;
      const v = objects.get(key);
      return { size: v.length, body: new Response(v).body, text: async () => (typeof v === 'string' ? v : new TextDecoder().decode(v)),
        writeHttpMetadata() {}, httpEtag: '"x"' };
    },
    async head(key) { return objects.has(key) ? { size: 1 } : null; },
    async list({ prefix }) { return { objects: [...objects.keys()].filter((k) => k.startsWith(prefix)).map((key) => ({ key })), truncated: false }; },
    async delete(keys) { for (const k of [].concat(keys)) objects.delete(k); },
  };
}
const manifest = (commit, commitTime) => normalizeNightlyManifest({
  version: '0.8.0', commit, commitTime, files: { windows: 'GutsAndBolts-Windows.zip', linux: 'GutsAndBolts-Linux.tar.gz' },
});
const upload = (env, commit) => {
  env.DOWNLOADS.objects.set(nightlyKey(commit, 'GutsAndBolts-Windows.zip'), 'win ' + commit);
  env.DOWNLOADS.objects.set(nightlyKey(commit, 'GutsAndBolts-Linux.tar.gz'), 'linux ' + commit);
};

test('nightly manifests need a full commit, a time and desktop files only', () => {
  const m = manifest(A, 1794000000);
  assert.equal(m.commit, A);
  assert.equal(m.date, '2026-11-06');
  assert.equal(normalizeNightlyManifest({ version: '0.8.0', commit: 'abc', commitTime: 1, files: { linux: 'x.tar.gz' } }), null);
  assert.equal(normalizeNightlyManifest({ version: '0.8.0', commit: A, files: { linux: 'x.tar.gz' } }), null);
  assert.equal(normalizeNightlyManifest({ version: '0.8.0', commit: A, commitTime: 1, files: { android: 'x.apk' } }), null);
  assert.equal(normalizeNightlyManifest({ version: '0.8.0', commit: A, commitTime: 1, files: { linux: '../latest.json' } }), null);
});

test('only the build workflow on main may publish nightly builds (not version tags)', () => {
  assert.equal(mayPublishNightly(claims('refs/heads/main')), true);
  assert.equal(mayPublishNightly(claims('refs/tags/v1.0.0')), false);
  assert.equal(mayPublishNightly(claims('refs/heads/feature')), false);
  assert.equal(mayPublishNightly(null), false);
});

test('releases may carry their commit, so the Player can tell them from a nightly', () => {
  const r = normalizeReleaseManifest({ version: '0.8.0', commit: A, files: { linux: 'GutsAndBolts-Linux.tar.gz' } });
  assert.equal(r.commit, A);
  assert.equal(normalizeReleaseManifest({ version: '0.8.0', commit: 'nope', files: { linux: 'x.tar.gz' } }), null);
});

test('publishing a nightly never touches the stable download', async () => {
  const env = { DOWNLOADS: bucket() };
  env.DOWNLOADS.objects.set('latest.json', '{"version":"0.8.0"}');
  env.DOWNLOADS.objects.set('latest/GutsAndBolts-Windows.zip', 'stable win');
  upload(env, A);
  const r = await publishNightly(env, manifest(A, 100));
  assert.equal(r.status, 200);
  assert.equal(env.DOWNLOADS.objects.get('latest.json'), '{"version":"0.8.0"}');
  assert.equal(env.DOWNLOADS.objects.get('latest/GutsAndBolts-Windows.zip'), 'stable win');
  assert.equal(JSON.parse(env.DOWNLOADS.objects.get('nightly.json')).commit, A);
});

test('a nightly with missing files is refused', async () => {
  const env = { DOWNLOADS: bucket() };
  env.DOWNLOADS.objects.set(nightlyKey(A, 'GutsAndBolts-Windows.zip'), 'w');
  const r = await publishNightly(env, manifest(A, 100));
  assert.equal(r.status, 400);
  assert.equal(env.DOWNLOADS.objects.has('nightly.json'), false);
});

test('an older build finishing late never replaces a newer nightly', async () => {
  const env = { DOWNLOADS: bucket() };
  upload(env, B);
  assert.equal((await publishNightly(env, manifest(B, 200))).status, 200);
  upload(env, A);
  assert.equal((await publishNightly(env, manifest(A, 100))).status, 409);
  assert.equal(JSON.parse(env.DOWNLOADS.objects.get('nightly.json')).commit, B);
});

test('only the newest two nightly builds are kept (free storage stays small)', async () => {
  const env = { DOWNLOADS: bucket() };
  upload(env, A); await publishNightly(env, manifest(A, 100));
  upload(env, B); await publishNightly(env, manifest(B, 200));
  upload(env, C); await publishNightly(env, manifest(C, 300));
  const commits = new Set([...env.DOWNLOADS.objects.keys()].filter((k) => k.startsWith('nightly/')).map((k) => k.split('/')[1]));
  assert.deepEqual([...commits].sort(), [B, C]);
  assert.equal(await pruneNightlies(env, new Set([C])), 2);
});

test('the Player can read nightly.json and download a nightly file', async () => {
  const env = { DOWNLOADS: bucket() };
  upload(env, A); await publishNightly(env, manifest(A, 100));
  const get = (path) => handleDownload(new Request('https://gutsandbolts.net' + path), env, new URL('https://gutsandbolts.net' + path));
  const j = await (await get('/download/nightly.json')).json();
  assert.equal(j.commit, A);
  assert.equal(j.channel, 'nightly');
  const f = await get('/download/nightly/' + A + '/GutsAndBolts-Windows.zip');
  assert.equal(f.status, 200);
  assert.equal(await f.text(), 'win ' + A);
  assert.equal((await get('/download/nightly/' + A + '/..%2Flatest.json')).status, 404);
  assert.equal((await get('/download/nightly/short/GutsAndBolts-Windows.zip')).status, 404);
});

test('uploads to the nightly channel need a token (no anonymous uploads)', async () => {
  const env = { DOWNLOADS: bucket() };
  const path = '/download/upload/GutsAndBolts-Windows.zip?channel=nightly&commit=' + A;
  const r = await handleDownload(new Request('https://gutsandbolts.net' + path, { method: 'PUT', body: 'x' }), env, new URL('https://gutsandbolts.net' + path));
  assert.equal(r.status, 403);
});
