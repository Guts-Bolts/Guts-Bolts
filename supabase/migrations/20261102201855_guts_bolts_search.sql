-- Supabase migration version 20261102201855 (applied to project pfpzmmsosyetkcoymoct).
-- Better search: the name, description, genres and creator of every game, catalog item and
-- Library thing, copied here by the Cloudflare Worker so searching can forgive typos
-- ("zombe" finds "Zombie Survival") and match whole words anywhere.
-- The Durable Object still decides what each visitor may see: search only hands back ids,
-- and the Worker runs them through the normal catalog rules (private, under review...).
-- Only the Worker (service_role) can touch it.

create extension if not exists pg_trgm with schema extensions;

create table if not exists public.gb_search_items (
  id text primary key,                       -- the asset id
  section text not null,                     -- 'game', 'catalog' (things to wear) or the Library kind
  kind text not null,
  name text not null default '',
  description text not null default '',
  genres text not null default '',
  creator_name text not null default '',
  popularity bigint not null default 0,      -- plays + sales, roughly (for ties)
  removed boolean not null default false,    -- deleted, private or under review
  updated_at timestamptz not null,
  doc tsvector generated always as (
    to_tsvector('simple', coalesce(name, '') || ' ' || coalesce(description, '') || ' ' || coalesce(genres, '') || ' ' || coalesce(creator_name, ''))
  ) stored
);
create index if not exists gb_search_items_doc_idx on public.gb_search_items using gin (doc);
create index if not exists gb_search_items_name_trgm_idx on public.gb_search_items using gin (lower(name) extensions.gin_trgm_ops);
create index if not exists gb_search_items_section_idx on public.gb_search_items (section) where not removed;
alter table public.gb_search_items enable row level security;
revoke all on table public.gb_search_items from anon, authenticated, service_role;
grant select, insert, update on table public.gb_search_items to service_role;

-- The ids that best match p_query in one section (or all of them), best first.
-- Whole words match through the text search; misspelled names through trigram similarity.
create or replace function public.gb_search(p_query text, p_section text default null, p_limit int default 100)
returns table (id text, score real)
language sql stable
set search_path = ''
as $$
  with q as (
    select websearch_to_tsquery('simple', left(coalesce(p_query, ''), 100)) as tq,
           lower(left(coalesce(p_query, ''), 100)) as lq
  )
  select i.id,
         (ts_rank(i.doc, q.tq) + 2 * extensions.word_similarity(q.lq, lower(i.name))
           + ln(1 + greatest(i.popularity, 0)) / 50)::real as score
  from public.gb_search_items i, q
  where not i.removed
    and char_length(q.lq) >= 2
    and (p_section is null or i.section = p_section)
    and (i.doc @@ q.tq
         or extensions.word_similarity(q.lq, lower(i.name)) >= 0.4
         or lower(i.creator_name) = q.lq)
  order by score desc, i.id
  limit least(greatest(coalesce(p_limit, 100), 1), 200);
$$;
revoke all on function public.gb_search(text, text, int) from public, anon, authenticated;
grant execute on function public.gb_search(text, text, int) to service_role;
