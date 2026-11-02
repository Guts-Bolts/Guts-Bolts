// Server-only Supabase Data API access for the Cloudflare Worker.
// Never import this module from website/ or any client-side application.

const REQUEST_TIMEOUT_MS = 5000;

function supabaseConfig(env) {
  const projectUrl = typeof env.SUPABASE_URL === 'string' ? env.SUPABASE_URL.trim() : '';
  const secretKey = typeof env.SUPABASE_SECRET_KEY === 'string' ? env.SUPABASE_SECRET_KEY.trim() : '';
  if (!projectUrl || !secretKey) return null;

  let url;
  try {
    url = new URL(projectUrl);
  } catch {
    return null;
  }

  // Only allow the configured Supabase HTTPS origin. Do not let a bad setting
  // redirect the privileged API key to an arbitrary host.
  if (url.protocol !== 'https:' || !url.hostname.endsWith('.supabase.co') ||
      url.username || url.password || url.pathname !== '/' || url.search || url.hash) {
    return null;
  }

  return { origin: url.origin, secretKey };
}

/**
 * Make a server-side request to this project's PostgREST API.
 * The secret key maps to Supabase's service_role and bypasses RLS. Callers must
 * perform Guts&Bolts authorization checks before reading or changing user data.
 */
export async function supabaseRest(env, path, options = {}) {
  const config = supabaseConfig(env);
  if (!config) throw new Error('Supabase is not configured correctly.');
  if (typeof path !== 'string' || !path.startsWith('/rest/v1/')) {
    throw new TypeError('Supabase requests must target the REST Data API.');
  }

  const target = new URL(path.slice(1), config.origin + '/');
  if (target.origin !== config.origin || !target.pathname.startsWith('/rest/v1/')) {
    throw new TypeError('Invalid Supabase API path.');
  }

  const headers = new Headers(options.headers || {});
  headers.delete('authorization');
  headers.set('apikey', config.secretKey);
  if (!headers.has('accept')) headers.set('accept', 'application/json');

  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  try {
    return await fetch(target, {
      ...options,
      headers,
      signal: controller.signal,
      // Cloudflare's fetch only knows 'follow' and 'manual': 'error' throws
      // before any request is sent. With 'manual' a redirect comes back as a
      // 3xx answer, which callers already treat as a failure (not response.ok).
      redirect: 'manual',
    });
  } finally {
    clearTimeout(timeout);
  }
}

const LIVE_TOPIC = /^gb-(activity|me-[a-f0-9]{32})$/;
const LIVE_EVENT = /^[a-z]{1,20}$/;

/**
 * Send Realtime Broadcast pings ({ topic, event }) to the website's open pages. Pings carry
 * no data: a page that hears one asks the Guts&Bolts server again, which checks who it is.
 */
export async function broadcastSupabase(env, pings) {
  const config = supabaseConfig(env);
  if (!config) throw new Error('Supabase is not configured correctly.');
  const messages = (Array.isArray(pings) ? pings : [])
    .filter((p) => p && LIVE_TOPIC.test(p.topic) && LIVE_EVENT.test(p.event))
    .slice(0, 100)
    .map((p) => ({ topic: p.topic, event: p.event, payload: {}, private: false }));
  if (!messages.length) return;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
  try {
    const response = await fetch(config.origin + '/realtime/v1/api/broadcast', {
      method: 'POST',
      headers: { apikey: config.secretKey, 'content-type': 'application/json' },
      body: JSON.stringify({ messages }),
      signal: controller.signal,
      redirect: 'manual',
    });
    if (response.body) await response.body.cancel();
    if (!response.ok) throw new Error('Supabase broadcast failed (' + response.status + ').');
  } finally {
    clearTimeout(timeout);
  }
}

/**
 * What the website needs to listen for live pings: the project URL and its publishable key.
 * Both are public by design (the publishable key can't read any Guts&Bolts table).
 */
