# Guts & Bolts platform expansion architecture

**Status:** Proposed product and technical architecture; documentation only. This document does not claim that the described products or services have been implemented.

**Goal:** Evolve Guts & Bolts from its current game platform into a unified ecosystem for games, social interaction, video, livestreaming, watch parties, communities, Fyxa AI, and a creator marketplace while preserving the existing game engine, accounts, and economy.

The first implementation should be a modular expansion of the current platform, not a rewrite or a premature split into many microservices.

## 1. Existing production baseline

The current production deployment is centered on:

- Cloudflare Worker `project-guts-and-bolts`.
- `GbServerObject`, the SQLite-backed Durable Object receiving `/api` and `/ws`.
- Existing G&B signed-request authentication and account model.
- Existing application state, including users, assets, groups, forum, files, game data, and trades.
- R2 `DOWNLOADS` for downloadable files and `DB_BACKUPS` for database backups.
- Supabase as a supporting projection/activity/search service, not as the authority for accounts, balances, inventory, permissions, or core content.
- Existing website and app under `website/`.
- Existing game engine, Studio, and independently hosted game-server architecture.

Preserve the production database's synchronous mutation invariants. In particular, do not add an `await` between reading mutable state and committing related balance, purchase, trade, inventory, or other coupled writes without an explicit concurrency/transaction design.

Do not replace the current account system with Supabase Auth. Do not move the Bolts ledger or authoritative inventory into an analytics/search database.

## 2. Product pillars

1. **Games:** existing Player, Studio, game engine, game publishing, and independently hosted multiplayer servers.
2. **Video:** creator channels, uploads, playback, discovery, comments, reactions, subscriptions, and creator analytics.
3. **Livestreaming:** creator broadcasts, live chat, moderation, and optional replay archives.
4. **Watch parties:** synchronized viewing rooms with host controls, membership, chat, invitations, and moderation.
5. **Social:** shared profiles, friends, presence, notifications, messaging, blocks, and activity.
6. **Communities:** groups, forums, community discovery, moderation, and shared events.
7. **Fyxa AI:** AI tools and assistants integrated with G&B identity and permissions, with a separately governed user-content training pipeline.
8. **Marketplace and creator economy:** creator assets, experiences, eligible digital goods, and staged monetization using the existing Bolts authority where appropriate.

## 3. Architecture principles

- One G&B account identity across all products.
- Existing G&B account and permission authority remains authoritative.
- Keep the Cloudflare Worker as the API gateway.
- Extend existing social, group, forum, notification, presence, moderation, and wallet capabilities rather than duplicating them.
- Use Durable Objects for state that requires ordered coordination, not as a default home for every service.
- Use object storage and specialist media infrastructure for large media workloads.
- Run asynchronous processing through queues or equivalent job infrastructure.
- Keep video encoding, livestream delivery, and AI training isolated from game-server workloads.
- Make each product independently feature-flagged and degradable.
- Use provider abstractions for media storage/streaming and AI inference/training.
- Introduce additive, backward-compatible schema changes with explicit migration and rollback plans.
- Treat all user-submitted text, files, media, metadata, and AI reference sources as untrusted input.

## 4. Logical architecture

```text
G&B Web / Desktop / Mobile / Player / Studio
                    |
          Existing G&B authentication
                    |
             Cloudflare Worker
             /api, /ws, assets
                    |
       GbServerObject (authoritative)
       - accounts and permissions
       - friends, blocks, presence
       - groups, forums, notifications
       - Bolts and inventory
       - media metadata and preferences
       - signed requests and audit metadata
                    |
       +------------+-------------+
       |            |             |
   R2 / media    Queues/jobs   Specialist services
   object store  and outbox    - video provider
       |            |          - Fyxa inference
       |            |          - isolated training
       |            |          - moderation tools
       |            |
       +------- supporting projections -------+
                       Supabase
              search/activity/analytics only
```

This is a logical boundary diagram, not a statement that all components are deployed today.

## 5. Identity, social, and permissions

Use one stable G&B account ID across every product.

