# Fyxa AI data governance and training architecture

**Status:** Proposed architecture; documentation only. No production behavior, database schema, consent flow, or training pipeline is implemented by this document.

**Scope:** Define how Guts & Bolts may make eligible community content available to Fyxa AI while preserving user rights, privacy, account controls, and traceable model provenance.

## 1. Current repository architecture and constraints

The design must fit the system that exists today:

- `project-guts-and-bolts` is the Cloudflare Worker.
- `/api` and `/ws` route to the `GbServerObject` Durable Object.
- `GbServerObject` uses SQLite through `ctx.storage.sql`; it is authoritative for G&B accounts and application state.
- The production database currently includes tables such as `users`, `assets`, `groups`, `forum`, `meta`, `files`, `nonces`, `gamedata`, and `pasttrades`. Most application entities are JSON records loaded into in-memory Maps at startup.
- Core request mutation paths are synchronous and persist through `flush()`. Preserve the existing no-`await` mutation invariant for balances, purchases, trades, inventory, and other coupled state changes.
- R2 currently has `DOWNLOADS` and `DB_BACKUPS` bindings. Do not assume a private user-media bucket already exists.
- Supabase is a supporting projection/activity/search system. It is not authoritative for accounts, balances, inventory, permissions, or core content. Its current outbox flow is asynchronous and retryable.
- `wrangler.jsonc` currently declares a Cloudflare Workers AI `AI` binding used by in-game chat translation. This does not establish that Fyxa's model-serving or training infrastructure already exists.
- There is currently one `GbServerObject` SQLite Durable Object class and one `main` coordination object. New Durable Object classes require explicit code, bindings, and a versioned Wrangler migration.

Relevant operational references:

- [Production database operations](../operations/production-database.md)
- [Supabase live activity services](../operations/supabase-live-activity.md)
- [Database backups](../operations/database-backups.md)

## 2. Product and policy principles

1. Users retain ownership of their uploads.
2. Permission to host or display content is not automatically permission to train a model on it.
3. Public visibility is not, by itself, proof of training rights.
4. Platform operation, search/indexing, retrieval-augmented generation (RAG), model evaluation, and model-weight training are distinct processing purposes.
5. Private communications and designated confidential data are excluded from general model training.
6. Account-level opt-out must be enforceable in the data pipeline, not merely described in a policy.
7. New uses must not be silently applied retroactively to historical uploads. Provide clear advance notice and obtain any authorization required for the relevant user and jurisdiction.
8. Do not promise model unlearning unless the capability exists and has been validated.
9. Human review remains responsible for moderation and high-impact eligibility decisions; automated classifiers may assist but must not be the sole authority for ambiguous rights claims.
10. The implementation must be provider-agnostic and must not couple AI workloads to game-server availability.

## 3. Separate data-use purposes

Every content-processing job must declare its purpose. One permission must not implicitly authorize another.

| Purpose | Description | Eligibility rule |
|---|---|---|
| Platform operation | Store, render, deliver, back up, and moderate content | Necessary for the requested platform feature, subject to its terms and privacy notice |
| Search indexing | Build searchable metadata or indexes | Separate eligibility and visibility checks |
| AI reference / RAG | Retrieve source content as context for a Fyxa response | Separate preference, access-control, visibility, and rights checks |
| Model evaluation | Use examples to test model quality, safety, and accuracy | Separate eligibility and dataset approval |
| Model training | Use content to train or fine-tune model weights | Explicit training eligibility, rights basis, privacy screening, and consent/notice requirements |

RAG does not mean the content is used to update model weights. The user interface and privacy notice must explain that distinction.

## 4. Initial content eligibility matrix

This matrix is a conservative launch baseline. Legal review and product-specific controls may narrow it further.

| Content category | General model training | AI reference / RAG |
|---|---|---|
| Public videos, images, artwork, and creator uploads | Eligible only if the uploader has the required rights and the account/content authorization permits it | Separate opt-in/setting and normal access checks |
| Public forum and community posts | Same rights and preference checks; exclude sensitive personal information | Respect visibility, blocks, moderation, and account preferences |
| Public game descriptions, documentation, tutorials, and creator assets | Eligible subject to rights and authorization | Eligible subject to visibility and authorization |
| Private messages and private group conversations | Excluded | Excluded unless a distinct, explicit feature and authorization is designed |
| Private watch-party chat or voice | Excluded | Excluded |
| Passwords, authentication tokens, payment credentials, and secrets | Always excluded | Always excluded |
| Confidential/private documents | Excluded by default | Excluded by default |
| Sensitive personal data | Excluded from general training unless a separately reviewed lawful basis and safeguards permit it | Restrict or exclude; never bypass source access controls |
| Content involving minors | Not automatically eligible; require age-aware legal review and applicable authorization | Apply age-appropriate restrictions and source permissions |
| Third-party copyrighted media without training rights | Excluded | Do not treat public availability as permission to reproduce or use as a source |

