import { test } from 'node:test';
import assert from 'node:assert/strict';
import { screenFyxaTrainingSource, MAX_FYXA_SCREEN_BYTES } from '../fyxa-dataset-screening.js';

const encode = (value) => new TextEncoder().encode(JSON.stringify(value));

test('a clean text-only scene is screened but never marked dataset eligible', () => {
  const scan = screenFyxaTrainingSource(encode({
    format: 'gbscene', version: 2,
    info: { title: 'Starter obby', description: 'A simple obstacle course.' },
    nodes: [{ kind: 'Script', name: 'RoundController', source: 'local Players = game:GetService("Players")' }],
  }));
  assert.equal(scan.status, 'screened_pending_human_and_age_review');
  assert.equal(scan.datasetEligible, false);
  assert.equal(scan.ageStatus, 'not_verified');
  assert.equal(scan.textFields >= 2, true);
  assert.equal(Object.hasOwn(scan, 'extractedText'), false);
});

test('credentials and secret-like values block a source', () => {
  const scan = screenFyxaTrainingSource(encode({
    info: { title: 'Example' },
    scripts: [{ name: 'Config', source: 'local api_key = "sk_test_1234567890abcdef"' }],
  }));
  assert.equal(scan.status, 'blocked_sensitive_material');
  assert.ok(scan.reasons.includes('generic_secret_assignment'));
  assert.equal(scan.datasetEligible, false);
});

test('personal data patterns require manual review without returning source text', () => {
  const scan = screenFyxaTrainingSource(encode({
    info: { description: 'Contact player@example.com for help.' },
  }));
  assert.equal(scan.status, 'manual_review');
  assert.ok(scan.reasons.includes('email_address'));
  assert.equal(JSON.stringify(scan).includes('player@example.com'), false);
});

test('opaque geometry, binary media, and malformed sources are never passed as clean', () => {
  const geometry = screenFyxaTrainingSource(encode({ nodes: [{ vertices: [1, 2, 3] }] }));
  assert.equal(geometry.status, 'manual_review');
  assert.ok(geometry.reasons.includes('opaque_geometry_or_media'));
  const malformed = screenFyxaTrainingSource(new TextEncoder().encode('{not json'));
  assert.equal(malformed.status, 'manual_review');
  assert.ok(malformed.reasons.includes('source_not_valid_utf8_json'));
});

test('oversized, empty, and invalid inputs fail closed', () => {
  assert.equal(screenFyxaTrainingSource(new Uint8Array()).status, 'manual_review');
  assert.equal(screenFyxaTrainingSource('not bytes').status, 'manual_review');
  assert.ok(screenFyxaTrainingSource(new Uint8Array(MAX_FYXA_SCREEN_BYTES + 1)).reasons.includes('source_exceeds_screening_limit'));
});
