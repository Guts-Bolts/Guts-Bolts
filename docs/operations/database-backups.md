# Database diagnostics and backups

## Production database

The production application database is the SQLite database attached to the `GbServerObject` Durable Object named `main`. The Worker uses `ctx.storage.sql`; this is not the connected Supabase project.

## Staff diagnostics

The signed server operation `staff.database.stats` is restricted to staff. It returns aggregate metadata only:

- SQLite database size in bytes
- page count, page size, and free-page count
- row counts for each application table
- total application rows
- collection timestamp

It does not return player records, file contents, secrets, or raw SQL rows. Staff tools can call it through the normal signed API operation mechanism.

## Independent backup system

A private R2 bucket named `guts-and-bolts-db-backups` stores logical exports separately from the public downloads bucket. The Worker binding is `DB_BACKUPS`.

A Cloudflare Cron Trigger runs daily at 03:00 UTC. It calls the existing `main` Durable Object through an internal RPC method; there is no public HTTP backup endpoint.

Each backup contains:
- a transactionally consistent snapshot of the nine application tables
- each table's CREATE TABLE schema
- BLOB values encoded as tagged base64 values
- aggregate database diagnostics
- a gzip-compressed JSON payload
- a separate manifest with SHA-256, byte count, row counts, timestamp, and R2 ETag

Objects are written under `daily/YYYY-MM-DD/`. The scheduled job removes only objects under this backup prefix that are older than 35 days. It does not touch the downloads bucket or other R2 objects.

### Memory safety limit

The backup job refuses to start when the reported SQLite database size exceeds `DB_BACKUP_MAX_BYTES` (default 128 MiB). The export is assembled in memory before compression so that every table comes from one synchronous SQLite transaction. If the database grows beyond this threshold, backups fail closed rather than risk exhausting the Durable Object isolate. Increase the limit only after measuring memory headroom and changing the export implementation to stream safely.

### Restore status

This PR creates and schedules independent exports; it does not automatically overwrite the live database from an export. Cloudflare's 30-day Durable Object SQLite PITR remains the supported immediate rollback mechanism. A restore/import workflow must be separately reviewed and tested against an isolated database before it is allowed to mutate production state.

## Operations and verification

- Check the Cloudflare Worker Cron Trigger execution history and Worker logs for scheduled failures.
- Inspect the private R2 bucket for a daily payload and manifest.
- Verify SHA-256 against the uncompressed payload after gzip decompression.
- Compare manifest table row counts with a fresh staff diagnostic response.
- Perform restore drills only in an isolated test namespace/database.
- Alert on missing daily manifests, failed scheduled invocations, and database size approaching the backup ceiling.

The first scheduled backup occurs at the next 03:00 UTC run after this configuration is deployed. No production database is copied or modified during deployment.
