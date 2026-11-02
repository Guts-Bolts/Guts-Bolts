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
- All three tables have RLS enabled with no client policies; this is intentional default-deny access.
- anon and authenticated have no table privileges. service_role bypasses RLS and has only the specific table privileges required by the Worker.
- Auction and forum feed routes return selected public columns only. Notification reads filter by the authenticated Guts&Bolts account ID obtained from the existing signed request.
- No Supabase Auth identities are created, and no password, email, wallet, balance, bid reservation, or forum post body is copied into these tables. Support-ticket, appeal, custom gift-note, comment, and roadmap notification text is replaced with generic summaries before archiving.

## Schema

The applied schema and least-privilege grants are tracked in supabase/migrations/20261101162338_guts_bolts_live_activity.sql and supabase/migrations/20261101163522_guts_bolts_live_activity_least_privilege.sql.

## Operational notes

- The current feed endpoints cap responses at 20 records; notification archive reads cap at 200.
- Notification history is mirrored independently of the native 50-entry notification preview. The native store remains authoritative for unread badge counts.
- Event rows are append-only except notification read_at. Add a retention policy only after deciding the desired audit/history window.
- Monitor the Durable Object supabase_outbox table and Supabase API health endpoint at /api/supabase/health if feed delivery stalls.

## More features on Supabase (PR #240)

All of these follow the same rules: the Durable Object stays the real copy and decides who
may see what; Supabase only stores supporting copies; every table has RLS on with no client
policies, and only the Worker's secret key (service_role) can reach them.

- **Live updates:** after a request (and after outbox rows land) the Worker sends data-free
  Realtime Broadcast pings: topic `gb-activity` (events `auctions`, `forum`) and each signed-up
  account's secret topic `gb-me-<random>` (events `me`, `feed`; the website learns its topic from
  `me.live`). `website/app/live.js` listens with the publishable key (`SUPABASE_PUBLISHABLE_KEY`
  in wrangler.jsonc, served at `/api/supabase/live`) and asks the server again when a ping comes.
  Bursts are folded into one refresh, since anyone with the publishable key could send pings.
- **Leaderboards:** `gb_leaderboard_scores` (ordered DataStore writes of public games, latest
  value wins) and `gb_leaderboard_top()`; op `game.leaderboards`.
- **Creator play times:** `gb_play_events` (game, creator, new/returning/guest, time; never who)
  and `gb_creator_playtimes()`; op `creator.playtimes`.
- **Search:** `gb_search_items` (sent when an asset's searchable words change; private, under
  review and deleted ones are marked removed) and `gb_search()` (text search + pg_trgm). A `list`
  search adds these near matches after the plain ones, through the normal catalog rules.
- **Friend activity:** `gb_friend_activity`; `feed.list` merges it, checking blocks, the
  person's status privacy and that the thing is still public every time.
- **Staff numbers:** `gb_daily_stats` (a reading every 5 minutes via `gb_record_daily()`) and
  `gb_staff_numbers()`; op `admin.numbers`.

Ops that read Supabase leave `{ supabase: {...} }` in their answer and
`GbServerObject.supabaseFollowUp` fills it in; if Supabase is down the page just leaves that part out.