A user upload may contain material owned by someone else or information about another person. The uploader's account preference alone cannot establish third-party rights.

## 5. User controls and authorization

Add a **Privacy & AI Data** section to G&B account settings.

### Account-level controls

- **Fyxa model training:** allow or disallow future use of the account's eligible content for model training.
- **Fyxa reference indexing:** independently allow or disallow eligible content from being indexed for AI retrieval.
- Show the current policy and notice versions, the effective date, and a concise explanation of each purpose.

### Per-content controls

Where the content type supports it, expose independent controls for:

- Include in Fyxa training.
- Exclude from Fyxa training.
- Allow AI reference indexing.
- Exclude from AI reference indexing.

The global opt-out must override per-content inclusion for future processing. A user must not need to edit every existing upload to make an account-wide opt-out effective.

### Consent model

The product must support more than one legal authorization model:

- Where affirmative opt-in is required, do not process the content for that purpose until the authorization is recorded.
- Where a clearly disclosed opt-out model is legally permissible, provide prominent advance notice, an accessible opt-out, and a meaningful opportunity to exercise it before processing.
- Do not assume that a Terms of Service clause alone is sufficient.
- Do not retroactively include historical uploads under a newly introduced policy without the required notice and authorization.
- Preserve the exact policy/notice version and authorization method associated with each eligibility decision.

The launch configuration should be selected after jurisdiction-specific legal review, not hard-coded as universally opt-out by default.

## 6. Proposed authoritative data model

The following are logical schema proposals, not existing tables or approved SQL migrations. Before implementation, inspect `worker/server.js`, the Durable Object initialization/schema code, serialization patterns, deletion flows, and current account settings operations. Keep authoritative preference and eligibility state in the existing G&B account authority unless a reviewed architecture decision explicitly changes that boundary.

### `ai_training_preferences`

| Field | Meaning |
|---|---|
| `account_id` | G&B account identifier; primary key |
| `training_enabled` | Account-level training preference |
| `reference_enabled` | Account-level AI reference preference |
| `policy_version` | Policy version presented/accepted |
| `notice_version` | Notice version shown |
| `consent_method` | Recorded authorization model/method |
| `updated_at` | Preference update timestamp |
| `effective_at` | Timestamp from which the preference applies |

### `ai_content_eligibility`

| Field | Meaning |
|---|---|
| `content_id` | Existing G&B content identifier |
| `owner_id` | Account that submitted/owns the G&B record |
| `content_type` | Video, image, post, document, game asset, etc. |
| `training_eligible` | Eligibility for model training |
| `reference_eligible` | Eligibility for retrieval/indexing |
| `rights_status` | Rights/authorization review state |
| `privacy_status` | Public, private, restricted, or equivalent |
| `exclusion_reason` | Stable reason code for exclusion |
| `policy_version` | Rules version used for the decision |
| `updated_at` | Last evaluation timestamp |

### `ai_dataset_registry`

Record immutable dataset versions and their manifests. Each version must identify its purpose, source-content manifest, eligibility snapshot, policy version, creation time, intended model family, approval state, and integrity digest.

### `ai_training_jobs`

Track queued, running, completed, cancelled, and failed jobs, including dataset version, target model, training configuration reference, timestamps, and operator/service identity.

### `ai_training_audit`

Append-only audit events for preference changes, eligibility decisions, inclusion/exclusion, dataset approvals, job lifecycle, and model release. Avoid placing raw private content or secrets in audit payloads.

### `ai_model_versions`

Track model identifier, training dataset versions, training configuration reference, evaluation results, approval state, release status, and deployment timestamps.

### Storage and scale constraints

The current `GbServerObject` is already a single coordination point with in-memory Maps and SQLite persistence. Do not add large media blobs, training examples, embeddings, or dataset manifests to its hot account-state path. Store source media and large derived artifacts in appropriately private object storage or a dedicated provider. Store compact authoritative permissions, references, and audit metadata in the account authority. Measure database growth and request costs before choosing schema layout or partitioning.

## 7. Processing pipeline

