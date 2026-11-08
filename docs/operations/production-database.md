# Production database operations

This runbook describes the production Guts & Bolts database and the recovery work required before treating it as production-ready.

## Production topology

- The production Worker is `project-guts-and-bolts`.
- The Worker routes `/api` and `/ws` to the `GbServerObject` Durable Object named `main`.
- `GbServerObject` uses the SQLite storage API through `ctx.storage.sql`.
- Application records are stored in SQLite tables including `users`, `assets`, `groups`, `forum`, `meta`, `files`, `nonces`, `gamedata`, and `pasttrades`. Fyxa's account preferences, per-content eligibility decisions, and audit events are stored in the additive `ai_training_preferences`, `ai_content_eligibility`, and `ai_training_audit` tables.
- Large file data is stored in `files` in 1 MiB pieces.
- Fyxa eligibility and preference records are compact SQLite metadata, not media blobs or model-training datasets. The eligibility tables are included in the logical R2 backup export; training itself is not implemented by these tables.
- The application currently loads users, assets, groups, and forum records into in-memory Maps when the Durable Object starts. Most application lookups then operate on those Maps; this is not a conventional SQL-per-request/N+1 architecture.

## Current consistency and scale characteristics

The request handler's `handle()` and `op()` paths are synchronous. They perform validation and state changes, then `flush()` persists dirty records using synchronous SQLite calls in the same request turn. This avoids JavaScript `await` points inside the business-operation mutation path.

Cloudflare documents that synchronous SQLite storage calls do not yield the event loop, and a sequence of storage writes without an intervening `await` is atomic. Preserve this property for balance changes, purchases, trades, auction bids, and other multi-record operations. Do not add an `await` between reading mutable state and committing its related writes without introducing an explicit concurrency/transaction design.

Important scaling constraints to measure:

1. The Worker currently routes API requests and multiplayer WebSocket connections to the same globally named Durable Object (`main`). It is a single coordination point.
2. On startup, the object scans and parses all rows from `users`, `assets`, `groups`, and `forum` into memory. Startup cost and memory use therefore grow with the total stored dataset.
3. Several operations iterate whole in-memory collections (for example, catalog/pass lookups and limited-copy ownership scans). These are in-memory full scans, not N+1 SQL queries, but can become expensive as collections grow.
4. Most application entities are stored as JSON blobs keyed by ID. SQL indexes are currently limited; `pasttrades` has indexes for both participants and timestamp.
5. WebSocket relay state is held in memory. A Durable Object restart can close live game connections; the application has takeover logic, but reconnect behavior should be load-tested.

Do not shard or migrate the database based on a 1,000-user target alone. First collect realistic cold-start time, database size, request latency, CPU duration, memory behavior, concurrent WebSocket count, and SQL rows read/written.

## Recovery capabilities

The production namespace is SQLite-backed. Cloudflare provides Durable Object SQLite Point-in-Time Recovery (PITR) for the entire object's SQLite database, including SQL data and KV data, to a point within the previous 30 days.

Relevant Cloudflare documentation:

- https://developers.cloudflare.com/durable-objects/api/sqlite-storage-api/#pitr-point-in-time-recovery-api
- https://developers.cloudflare.com/durable-objects/best-practices/access-durable-objects-storage/

The runtime APIs are:

- `ctx.storage.getCurrentBookmark()` — capture the current recovery point.
- `ctx.storage.getBookmarkForTime(timestamp)` — obtain a bookmark for a time in the previous 30 days.
- `ctx.storage.onNextSessionRestoreBookmark(bookmark)` — schedule a restore on the next Durable Object session. Cloudflare documents that the object should then be aborted/restarted to complete the restore. The method returns a bookmark from immediately before recovery, which can be used to undo the restore.

### Critical limitation

The current Guts & Bolts Worker does **not** expose an operator-controlled endpoint or command for capturing PITR bookmarks or initiating a restore. Do not attempt recovery by editing live data through Data Studio or by deleting/recreating the Durable Object namespace.

Before an incident, implement and review an authenticated, staff/operator-only recovery mechanism that:

1. Captures and securely records a pre-operation bookmark.
2. Requires an explicit confirmation and a target timestamp/bookmark.
3. Schedules the restore and restarts the object using the documented Cloudflare API.
4. Emits an audit event without logging secrets or player data.
5. Verifies core tables and application invariants after restart.
6. Supports an immediate rollback to the pre-restore bookmark.

A restore rewinds the **entire** Durable Object database. Writes after the chosen recovery point will be lost unless they are separately captured and reconciled. Treat restore as a high-impact incident operation.

## Independent backups

PITR is valuable but is not an independent export. No separate, verified off-object database backup or restore drill was established during the repository audit.

Before implementing scheduled exports, measure the current database size and write volume. The database includes account records and file BLOBs, so an export must be encrypted, access-restricted, integrity-checked, and stored separately from the production namespace. Do not place unencrypted database dumps in the public downloads bucket.

Recommended backup design:

- A dedicated private R2 bucket, separate from the public downloads bucket.
- A scheduled export with a versioned manifest, timestamp, schema/application revision, row counts, and checksums.
- Encryption before upload; key material stored as a Cloudflare secret, never in Git.
- Retention policy with daily and weekly restore points.
- A restore verifier that imports into an isolated test namespace/database and checks record counts, foreign references, Bolts ledger invariants, ownership, and file hashes.
- Alerts on missed backups, failed exports, checksum mismatches, and failed restore drills.

Do not declare the backup system complete until an isolated restore has succeeded.

## Readiness measurements

Collect and retain these measurements before and during load tests:

- Durable Object SQLite database size and distance from the 10 GB per-object storage limit.
- Counts and serialized sizes for each application table.
- Constructor cold-start duration and memory footprint at realistic data volumes.
- API p50/p95/p99 latency by operation family.
- Worker and Durable Object CPU duration and exceptions.
- Concurrent WebSocket connections, message throughput, and reconnect rates.
- Full-map scan durations for catalog, ownership, and social queries.
- Rows read/written and top SQL statements after tracing is enabled.

Use production-like synthetic data for load tests. Never copy real player records into an unsecured test environment.

## Release and recovery checklist

- [ ] Capture a current PITR bookmark before a planned high-risk data migration.
- [ ] Confirm the operator-only PITR recovery mechanism is deployed and access-controlled.
- [ ] Confirm an independent encrypted backup exists outside the production Durable Object.
- [ ] Complete an isolated restore drill and record duration and validation results.
- [ ] Confirm the rollback bookmark and incident owner are recorded.
- [ ] Verify Worker deployment rollback is available separately from database rollback.
- [ ] Run concurrency and load tests against production-like data before increasing traffic.
