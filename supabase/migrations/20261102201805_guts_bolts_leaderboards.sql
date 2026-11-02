-- Supabase migration version 20261102201805 (applied to project pfpzmmsosyetkcoymoct).
-- Game leaderboards: the scores games keep in OrderedDataStores, copied here by the
-- Cloudflare Worker so the game's page can show the top players of each board.
-- The Durable Object's gamedata table stays the real copy (GetSortedAsync reads it);
-- this one is only for showing. Only the Worker (service_role) can touch it.

create table if not exists public.gb_leaderboard_scores (
  id text primary key,                      -- game | store | key
  game_id text not null,
  store text not null check (char_length(store) between 1 and 100),
  entry_key text not null check (char_length(entry_key) between 1 and 100),
  player_name text not null default '' check (char_length(player_name) <= 40),
  player_user_id bigint,
  score double precision,                   -- null: the key was removed
  updated_at timestamptz not null
);
create index if not exists gb_leaderboard_scores_board_idx
  on public.gb_leaderboard_scores (game_id, store, score desc) where score is not null;
alter table public.gb_leaderboard_scores enable row level security;
revoke all on table public.gb_leaderboard_scores from anon, authenticated, service_role;
grant select, insert, update on table public.gb_leaderboard_scores to service_role;

-- The top p_per_board scores of every board of one game (biggest first), with their rank.
create or replace function public.gb_leaderboard_top(p_game text, p_per_board int default 10)
returns table (store text, rank bigint, player_name text, player_user_id bigint, score double precision)
language sql stable
set search_path = ''
as $$
  select r.store, r.rank, r.player_name, r.player_user_id, r.score
  from (
    select s.store, s.player_name, s.player_user_id, s.score,
           row_number() over (partition by s.store order by s.score desc, s.entry_key) as rank
    from public.gb_leaderboard_scores s
    where s.game_id = p_game and s.score is not null
  ) r
  where r.rank <= least(greatest(p_per_board, 1), 25)
  order by r.store, r.rank;
$$;
revoke all on function public.gb_leaderboard_top(text, int) from public, anon, authenticated;
grant execute on function public.gb_leaderboard_top(text, int) to service_role;
