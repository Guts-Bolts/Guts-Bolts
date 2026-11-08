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
  assert.ok(enableTraining(s, owner).ok);

  const attested = s.call(owner, 'ai.content.eligibility.update', {
    contentId: asset.id, trainingChoice: 'include', rightsAttested: true,
  });
  assert.ok(attested.ok, attested.error);

  const approved = s.call(s.staff, 'ai.content.review', {
    contentId: asset.id, rightsStatus: 'approved',
  });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.privacyStatus, 'private');
  assert.equal(approved.eligibility.trainingEligible, false);
  assert.equal(approved.eligibility.trainingExclusionReason, 'not_public');
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
  const approved = s.call(s.staff, 'ai.content.review', { contentId: asset.id, rightsStatus: 'approved' });
  assert.ok(approved.ok, approved.error);
  assert.equal(approved.eligibility.trainingEligible, true);

  const optedOut = s.call(owner, 'ai.preferences.update', { trainingEnabled: false });
  assert.ok(optedOut.ok, optedOut.error);
  assert.equal(optedOut.preferences.trainingEnabled, false);

  const state = s.call(owner, 'ai.content.eligibility.get', { contentId: asset.id });
  assert.ok(state.ok, state.error);
  assert.equal(state.eligibility.trainingEligible, false);
  assert.equal(state.eligibility.trainingExclusionReason, 'account_training_disabled');

  const history = s.call(owner, 'ai.training.history');
  assert.ok(history.ok, history.error);
  assert.ok(history.events.some((event) => event.event === 'preferences.updated' && event.details.trainingEnabled === false));
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
    contentId: asset.id, rightsStatus: 'approved',
  });
  assert.equal(selfApproval.ok, false);
});
