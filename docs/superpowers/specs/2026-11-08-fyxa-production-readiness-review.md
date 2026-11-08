# Fyxa production-readiness review

**Review date:** 2026-11-08  
**Scope:** PR #283, platform-agent architecture, task/skill implementation, and external MCP HTTP client.  
**Status:** Findings accepted into the engineering plan; this document does not claim the fixes are implemented.

## Executive decision

The platform-agent direction is sound, but the current branch is a development foundation, not a production-ready remote-agent release. Keep PR #283 draft and unmerged until the supported build/test matrix is green and the critical security/protocol work below is complete.

The highest-priority correction is MCP protocol compatibility. The current HTTP client in AssistantPanel uses the 2025-era initialize/initialized handshake and session ID model. The MCP 2026-07-28 protocol defines a stateless lifecycle: no initialize handshake or protocol session ID; each request carries protocol version and client capabilities in `_meta`, with modern HTTP routing headers. Implement the modern protocol deliberately, and retain legacy support only as an explicit compatibility path for older servers.

## Findings and decisions

### P0 — protocol, authorization, and trust

1. **MCP 2026-07-28 stateless protocol**
   - Current code performs `initialize`, sends `notifications/initialized`, persists `Mcp-Session-Id`, and sends session-bound requests.
   - Implement 2026-07-28 requests with `io.modelcontextprotocol/protocolVersion` and `io.modelcontextprotocol/clientCapabilities` in `_meta`; send required modern HTTP headers (`MCP-Protocol-Version`, `Mcp-Method`, and applicable `Mcp-Name`); support `server/discover` and per-request protocol errors.
   - Keep 2025-era handshake support only for servers that require it. Do not silently downgrade on arbitrary network/auth failures.
   - Add wire-level tests for both eras, malformed metadata, header/body mismatch, and a server behind round-robin routing.

2. **OAuth for protected remote MCP**
   - Clarification: OAuth is not mandatory for every public, unauthenticated HTTP MCP endpoint. When a server uses the MCP authorization framework, implement the conformant OAuth 2.1 client flow rather than treating a manually pasted bearer token as the production auth solution.
   - Required design areas include authorization-server and protected-resource metadata discovery, PKCE S256 for authorization-code flow, RFC 8707 resource binding/audience validation, issuer validation (RFC 9207), issuer-bound credential storage, scopes/step-up, refresh and revocation/error handling, and safe localhost redirect handling.
   - Static bearer credentials may remain a clearly labelled local-development compatibility option, not the default for protected remote servers.
   - Never expose tokens or authorization headers to model context, tool descriptions, audit logs, crash reports, or UI diagnostics.

3. **Tool-definition integrity / MCPoison resistance**
   - Compute a canonical fingerprint over each tool's name, description, input schema, and security-relevant annotations.
   - Bind the fingerprint to the server identity and endpoint. When discovery changes a definition, invalidate prior approval and require the user to review the changed definition before any call.
   - Do not treat server enablement or approval of a different definition as approval of the new definition.
   - Test description-only changes, schema-only changes, reordered JSON keys, server URL changes, and unchanged definitions.

4. **Untrusted MCP metadata and results**
   - Treat tool descriptions, annotations, resource content, and tool results as untrusted data. They may inform a task but cannot change system policy, capability grants, or confirmation requirements.
   - Do not rely on fragile regex attempts to remove "executable instructions." Prefer bounded metadata, clear untrusted-content delimiting, conservative tool descriptions, and a hard separation between model suggestions and host authorization.
   - Every mutating external call still passes the host's approval and permission checks. Tool results never auto-trigger an unapproved follow-up action.

5. **Structural capability enforcement**
   - The registry and execution dispatcher are the source of truth for callable capabilities. The UI must report unavailable domains as an application-owned state, not ask the model to self-report its limitations.
   - First-party services must independently authorize identity, ownership, scopes, and resource state on every operation.

### P1 — task integrity, runtime reliability, and context

6. **Task evidence transaction**
   - Current task-store file operations use a process-local mutex, but evidence lookup/issuance and task completion are separate operations. Evidence is currently associated with the first in-progress task, which is ambiguous if multiple tasks are active.
   - Replace this with an explicit active task ID and a store operation that atomically validates status, validates a one-time evidence token, appends the completion note, changes status, and consumes the token under one lock/transaction.
   - Reject stale, cross-task, replayed, and concurrent completions. Add multithreaded and multi-process tests before claiming cross-process safety.

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

- [ ] P0 modern/legacy MCP protocol tests pass.
- [ ] P0 tool-definition fingerprint and approval invalidation tests pass.
- [ ] P0 protected-remote OAuth flow and token audience/issuer tests pass before protected remote MCP is enabled for general users.
- [ ] P0 trust-boundary tests prove external content cannot grant permissions or bypass approval.
- [ ] P1 task evidence completion is atomic and replay-safe.
- [ ] P1 cancellation tests cover provider calls, MCP HTTP, queued work, and shutdown.
- [ ] P1 dynamic tool loading and provider capability adaptation tests pass.
- [ ] Current-head Windows, Linux, and test CI jobs pass; no unverified build claims.
- [ ] PR stays draft until all required gates are satisfied.

## Source notes

- MCP 2026-07-28 changelog and stateless protocol announcement: https://github.com/modelcontextprotocol/modelcontextprotocol/blob/main/docs/specification/2026-07-28/changelog.mdx and https://blog.modelcontextprotocol.io/posts/2026-07-28/
- MCP OAuth 2.1 client guidance and resource binding: https://ts.sdk.modelcontextprotocol.io/v2/clients/oauth
- MCPoison / CVE-2025-54136: https://research.checkpoint.com/2025/cursor-vulnerability-mcpoison/
- MCP tool-level error semantics (`isError: true`): https://ts.sdk.modelcontextprotocol.io/v2/servers/errors
