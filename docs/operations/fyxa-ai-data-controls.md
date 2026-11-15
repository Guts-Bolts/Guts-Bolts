# Fyxa AI data controls (initial backend)

**Implementation status:** Account preferences and public-asset eligibility are implemented. Fyxa Studio now submits bounded quality metadata and explicit response feedback to the Worker when training is enabled. This metadata path does not transmit prompts, answers, tool arguments, tool results, screenshots, or project files, and it does not train model weights. A content-training pipeline is still not implemented.

## Current scope

The first implementation covers existing G&B records in the `assets` collection (games and creator-uploaded asset types). It does not yet cover forum posts, groups, messages, voice, livestreams, watch-party chat, or future video uploads. Each new content source must be integrated explicitly and must preserve its own visibility, deletion, and moderation rules.

The authoritative records are in `GbServerObject` SQLite:

- `ai_training_preferences`: one account-level preference record.
- `ai_content_eligibility`: per-asset preference, rights attestation, review status, and computed eligibility.
- `ai_training_audit`: user-visible preference and rights-review history.
- `ai_learning_signals`: short-lived, account-scoped provider/model/category/tool-outcome metadata and optional user feedback; no conversation content.

These tables are created additively by the existing Durable Object constructor. No new Durable Object class, binding, Wrangler migration, or Supabase table is required.

## Default behavior

Changed 2026-11-09 (Script chose opt-out for training):

