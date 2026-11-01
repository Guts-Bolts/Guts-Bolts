import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp } from './helpers.mjs';

test('database diagnostics are available only to staff and contain aggregate metadata', () => {
  const s = makeServer();
  const allowed = s.op('staff.database.stats', s.staff, {});
  assert.equal(allowed.ok, true);
  assert.equal(typeof allowed.database.databaseSizeBytes, 'number');
  assert.equal(allowed.database.tableRows.users, 1);
  assert.equal(allowed.database.tableRows.assets >= 0, true);
  assert.equal(allowed.database.totalRows > 0, true);

  const player = signUp(s, 'DiagnosticsPlayer');
  const denied = s.op('staff.database.stats', player, {});
  assert.equal(denied.ok, false);
  assert.match(denied.error, /Only staff/);
});

test('database backup writes a compressed snapshot and manifest to private R2 binding', async () => {
  const s = makeServer();
  const stored = new Map();
  s.sql.exec('INSERT INTO files (id, part, data) VALUES (?, ?, ?)', 'blob-test', 0, new Uint8Array([1, 2, 3]));
  s.env.DB_BACKUPS = {
    async put(key, value, options) {
      const bytes = await new Response(value).arrayBuffer();
      stored.set(key, { bytes, options });
      return { etag: 'test-etag' };
    },
    async list() { return { objects: [] }; },
    async delete() {},
  };

  const result = await s.backupToR2();
  assert.equal(result.ok, true);
  assert.match(result.key, /^daily\/\d{4}-\d{2}-\d{2}\/database\.json\.gz$/);
  assert.equal(stored.has(result.key), true);
  assert.equal(stored.has(result.key.replace('database.json.gz', 'manifest.json')), true);

  const compressed = stored.get(result.key).bytes;
  const decompressed = await new Response(new Blob([compressed]).stream().pipeThrough(new DecompressionStream('gzip'))).text();
  const snapshot = JSON.parse(decompressed);
  assert.equal(snapshot.format, 'guts-and-bolts-sqlite-json-v1');
  assert.equal(snapshot.tables.find((table) => table.name === 'users').rows.length, 1);
  assert.equal(snapshot.tables.find((table) => table.name === 'assets').schema !== null, true);
  const blob = snapshot.tables.find((table) => table.name === 'files').rows[0];
  assert.equal(blob.data.$gbBinary, 'AQID');
  const manifest = JSON.parse(new TextDecoder().decode(stored.get(result.key.replace('database.json.gz', 'manifest.json')).bytes));
  assert.equal(manifest.sha256, result.sha256);
  assert.equal(manifest.tableRows.files, 1);
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(decompressed));
  const verified = Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, '0')).join('');
  assert.equal(verified, result.sha256);
});
