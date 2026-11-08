import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const NOTICE = '2026-11-08-v1';

function enableTraining(s, user, extra = {}) {
  return s.call(user, 'ai.preferences.update', Object.assign({
    trainingEnabled: true, confirm: true, noticeVersion: NOTICE,
  }, extra));
}

test('Fyxa preferences are disabled by default and require the current notice to opt in', () => {
  const s = makeServer();
  const user = s.signUp('alice');

  const initial = s.call(user, 'ai.preferences.get');
  assert.ok(initial.ok);
  assert.equal(initial.preferences.trainingEnabled, false);
  assert.equal(initial.preferences.referenceEnabled, false);
  assert.equal(initial.preferences.currentNoticeVersion, NOTICE);

  const missingNotice = s.call(user, 'ai.preferences.update', { trainingEnabled: true, confirm: true });
  assert.equal(missingNotice.ok, false);

  const missingConfirmation = s.call(user, 'ai.preferences.update', {
    trainingEnabled: true, noticeVersion: NOTICE,
  });
  assert.equal(missingConfirmation.ok, false);

  const enabled = enableTraining(s, user);
  assert.ok(enabled.ok, enabled.error);
  assert.equal(enabled.preferences.trainingEnabled, true);
  assert.equal(enabled.preferences.referenceEnabled, false);
  assert.equal(enabled.preferences.consentMethod, 'affirmative_opt_in');
  assert.equal(enabled.preferences.noticeVersion, NOTICE);
});

test('content cannot become training eligible until the owner opts in, attests rights, and staff approves', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = s.addGame(owner, 'Public game');
  const enabled = enableTraining(s, owner);
  assert.ok(enabled.ok, enabled.error);

  const noAttestation = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include',
  });
  assert.equal(noAttestation.ok, false);

  const attested = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
  });
  assert.ok(attested.ok, attested.error);
  assert.equal(attested.eligibility.rightsStatus, 'pending_review');
  assert.equal(attested.eligibility.trainingEligible, false);
  assert.equal(attested.eligibility.trainingExclusionReason, 'rights_pending_review');

  const approved = s.call(s.staff, 'ai.content.review', {
    contentId: asset.id, rightsStatus: 'approved', reason: 'Reviewed rights evidence',
  });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.trainingEligible, true);
  assert.equal(approved.eligibility.trainingExclusionReason, null);
});

test('private content remains excluded even after opt-in, rights attestation, and staff approval', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = s.addGame(owner, 'Private game', { access: 'private' });
  assert.ok(enableTraining(s, owner, { referenceEnabled: true }).ok);

  const attested = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', referenceChoice: 'include', rightsAttested: true,
  });
  assert.ok(attested.ok, attested.error);

  const approved = s.call(s.staff, 'ai.content.review', {
    contentId: asset.id, rightsStatus: 'approved', reason: 'Creator rights attestation reviewed',
  });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.privacyStatus, 'private');
  assert.equal(approved.eligibility.trainingEligible, false);
  assert.equal(approved.eligibility.referenceEligible, false);
  assert.equal(approved.eligibility.trainingExclusionReason, 'not_public');
  assert.equal(approved.eligibility.referenceExclusionReason, 'not_public');
});

test('global opt-out overrides per-content inclusion immediately and is recorded', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = s.addGame(owner, 'Public game');
  assert.ok(enableTraining(s, owner).ok);

  const content = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
  });
  assert.ok(content.ok, content.error);
  const approved = s.call(s.staff, 'ai.content.review', { contentId: asset.id, rightsStatus: 'approved', reason: 'Creator rights attestation reviewed' });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.trainingEligible, true);

  const optedOut = s.call(owner, 'ai.preferences.update', { trainingEnabled: false });
  assert.ok(optedOut.ok, optedOut.error);
  assert.equal(optedOut.preferences.trainingEnabled, false);

  const state = s.call(owner, 'ai.content.eligibility.get', { contentId: asset.id });
  assert.ok(state.ok, state.error);
  assert.equal(state.eligibility.trainingEligible, false);
  assert.equal(state.eligibility.trainingExclusionReason, 'account_training_disabled');
  assert.equal(Number(s.aiContentRow(asset.id).training_eligible), 0);
  const reasserted = s.call(owner, 'ai.content.eligibility.update', { contentId: asset.id, trainingChoice: 'include' });
  assert.ok(reasserted.ok, reasserted.error);
  assert.equal(reasserted.eligibility.trainingEligible, false, 'per-content inclusion cannot override the account opt-out');

  const history = s.call(owner, 'ai.training.history');
  assert.ok(history.ok, history.error);
  assert.ok(history.events.some((event) => event.event === 'preferences.updated' && event.details.trainingEnabled === false));
  assert.ok(history.events.some((event) => event.event === 'content.rights.reviewed' && event.details.rightsStatus === 'approved'));
});

test('a user cannot change another account’s content eligibility or approve their own rights', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const other = s.signUp('bob');
  const asset = s.addGame(owner, 'Alice game');

  const stolen = s.call(other, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
  });
  assert.equal(stolen.ok, false);

  assert.ok(enableTraining(s, owner).ok);
  const attested = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
    rightsStatus: 'approved',
  });
  assert.ok(attested.ok, attested.error);
  assert.equal(attested.eligibility.rightsStatus, 'pending_review');
  assert.equal(attested.eligibility.trainingEligible, false);

  const selfApproval = s.call(owner, 'ai.content.review', {
    contentId: asset.id, rightsStatus: 'approved', reason: 'Attempted self approval',
  });
  assert.equal(selfApproval.ok, false);
});

