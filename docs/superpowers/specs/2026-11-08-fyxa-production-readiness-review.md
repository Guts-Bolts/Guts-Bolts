# Fyxa production-readiness review

**Review date:** 2026-11-08  
**Scope:** PR #283, platform-agent architecture, task/skill implementation, and external MCP HTTP client.  
**Status:** Findings accepted into the engineering plan. Modern MCP transport, protected-server OAuth/PKCE, tool-definition pinning, untrusted-content labeling, and atomic task evidence have implementation work on the feature branch; current-head CI and end-to-end integration tests remain required.

## Executive decision

The platform-agent direction is sound, but the current branch is a development foundation, not a production-ready remote-agent release. Keep PR #283 draft and unmerged until the supported build/test matrix is green and the critical security/protocol work below is complete.

The HTTP client negotiates the MCP 2026-07-28 stateless protocol first. It sends `server/discover`, attaches protocol metadata to each request, emits `MCP-Protocol-Version` and `Mcp-Method`, and emits `Mcp-Name` for named operations. It does not send or retain a session ID in modern mode. Legacy initialize/initialized and session handling remain a narrow compatibility path, entered only after a structured `-32022` unsupported-version or `-32601` unknown-method response; transport, TLS, and authorization failures do not trigger downgrade. Wire-envelope tests are included; current-head CI is required before treating them as verified.

## Findings and decisions

### P0 — protocol, authorization, and trust

1. **MCP 2026-07-28 stateless protocol**
   - Current code performs `initialize`, sends `notifications/initialized`, persists `Mcp-Session-Id`, and sends session-bound requests.
   - Implement 2026-07-28 requests with `io.modelcontextprotocol/protocolVersion` and `io.modelcontextprotocol/clientCapabilities` in `_meta`; send required modern HTTP headers (`MCP-Protocol-Version`, `Mcp-Method`, and applicable `Mcp-Name`); support `server/discover` and per-request protocol errors.
   - Keep 2025-era handshake support only for servers that require it. Do not silently downgrade on arbitrary network/auth failures.
   - Add wire-level tests for both eras, malformed metadata, header/body mismatch, and a server behind round-robin routing.

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

5. **Structural capability enforcement**
   - The registry and execution dispatcher are the source of truth for callable capabilities. The UI must report unavailable domains as an application-owned state, not ask the model to self-report its limitations.
   - First-party services must independently authorize identity, ownership, scopes, and resource state on every operation.

### P1 — task integrity, runtime reliability, and context

6. **Task evidence transaction — implementation added; tests pending CI**
   - Fyxa now tracks one explicit active task rather than choosing the first in-progress task. Successful tool results issue task-bound evidence tokens persisted in the task store.
   - `completeWithEvidence` validates task status and token, appends the verification note, changes status, and consumes all pending tokens in one locked atomic file replacement. Ordinary `update(..., completed, ...)` is rejected.
   - The store uses both a process mutex and an OS-level lock file (Windows `LockFileEx`, POSIX `flock`) to serialize cooperating processes. Tests cover wrong-task tokens, replay, direct-completion bypass, and concurrent completion; CI execution is pending.

7. **Cancellation and bounded execution**
   - Current MCP HTTP requests have a finite request timeout and LLM requests have a finite timeout, but an in-flight request is not cooperatively cancellable. A timeout alone is not cancellation.
   - Add a cancellation token from the UI through the agent loop, queued calls, transport, and process manager. Ensure stop and shutdown complete within a documented bound, with no late tool result mutating a stopped task.
   - Bound concurrency, response bytes, tool-call count, total task time, and per-provider retries.

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

### P2 — operational quality

11. **Observability**
    - Add structured spans/events around provider requests, MCP discovery/calls, approval decisions, tool execution, and task transitions.
    - Distinguish JSON-RPC protocol failures from tool-level failures. An MCP `tools/call` response can be a successful JSON-RPC result with `isError: true`; it must be counted as a failed tool execution.
    - Keep redacted audit records for compliance; tracing complements rather than replaces them. Make OpenTelemetry export optional and avoid leaking arguments, credentials, or private result content.

12. **Skills versioning and conflict handling**
    - Add explicit skill metadata (stable ID, version, author/source, compatibility, changelog or digest) and show when a user skill overrides a bundled skill.
    - Skills remain inert instruction documents, never executable code or permissions. If executable extensions are added later, they require a separate sandbox design.
    - Define deterministic conflict handling for overlapping skill IDs and never let skill text register or replace tools.

13. **Approval fatigue / trust policy**
    - Keep per-call approval as the safe baseline. Design a future opt-in trust model with explicit scope, server/tool fingerprint binding, expiration, revocation, and stronger confirmation for writes/destructive actions.
    - Do not implement blanket server-level approval.

14. **Provider rate limits and graceful degradation**
    - Surface 429/rate-limit state and retry-after information. Use bounded backoff and preserve task state.
    - Offer provider switching only when the user has configured the alternate provider; do not silently send private conversation data to another provider.

15. **Prompt caching**
    - Arrange stable system instructions and stable tool prefixes before variable conversation/context. Use provider-specific prompt caching only where supported and verify cache behavior; never let cache optimizations weaken tenant or user isolation.

## Delivery gates

- [ ] P0 modern/legacy MCP protocol tests pass on current-head CI (test target added; execution pending).
- [ ] P0 tool-definition fingerprint and approval invalidation tests pass on current-head CI (BLAKE2b pinning and live preflight implemented; focused unit tests added).
- [ ] P0 protected-remote OAuth flow and token audience/issuer tests pass before protected remote MCP is enabled for general users.
- [ ] P0 trust-boundary tests prove external content cannot grant permissions or bypass approval.
- [ ] P1 task evidence completion is atomic and replay-safe (store implementation and focused tests added; current-head CI pending).
- [ ] P1 cancellation tests cover provider calls, MCP HTTP, queued work, and shutdown.
- [ ] P1 dynamic tool loading and provider capability adaptation tests pass.
- [ ] Current-head Windows, Linux, and test CI jobs pass; no unverified build claims.
- [ ] PR stays draft until all required gates are satisfied.

## Source notes

- MCP 2026-07-28 changelog and stateless protocol announcement: https://github.com/modelcontextprotocol/modelcontextprotocol/blob/main/docs/specification/2026-07-28/changelog.mdx and https://blog.modelcontextprotocol.io/posts/2026-07-28/
- MCP OAuth 2.1 client guidance and resource binding: https://ts.sdk.modelcontextprotocol.io/v2/clients/oauth
- MCPoison / CVE-2025-54136: https://research.checkpoint.com/2025/cursor-vulnerability-mcpoison/
- MCP tool-level error semantics (`isError: true`): https://ts.sdk.modelcontextprotocol.io/v2/servers/errors