- **Training is ON by default** for every signed-up account, and the account can turn it off in Settings ("Fyxa and your uploads"). Turning it off needs no confirmation; turning it back on needs none either. Guest (not signed-up) accounts are never included.
- **AI reference indexing stays OFF** until an explicit opt-in, which requires the current notice version and `confirm: true`.
- The current notice and policy version is `2026-11-09-v2`. The API returns `currentNoticeVersion` and `currentPolicyVersion` so clients do not need to guess. `consentMethod` (about training) is `default_opt_out` (never changed), `chose_on`, `opt_out`, or `account_deleted`.
- A policy or notice version change suspends **reference** use until the account reconfirms. Training (opt-out) is not suspended by a version change.
- The Terms of Service (website `#/terms`, sections 6 and 11, and the Player's short version) describe this. Keep them in step with this file.
- Assets of accounts that have training (or reference) on are registered for eligibility checks when they are saved, and all of an account's existing assets are registered when it changes its preferences. After an account turns both off, new assets are not registered.
- If a policy or notice version changes, previously enabled processing becomes ineligible until the account explicitly reconfirms the current notice.
- Existing and new assets are not automatically eligible: the creator must attest that they have the necessary rights, and staff must approve the rights review.
- Once rights are approved, the account-wide preference applies to assets whose per-content choice is `inherit`. An explicit per-content `exclude` overrides account-level inclusion.
- Private assets, assets awaiting/rejected in moderation, exclusive/promo-only assets, and assets whose creator no longer matches the recorded owner remain excluded. When ownership changes, the new owner must make a fresh rights attestation and obtain a new staff review; the prior owner's approval is never inherited.
- A global account opt-out immediately recomputes stored asset eligibility, deletes that account's pending learning signals, and blocks future signal capture. Per-content inclusion cannot override it.
- Studio records a quality signal after a completed Fyxa turn only for signed-in accounts whose training preference is enabled. The signal contains provider, model, a coarse topic category, and tool names/outcomes only. Tool arguments and tool outputs are never sent by this telemetry path.
- A tool error is labelled `tool_failure`; otherwise the signal waits for user feedback rather than assuming the answer was correct. Users can mark the response helpful or identify a reason it was not helpful.
- Learning signals are capped at 50 per account per day and retained for at most 90 days. Users can view or delete their own signal history. Account deletion removes all learning signals.
- Deleting an asset removes its eligibility row and leaves an audit event.
- Deleting an account revokes both AI preferences, clears its content rights attestations, disables all retained content eligibility, and records the deletion event. Published games/items remain under the platform's existing account-deletion behavior, but they are no longer eligible for Fyxa use.

## Signed API operations

All operations use the existing G&B signed-request mechanism and require a signed-in account.

### Read preferences

```json
{ "op": "ai.preferences.get", "args": {} }
```

Returns `trainingEnabled`, `referenceEnabled`, accepted `policyVersion` and `noticeVersion`, `consentMethod`, timestamps, and the current policy/notice versions.

### Update preferences

```json
{
  "op": "ai.preferences.update",
  "args": {
    "trainingEnabled": true,
    "confirm": true,
    "noticeVersion": "2026-11-08-v1"
  }
}
```

Either boolean may be updated independently. When turning on a purpose, clients must present the current notice and send `confirm: true`. Turning a purpose off does not require confirmation. Updating both values in one request is supported.

### Read one asset's eligibility

```json
{ "op": "ai.content.eligibility.get", "args": { "contentId": "ASSET_ID" } }
```

Only the asset creator can read its eligibility details. Other accounts receive the same not-found response used for missing assets.

### Update one asset's preference and rights attestation

```json
{
  "op": "ai.content.eligibility.update",
  "args": {
    "contentId": "ASSET_ID",
    "trainingChoice": "inherit",
    "referenceChoice": "exclude",
    "rightsAttested": true
  }
}
```

Choices are `inherit`, `include`, or `exclude`. `inherit` follows the account-level preference; `exclude` overrides it. Setting an inclusion choice requires a rights attestation. A rights attestation moves the asset to `pending_review`; it does not approve the rights claim.

Clients cannot set `rightsStatus`. Extra client-supplied fields such as `rightsStatus: "approved"` are ignored.

### Staff rights-review queue

```json
{ "op": "ai.content.reviewQueue", "args": { "limit": 50, "offset": 0 } }
```

Only staff can retrieve the pending review queue. It returns content metadata and creator details, not raw upload bytes.

### Staff rights review

```json
{
  "op": "ai.content.review",
  "args": {
    "contentId": "ASSET_ID",
    "rightsStatus": "approved",
    "reason": "Reviewed creator rights evidence"
  }
}
```

Only staff may use this operation. Approval requires a creator attestation and a review note. Supported states are `approved`, `rejected`, and `unverified`. Staff review is an internal rights gate, not a substitute for legal review or a guarantee that third-party rights have been established.

### User history

```json
{ "op": "ai.training.history", "args": {} }
```

Returns up to the latest 50 preference, content-choice, rights-review, and deletion events associated with the account.

### Fyxa response-quality metadata

After a completed Studio turn, the client may submit:

```json
{
  "op": "ai.learning.capture",
  "args": {
    "provider": "ollama",
    "model": "qwen3:8b",
    "category": "scripting",
    "tools": [{ "name": "get_script_source", "outcome": "success" }]
  }
}
```

The server requires a signed-in account and `trainingEnabled: true`. It validates provider, category, tool names, and outcomes; limits capture to 50 signals per account per day; and expires signals after 90 days. It stores only metadata. It does not accept prompt text, assistant response text, tool arguments, tool output, images, or source files.

Tool errors are marked `tool_failure`. Successful tool execution alone is not treated as proof that the answer was correct; other signals remain `awaiting_feedback` until the user rates the response.

The Studio panel submits explicit feedback with `ai.learning.feedback` using a signal ID, rating (`1` or `-1`), and a reason code. Supported negative reasons are `incorrect`, `incomplete`, `irrelevant`, `tool_failure`, `unsafe`, and `other`. Users can read their latest 50 records with `ai.learning.history` and erase them with `ai.learning.delete`. Staff can view category-level aggregate counts with `ai.learning.admin.summary`; this operation does not expose individual accounts or content.

These signals are product-quality telemetry, not a fine-tuning dataset. They are not currently fed back into model weights or automatically treated as verified ground truth.

### Staff-only game-source screening preview

The Worker now exposes two staff-only operations for the first-pass screening stage:

```json
{ "op": "ai.datasets.admin.screen", "args": { "limit": 5, "offset": 0 } }
```

This scans at most five currently eligible public game uploads per call. Eligibility is recalculated from the current asset record, account training preference, per-content exclusion, moderation state, creator ownership, rights attestation, and staff rights review. The source file is read from the existing private Durable Object file store. The screening helper examines bounded UTF-8 JSON text for obvious credentials, personal-data patterns, opaque geometry/media, malformed content, and resource-limit violations.

The operation returns only the asset identifier/title, source revision marker, scan status, stable reason codes, and aggregate scan counts. It does not return or persist extracted source text. Results are cached as small metadata records and invalidated when the asset revision changes. Account opt-out, account deletion, and asset deletion remove the associated screening metadata. A second staff-only operation, `ai.datasets.admin.screenings`, lists current screening results after rechecking ownership, eligibility, and source revision.

**A clean scan is not approval.** Every result still has `ageStatus: "not_verified"` and `datasetEligible: false`. The scanner is a conservative heuristic, not a full semantic privacy or safety classifier. It does not verify age, establish copyright ownership, or replace human review. No raw source is exported and dataset export remains disabled.

## Eligibility rules

A content record is eligible for a purpose only when all of these are true:

1. The current asset exists and its recorded owner still matches the asset creator.
2. Its visibility is public.
3. It is not awaiting or rejected by G&B moderation and is not exclusive or promo-only.
4. The owner's account-level purpose is on (training: on unless opted out; reference: explicitly enabled).
5. The content's per-asset choice is not `exclude`.
6. The creator has recorded a rights attestation.
7. Staff has set its rights status to `approved`.

The server recalculates stored eligibility when account preferences change, when tracked assets are saved (including visibility and moderation changes), and when assets or accounts are deleted. If a policy/notice version is no longer current, the server marks the content ineligible until the account reconfirms. Future dataset builders must query only eligible records and re-check current preferences and source state immediately before dataset freeze/training. Do not rely on an old eligibility snapshot.

## Audit and backup

Preference changes and content choices are recorded in `ai_training_audit`. Staff review events are recorded against the content owner's account and include the reviewing staff account ID. The audit table is included in database diagnostics and the existing logical R2 backup export.

## Explicit limitations

- No dataset export, frozen dataset registry, model training, fine-tuning, semantic answer evaluation or AI reference index is implemented here. The new quality signals are telemetry only; the game-source screening preview is an internal preflight and does not create training examples.
- This telemetry deliberately excludes raw prompts, responses, tool arguments/results, screenshots, and project files. A staff-only preflight now scans bounded public game source for obvious secrets, personal-data patterns, and opaque media, but this is not a complete content-safety screen. Age safeguards, human review, legal review, private dataset storage, and a tested dataset authorization flow are still required before raw user content can be exported or used to train model weights.
- No raw conversation or game-file content is currently selected or transmitted for model-weight training by this backend. Only the metadata signals described above are collected when the account's opt-out switch is on.
- No automatic model unlearning is implemented.
- The current rights-review mechanism is a staff gate plus a recorded review note; the operational evidence-review process still needs to be defined.
- The initial version covers existing asset records only. Other content classes must not be assumed to be covered.
- The Terms of Service and Privacy Policy must be reviewed and published to match the actual product behavior before enabling user-content processing.