test('visibility changes and deletion revoke stored eligibility', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = s.addGame(owner, 'Public game');
  assert.ok(enableTraining(s, owner).ok);

  const content = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
  });
  assert.ok(content.ok, content.error);
  const approved = s.call(s.staff, 'ai.content.review', { contentId: asset.id, rightsStatus: 'approved', reason: 'Creator rights attestation reviewed' });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.trainingEligible, true);

  asset.access = 'private';
  s.saveAsset(asset);
  s.flush();
  const privateState = s.call(owner, 'ai.content.eligibility.get', { contentId: asset.id });
  assert.equal(privateState.eligibility.trainingEligible, false);
  assert.equal(privateState.eligibility.trainingExclusionReason, 'not_public');

  asset.access = 'public';
  s.saveAsset(asset);
  s.flush();
  assert.equal(s.call(owner, 'ai.content.eligibility.get', { contentId: asset.id }).eligibility.trainingEligible, true);

  const deleted = s.call(owner, 'delete', { id: asset.id });
  assert.ok(deleted.ok, deleted.error);
  assert.equal(s.aiContentRow(asset.id), null);
});

test('account opt-in enrolls existing and future uploads without bypassing rights review', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const existing = s.addGame(owner, 'Existing public game');

  assert.equal(s.aiContentRow(existing.id), null, 'accounts that have not opted in do not create AI records');
  assert.ok(enableTraining(s, owner).ok);
  assert.ok(s.aiContentRow(existing.id), 'existing owned uploads are enrolled for eligibility checks');

  const newAsset = s.addGame(owner, 'New public game');
  assert.ok(s.aiContentRow(newAsset.id), 'new uploads are enrolled automatically after opt-in');

  const attested = s.call(owner, 'ai.content.eligibility.update', {
    contentId: existing.id, rightsAttested: true,
  });
  assert.ok(attested.ok, attested.error);
  assert.equal(attested.eligibility.trainingChoice, 'inherit');
  assert.equal(attested.eligibility.trainingEligible, false);

  const approved = s.call(s.staff, 'ai.content.review', {
    contentId: existing.id, rightsStatus: 'approved', reason: 'Creator rights attestation reviewed',
  });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.trainingEligible, true, 'inherit follows the account-wide opt-in after rights approval');
});

test('account deletion revokes training preferences and all retained content eligibility', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = s.addGame(owner, 'Published game');
  assert.ok(enableTraining(s, owner).ok);
  const attested = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
  });
  assert.ok(attested.ok, attested.error);
  const approved = s.call(s.staff, 'ai.content.review', {
    contentId: asset.id, rightsStatus: 'approved', reason: 'Creator rights attestation reviewed',
  });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.trainingEligible, true);

  const accountId = owner.id;
  s.deleteAccount(owner);
  s.flush();

  const prefs = s.aiPreferencesFor(accountId);
  assert.equal(prefs.trainingEnabled, false);
  assert.equal(prefs.referenceEnabled, false);
  assert.equal(prefs.consentMethod, 'account_deleted');
  const row = s.aiContentRow(asset.id);
  assert.equal(Number(row.training_eligible), 0);
  assert.equal(Number(row.reference_eligible), 0);
  assert.equal(Number(row.rights_attested), 0);
  assert.equal(row.training_exclusion_reason, 'account_deleted');
  assert.equal(s.aiEnabledAccounts.has(accountId), false);
});

test('a policy or notice version change suspends eligibility until the account reconfirms', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = s.addGame(owner, 'Public game');
  assert.ok(enableTraining(s, owner).ok);

  const attested = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
  });
  assert.ok(attested.ok, attested.error);
  const approved = s.call(s.staff, 'ai.content.review', {
    contentId: asset.id, rightsStatus: 'approved', reason: 'Creator rights attestation reviewed',
  });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.trainingEligible, true);

  s.sql.exec('UPDATE ai_training_preferences SET policy_version = ?, notice_version = ? WHERE account_id = ?',
    'older-policy', 'older-notice', owner.id);
  const suspended = s.call(owner, 'ai.content.eligibility.get', { contentId: asset.id });
  assert.equal(suspended.eligibility.trainingEligible, false);
  assert.equal(suspended.eligibility.trainingExclusionReason, 'notice_outdated');

  const reconfirmed = s.call(owner, 'ai.preferences.update', {
    trainingEnabled: true, confirm: true, noticeVersion: NOTICE,
  });
  assert.ok(reconfirmed.ok, reconfirmed.error);
  assert.equal(reconfirmed.preferences.noticeVersion, NOTICE);
  const restored = s.call(owner, 'ai.content.eligibility.get', { contentId: asset.id });
  assert.equal(restored.eligibility.trainingEligible, true);
});

test('only staff can view the pending rights-review queue', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = s.addGame(owner, 'Rights review candidate');
  assert.ok(enableTraining(s, owner).ok);
  const attested = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
  });
  assert.ok(attested.ok, attested.error);

  const denied = s.call(owner, 'ai.content.reviewQueue');
  assert.equal(denied.ok, false);

  const queue = s.call(s.staff, 'ai.content.reviewQueue');
  assert.ok(queue.ok, queue.error);
  assert.ok(queue.items.some((item) => item.contentId === asset.id && item.title === asset.name));
});
