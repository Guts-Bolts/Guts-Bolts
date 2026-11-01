-- Supabase migration version 20261101162338 (applied to project pfpzmmsosyetkcoymoct).
-- The Durable Object remains authoritative for accounts, Bolts, bids and forum posts.
-- Only the Cloudflare Worker service_role may access these supporting activity tables.
-- No anon/authenticated policies or grants are created: RLS defaults to deny.

-- Supporting activity storage for Guts&Bolts.
-- The Durable Object remains authoritative for accounts, Bolts, bids and forum posts.
-- These tables are written/read only by the Cloudflare Worker using its server-only
-- Supabase secret key. They are not directly accessible to anon/authenticated clients.

create table if not exists public.gb_notifications (
  id text primary key,
  account_id text not null,
  kind text not null check (char_length(kind) between 1 and 32),
  message text not null check (char_length(message) <= 500),
  about text not null default '',
  created_at timestamptz not null,
  read_at timestamptz
);
create index if not exists gb_notifications_account_created_idx
  on public.gb_notifications (account_id, created_at desc);
alter table public.gb_notifications enable row level security;
revoke all on table public.gb_notifications from anon, authenticated;
grant select, insert, update on table public.gb_notifications to service_role;

create table if not exists public.gb_auction_events (
  id text primary key,
  auction_id text not null,
  asset_id text not null,
  asset_name text not null,
  event_type text not null check (event_type in ('auction_started', 'bid_placed', 'auction_cancelled', 'auction_completed')),
  actor_name text not null default '',
  amount bigint,
  current_high_bid bigint not null default 0,
  ends_at timestamptz,
  created_at timestamptz not null
);
create index if not exists gb_auction_events_created_idx
  on public.gb_auction_events (created_at desc);
create index if not exists gb_auction_events_asset_created_idx
  on public.gb_auction_events (asset_id, created_at desc);
alter table public.gb_auction_events enable row level security;
revoke all on table public.gb_auction_events from anon, authenticated;
grant select, insert on table public.gb_auction_events to service_role;

create table if not exists public.gb_forum_activity (
  id text primary key,
  thread_id text not null,
  board_id text not null,
  board_name text not null,
  thread_title text not null,
  event_type text not null check (event_type in ('thread_created', 'thread_reply')),
  actor_name text not null,
  post_excerpt text not null default '',
  created_at timestamptz not null
);
create index if not exists gb_forum_activity_created_idx
  on public.gb_forum_activity (created_at desc);
create index if not exists gb_forum_activity_thread_created_idx
  on public.gb_forum_activity (thread_id, created_at desc);
alter table public.gb_forum_activity enable row level security;
revoke all on table public.gb_forum_activity from anon, authenticated;
grant select, insert on table public.gb_forum_activity to service_role;
