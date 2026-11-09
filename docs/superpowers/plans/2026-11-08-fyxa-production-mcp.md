# Fyxa Production MCP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete Fyxa's external MCP lifecycle with secure credentials, HTTP and stdio transports, OAuth PKCE, asynchronous execution, approval, audit, and tests.

**Architecture:** Extract external MCP concerns from AssistantPanel into a focused manager. A serialized worker owns network/process operations; the UI consumes completed results on the main thread. Platform-specific credential and process adapters sit behind small interfaces.

**Tech Stack:** Existing C++17 codebase, nlohmann::json, Net::httpPost/TLS, platform APIs, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-11-08-fyxa-production-mcp-design.md`

## Global Constraints
- Preserve Ollama, Claude, native AI tools, Studio MCP hosting, connected-app relay, and native AI Activity undo.
- Never store credentials in plaintext assistant.json.
- Remote endpoints require HTTPS; cleartext HTTP is loopback-only.
- All network/process I/O runs outside the editor UI thread.
- stdio is unavailable in WebAssembly builds.
- Arbitrary external side effects are not assumed reversible.

## Review Focus
- Malformed/hostile endpoint URLs and header injection: endpoint parser tests.
- MCP JSON/SSE framing, chunked/empty/oversized responses: transport tests.
- Child process exits, noisy stderr, partial lines, and shutdown: process lifecycle tests.
- OAuth state mismatch, expired tokens, failed refresh, and callback replay: auth tests.
- Server removal/disable during queued calls and approval denial: worker/authorization tests.

---

### Task 1: MCP protocol and endpoint primitives
**Files:** Create src/editor/ExternalMcpProtocol.h/.cpp and tests/ExternalMcpProtocolTests.cpp; modify CMakeLists.txt.
**Interfaces:** parseEndpoint(url, Endpoint&, error), parseJsonOrSse(body, json&, error), qualifiedToolName(serverId, toolName).
- [ ] Write tests for HTTPS, loopback HTTP, invalid schemes, userinfo, IPv6, ports, tool IDs, JSON and SSE.
- [ ] Run focused tests and confirm expected failures.
- [ ] Implement strict parser/protocol helpers with size limits.
- [ ] Run focused tests and relevant CTest suite.
- [ ] Commit.

### Task 2: Secure credential storage
**Files:** Create src/core/CredentialStore.h/.cpp and tests/CredentialStoreTests.cpp; modify CMakeLists.txt.
**Interfaces:** store(key, secret, error), retrieve(key, secret, error), erase(key, error), persistentAvailable().
- [ ] Test credential reference migration and session-only fallback.
- [ ] Implement Windows Credential Manager and macOS Keychain adapters; implement Linux Secret Service only if supported by existing dependencies, otherwise session-only fallback.
- [ ] Add WebAssembly session-only behavior.
- [ ] Migrate legacy assistant.json secrets without logging or re-emitting them.
- [ ] Verify serialized settings contain no secret values.
- [ ] Commit.

### Task 3: stdio process transport
**Files:** Create src/editor/McpProcess.h/.cpp and tests/McpProcessTests.cpp; modify CMakeLists.txt.
**Interfaces:** start(command, args, env, error), sendLine(json), pollLine(line), stop(gracePeriod).
- [ ] Test request/response, stderr isolation, malformed output, early exit, output caps, and termination.
- [ ] Implement Windows pipes/CreateProcess and POSIX pipes/fork/exec with explicit executable+argument arrays, never shell interpolation.
- [ ] Ensure child processes are reaped on shutdown.
- [ ] Commit.

### Task 4: OAuth 2.1 + PKCE
**Files:** Create src/editor/McpOAuth.h/.cpp and tests/McpOAuthTests.cpp; modify CMakeLists.txt.
**Interfaces:** beginAuthorization(server, callbackPort, result, error), handleCallback(request, result, error), refresh(refreshToken, result, error).
- [ ] Test PKCE S256, state verification, callback validation, metadata parsing, expiry, and refresh failures.
- [ ] Implement authorization/resource metadata discovery, browser launch, loopback callback, code exchange, and refresh.
- [ ] Persist tokens only through CredentialStore.
- [ ] Fall back to manual bearer token for incompatible servers.
- [ ] Commit.

### Task 5: Async MCP manager
**Files:** Create src/editor/ExternalMcp.h/.cpp and tests/ExternalMcpTests.cpp; modify CMakeLists.txt.
**Interfaces:** addServer(ServerConfig), removeServer(id), setEnabled(id, bool), refreshTools(id), callTool(id, tool, args, approval), poll(Completion&), shutdown().
- [ ] Test queue ordering, cancellation, server-state revalidation, pagination, sessions, timeout, and response limits.
- [ ] Implement one worker queue for HTTP and stdio.
- [ ] Integrate OAuth and CredentialStore.
- [ ] Ensure shutdown signals cancellation and never waits indefinitely on a remote server.
- [ ] Commit.

### Task 6: Approval, activity, and AssistantPanel integration
**Files:** Modify src/editor/panels/AssistantPanel.cpp/.h; create src/editor/ExternalMcpActivity.h/.cpp; update activity UI only where required.
- [ ] Test default-deny approval, read-only trust, redaction, and audit persistence.
- [ ] Add UI for HTTP/stdio configuration, OAuth connect, secure-token status, per-server permissions, and pending approvals.
- [ ] Route Ollama and Claude tool calls through ExternalMcp.
- [ ] Display server/tool/arguments/results in external-action activity history.
- [ ] Keep native tool execution on existing runAiToolFrom path.
- [ ] Never claim arbitrary external tools are undoable without a server-provided compensating operation.
- [ ] Commit.

### Task 7: End-to-end verification and documentation
- [ ] Run formatter and compiler for supported desktop targets.
- [ ] Run focused tests and full CTest suite.
- [ ] Run GitHub Actions and inspect all job logs.
- [ ] Verify WebAssembly excludes unsupported stdio/keychain controls.
- [ ] Verify secret migration and settings compatibility.
- [ ] Update PR #283 with accurate support matrix and limitations.
- [ ] Request review; do not merge until checks are green.
