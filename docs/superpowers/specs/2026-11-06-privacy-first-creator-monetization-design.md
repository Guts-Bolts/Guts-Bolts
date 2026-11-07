# Privacy-First Accounts and Creator Monetization

**Date:** 2026-11-06  
**Status:** Approved direction; implementation and legal review pending

## Intent

Guts & Bolts is an 18+ game creation and gaming platform. Participation should remain pseudonymous by default. Users should not have to disclose legal identity or financial details merely to create an account, publish content, play games, or use ordinary platform features. Users who voluntarily seek monetary creator payouts must complete the identity and payout checks required by Stripe.

The platform should collect only information necessary to operate accounts, secure the service, deliver user-requested features, and meet applicable legal obligations. Content users create is a core platform record; it is not permission to collect unrelated personal information.

## Existing architecture observed

- The account system is implemented in the Cloudflare Worker Durable Object server and mirrored in the C++ server under `src/server`.
- Sign-up uses a username and password-derived cryptographic material. The Player UI says the password never leaves the device and an email is optional for account recovery.
- Supabase currently contains platform activity/search/notification tables, not the platform's primary account store or a Stripe integration.
- The server already distinguishes creator verification for certain upload/review permissions. This must not be conflated with legal identity verification or Stripe payout verification.
- The repository requires matching server behavior in `worker/server.js` and `src/server`.

## Goals

1. Keep ordinary users pseudonymous.
2. Require no legal name, government ID, address, or payout details to create or use a standard account, unless a specific legal or safety obligation requires a narrowly scoped check.
3. Make monetization opt-in and separate from ordinary account access.
4. Route creator identity verification and payout onboarding through Stripe Connect-hosted/embedded flows.
5. Store only a Stripe connected-account reference and the minimum operational status/capability fields needed by G&B; never store ID scans, SSNs, bank account numbers, or full KYC payloads.
6. Keep creator legal identity and payout details private from public profiles, buyers, and other creators.
7. Require explicit staff approval for each asset or asset class eligible for monetary creator compensation.
8. Keep purchased Bolts, user Bolt balances, and any creator compensation ledger separate.
9. Keep real payouts disabled until the money flow has received qualified legal review and Stripe confirmation.

## Non-goals

- Do not force all users through Stripe or government-ID verification.
- Do not expose creator legal names or payout details publicly.
- Do not store copies of Stripe identity documents or financial instruments.
- Do not promise that purchased Bolts are absolutely non-refundable; mandatory law, fraud, unauthorized payments, and card-network disputes may require reversals or refunds.
- Do not create a direct Bolt-to-cash redemption promise.
- Do not enable live-mode charges or creator payouts as part of this initial work.

## Product policy

### Standard participation

- Users participate using their G&B username/account identifier.
- Public identity is the chosen username and user-selected profile content.
- Optional account-recovery contact information remains optional and must be used only for recovery/security communications.
- No platform-wide legal-identity collection is introduced.
- Age restriction is a separate control: Stripe creator verification does not prove every visitor is 18+. Any age assurance required by law must be designed separately, proportionately, and with privacy review.

### Creator monetization

- Monetization is a separate opt-in enrollment.
- A creator submits an asset for monetization review and supplies evidence that they own it or hold sufficient commercial rights.
- Staff review ownership/licensing, content policy, pricing, and eligibility. Uploading or listing an asset does not make it monetizable.
- Only explicitly approved assets can generate eligible creator compensation.
- Creators complete Stripe Connect onboarding and all Stripe-required verification before becoming payout eligible.
- G&B stores the connected account ID, onboarding state, required capability/payout state, and timestamps needed for operations. Do not persist KYC document content or full Stripe person/account objects.
- Public profile and asset pages never reveal legal identity or payout details.
- Staff can suspend or revoke eligibility for fraud, false rights claims, legal issues, or policy violations, with an auditable reason and history.

## Financial separation

Maintain separate accounting concepts:
- Purchased Bolts: platform currency credited only after verified payment confirmation.
- Spent Bolts: internal platform purchases/auction activity; not a cash balance.
- Creator compensation: a separate contractual/platform-funded amount, not a redeemable Bolt balance.
- Platform revenue and payment/refund/dispute adjustments.

