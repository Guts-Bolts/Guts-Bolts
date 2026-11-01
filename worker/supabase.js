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
