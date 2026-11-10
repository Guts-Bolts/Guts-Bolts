import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const NOTICE = '2026-11-09-v2';

function enableTraining(s, user, extra = {}) {
  return s.call(user, 'ai.preferences.update', Object.assign({
    trainingEnabled: true, confirm: true, noticeVersion: NOTICE,
  }, extra));
}

test('training is on by default (opt-out), reference is off until you confirm the notice (opt-in)', () => {
  const s = makeServer();
  const user = s.signUp('alice');

  const initial = s.call(user, 'ai.preferences.get');
  assert.ok(initial.ok);
  assert.equal(initial.preferences.trainingEnabled, true, 'training starts on');
  assert.equal(initial.preferences.consentMethod, 'default_opt_out');
  assert.equal(initial.preferences.referenceEnabled, false, 'reference starts off');
  assert.equal(initial.preferences.currentNoticeVersion, NOTICE);

  const off = s.call(user, 'ai.preferences.update', { trainingEnabled: false });
  assert.ok(off.ok, off.error);
  assert.equal(off.preferences.trainingEnabled, false);
  assert.equal(off.preferences.consentMethod, 'opt_out');
  assert.equal(s.call(user, 'ai.preferences.get').preferences.trainingEnabled, false, 'the opt-out sticks');

  const backOn = s.call(user, 'ai.preferences.update', { trainingEnabled: true });
  assert.ok(backOn.ok, backOn.error);
  assert.equal(backOn.preferences.trainingEnabled, true);
  assert.equal(backOn.preferences.consentMethod, 'chose_on');

  const missingNotice = s.call(user, 'ai.preferences.update', { referenceEnabled: true, confirm: true });
  assert.equal(missingNotice.ok, false);
  const missingConfirmation = s.call(user, 'ai.preferences.update', { referenceEnabled: true, noticeVersion: NOTICE });
  assert.equal(missingConfirmation.ok, false);
  const reference = s.call(user, 'ai.preferences.update', { referenceEnabled: true, confirm: true, noticeVersion: NOTICE });
  assert.ok(reference.ok, reference.error);
  assert.equal(reference.preferences.referenceEnabled, true);
  assert.equal(reference.preferences.noticeVersion, NOTICE);
});

test('guests are never included, and their uploads aren\'t tracked', () => {
  const s = makeServer();
  const guest = s.user('c'.repeat(63) + 'd');   // never signed up: userId stays 0
  assert.equal(guest.userId, 0);
  s.saveUser(guest);
  s.flush();
  assert.equal(s.aiPreferencesFor(guest.id).trainingEnabled, false);
  assert.equal(s.aiAccountUsesAi(guest.id), false);
  const game = s.addGame(guest, 'Guest game');
  assert.equal(s.aiContentRow(game.id), null);
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

test('uploads are tracked by default but need rights review; opting out stops new tracking', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const existing = s.addGame(owner, 'Existing public game');
  assert.ok(s.aiContentRow(existing.id), 'uploads are tracked for eligibility checks by default');
  assert.equal(s.call(owner, 'ai.content.eligibility.get', { contentId: existing.id }).eligibility.trainingEligible, false,
    'tracked is not the same as eligible: rights review still applies');

  const attested = s.call(owner, 'ai.content.eligibility.update', { contentId: existing.id, rightsAttested: true });
  assert.ok(attested.ok, attested.error);
  assert.equal(attested.eligibility.trainingChoice, 'inherit');
  assert.equal(attested.eligibility.trainingEligible, false);
  const approved = s.call(s.staff, 'ai.content.review', {
    contentId: existing.id, rightsStatus: 'approved', reason: 'Creator rights attestation reviewed',
  });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.trainingEligible, true, 'inherit follows the default-on account setting after rights approval');

  assert.ok(s.call(owner, 'ai.preferences.update', { trainingEnabled: false }).ok);
  const later = s.addGame(owner, 'Made after opting out');
  assert.equal(s.aiContentRow(later.id), null, 'nothing new is tracked after opting out');
  assert.equal(s.call(owner, 'ai.content.eligibility.get', { contentId: existing.id }).eligibility.trainingEligible, false);
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
  assert.equal(s.aiAccountUsesAi(accountId), false);
});

test('a policy or notice version change suspends reference (opt-in) until reconfirmed, but not training (opt-out)', () => {
  const s = makeServer();
  const owner = s.signUp('alice');
  const asset = s.addGame(owner, 'Public game');
  assert.ok(enableTraining(s, owner, { referenceEnabled: true }).ok);

  const attested = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', referenceChoice: 'include', rightsAttested: true,
  });
  assert.ok(attested.ok, attested.error);
  const approved = s.call(s.staff, 'ai.content.review', {
    contentId: asset.id, rightsStatus: 'approved', reason: 'Creator rights attestation reviewed',
  });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.trainingEligible, true);
  assert.equal(approved.eligibility.referenceEligible, true);

  s.sql.exec('UPDATE ai_training_preferences SET policy_version = ?, notice_version = ? WHERE account_id = ?',
    'older-policy', 'older-notice', owner.id);
  const suspended = s.call(owner, 'ai.content.eligibility.get', { contentId: asset.id });
  assert.equal(suspended.eligibility.trainingEligible, true);
  assert.equal(suspended.eligibility.referenceEligible, false);
  assert.equal(suspended.eligibility.referenceExclusionReason, 'notice_outdated');

  const reconfirmed = s.call(owner, 'ai.preferences.update', { referenceEnabled: true, confirm: true, noticeVersion: NOTICE });
  assert.ok(reconfirmed.ok, reconfirmed.error);
  assert.equal(reconfirmed.preferences.noticeVersion, NOTICE);
  assert.equal(s.call(owner, 'ai.content.eligibility.get', { contentId: asset.id }).eligibility.referenceEligible, true);
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

test('asset ownership changes invalidate the previous creator’s rights approval', () => {
  const s = makeServer();
  const oldOwner = s.signUp('alice');
  const newOwner = s.signUp('bob');
  const asset = s.addGame(oldOwner, 'Transferred game');
  assert.ok(enableTraining(s, oldOwner).ok);

  const attested = s.call(oldOwner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
  });
  assert.ok(attested.ok, attested.error);
  const approved = s.call(s.staff, 'ai.content.review', {
    contentId: asset.id, rightsStatus: 'approved', reason: 'Original creator rights reviewed',
  });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.trainingEligible, true);

  asset.creator = newOwner.id;
  s.saveAsset(asset);
  s.flush();
  assert.equal(s.call(oldOwner, 'ai.content.eligibility.get', { contentId: asset.id }).ok, false);

  assert.ok(enableTraining(s, newOwner).ok);
  const reset = s.call(newOwner, 'ai.content.eligibility.get', { contentId: asset.id });
  assert.ok(reset.ok, reset.error);
  assert.equal(reset.eligibility.rightsAttested, false);
  assert.equal(reset.eligibility.rightsStatus, 'unverified');
  assert.equal(reset.eligibility.trainingEligible, false);

  const cannotInherit = s.call(newOwner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include',
  });
  assert.equal(cannotInherit.ok, false);
  const newAttestation = s.call(newOwner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
  });
  assert.ok(newAttestation.ok, newAttestation.error);
  assert.equal(newAttestation.eligibility.rightsStatus, 'pending_review');
  assert.equal(newAttestation.eligibility.trainingEligible, false);
});
