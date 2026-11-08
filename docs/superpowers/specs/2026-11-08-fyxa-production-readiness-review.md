# Fyxa production-readiness review

**Review date:** 2026-11-08  
**Scope:** PR #283, platform-agent architecture, task/skill implementation, and external MCP HTTP client.  
**Status:** Authoritative source of truth for implementation status and release gates. Modern MCP transport, protected-server OAuth/PKCE, tool-definition pinning, untrusted-content labeling, atomic task evidence, cancellable DNS waiting, connected-app relay approval/fingerprinting, idempotency metadata, and the approval-record schema have implementation work on the feature branch; current-head CI and end-to-end integration tests remain required.

## Executive decision

The platform-agent direction is sound, but the current branch is a development foundation, not a production-ready remote-agent release. Keep PR #283 draft and unmerged until the supported build/test matrix is green and the critical security/protocol work below is complete.

The HTTP client negotiates the MCP 2026-07-28 stateless protocol first. It sends `server/discover`, attaches protocol metadata to each request, emits `MCP-Protocol-Version` and `Mcp-Method`, and emits `Mcp-Name` for named operations. It does not send or retain a session ID in modern mode. Legacy initialize/initialized and session handling remain a narrow compatibility path, entered only after a structured `-32022` unsupported-version or `-32601` unknown-method response; transport, TLS, and authorization failures do not trigger downgrade. Wire-envelope tests are included; current-head CI is required before treating them as verified.

## Findings and decisions

### P0 — protocol, authorization, and trust

1. **MCP 2026-07-28 stateless protocol**
   - Modern requests use the 2026-07-28 stateless envelope with protocol/client metadata in `_meta`, required modern HTTP headers, and no `Mcp-Session-Id` or legacy initialized notification.
   - Legacy initialize/initialized and session handling remain a narrow compatibility path, entered only after a structured unsupported-version or unknown-method response. Transport, TLS, and authorization failures do not trigger downgrade.
   - Wire-level tests cover modern and legacy envelopes, malformed metadata, missing routing names, and header-injection attempts. Reordered-key and live round-robin-server integration tests remain outstanding.

2. **OAuth for protected remote MCP — core authorization and refresh flow implemented; step-up/revocation remain**
   - OAuth remains optional for public, unauthenticated HTTP MCP endpoints. Protected servers can now use the browser-based authorization-code flow with PKCE S256.
   - Fyxa discovers protected-resource metadata from `WWW-Authenticate` or RFC 9728 well-known URLs, validates the exact resource URI, discovers authorization-server metadata, and requires an exact issuer match.
   - The authorization request and token exchange both include the RFC 8707 `resource` indicator. RFC 9207 `iss` is checked before processing either successful or error callbacks; missing `iss` is rejected when the server advertises it as required.
   - Fyxa uses OS cryptographic randomness, a loopback-only callback listener, PKCE S256, and public-client (`token_endpoint_auth_method=none`) registration. It supports a user-supplied pre-registered client ID (including a client-ID metadata URL) or Dynamic Client Registration when offered. DCR is retained as a compatibility path because the current desktop client does not publish a stable Client ID Metadata Document.
   - Access and refresh tokens are stored in the OS credential vault where available; unsupported platforms retain them only in memory. Refresh tokens are bound to the stored issuer/resource/client and the issuer metadata plus token endpoint are revalidated before a refresh grant. Expired tokens are refreshed on HTTP 401 for both modern stateless and legacy session-based MCP.
   - Remaining OAuth work: incremental scope/step-up authorization, explicit revocation, broader multi-authorization-server selection, and live integration tests against protected MCP servers. Static bearer credentials remain available for local-development compatibility.
   - Never expose tokens or authorization headers to model context, tool descriptions, audit logs, crash reports, or UI diagnostics.

3. **Tool-definition integrity / MCPoison resistance — implementation added; tests pending CI**
   - Fyxa computes a canonical BLAKE2b-256 fingerprint over tool name, description, input schema, annotations, server identity, and endpoint.
   - The fingerprint is pinned when the model queues a call. Immediately before execution, Fyxa fetches the live `tools/list` catalog and compares the definition; missing or changed definitions fail closed and require rediscovery/re-approval.
   - External calls still require per-call approval. Server enablement is not blanket authorization.
   - Unit tests cover description, schema, and endpoint changes; reordered-key and full live-server integration tests remain to be added.

4. **Untrusted MCP metadata and results — partial defense added**
   - External tool descriptions now carry an explicit untrusted-metadata marker; tool results are wrapped as untrusted data, and the system guide tells the model not to follow embedded instructions or let them alter permissions/approval.
   - This is defense in depth, not a complete prompt-injection solution. Structural host authorization remains authoritative; metadata and results never grant capabilities.
   - Do not rely on regex attempts to remove executable instructions. Continue testing malicious descriptions, schemas, annotations, resource content, and tool results against the host-side permission boundary.