Shared account capabilities:

- Authentication and account security.
- Username, profile, avatar, and account status.
- Friends, blocks, and privacy preferences.
- Notifications and messaging.
- Moderation state and enforcement.
- Existing Bolts wallet and authoritative transaction history.

Product-specific settings should remain scoped to their product, such as video playback preferences, creator-channel presentation, watch-room defaults, and Fyxa preferences.

Presence may expose states such as online, away, do-not-disturb, playing, watching, in a watch party, livestreaming, or using Fyxa. Each state needs account-level privacy controls. Blocks and moderation restrictions must apply consistently across products.

Extend existing friends, chat, presence, groups, forums, and notification operations. Do not create parallel social graphs or identities for video and watch parties.

## 6. Video platform

### Product scope

- Creator channels and handles.
- Video uploads and publishing.
- Public, unlisted, private, and restricted visibility where supported.
- Playback, thumbnails, captions, descriptions, and metadata.
- Comments, reactions, subscriptions, and discovery.
- Creator dashboards and analytics.
- Reporting, moderation, copyright complaints, and takedowns.

### Upload and processing design

1. Authenticated client requests an upload session.
2. Worker validates account, quota, content type, and upload policy.
3. Client uploads directly to the media provider or authorized object storage using a short-lived upload capability.
4. Provider processes/encodes the media asynchronously.
5. Signed provider webhook is verified, timestamp-checked, and handled idempotently.
6. G&B updates authoritative media metadata and moderation state.
7. Content becomes publishable only after required processing, rights, and moderation gates pass.

Do not stream large upload bodies through the Worker. Do not build a custom FFmpeg fleet for the initial release unless provider capabilities, cost, or product requirements justify it.

Evaluate Cloudflare Stream for upload, encoding, playback, and livestreaming. This is a vendor evaluation, not a decision that it is already configured. Keep an abstraction so the provider can be changed.

### Proposed logical records

- `creator_channels`: account, handle, channel presentation, status.
- `media_assets`: owner, provider, provider ID, type, visibility, moderation, processing state, metadata, publication/deletion timestamps.
- `media_uploads`: upload session, provider upload ID, expected size, content type, status, expiration.
- `media_subscriptions`: subscriber/channel relation.
- `media_comments`: asset, author, parent, body, moderation status.
- `media_reactions`: account/asset reaction.

These are design-level entities only. Final schema must follow current repository conventions and storage limits.

## 7. Watch parties

Use a dedicated `GbWatchRoom` Durable Object per room when a prototype demonstrates that the coordination boundary is needed. Do not add a binding or migration until the class and migration are ready together.

Room responsibilities:

- Authoritative room membership and roles.
- Host-controlled playback state.
- Ordered playback commands and state versions.
- Invitations and membership changes.
- Room chat and moderation events.
- Room lifecycle and expiration.

Possible roles:

- Host: playback and room settings.
- Moderator: manage participants and chat.
- Participant: watch and chat.
- Viewer: watch only.

Store invitation tokens as hashes, with expiration and revocation. Playback commands should carry monotonically increasing state versions. Clients compare local playback to authoritative state and correct drift using tested thresholds; initial thresholds must be validated with real network conditions.

Only allow sources G&B is authorized to host or synchronize, including G&B media, approved broadcasts, public-domain media, or providers that explicitly authorize the integration. Do not bypass DRM, subscriptions, or access controls for third-party streaming services.

## 8. Livestreaming

- Use provider-managed RTMPS or equivalent ingest.
- Issue private, revocable stream credentials.
- Keep ingest keys out of client logs and public APIs.
- Verify provider webhooks and make event handling idempotent.
- Use provider playback delivery rather than routing media through the Worker.
- Integrate live chat with existing G&B accounts, blocks, and moderation.
- Provide staff controls to suspend a stream and end a broadcast.
- Support replay archives only where creator settings, rights, and provider capabilities allow.
- Apply quotas, rate limits, abuse detection, reporting, and retention rules.

A separate `GbLiveRoom` Durable Object should be considered only if live-session coordination requires it; do not create it preemptively.