1. Receive upload through the existing authenticated G&B flow.
2. Store the content in its authorized storage location.
3. Resolve the uploader, content identifier, content type, visibility, and moderation state.
4. Evaluate account-level preferences.
5. Evaluate per-content preferences and any required affirmative authorization.
6. Verify that the uploader has the necessary rights or that another documented lawful basis applies.
7. Screen for secrets, sensitive personal information, restricted content, and material involving minors.
8. Create a purpose-specific eligibility decision with a stable reason code and policy version.
9. Register eligible source identifiers in a versioned dataset manifest.
10. Re-check current preferences and eligibility immediately before dataset freeze/training start.
11. Run training in isolated infrastructure, not in the public Worker request path or game-server process.
12. Evaluate the candidate model for quality, safety, privacy leakage, and regressions.
13. Require an authorized release approval and record model-to-dataset lineage.
14. Deploy through a versioned rollout with rollback capability.

All processing must be asynchronous. Large media uploads must not be proxied through a Worker request body or block the core API. Use provider-supported direct upload sessions where available.

## 8. Opt-out, deletion, and previously trained models

When a user opts out:

1. Persist the preference change immediately in the authoritative account system.
2. Prevent new selection of that account's content for the relevant purpose.
3. Remove affected content from queued and not-yet-frozen jobs.
4. Revalidate manifests immediately before training begins.
5. Exclude the content from future dataset versions.
6. Record affected content and job decisions in the audit trail.
7. Reflect the effective date and processing status in user-facing history.

Content already used by a completed training run is a separate case. Removing the source file or dataset entry does not automatically reverse the model's learned parameters. Do not promise automatic removal of learned influence. Define a documented remediation process with legal review; depending on the case and validated capabilities, it may require retraining, targeted unlearning, model retirement, or another remedy.

Deletion must be coordinated across source storage, search indexes, RAG caches, derived artifacts, queued jobs, and retention systems. Legal holds and required retention must be documented and narrowly scoped.

## 9. API boundaries

Extend the existing signed-request API and account authorization model. Do not introduce Supabase Auth or a parallel user identity.