5. **Structural capability enforcement and connected-app relay trust boundary**
   - The registry and execution dispatcher are the source of truth for callable capabilities. The UI must report unavailable domains as an application-owned state, not ask the model to self-report its limitations.
   - First-party services must independently authorize identity, ownership, scopes, and resource state on every operation.
   - Connected-app relay calls now receive the same Studio-side boundary as external MCP: tool definitions are fingerprinted when tools/list is returned; each tools/call must match that app's cached fingerprint; every call pauses for explicit Studio approval; approval rechecks the live tool fingerprint; the Studio MCP dispatcher re-applies its connected-app tool allowlist and write-scope gate immediately before dispatch.
   - Relay tool descriptions/results carry explicit untrusted-data labels. Approval requests, denials, fingerprint mismatches, and execution outcomes are written to the redacted MCP activity audit log.
   - OAuth scopes and server-side mcp-policy.js remain necessary but are not substitutes for the Studio-side approval gate. A connected-app token is not blanket approval.

### P1 — task integrity, runtime reliability, and context

6. **Task evidence transaction — implementation added; tests pending CI**
   - Fyxa now tracks one explicit active task rather than choosing the first in-progress task. Successful tool results issue task-bound evidence tokens persisted in the task store.
   - `completeWithEvidence` validates task status and token, appends the verification note, changes status, and consumes all pending tokens in one locked atomic file replacement. Ordinary `update(..., completed, ...)` is rejected.
   - The store uses both a process mutex and an OS-level lock file (Windows `LockFileEx`, POSIX `flock`) to serialize cooperating processes. Tests cover wrong-task tokens, replay, direct-completion bypass, and concurrent completion; CI execution is pending.

7. **Cancellation and bounded execution — cancellable DNS wait implemented; full shutdown verification pending**
   - Fyxa's Stop action now sets a shared atomic cancellation token captured by provider and approved external-MCP workers. The HTTP client checks it before connect, during non-blocking connect waits, TLS handshake waits, request writes, response-header reads, and response-body reads. Socket readiness waits poll in 50 ms slices.
   - Late provider responses are discarded. Late MCP completions are converted to cancellation errors and cannot issue task evidence or persist a refreshed token after Stop. Queued external calls are returned as not executed. The UI warns that an MCP server may already have received and acted on an in-flight request; cancellation is not remote rollback.
   - A loopback HTTP regression test holds a request open, signals cancellation, and requires the client to return within one second. The test is registered in CTest; current-head execution is still pending.
   - DNS lookup now runs behind a bounded resolver lane (maximum four outstanding system lookups). The calling worker polls cancellation while waiting and returns promptly; the resolver thread owns shared state and cannot access caller stack memory after cancellation. Outbound Connection::connectTo accepts the same cancellation token, and the connected-app relay cancels a pending dial when disabled or destroyed.
   - The OS getaddrinfo operation itself is not forcibly interrupted; a cancelled lookup may finish in the detached resolver lane. The lane is capped and new lookups fail fast when saturated. This bounds agent/relay shutdown, but platform CI and a stalled-resolver stress test are still required before claiming a hard system-wide resolver bound.
   - OAuth/discovery futures also need cancellation propagated through every metadata and token request; the implementation is partial. Continue bounding concurrency, response bytes, tool-call count, total task time, and per-provider retries.

8. **Dynamic capability/tool loading**
   - Do not send the entire platform registry to every model turn. Add domain/task intent selection, then expose only the small set of relevant tools; permit explicit discovery when needed.
   - Keep authorization independent of tool visibility. A hidden tool is not an authorization mechanism.
   - Cache deterministic tool catalogs where supported and invalidate on capability/version changes.

9. **Provider capability negotiation**
   - Define adapter capabilities for parallel tool calls, maximum tool count/schema size, structured output, streaming, cancellation, context budget, and retry behavior.
   - The orchestrator must adapt concurrency and prompt construction to the active model; do not assume Ollama and Claude behave identically.
   - Model allowlisting/trust labels are a useful policy layer, but credentials and secrets must never enter model context regardless of trust level.

10. **Conversation and task recovery**
    - Persist bounded conversation checkpoints linked to task IDs, with explicit user controls, retention limits, and secret/PII filtering.
    - On restart, resume only from a validated checkpoint and re-evaluate capabilities and permissions. Never replay side-effecting tool calls automatically.


11. **Single-active-task v1 scope — explicitly accepted product constraint**
   - Fyxa v1 intentionally supports one explicit active task. This is not an accidental scheduler limitation: the task-evidence model binds successful tool results to one active task and does not yet support interleaved evidence streams.
   - When a user turn contains multiple independent goals, Fyxa must choose at most one active task, create/retain the others as pending, tell the user what is deferred, and ask which pending task to prioritize after the active task is completed or paused. It must never silently start a second task or replace the current task.
   - The task dispatcher now rejects a second in_progress transition while another task is active. The user-facing deferral wording and multi-request behavior still need integration tests.

