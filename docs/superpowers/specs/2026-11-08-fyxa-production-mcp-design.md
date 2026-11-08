# Fyxa Production MCP Design

## Intent
Make Fyxa a dependable in-Studio assistant that can use native Guts & Bolts tools and user-configured external MCP servers without freezing the editor or silently performing risky third-party actions.

## Goals
- Support Streamable HTTP and local stdio MCP transports.
- Support bearer tokens and OAuth 2.1 authorization-code + PKCE for compatible HTTP servers.
- Keep secrets out of assistant.json; use OS credential storage where available and session-only credentials otherwise.
- Run discovery and tool calls off the UI thread, with cancellation, timeouts, bounded responses, and clean shutdown.
- Show external tool identity, arguments, result, and status in an activity log.
- Require explicit user approval for external write/unknown tools; allow user-configured read-only trust for known safe tools.
- Preserve native AI Activity and undo behavior for native Studio tools.
- Never claim arbitrary third-party side effects can be rolled back. Support undo only when a server explicitly implements a compatible compensating operation.

## Non-goals
- No blanket guarantee that third-party MCP servers are safe or reversible.
- No plaintext storage of OAuth refresh tokens or bearer tokens in assistant.json.
- No silent elevation of external tools to trusted status.
- No browser OAuth in headless builds or platforms without a callback-capable browser flow; those connections remain token-based/session-only.

## Approaches
1. Keep adding transport, process, and OAuth code inline in AssistantPanel. Rejected: the panel already owns UI, providers, settings, MCP hosting, and relay.
2. Extract an ExternalMcp manager and focused protocol, process, OAuth, and credential components. Chosen: keep UI orchestration separate; serialize external I/O on a worker; reuse existing Socket TLS and JSON facilities.
3. Add a separate proxy daemon. Rejected: unnecessary installation/deployment lifecycle.

## Architecture
- ExternalMcp owns server definitions, sessions, tool schemas, OAuth state, credential references, and an asynchronous operation queue.
- HTTP uses Net::httpPost with HTTPS validation, MCP protocol negotiation, session headers, JSON/SSE parsing, pagination, and bounded responses.
- stdio uses a cross-platform child-process adapter with stdin/stdout pipes; stderr is captured separately and never parsed as protocol output.
- A worker serializes I/O and returns immutable completions; the editor applies UI changes only on its main thread.
- Credential adapters use Windows Credential Manager, macOS Keychain, and Linux Secret Service when available. If unavailable, credentials are session-only and the UI says so.
- OAuth uses metadata discovery, PKCE S256, browser launch, loopback callback, state validation, refresh, and secure-store persistence.
- External MCP and connected-app actions have a shared redacted audit record with server/app, tool, timestamp, redacted arguments, result, approval decision, fingerprint, and idempotency key where applicable. Native AI Activity remains authoritative for native Studio edits.

## Safety
- Remote cleartext HTTP is denied; loopback HTTP is allowed.
- Reject URL userinfo, malformed hosts/ports, header injection, duplicate tool identifiers, invalid schemas, and oversized responses.
- Never include credentials, authorization codes, or refresh tokens in logs, prompts, tool results, or crash messages.
- Require per-call user approval for every external MCP tool invocation. Approval shows server origin and arguments with secrets redacted. Connected-app relay calls must be fingerprinted at discovery and revalidated immediately before the approved call.
- Revalidate enabled state and tool identity immediately before execution.
- Cancellation and shutdown terminate/reap child processes and do not wait indefinitely on remote servers. DNS waiting is isolated behind a bounded cancellable resolver lane; see the production-readiness review for its remaining platform verification.
- Arbitrary third-party side effects are not assumed reversible.

## Compatibility
- Preserve Ollama, Claude, native AI tools, Studio MCP hosting, connected-app relay, and native AI Activity undo.
- Read existing assistant.json settings; migrate legacy plaintext keys/tokens into secure storage where possible, then remove them from the file.
- WebAssembly omits stdio and OS credential adapters and disables unsupported controls.
