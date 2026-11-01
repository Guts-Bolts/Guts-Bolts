# Supabase live activity services

Supabase stores supporting activity records for the Cloudflare Worker. It does not replace the Guts&Bolts Durable Object as the authority for accounts, balances, bids, inventory, forum permissions, or forum content.

## Services

- **Auction broadcasts:** the Worker records auction start, bid, cancellation, and completion events. The public homepage reads a small recent-event feed from /api/activity/auctions.
- **Notification archive:** every native Guts&Bolts notification is mirrored into gb_notifications. The existing notes.list response merges the native notification list with up to 200 archived entries. notes.read updates both stores.
- **Forum activity:** thread creation and replies create short public activity records. The forum index reads /api/activity/forum.

The website refreshes these feeds periodically through the Worker. This deliberately uses the existing Guts&Bolts request/authentication system rather than introducing Supabase Auth or exposing a Supabase key to browsers. It is a polling feed, not a direct browser subscription to Supabase Realtime.

## Data flow and reliability

1. A successful operation changes the Durable Object's authoritative state.
2. The Worker writes an event to the Durable Object's local SQLite outbox in the same request lifecycle.
3. A Durable Object alarm delivers batches to Supabase using idempotent inserts (id is the conflict key).
4. Failed delivery leaves rows in the outbox and schedules a retry. Supabase outages do not roll back or block core Guts&Bolts operations.
5. Notification history reads fall back to the native Durable Object list if Supabase is unavailable.

## Security

- SUPABASE_URL is public project configuration in wrangler.jsonc.
- SUPABASE_SECRET_KEY is a Cloudflare Worker secret. Never put it in website assets, desktop/mobile clients, source control, or public logs.
- All three tables have RLS enabled.
- anon and authenticated have no table privileges. Only service_role has the specific privileges required by the Worker.
- Auction and forum feed routes return selected public columns only. Notification reads filter by the authenticated Guts&Bolts account ID obtained from the existing signed request.
- No Supabase Auth identities are created, and no password, email, wallet, balance, bid reservation, or private message content is copied into these tables.

## Schema

The applied schema is documented in supabase/live_activity_schema.sql.

## Operational notes

- The current feed endpoints cap responses at 20 records; notification archive reads cap at 200.
- Notification history is mirrored independently of the native 50-entry notification preview. The native store remains authoritative for unread badge counts.
- Event rows are append-only except notification read_at. Add a retention policy only after deciding the desired audit/history window.
- Monitor the Durable Object supabase_outbox table and Supabase API health endpoint at /api/supabase/health if feed delivery stalls.