12. **Idempotency for external side effects — metadata propagation added; remote enforcement is conditional**
   - Every outbound external MCP tool invocation receives a cryptographically random idempotency key in params._meta[io.gutsandbolts/idempotency-key]. The key is retained with the queued call and included in the redacted activity audit record.
   - Remote MCP servers must explicitly implement and persist deduplication for this extension. A client-generated key cannot prevent duplicate execution when a server ignores it. Do not automatically retry an ambiguous side-effecting timeout unless the same key is reused and the remote service documents deduplication behavior.
   - First-party purchase, transfer, publish, and other side-effecting service providers must also accept a server-enforced idempotency key before they are exposed.

13. **Approval record model — schema and focused tests added; current behavior remains per-call**
   - FyxaApprovalRecord defines audit-linkable, time-boxed records bound to server identity/fingerprint, exact tool identity/fingerprint, permission tier, issue/expiry times, and revocation time.
   - Server-wide grants are restricted to read-only scope. Write grants require exact tool binding. The current UI continues to require individual approval for every external MCP and connected-app tool call; no blanket grant is enabled.
   - The record model is additive groundwork only. Persisting grants, revocation UI, and trust-tier evaluation remain future work and must not be described as implemented.

### P2 — operational quality

14. **Observability**
    - Add structured spans/events around provider requests, MCP discovery/calls, approval decisions, tool execution, and task transitions.
    - Distinguish JSON-RPC protocol failures from tool-level failures. An MCP `tools/call` response can be a successful JSON-RPC result with `isError: true`; it must be counted as a failed tool execution.
    - Keep redacted audit records for compliance; tracing complements rather than replaces them. Make OpenTelemetry export optional and avoid leaking arguments, credentials, or private result content.

15. **Skills versioning and conflict handling**
    - Add explicit skill metadata (stable ID, version, author/source, compatibility, changelog or digest) and show when a user skill overrides a bundled skill.
    - Skills remain inert instruction documents, never executable code or permissions. If executable extensions are added later, they require a separate sandbox design.
    - Define deterministic conflict handling for overlapping skill IDs and never let skill text register or replace tools.

16. **Approval fatigue / trust policy**
    - Keep per-call approval as the safe baseline. Design a future opt-in trust model with explicit scope, server/tool fingerprint binding, expiration, revocation, and stronger confirmation for writes/destructive actions.
    - Do not implement blanket server-level approval.

17. **Provider rate limits and graceful degradation**
    - Surface 429/rate-limit state and retry-after information. Use bounded backoff and preserve task state.
    - Offer provider switching only when the user has configured the alternate provider; do not silently send private conversation data to another provider.

18. **Prompt caching**
    - Arrange stable system instructions and stable tool prefixes before variable conversation/context. Use provider-specific prompt caching only where supported and verify cache behavior; never let cache optimizations weaken tenant or user isolation.

## Delivery gates

- [ ] P0 modern/legacy MCP protocol tests pass on current-head CI (test target added; execution pending).
- [ ] P0 tool-definition fingerprint and approval invalidation tests pass on current-head CI (BLAKE2b pinning and live preflight implemented; focused unit tests added).
- [ ] P0 protected-remote OAuth flow and token audience/issuer tests pass before protected remote MCP is enabled for general users.
- [ ] P0 trust-boundary tests prove external MCP and connected-app content cannot grant permissions or bypass approval; connected-app relay calls require current tool fingerprints and per-call Studio approval.
- [ ] P1 task evidence completion is atomic and replay-safe (store implementation and focused tests added; current-head CI pending).
- [ ] P1 cancellation tests cover provider calls, MCP HTTP, queued work, cancellable DNS waits, pending relay dials, and shutdown.
- [ ] P1 dynamic tool loading and provider capability adaptation tests pass.
- [ ] P1 idempotency tests verify key stability across a retry of the same queued invocation and audit redaction; remote deduplication is verified per integration.
- [ ] P1 approval-record tests cover exact server/tool fingerprints, read/write scope, expiration, revocation, and serialization.
- [ ] Current-head Windows, Linux, and test CI jobs pass; no unverified build claims.
- [ ] PR stays draft until all required gates are satisfied.

## Source notes

- MCP 2026-07-28 changelog and stateless protocol announcement: https://github.com/modelcontextprotocol/modelcontextprotocol/blob/main/docs/specification/2026-07-28/changelog.mdx and https://blog.modelcontextprotocol.io/posts/2026-07-28/
- MCP OAuth 2.1 client guidance and resource binding: https://ts.sdk.modelcontextprotocol.io/v2/clients/oauth
- MCPoison / CVE-2025-54136: https://research.checkpoint.com/2025/cursor-vulnerability-mcpoison/
- MCP tool-level error semantics (`isError: true`): https://ts.sdk.modelcontextprotocol.io/v2/servers/errors
