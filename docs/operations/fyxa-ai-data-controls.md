# Fyxa AI data controls (initial backend)

**Implementation status:** Backend preferences and eligibility metadata are implemented in the Worker. This is not a training pipeline and does not send user content to an AI provider.

## Current scope

The first implementation covers existing G&B records in the `assets` collection (games and creator-uploaded asset types). It does not yet cover forum posts, groups, messages, voice, livestreams, watch-party chat, or future video uploads. Each new content source must be integrated explicitly and must preserve its own visibility, deletion, and moderation rules.

The authoritative records are in `GbServerObject` SQLite:

- `ai_training_preferences`: one account-level preference record.
- `ai_content_eligibility`: per-asset preference, rights attestation, review status, and computed eligibility.
- `ai_training_audit`: user-visible preference and rights-review history.

These tables are created additively by the existing Durable Object constructor. No new Durable Object class, binding, Wrangler migration, or Supabase table is required.

## Default behavior

- Training is disabled for every account until an explicit opt-in.
- AI reference indexing is disabled independently until an explicit opt-in.
- Enabling either setting requires the current notice version and an explicit confirmation in the signed request.
- The current notice and policy version is `2026-11-08-v1`. The API returns `currentNoticeVersion` and `currentPolicyVersion` so clients do not need to guess.
- When an account opts in, existing owned assets are registered for eligibility checks. New assets created while the account has either AI setting enabled are registered automatically.
- If a policy or notice version changes, previously enabled processing becomes ineligible until the account explicitly reconfirms the current notice.
- Existing and new assets are not automatically eligible: the creator must attest that they have the necessary rights, and staff must approve the rights review.
- Once rights are approved, the account-wide preference applies to assets whose per-content choice is `inherit`. An explicit per-content `exclude` overrides account-level inclusion.
- Private assets, assets awaiting/rejected in moderation, exclusive/promo-only assets, and assets whose creator no longer matches the recorded owner remain excluded. When ownership changes, the new owner must make a fresh rights attestation and obtain a new staff review; the prior owner's approval is never inherited.
- A global account opt-out immediately recomputes stored eligibility. Per-content inclusion cannot override it.
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

## Eligibility rules

A content record is eligible for a purpose only when all of these are true:

1. The current asset exists and its recorded owner still matches the asset creator.
2. Its visibility is public.
3. It is not awaiting or rejected by G&B moderation and is not exclusive or promo-only.
4. The owner has explicitly enabled the corresponding account-level purpose.
5. The content's per-asset choice is not `exclude`.
6. The creator has recorded a rights attestation.
7. Staff has set its rights status to `approved`.

The server recalculates stored eligibility when account preferences change, when tracked assets are saved (including visibility and moderation changes), and when assets or accounts are deleted. If a policy/notice version is no longer current, the server marks the content ineligible until the account reconfirms. Future dataset builders must query only eligible records and re-check current preferences and source state immediately before dataset freeze/training. Do not rely on an old eligibility snapshot.

## Audit and backup

Preference changes and content choices are recorded in `ai_training_audit`. Staff review events are recorded against the content owner's account and include the reviewing staff account ID. The audit table is included in database diagnostics and the existing logical R2 backup export.

## Explicit limitations

- No dataset export, dataset registry, model training, fine-tuning, model evaluation, AI reference index, or Fyxa UI is implemented here.
- No content is currently selected or transmitted for training by this backend.
- No automatic model unlearning is implemented.
- The current rights-review mechanism is a staff gate plus a recorded review note; the operational evidence-review process still needs to be defined.
- The initial version covers existing asset records only. Other content classes must not be assumed to be covered.
- The Terms of Service and Privacy Policy must be reviewed and published to match the actual product behavior before enabling user-content processing.