export function supabasePublicConfig(env) {
  const config = supabaseConfig(env);
  const key = typeof env.SUPABASE_PUBLISHABLE_KEY === 'string' ? env.SUPABASE_PUBLISHABLE_KEY.trim() : '';
  if (!config || !/^sb_publishable_[A-Za-z0-9_-]{10,100}$/.test(key)) return null;
  return { url: config.origin, key };
}

/** Return a safe connection summary without exposing credentials or schema. */
export async function checkSupabaseHealth(env) {
  if (!supabaseConfig(env)) {
    return { configured: false, connected: false, reason: 'not_configured' };
  }

  try {
    const response = await supabaseRest(env, '/rest/v1/', {
      headers: { accept: 'application/openapi+json' },
    });
    const status = response.status;
    if (response.body) await response.body.cancel();
    if (response.ok) return { configured: true, connected: true };
    return {
      configured: true,
      connected: false,
      reason: status === 401 || status === 403 ? 'invalid_credentials' : 'api_error',
      status,
    };
  } catch (error) {
    return {
      configured: true,
      connected: false,
      reason: error?.name === 'AbortError' ? 'timeout' : 'unreachable',
    };
  }
}

const SUPABASE_ACTIVITY_TABLES = new Set(['gb_auction_events', 'gb_forum_activity']);

/** Insert idempotent outbox rows. Replayed delivery cannot duplicate an event. */
// The tables the Worker writes. Events are written once; the others are "latest value wins".
const INSERT_TABLES = new Set(['gb_notifications', 'gb_auction_events', 'gb_forum_activity', 'gb_play_events', 'gb_friend_activity']);
const UPSERT_TABLES = new Set(['gb_leaderboard_scores', 'gb_search_items']);

export async function insertSupabaseRows(env, table, rows) {
  if (!INSERT_TABLES.has(table) && !UPSERT_TABLES.has(table)) {
    throw new TypeError('Unsupported Supabase activity table.');
  }
  if (!Array.isArray(rows) || !rows.length) return;
  const response = await supabaseRest(env, '/rest/v1/' + table + '?on_conflict=id', {
    method: 'POST',
    headers: {
      'content-type': 'application/json',
      prefer: (UPSERT_TABLES.has(table) ? 'resolution=merge-duplicates' : 'resolution=ignore-duplicates') + ',return=minimal',
    },
    body: JSON.stringify(rows),
  });
  if (!response.ok) {
    if (response.body) await response.body.cancel();
    throw new Error('Supabase insert failed (' + response.status + ').');
  }
  if (response.body) await response.body.cancel();
}

/** Read a player's private notification archive. The caller must authenticate first. */
export async function listSupabaseNotifications(env, accountId, limit = 200) {
  if (typeof accountId !== 'string' || !/^[a-f0-9]{32,64}$/i.test(accountId)) {
    throw new TypeError('Invalid notification account.');
  }
  const count = Math.max(1, Math.min(200, Math.floor(Number(limit) || 200)));
  const query = new URLSearchParams({
    select: 'id,kind,message,about,created_at,read_at',
    account_id: 'eq.' + accountId,
    order: 'created_at.desc',
    limit: String(count),
  });
  const response = await supabaseRest(env, '/rest/v1/gb_notifications?' + query.toString());
  if (!response.ok) {
    if (response.body) await response.body.cancel();
    throw new Error('Supabase notification lookup failed (' + response.status + ').');
  }
  return await response.json();
}

/** Mark all archived notifications read for one authenticated account. */
export async function markSupabaseNotificationsRead(env, accountId) {
  if (typeof accountId !== 'string' || !/^[a-f0-9]{32,64}$/i.test(accountId)) {
    throw new TypeError('Invalid notification account.');
  }
  const query = new URLSearchParams({ account_id: 'eq.' + accountId, read_at: 'is.null' });
  const response = await supabaseRest(env, '/rest/v1/gb_notifications?' + query.toString(), {
    method: 'PATCH',
    headers: { 'content-type': 'application/json', prefer: 'return=minimal' },
    body: JSON.stringify({ read_at: new Date().toISOString() }),
  });
  if (!response.ok) {
    if (response.body) await response.body.cancel();
    throw new Error('Supabase notification update failed (' + response.status + ').');
  }
  if (response.body) await response.body.cancel();
}

