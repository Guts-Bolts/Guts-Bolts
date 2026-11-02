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
      redirect: 'error',
    });
  } finally {
    clearTimeout(timeout);
  }
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
export async function insertSupabaseRows(env, table, rows) {
  if (!['gb_notifications', 'gb_auction_events', 'gb_forum_activity'].includes(table)) {
    throw new TypeError('Unsupported Supabase activity table.');
  }
  if (!Array.isArray(rows) || !rows.length) return;
  const response = await supabaseRest(env, '/rest/v1/' + table + '?on_conflict=id', {
    method: 'POST',
    headers: {
      'content-type': 'application/json',
      prefer: 'resolution=ignore-duplicates,return=minimal',
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