Proposed operation names (adapt to the repository's actual operation dispatch conventions before implementation):

| Operation | Purpose |
|---|---|
| `ai.preferences.get` | Read account AI data preferences |
| `ai.preferences.update` | Update account preferences and record policy/notice version |
| `ai.content.eligibility.get` | Read an upload's purpose-specific eligibility |
| `ai.content.eligibility.update` | Update supported per-content choices |
| `ai.content.exclude` | Exclude content from future eligible datasets |
| `ai.training.history` | Return user-facing data-use history |
| `ai.training.status` | Return permitted processing status |
| `ai.datasets.admin.list` | Staff-only dataset inspection |
| `ai.training.jobs.admin.list` | Staff-only training job inspection |

Only trusted server-side processes may assert rights verification, approve datasets, or release models. Client input must never be trusted to set those states.

## 10. Infrastructure boundaries

- **Cloudflare Worker:** authentication, authorization, API routing, and short-lived job submission.
- **GbServerObject:** authoritative account preferences, compact eligibility metadata, and platform permissions.
- **R2 or approved media provider:** private source media and large derived artifacts; establish a dedicated private bucket/provider rather than reusing the public downloads bucket.
- **Cloudflare Queues or equivalent:** asynchronous eligibility, indexing, and dataset preparation jobs.
- **Training compute:** isolated, access-controlled, cost-limited infrastructure with no dependency on game-server processes.
- **Fyxa inference service:** provider-agnostic serving interface with model version pinning and controlled rollout.
- **Audit/observability:** dataset provenance, job state, policy decisions, security events, and cost metrics without logging private content or credentials.
- **Supabase:** optional supporting projections/analytics only; never the authority for account preferences, eligibility, or model-training consent.

Cloudflare Workers AI's existing `AI` binding is currently documented as being used for chat translation. Reuse it for Fyxa only after validating supported model, inference, data-handling, and training capabilities. Do not assume it provides custom model training.

## 11. Terms of Service and Privacy Policy requirements

The final legal text must match implemented behavior and be reviewed by counsel before launch. The following is a drafting outline, not final legal advice.

### Terms of Service

- Explain that users retain ownership of their content.
- Define a limited, non-exclusive license for specifically eligible AI purposes.
- Explain that the license applies only when the user has the necessary rights and authorization.
- Distinguish hosting/display rights from AI reference and model-training rights.
- Describe account-level and per-content controls.
- Explain the effect and limits of opt-out, including previously completed training.
- Prohibit implying that an upload grants G&B rights in third-party material.

### Privacy Policy

- Identify categories of content and account data processed.
- List each purpose separately: platform operation, search, AI reference, evaluation, and model training.
- Explain the authorization/consent model, controls, retention, processors/providers, and relevant user rights.
- Explain treatment of minors and sensitive personal information.
- Describe deletion, opt-out, dataset exclusion, and previously trained model limitations.
- Provide advance notice of material changes and obtain additional authorization where required.

Do not publish the draft below as final policy without matching implementation and legal review:

> Guts & Bolts may use eligible content submitted or published on the platform to develop, train, fine-tune, evaluate, and improve Fyxa AI and related services, subject to the privacy settings, eligibility requirements, and choices described in our policies.
>
> You retain ownership of content you upload. Where content is eligible for an AI-related use and you have the necessary rights and authorization, you grant Guts & Bolts a non-exclusive, worldwide license to process, reproduce, analyze, and use that content for the specific AI purposes described in our policies. This license does not transfer ownership of your content.
>
> You may disable use of your eligible content for future Fyxa AI training through your account's Privacy & AI Data settings. Where supported, you may also control AI-related use on individual uploads. When you opt out, your content will no longer be selected for future training datasets, subject to reasonable processing time and applicable legal requirements.
>
> Disabling training does not necessarily reverse the effects of content already used to train a completed model. We will explain available options and take actions required by applicable law and our published policies. We will not claim to have removed training influence unless that result has been achieved and verified.
>
> You must have the necessary rights and authorization to submit content for the uses described in these policies. Uploading content does not give Guts & Bolts permission to disregard copyright, privacy, publicity, or other third-party rights.

## 12. Security and governance requirements

- Rights and ownership checks must be purpose-specific and auditable.
- Enforce least privilege for dataset storage, training jobs, model artifacts, and inference credentials.
- Encrypt source and derived data in transit and at rest.
- Keep secrets and provider credentials in platform secret storage, never in Git or client bundles.
- Apply retention limits to raw training exports and intermediate artifacts.
- Prevent private content from appearing in logs, analytics, public search, or model datasets.
- Provide copyright/rights complaints and a rapid dataset-exclusion path.
- Maintain a human review path for ambiguous rights, sensitive data, and minors.
- Require security review before adding new AI vendors or data processors.
- Test prompt injection and malicious instructions embedded in user-submitted content; treat source content as untrusted data.
- Record model lineage and evaluation evidence for every production release.
- Establish incident response for accidental inclusion, unauthorized access, and policy violations.

## 13. Rollout gates

### Gate A — policy and inventory

- [ ] Inventory every upload/content type and its existing visibility/deletion semantics.
- [ ] Decide permitted AI purposes per content type.
- [ ] Complete jurisdiction and age-related legal review.
- [ ] Finalize notice, authorization model, and historical-content transition plan.

### Gate B — controls and eligibility

- [ ] Implement account-level preferences.
- [ ] Implement supported per-content controls.
- [ ] Implement policy/notice version recording.
- [ ] Implement deterministic eligibility decisions and reason codes.
- [ ] Implement opt-out propagation and deletion coordination.
- [ ] Add tests proving private and excluded content cannot enter datasets.

### Gate C — dataset governance

- [ ] Implement immutable dataset manifests and integrity checks.
- [ ] Revalidate preferences before dataset freeze and training.
- [ ] Implement audit history and staff-only approvals.
- [ ] Verify source-to-model lineage.

### Gate D — training and release

- [ ] Select and contract with compliant training infrastructure.
- [ ] Implement isolated, asynchronous jobs and cost controls.
- [ ] Add privacy-leakage, safety, and quality evaluations.
- [ ] Implement model versioning, staged rollout, and rollback.
- [ ] Complete security, legal, and operational launch reviews.

## 14. Acceptance criteria

The work is not launch-ready until all of the following are demonstrated:

- One G&B account identity controls preferences across all G&B products.
- Private messages and private watch-party conversations cannot enter general training or AI reference indexes.
- A global opt-out prevents future dataset selection even if a content-level setting previously allowed training.
- A queued job cannot proceed with stale eligibility after an opt-out.
- Every dataset version has an integrity-checked source manifest and policy version.
- Every production model can be traced to approved dataset versions.
- No client can self-assert rights verification or approve a dataset/model.
- AI processing does not block core API, account, Bolts, or game-server operations.
- Deletion and previously trained model behavior are accurately described to users.
- Legal, security, age-safety, and operational gates are signed off before enabling training on user content.

## 15. Explicit non-goals for the first implementation

- Automatically training on every upload.
- Training on private communications.
- Treating public visibility as proof of copyright or training rights.
- Replacing the existing G&B account system or authoritative database.
- Moving balances, inventory, permissions, or core transactions into Supabase.
- Running training workloads in the game server or request-serving Worker.
- Promising instant model unlearning.
- Launching user-content training before authorization, opt-out, provenance, and deletion controls are tested.