// The database functions the Worker may call (each one's grants allow only service_role).
const RPC_FUNCTIONS = new Set(['gb_leaderboard_top', 'gb_creator_playtimes', 'gb_search']);

/** Call one of the known database functions with JSON arguments; returns its JSON answer. */
export async function supabaseRpc(env, fn, args) {
  if (!RPC_FUNCTIONS.has(fn)) throw new TypeError('Unknown Supabase function.');
  const response = await supabaseRest(env, '/rest/v1/rpc/' + fn, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(args || {}),
  });
  if (!response.ok) {
    if (response.body) await response.body.cancel();
    throw new Error('Supabase ' + fn + ' failed (' + response.status + ').');
  }
  return await response.json();
}

/**
 * The top scores of each of a game's leaderboards (its OrderedDataStores), through the
 * gb_leaderboard_top database function. The caller checks the visitor may see the game.
 */
export async function listSupabaseLeaderboards(env, gameId, perBoard = 10) {
  if (typeof gameId !== 'string' || !/^[A-Za-z0-9_-]{1,80}$/.test(gameId)) throw new TypeError('Invalid game.');
  const response = await supabaseRest(env, '/rest/v1/rpc/gb_leaderboard_top', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ p_game: gameId, p_per_board: Math.max(1, Math.min(25, Math.floor(Number(perBoard) || 10))) }),
  });
  if (!response.ok) {
    if (response.body) await response.body.cancel();
    throw new Error('Supabase leaderboard lookup failed (' + response.status + ').');
  }
  return await response.json();
}

/** The newest things some accounts did (friend activity). The caller decides whose and checks privacy. */
export async function listSupabaseFriendActivity(env, accountIds, limit = 40) {
  const ids = (Array.isArray(accountIds) ? accountIds : []).filter((id) => typeof id === 'string' && /^[a-f0-9]{32,64}$/i.test(id)).slice(0, 150);
  if (!ids.length) return [];
  const query = new URLSearchParams({
    select: 'account_id,kind,about_id,detail,created_at',
    account_id: 'in.(' + ids.join(',') + ')',
    created_at: 'gte.' + new Date(Date.now() - 14 * 86400000).toISOString(),
    order: 'created_at.desc',
    limit: String(Math.max(1, Math.min(100, Math.floor(Number(limit) || 40)))),
  });
  const response = await supabaseRest(env, '/rest/v1/gb_friend_activity?' + query.toString());
  if (!response.ok) {
    if (response.body) await response.body.cancel();
    throw new Error('Supabase friend activity lookup failed (' + response.status + ').');
  }
  return await response.json();
}

/** Read only the public event columns needed by the website activity feeds. */
export async function listSupabaseActivity(env, table, limit = 8) {
  if (!SUPABASE_ACTIVITY_TABLES.has(table)) throw new TypeError('Unsupported activity feed.');
  const count = Math.max(1, Math.min(20, Math.floor(Number(limit) || 8)));
  const fields = table === 'gb_auction_events'
    ? 'id,asset_id,asset_name,event_type,actor_name,amount,current_high_bid,ends_at,created_at'
    : 'id,thread_id,board_id,board_name,thread_title,event_type,actor_name,post_excerpt,created_at';
  const query = new URLSearchParams({ select: fields, order: 'created_at.desc', limit: String(count) });
  const response = await supabaseRest(env, '/rest/v1/' + table + '?' + query.toString());
  if (!response.ok) {
    if (response.body) await response.body.cancel();
    throw new Error('Supabase activity lookup failed (' + response.status + ').');
  }
  return await response.json();
}

