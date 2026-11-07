# Privacy-First Creator Monetization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add opt-in creator monetization to Guts & Bolts while keeping standard accounts pseudonymous and delegating legal identity and payout verification to Stripe Connect.

**Architecture:** Preserve the existing G&B account system in the Cloudflare Durable Object server and its C++ counterpart. Add a server-owned monetization enrollment/review state machine and a Stripe Connect boundary that stores only opaque account references and minimal capability/status fields. Do not use Supabase as the primary account or KYC store; use it only if existing project conventions justify non-sensitive audit metadata.

**Tech Stack:** Cloudflare Workers/Durable Objects JavaScript, mirrored C++ server, existing website app (vanilla JS), existing test harness, Stripe Connect test mode. No new dependency until current package/config is inspected.

**Spec:** `docs/superpowers/specs/2026-11-06-privacy-first-creator-monetization-design.md`

## Global Constraints

- Standard G&B accounts remain username-based and do not require Stripe or legal-identity verification.
- Optional recovery contact information remains optional for ordinary accounts.
- Stripe identity verification is required only when a user opts into monetary creator payouts.
- Never store ID scans, SSNs, bank account numbers, or full KYC payloads in G&B or Supabase.
- Existing creator-upload verification and Stripe payout verification are distinct states.
- Mirror relevant server behavior in `worker/server.js` and `src/server`.
- Only explicitly approved assets can qualify for creator compensation.
- No Bolt-to-cash conversion or automatic sale-funded cash obligation is introduced.
- Use Stripe test mode only; no live charges or payouts.
- No public API or profile may expose connected account IDs or legal identity.

## Review Focus

- A regular user who never opens monetization must retain the existing account flow with no Stripe dependency.
- A creator whose Stripe onboarding is incomplete must not become payout eligible.
- A client attempting to submit forged verification or staff approval must be rejected.
- Duplicate or out-of-order Stripe webhook deliveries must not duplicate or regress state.
- Revoked or suspended asset eligibility must stop new compensation accrual without deleting audit history.

---

## Task 1: Server-side monetization state and persistence

**Files:**
- Modify: `worker/server.js`
- Modify: `src/server` account/server implementation (locate exact source file before editing)
- Create: focused Worker tests under `worker/test/`
- Create: focused C++ tests under the existing server test layout

**Interfaces:**
- Account identity remains the existing opaque G&B account ID.
- Monetization enrollment state is separate from existing creator upload verification.
- Server-owned fields: opt-in state, Stripe connected-account reference, onboarding status, payout capability status, and timestamps. Do not persist KYC document fields.

- [ ] Write tests proving ordinary accounts default to not enrolled and can use existing account operations unchanged.
- [ ] Run those tests and confirm the new assertions fail before implementation.
- [ ] Add server-owned enrollment/status fields and validation. No client-settable verified/payout-eligible flag.
- [ ] Mirror behavior in the Worker and C++ server.
- [ ] Run the focused Worker and C++ tests.
- [ ] Commit the state/persistence slice.

## Task 2: Creator asset monetization review

**Files:**
- Modify: `worker/server.js`
- Modify: matching C++ server handler
- Create: focused tests in Worker and C++ server test suites
- Create or update: staff-facing review UI only after locating the existing creator/admin review surface

**Interfaces:**
- Asset review states: `not_submitted`, `pending_review`, `approved`, `rejected`, `suspended`, `revoked`.
- A submission includes asset ID, creator account ID, rights/ownership attestation, evidence reference, requested price/monetization details, and submission timestamp.
- Every staff decision stores staff account ID, decision timestamp, reason code, and explanation.
- Client callers may submit and view their own submissions; only authorized staff may approve, reject, suspend, or revoke.

- [ ] Write state-transition tests, including invalid transitions and non-owner submissions.
- [ ] Verify tests fail.
- [ ] Implement server-side transition validation and append-only decision history.
- [ ] Mirror the rules in the C++ server.
- [ ] Add or wire the existing staff review UI.
- [ ] Run focused tests and commit.

## Task 3: Stripe Connect test-mode onboarding

**Files:**
- Create: focused Stripe Connect module under `worker/`
- Modify: `worker/index.js` for authenticated routes and webhook routing
- Modify: `worker/server.js` for account-to-connected-account association
- Modify: Worker environment/config documentation for test-mode secrets
- Create: Worker tests for onboarding and webhook handling

**Interfaces:**
- Authenticated opt-in endpoint creates or reuses a connected account for the current G&B account.
- Onboarding endpoint returns a short-lived Stripe-hosted onboarding URL.
- Webhook endpoint verifies Stripe signatures before processing events.
- Connected-account association is private and server-only.
- Persist only Stripe account ID, onboarding state, required capability state, payout-enabled state, and timestamps.
- Use Accounts v2-compatible Connect integration. Confirm exact current Stripe API contracts before implementation; do not use legacy account-type parameters.

- [ ] Add tests for opt-in gating, account ownership, webhook signature rejection, duplicate event idempotency, and status updates.
- [ ] Verify tests fail.
- [ ] Implement test-mode account creation and onboarding link generation.
- [ ] Implement signed webhook processing with idempotent event handling.
- [ ] Ensure no KYC payloads or sensitive financial details are logged or persisted.
- [ ] Run focused tests and commit.

## Task 4: Creator monetization UI and privacy copy

**Files:**
- Modify: existing creator/asset pages in `website/app/` (locate exact page modules)
- Modify: existing localization source using project extraction workflow
- Create: tests for visibility and state rendering

**Interfaces:**
- Standard users see no forced identity-verification prompt.
- Creator monetization page explains that Stripe handles identity and payout verification.
- Page shows not-enrolled, onboarding-required, pending, payout-ready, and restricted states.
- Public pages never display legal identity, Stripe account ID, or payout details.

- [ ] Add tests for state visibility and public/private field exclusion.
- [ ] Verify tests fail.
- [ ] Add the opt-in creator monetization UI and concise privacy disclosure.
- [ ] Run localization extraction/checks and website tests.
- [ ] Commit.

## Task 5: Security, rollout gates, and verification

**Files:**
- Modify: relevant Worker/C++ tests and security docs
- Modify: `docs/superpowers/specs/2026-11-06-privacy-first-creator-monetization-design.md` only if implementation details require correction

- [ ] Test that public and authenticated clients cannot write Stripe status, review decisions, or compensation records.
- [ ] Run existing Worker test suite and C++ test suite.
- [ ] Run Supabase security advisors if any Supabase schema is added; do not add Supabase KYC storage.
- [ ] Review logs and API responses for personal data leakage.
- [ ] Confirm Stripe integration is pinned to test mode and live payout code paths remain disabled.
- [ ] Record unresolved legal/Stripe approval gates for the Bolt-funded creator compensation model.
- [ ] Open a pull request with test evidence and explicit rollout blockers.

## Deferred until legal and Stripe approval

- Bolt-funded creator cash compensation formula and funding source.
- Live-mode charges, transfers, or payouts.
- Any claim that purchased Bolts are absolutely non-refundable.
- Any change to platform-wide age assurance beyond the existing access controls.
