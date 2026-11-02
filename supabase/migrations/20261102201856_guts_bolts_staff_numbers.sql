-- Supabase migration version 20261102201856 (applied to project pfpzmmsosyetkcoymoct).
-- Staff numbers: one row a day with how many accounts there are and the most people
-- online and playing at once that day (the Worker sends a reading every few minutes).
-- With gb_play_events this makes the Staff page's Numbers tab. Only the Worker
-- (service_role) can touch it.

create table if not exists public.gb_daily_stats (
  day date primary key,
  accounts bigint not null default 0,
  peak_online int not null default 0,
  peak_playing int not null default 0,
  updated_at timestamptz not null default now()
);
alter table public.gb_daily_stats enable row level security;
revoke all on table public.gb_daily_stats from anon, authenticated, service_role;
grant select on table public.gb_daily_stats to service_role;

-- A reading: keeps the day's biggest numbers.
create or replace function public.gb_record_daily(p_day date, p_accounts bigint, p_online int, p_playing int)
returns void
language sql volatile
set search_path = ''
as $$
  insert into public.gb_daily_stats as d (day, accounts, peak_online, peak_playing, updated_at)
  values (p_day, greatest(p_accounts, 0), greatest(p_online, 0), greatest(p_playing, 0), now())
  on conflict (day) do update set
    accounts = greatest(d.accounts, excluded.accounts),
    peak_online = greatest(d.peak_online, excluded.peak_online),
    peak_playing = greatest(d.peak_playing, excluded.peak_playing),
    updated_at = now();
$$;
revoke all on function public.gb_record_daily(date, bigint, int, int) from public, anon, authenticated;
grant execute on function public.gb_record_daily(date, bigint, int, int) to service_role;

-- The last p_days days (oldest first; days with no reading are left out) and the most
-- played games of the last 7 days.
create or replace function public.gb_staff_numbers(p_days int default 60)
returns jsonb
language sql stable
set search_path = ''
as $$
  with span as (select least(greatest(coalesce(p_days, 60), 1), 365) as n)
  select jsonb_build_object(
    'days', coalesce((
      select jsonb_agg(jsonb_build_object('day', to_char(d.day, 'YYYY-MM-DD'), 'accounts', d.accounts,
               'peakOnline', d.peak_online, 'peakPlaying', d.peak_playing, 'plays', coalesce(p.n, 0)) order by d.day)
      from public.gb_daily_stats d, span
      left join lateral (select count(*) as n from public.gb_play_events e
                         where e.created_at >= d.day and e.created_at < d.day + 1) p on true
      where d.day > (now() at time zone 'utc')::date - span.n), '[]'::jsonb),
    'topGames', coalesce((
      select jsonb_agg(jsonb_build_object('game', t.game_id, 'plays', t.n) order by t.n desc)
      from (select e.game_id, count(*) as n from public.gb_play_events e
            where e.created_at >= now() - interval '7 days' group by e.game_id order by n desc limit 10) t), '[]'::jsonb)
  );
$$;
revoke all on function public.gb_staff_numbers(int) from public, anon, authenticated;
grant execute on function public.gb_staff_numbers(int) to service_role;
