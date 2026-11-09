# Fyxa core sessions and memory

**Implementation status:** The initial account-scoped session metadata and explicit memory CRUD operations are implemented in the G&B Durable Object. This is not yet a chat inference endpoint, RAG index, autonomous task runner, or model-training pipeline.

## Design boundaries

- The existing G&B account identity and signed-request mechanism remain authoritative.
- Session and memory records are private to the authenticated account.
- A project ID is an opaque namespace supplied by the client. It is not proof of project membership or authorization to access project files.
- Conversational memory is operational user data. Creating or retrieving a memory does not enable model training or AI reference indexing.
- Training and reference eligibility continue to be governed independently by the existing Fyxa AI data controls.
- Memory audit records contain event metadata and resource IDs, not memory text.
- Session deletion removes memories explicitly linked to that session. Other memories remain until separately deleted.
- This first increment stores conversation metadata only. Message persistence and provider inference are deliberately separate follow-up work.

## Signed API operations

All operations use the existing signed G&B request flow and require a signed-in account.

### Sessions

Create a session:

```json
{ "op": "ai.sessions.create", "args": { "title": "Vehicle system", "projectId": "project-42" } }
```

List sessions (limit 1–50, default 30):

```json
{ "op": "ai.sessions.list", "args": { "projectId": "project-42", "limit": 30, "offset": 0 } }
```

Get or delete one session:

```json
{ "op": "ai.sessions.get", "args": { "sessionId": "SESSION_ID" } }
{ "op": "ai.sessions.delete", "args": { "sessionId": "SESSION_ID" } }
```

A session lookup or deletion by another account returns the same not-found response as an unknown session.

### Memory

Create a memory:

```json
{
  "op": "ai.memory.create",
  "args": {
    "content": "The vehicle system uses the engine physics API.",
    "memoryType": "project",
    "projectId": "project-42",
    "tags": ["vehicles", "physics"],
    "importance": 4
  }
}
```

Supported memory types: `user`, `feedback`, `project`, `reference`, and `preference`.

Content is limited to 4,000 UTF-8 bytes. Tags are deduplicated and limited to ten entries of 32 UTF-8 bytes each. Session titles are limited to 120 UTF-8 bytes; project namespace IDs are limited to 128 UTF-8 bytes. Importance is clamped to 1–5.

List, update, and delete:

```json
{ "op": "ai.memory.list", "args": { "projectId": "project-42", "memoryType": "project", "limit": 50, "offset": 0 } }
{ "op": "ai.memory.update", "args": { "memoryId": "MEMORY_ID", "content": "Updated note", "importance": 5 } }
{ "op": "ai.memory.delete", "args": { "memoryId": "MEMORY_ID" } }
```

A memory can optionally reference a session owned by the same account. A session cannot be referenced across accounts. Deleting a session removes only memories explicitly sourced from it.

## Storage

The Durable Object SQLite database contains:

- `ai_sessions`: account-scoped session metadata.
- `ai_memories`: account-scoped memory content, project namespace, type, tags, importance, and optional source session.
- `ai_memory_audit`: append-only metadata events for session and memory changes.

Indexes support account-scoped recency listing and project/type filtering. No media, embeddings, model artifacts, or large document payloads are stored in these tables.

## Remaining work before production chat

1. Add provider-neutral inference behind a server-only adapter; provider credentials must never be sent to clients.
2. Add message storage with strict role ownership. Clients may submit user messages; assistant/tool messages must be written only by trusted server-side inference and tool orchestration.
3. Add request limits, per-account quotas, cancellation, timeouts, and bounded response sizes.
4. Add explicit project membership/visibility checks before project context or files are retrieved. An opaque project ID alone is not authorization.
5. Add retrieval with source-level permission checks and citations. Do not retrieve private content merely because an account has enabled reference indexing.
6. Add memory retrieval/ranking and a retention policy. The current CRUD layer does not automatically infer, summarize, or retrieve memories.
7. Add tests for provider failures, authorization, prompt injection boundaries, deletion propagation, and concurrency.
8. Purge sessions, memories, and memory audit rows when an account is deleted.\n9. Keep model training as a separate pipeline with its existing explicit eligibility and rights-review gates.

## Test coverage

`worker/test/fyxa-core-memory.test.mjs` covers account isolation, project filtering, validation, memory lifecycle, session deletion behavior, and audit payload minimization.
