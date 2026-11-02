-- Supabase migration version 20261102201821 (applied to project pfpzmmsosyetkcoymoct).
-- Creator stats: one row each time someone opens a game (not its creator), so the
-- creator's stats page can show when people play, and how many come back.
-- Who played isn't kept: only the game, its creator, new or returning, and when.
-- Only the Worker (service_role) can touch it.

create table if not exists public.gb_play_events (
  id text primary key,
  game_id text not null,
  creator_id text not null,
  is_returning boolean not null default false,
  is_guest boolean not null default false,
  created_at timestamptz not null
);
create index if not exists gb_play_events_game_created_idx
  on public.gb_play_events (game_id, created_at desc);
alter table public.gb_play_events enable row level security;
revoke all on table public.gb_play_events from anon, authenticated, service_role;
grant select, insert on table public.gb_play_events to service_role;

-- For some games over the last p_days days (at most 365):
--   hours: plays in each hour of the day (UTC, 0-23), last 30 days
--   new / returning / guests: who the plays were from, last 30 days
--   days: [{day: 'YYYY-MM-DD', plays}] for each day of the whole p_days (UTC, oldest first)
create or replace function public.gb_creator_playtimes(p_games text[], p_days int default 90)
returns jsonb
language sql stable
set search_path = ''
as $$
  with span as (select least(greatest(coalesce(p_days, 90), 1), 365) as n),
  ev as (
    select e.created_at, e.is_returning, e.is_guest
    from public.gb_play_events e, span
    where e.game_id = any(p_games) and e.created_at >= (now() at time zone 'utc')::date - (span.n - 1)
  ),
  recent as (select * from ev where created_at >= now() - interval '30 days')
  select jsonb_build_object(
    'hours', (select jsonb_agg(coalesce(c.n, 0) order by h.h)
              from generate_series(0, 23) h(h)
              left join (select extract(hour from created_at at time zone 'utc')::int as h, count(*) as n from recent group by 1) c on c.h = h.h),
    'new', (select count(*) from recent where not is_returning and not is_guest),
    'returning', (select count(*) from recent where is_returning),
    'guests', (select count(*) from recent where is_guest),
    'days', (select jsonb_agg(jsonb_build_object('day', to_char(d.d, 'YYYY-MM-DD'), 'plays', coalesce(c.n, 0)) order by d.d)
             from span, generate_series((now() at time zone 'utc')::date - (span.n - 1), (now() at time zone 'utc')::date, interval '1 day') d(d)
             left join (select (created_at at time zone 'utc')::date as d, count(*) as n from ev group by 1) c on c.d = d.d::date)
  );
$$;
revoke all on function public.gb_creator_playtimes(text[], int) from public, anon, authenticated;
grant execute on function public.gb_creator_playtimes(text[], int) to service_role;
