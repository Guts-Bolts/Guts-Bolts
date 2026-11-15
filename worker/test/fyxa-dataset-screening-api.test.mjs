import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

function eligibleGame(s, owner, source, name = 'Training candidate') {
  const asset = s.addGame(owner, name);
  const bytes = new TextEncoder().encode(JSON.stringify(source));
  s.writeFile(asset.id, bytes);
  asset.size = bytes.length;
  s.saveAsset(asset);
  s.flush();
  const attested = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
  });
  assert.ok(attested.ok, attested.error);
  const approved = s.call(s.staff, 'ai.content.review', {
    contentId: asset.id, rightsStatus: 'approved', reason: 'Reviewed creator rights evidence',
  });
  assert.ok(approved.ok, approved.error);
  return asset;
}

test('only staff can run and inspect Fyxa dataset screening', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  assert.equal(s.call(owner, 'ai.datasets.admin.screen').ok, false);
  assert.equal(s.call(owner, 'ai.datasets.admin.screenings').ok, false);
});

test('screening reads only currently eligible public games and never exports source text', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = eligibleGame(s, owner, {
    format: 'gbscene', version: 2,
    info: { title: 'Example obby', description: 'A public obstacle course.' },
    nodes: [{ kind: 'Script', name: 'RoundController', source: 'local Players = game:GetService("Players")' }],
  });
  const result = s.call(s.staff, 'ai.datasets.admin.screen', { limit: 5 });
  assert.ok(result.ok, result.error);
  assert.equal(result.datasetExportEnabled, false);
  assert.equal(result.items.length, 1);
  assert.equal(result.items[0].contentId, asset.id);
  assert.equal(result.items[0].screening.status, 'screened_pending_human_and_age_review');
  assert.equal(result.items[0].screening.datasetEligible, false);
  assert.equal(result.items[0].screening.ageStatus, 'not_verified');
  assert.equal(JSON.stringify(result).includes('local Players'), false);
  const stored = [...s.sql.exec('SELECT result_json FROM ai_dataset_screenings WHERE content_id = ?', asset.id)][0];
  assert.ok(stored);
  assert.equal(stored.result_json.includes('local Players'), false);
});

test('secret-bearing game source is blocked and only reason codes are retained', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = eligibleGame(s, owner, {
    format: 'gbscene',
    nodes: [{ kind: 'Script', name: 'Config', source: 'local api_key = "sk_test_1234567890abcdef"' }],
  });
  const result = s.call(s.staff, 'ai.datasets.admin.screen', {});
  assert.ok(result.ok, result.error);
  assert.equal(result.items[0].contentId, asset.id);
  assert.equal(result.items[0].screening.status, 'blocked_sensitive_material');
  assert.ok(result.items[0].screening.reasons.includes('generic_secret_assignment'));
});

test('screening results are invalidated when the game source revision changes', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = eligibleGame(s, owner, { info: { description: 'First revision.' } });
  const first = s.call(s.staff, 'ai.datasets.admin.screen', {});
  assert.ok(first.ok);
  assert.equal([...s.sql.exec('SELECT COUNT(*) AS count FROM ai_dataset_screenings WHERE content_id = ?', asset.id)][0].count, 1);

  const bytes = new TextEncoder().encode(JSON.stringify({ info: { description: 'Second revision.' } }));
  s.writeFile(asset.id, bytes);
  asset.size = bytes.length;
  asset.updated += 1;
  s.saveAsset(asset);
  s.flush();
  assert.equal([...s.sql.exec('SELECT COUNT(*) AS count FROM ai_dataset_screenings WHERE content_id = ?', asset.id)][0].count, 0);
});

test('opt-out and deletion remove screening metadata and prevent further candidate selection', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = eligibleGame(s, owner, { info: { description: 'Candidate.' } });
  assert.ok(s.call(s.staff, 'ai.datasets.admin.screen', {}).ok);
  assert.equal([...s.sql.exec('SELECT COUNT(*) AS count FROM ai_dataset_screenings WHERE owner_id = ?', owner.id)][0].count, 1);

  assert.ok(s.call(owner, 'ai.preferences.update', { trainingEnabled: false }).ok);
  assert.equal([...s.sql.exec('SELECT COUNT(*) AS count FROM ai_dataset_screenings WHERE owner_id = ?', owner.id)][0].count, 0);
  assert.equal(s.call(s.staff, 'ai.datasets.admin.screen', {}).items.length, 0);

  // A staff deletion still clears any screening metadata attached to the source.
  s.sql.exec('INSERT OR REPLACE INTO ai_dataset_screenings (content_id, owner_id, source_revision, result_json, scanned_at) VALUES (?, ?, ?, ?, ?)',
    asset.id, owner.id, 'stale', '{}', Math.floor(Date.now() / 1000));
  assert.ok(s.call(owner, 'delete', { id: asset.id }).ok);
  assert.equal([...s.sql.exec('SELECT COUNT(*) AS count FROM ai_dataset_screenings WHERE content_id = ?', asset.id)][0].count, 0);
});
