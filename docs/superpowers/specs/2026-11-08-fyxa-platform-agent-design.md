# Fyxa Platform Agent Architecture

## Product definition

Fyxa is the general-purpose AI agent for Guts & Bolts. It is not a Linux helper, a Studio-only chatbot, or a wrapper around one model provider. It should assist users across the full Guts & Bolts experience, including Studio, game development, playing games, accounts, social features, inventory, marketplace, community services, and supported external integrations.

Studio is the first deeply integrated capability because it already has a native tool system, editor context, undo history, and an MCP server. It is a capability domain, not the product boundary.

**Fyxa v1 task concurrency constraint:** only one explicit task may be active at a time. Independent requests in the same turn must be represented as one active task plus pending tasks; Fyxa must disclose the deferral and ask the user which pending task to prioritize next. Interleaving task evidence across multiple active tasks is out of scope for v1.

## Core architecture

### 1. Agent runtime

A provider-neutral runtime owns:
- conversation state and model-independent tool-call orchestration;
- user and application context;
- capability discovery and tool routing;
- permission checks, confirmations, and audit events;
- cancellation, budgets, and error recovery.

Ollama, Claude, and future providers implement a model adapter. They do not own platform permissions or feature-specific business logic.

### 2. Capability providers

Each Guts & Bolts subsystem exposes a capability provider through a common registry. A provider declares:
- stable provider and tool identifiers;
- domain and short description;
- JSON input schema and structured output contract;
- read-only, mutating, destructive, and external-side-effect annotations;
- required user permissions and contextual preconditions;
- confirmation policy and whether a native undo/compensation operation exists.

Initial and planned domains:

| Domain | Example capabilities | Authority |
|---|---|---|
| Studio | inspect/edit scenes, scripts, assets, physics, playtests, debugging | Existing native AiTools and Editor |
| Games | discover/join games, inspect supported session state, player assistance | Game/session services and runtime APIs |
| Account | profile, settings, privacy, account help | Authenticated account service |
| Social | friends, presence, chat, groups | Social services and user permissions |
| Inventory | inspect owned items, equip, trade where supported | Inventory and trading services |
| Marketplace | search, inspect, create/manage listings where authorized | Marketplace service |
| Community | forums, moderation/reporting, creator/community information | Community APIs and role permissions |
| Integrations | connected apps and external MCP servers | Explicit user-granted integration permissions |

This table is a target capability map, not a claim that every provider is implemented today. A domain becomes actionable only when a real provider exposes its tools and the server independently enforces authorization.

### 3. Context model

The runtime supplies structured context rather than relying on the model to infer the user's current location:
- application surface: Studio, Player, website, or other supported client;
- active project, place, game, and session identifiers when available;
- selected objects and relevant editor/playtest state in Studio;
- authenticated account identity as an opaque session context, never as a secret in prompts;
- available capability providers and their permission state.

Only context needed for the current request should be sent to the model. Secrets, session tokens, private account data, and unrelated user data must not be placed in prompts.

### 4. Authorization and action safety

- Every tool call is authorized by the platform, not by model-generated claims.
- Read-only operations may run without an extra confirmation when the user has granted the relevant capability.
- Mutating, destructive, financial, publishing, moderation, trading, or externally visible operations require explicit confirmation according to risk.
- The service rechecks permissions and resource ownership at execution time.
- Tool results are structured and report the actual outcome.
- Studio-native edits continue through native AI Activity and undo.
- Remote actions are not described as undoable unless the responsible service supplies a real compensating operation.
- External MCP tools remain untrusted and separately approved; connecting a server is not blanket authorization.

### 5. Extensibility

Adding a Guts & Bolts feature should mean implementing a provider and registering its capabilities, not writing a new assistant or embedding service-specific logic in every model adapter. The same capability contracts should be usable from Studio Fyxa, Player Fyxa, and approved connected applications, with surface-specific policy.

## Current implementation boundary

The current Fyxa panel supports Ollama and Claude, native Studio AiTools, external MCP over HTTP, and Studio's existing MCP hosting/connected-app relay. It does not yet expose native account, social, inventory, marketplace, community, or game-service capability providers. The assistant must be explicit about that boundary and must never imply it has performed an action for which no tool exists.

## Delivery sequence

1. Establish the platform-wide agent contract and provider-neutral capability metadata.
2. Extract the current Studio AiTools into the common capability registry without changing their execution or undo semantics.
3. Add authenticated first-party service providers, starting with read-only account/profile and platform discovery operations.
4. Add social, inventory, marketplace, community, and game/session providers incrementally, each with server-side authorization and risk tests.
5. Expose context-aware Fyxa surfaces in Player and other Guts & Bolts clients.
6. Add cross-surface continuity, user-controlled memory, and policy-aware proactive assistance only after consent, retention, and privacy controls are defined.

## Acceptance criteria

- The product definition and system instructions describe Fyxa as the Guts & Bolts-wide agent.
- Model providers cannot bypass capability permissions.
- Missing providers are reported as unavailable, not simulated.
- Studio remains fully functional and preserves its native undo/audit behavior.
- Each new platform domain has contract tests for authorization, ownership boundaries, malformed inputs, failure handling, and auditability before being exposed to users.


## Production-readiness constraints

The detailed review and release gates are maintained in `docs/superpowers/specs/2026-11-08-fyxa-production-readiness-review.md`. Key architecture constraints:

- External MCP content and metadata are untrusted data, never policy or authorization.
- HTTP MCP must support the stateless `2026-07-28` lifecycle and explicit compatibility with legacy peers.
- Protected remote MCP authorization must use the MCP OAuth 2.1 authorization flow with issuer/resource binding; static bearer tokens are development compatibility only.
- Tool approvals are bound to canonical tool-definition fingerprints; definition changes invalidate approval.
- Task evidence validation and one-time consumption must be atomic and replay-safe.
- Provider requests and tool execution require cooperative cancellation, bounded concurrency, and time budgets.
- Tool schemas are selected dynamically by task/domain; the full platform registry is not injected into every prompt.
- Conversation recovery, provider capability negotiation, structured observability, and skill versioning are required before broad non-developer rollout.