The conversion, funding source, timing, and calculation for creator compensation are intentionally not defined here. A model where fiat-funded Bolt purchases are spent on creator content and then automatically converted into creator cash can create stored-value, money-transmission, marketplace-settlement, tax, and consumer-protection obligations. Do not implement automatic Bolt-to-cash conversion until counsel and Stripe confirm the exact structure.

## Technical design

### Account and privacy boundary

- Keep primary account authentication in the existing G&B account service.
- Do not make Supabase Auth or Stripe identity a prerequisite for standard G&B accounts.
- Do not copy optional recovery email or other account PII into Supabase analytics tables.
- Review event and diagnostic logging so request bodies, passwords, auth tokens, email addresses, Stripe payloads, and payment details are never logged.
- Use opaque internal account IDs for foreign keys; use usernames only where display is intended.
- Apply least-privilege access and RLS to any new Supabase tables. Client roles must not be able to write payout status, compensation entries, approvals, or webhook events.

### Stripe Connect boundary

- Use the existing development/test Stripe account only during implementation.
- Use current Stripe Connect Accounts v2 onboarding patterns; do not introduce legacy account-type parameters.
- Onboarding should be initiated only after the user chooses to monetize.
- Verify webhook signatures and process events idempotently on the trusted server.
- Treat Stripe events as the source of truth for verification/capability state; do not accept a client-supplied `verified=true`.
- Store only the minimum account reference and status fields.
- Keep payouts disabled by default until legal and Stripe approval. No live-mode calls or transfers in this project stage.

### Monetization review and audit

Asset state machine:
`not_submitted -> pending_review -> approved | rejected`; approved assets may later move to `suspended` or `revoked`. Re-submission and appeal history should be retained. Every staff decision records staff account ID, timestamp, reason code, and a concise explanation.

### Supabase

Supabase currently supports activity/search/notification data and is not the primary account database. Any future monetization schema should contain only operational references and ledger metadata, with:
- RLS enabled on every exposed table.
- No client insert/update/delete privileges for financial ledger, Stripe status, review decisions, or webhook idempotency records.
- Server-only writes for verified Stripe events and financial state transitions.
- Immutable append-only ledger entries, unique Stripe event IDs, and unique idempotency keys.
- No raw Stripe webhook payloads containing personal data persisted by default; retain only sanitized event metadata required for reconciliation.

### Security and privacy controls

- Never place Stripe secret keys or Supabase service-role keys in browser/client code.
- Do not use user-editable profile metadata for authorization.
- Keep Stripe account references private and access-controlled.
- Do not include connected account IDs in public API responses.
- Add retention/deletion rules for any optional recovery contact and monetization records, while preserving records legally required for tax, fraud, accounting, and disputes.
- Provide a plain-language explanation before creators leave G&B for Stripe onboarding.

## Rollout

1. Document and test the privacy boundary and monetization eligibility states.
2. Implement test-mode-only Connect onboarding and webhook status synchronization.
3. Implement asset monetization submission/review and audit history.
4. Implement Bolt checkout/ledger separately from creator compensation.
5. Obtain written legal and Stripe confirmation of the creator-compensation money flow.
6. Enable creator payouts only after approval and explicit live-mode launch authorization.

## Acceptance criteria

- A normal account can be created and used without Stripe onboarding or government-ID collection by G&B.
- A non-monetizing user cannot accidentally trigger Connect onboarding.
- A creator can opt into monetization and see a clear Stripe verification requirement.
- No asset generates creator compensation unless it is explicitly approved and the creator is payout eligible.
- Client requests cannot forge Stripe verification, staff approval, or ledger entries.
- Webhook retries do not duplicate status changes or financial entries.
- Public APIs and profile pages do not disclose legal identity, connected account IDs, or payout details.
- Automated tests cover anonymous/pseudonymous standard use, opt-in onboarding, pending/rejected/suspended states, webhook signature failure, duplicate webhook delivery, and unauthorized ledger writes.
- No live charges or payouts are enabled before legal/Stripe approval.
