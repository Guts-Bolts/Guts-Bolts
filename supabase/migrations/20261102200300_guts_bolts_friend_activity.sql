-- Friend activity for the home page's My Feed: "X is playing Y", "X made Y", "X earned a
-- badge in Y". The Worker checks who may see each line (blocks, the person's privacy
-- setting, the thing still being public) every time the feed is read.
-- Only the Worker (service_role) can touch it.

create table if not exists public.gb_friend_activity (
  id text primary key,
  account_id text not null,
  kind text not null check (kind in ('playing', 'created', 'badge')),
  about_id text not null,
  about_kind text not null default '',
  about_name text not null default '',
  detail text not null default '' check (char_length(detail) <= 100),
  created_at timestamptz not null
);
create index if not exists gb_friend_activity_account_created_idx
  on public.gb_friend_activity (account_id, created_at desc);
alter table public.gb_friend_activity enable row level security;
revoke all on table public.gb_friend_activity from anon, authenticated, service_role;
grant select, insert on table public.gb_friend_activity to service_role;