## 9. Fyxa AI

Fyxa must use the same G&B identity and permission model but keep inference and training workloads isolated.

- Provider-agnostic inference interface.
- Per-account usage limits and cost controls.
- Product-specific AI settings and safety boundaries.
- No access to private content merely because the user has a G&B account.
- No use of private messages, private watch-party conversations, credentials, or payment information for general model training.
- Separate platform operation, search indexing, RAG, evaluation, and model-training permissions.
- Purpose-specific eligibility, authorization, audit, dataset manifests, and model lineage.

The detailed data-governance design is in [Fyxa AI data governance and training architecture](fyxa-ai-data-governance.md). User-content training must not launch before its policy, authorization, opt-out, deletion, and provenance gates pass.

The current Workers AI binding is documented for chat translation. It is not proof that Fyxa model training or a dedicated inference service is already deployed.

## 10. Communities and marketplace

Communities should extend existing groups, forums, moderation, and membership permissions. Add discovery, events, community media, and integrations without duplicating the social graph.

The marketplace should extend existing asset ownership and Bolts transaction authority. Creator earnings, platform fees, and eventual payouts require distinct accounting records and controls. Do not silently convert Bolts into cash or imply that wallet balances are redeemable without an explicit product and legal design.

Stage monetization:

1. Existing game and cosmetic purchases.
2. Creator marketplace and eligible digital goods.
3. AI usage credits or paid AI tiers, with transparent quotas.
4. Optional video and livestream monetization.
5. Payouts only after tax, fraud, identity verification, payment-provider, and legal requirements are implemented.

## 11. Frontend organization

Keep the existing `website/app/` and migrate incrementally. Proposed product boundaries:

- `core/`: navigation, account identity, API client, notifications, permissions.
- `products/games/`: existing game discovery and launch.
- `products/video/`: feed, playback, channels, creator studio.
- `products/watch/`: room discovery, room UI, synchronized playback.
- `products/live/`: broadcast discovery and live viewing.
- `products/social/`: profiles, friends, presence, messages.
- `products/communities/`: groups, forums, events.
- `products/ai/`: Fyxa interface and preferences.
- `products/marketplace/`: creator assets and commerce.

These are proposed module boundaries, not a requirement to rename the current frontend immediately. Preserve current routes and compatibility while introducing new navigation and product routes behind feature flags.

## 12. API boundaries

Continue the existing signed-request API and operation dispatch. The following are conceptual operation families; map them to actual repository conventions before implementation.

| Domain | Example operations |
|---|---|
| Identity | `profile.get`, `profile.update` |
| Social | `friends.list`, `presence.get`, `presence.set`, `chat.send` |
| Communities | `community.list`, `community.create`, `community.join`, `community.leave` |
| Video | `video.upload.create`, `video.upload.status`, `video.upload.complete`, `video.publish`, `video.get`, `video.list`, `video.comment.create`, `video.reaction.set`, `video.subscribe` |
| Watch | `watch.room.create`, `watch.room.join`, `watch.room.leave`, `watch.room.invite`, `watch.room.control`, `watch.room.state`, `watch.room.close` |
| Live | `live.create`, `live.start`, `live.get`, `live.end`, `live.chat.send` |
| Fyxa | `ai.preferences.get`, `ai.preferences.update`, `ai.training.status` |

Regular user operations use existing signed requests. External provider webhooks use provider signature verification, timestamp checks, replay protection, and idempotency keys; do not pretend provider webhooks are ordinary signed user requests.

## 13. Reliability, security, and moderation

- Feature flags must be enforced server-side.
- Every new operation needs authorization, rate limits, validation, and abuse handling.
- Provider webhooks require signature verification, timestamp validation, and idempotent processing.
- Queued work must be retryable and observable.
- Preserve the existing outbox pattern for reliable supporting projections.
- Supabase failure must not roll back or block authoritative G&B operations.
- Private content must not leak through feeds, search, analytics, notifications, logs, or AI context.
- Apply blocks and moderation decisions consistently across product surfaces.
- Human moderation must remain available; AI assists rather than replaces staff decisions.
- Video requires reporting, copyright takedown, age-appropriate controls, and moderation review.
- Live services require chat controls and emergency stream termination.
- AI requires abuse controls, quotas, source permissions, and privacy safeguards.
- Design child safety and applicable privacy obligations before public launch.

