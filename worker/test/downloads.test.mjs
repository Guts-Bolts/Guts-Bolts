import { test } from 'node:test';
import assert from 'node:assert/strict';
import { isTrustedReleaseClaims } from '../downloads.js';

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