## 14. Observability and operational targets

Instrument before setting hard service-level promises. Candidate targets to validate under realistic load:

- API p95 latency under 300 ms for ordinary non-media operations.
- Watch-room command acknowledgment under 250 ms under expected regional conditions.
- Playback synchronization drift under 1 second for supported providers and network conditions.
- Playback start under 3 seconds where provider and network conditions permit.
- Monthly availability target of 99.9% for core platform APIs after operational readiness is demonstrated.

Measure API latency, Durable Object CPU and storage, cold-start duration, database growth, queue age/retries, upload/encoding failures, playback startup and rebuffering, watch-room drift, live ingest health, AI latency, AI cost per active user, moderation response, and provider outages.

Keep game-server health and capacity metrics separate from video delivery and AI training metrics.

## 15. Phased roadmap

### Phase 1 — platform foundation

- Audit current identity, permissions, API dispatch, storage, and frontend boundaries.
- Document product boundaries and data ownership.
- Define feature flags and compatibility strategy.
- Establish architecture decision records and migration/recovery plans.

### Phase 2 — social integration

- Normalize shared identity and privacy contracts.
- Extend existing friends, presence, blocks, messaging, groups, and notifications.
- Add cross-product activity only where users permit it.

### Phase 3 — watch-party MVP

- Prototype room coordination.
- Implement membership, roles, invitations, playback state, chat, and moderation.
- Validate synchronization and failure recovery.

### Phase 4 — video MVP

- Select and integrate media provider.
- Implement upload sessions, channels, processing, playback, moderation, and creator controls.
- Establish media retention, quota, and cost policies.

### Phase 5 — Fyxa data governance

- Implement account and per-content preferences.
- Implement purpose-specific eligibility, rights checks, audit records, and policy versioning.
- Test opt-out, deletion, private-content exclusion, and historical-content handling.

### Phase 6 — Fyxa training foundation

- Select compliant training infrastructure.
- Implement dataset manifests, asynchronous jobs, model lineage, evaluations, approvals, and rollback.
- Do not enable user-content training before governance gates pass.

### Phase 7 — livestreaming

- Implement provider ingest, stream credentials, live status, chat, moderation, and optional replay.

### Phase 8 — marketplace and creator economy

- Extend existing Bolts authority.
- Add creator analytics and staged monetization.
- Add payouts only after the full compliance and accounting design is ready.

### Phase 9 — ecosystem integration

- Unified discovery, navigation, notifications, product activity, and analytics.
- Improve independent scaling and cost controls based on measured demand.

## 16. First engineering milestone

**Milestone:** Platform Expansion Foundation.

Recommended pull-request sequence:

1. Architecture and data-ownership documentation.
2. Identity, permission, privacy, and feature-flag contracts.
3. Watch Room prototype and isolated tests.
4. Media provider abstraction and upload lifecycle.
5. Fyxa preferences and eligibility data model.
6. Dataset registry, audit, and training-job contracts.

Do not combine this broad program into one production code change. Each pull request should be small enough to review, test, deploy behind a feature flag, and roll back independently.

## 17. Foundation acceptance criteria

- Existing Player, Studio, website, account authentication, game servers, and Bolts transactions remain compatible.
- One G&B identity governs all products.
- New products reuse existing social and moderation systems.
- Video and AI workloads do not block the core API or game servers.
- Media provider callbacks are authenticated and idempotent.
- Private content is inaccessible outside its intended audience.
- Fyxa data purposes and opt-out behavior are enforceable and auditable.
- New Durable Object classes are introduced only with correct bindings and explicit migrations.
- Every schema change has compatibility, backup, migration, and rollback plans.
- Product features can be independently disabled without corrupting existing platform state.
