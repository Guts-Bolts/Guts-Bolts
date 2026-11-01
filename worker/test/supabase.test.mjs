import { test } from 'node:test';
import assert from 'node:assert/strict';
import { checkSupabaseHealth, supabaseRest } from '../supabase.js';

const env = {
  SUPABASE_URL: 'https://pfpzmmsosyetkcoymoct.supabase.co',
  SUPABASE_SECRET_KEY: 'sb_secret_test_only',
};

function withFetch(mock, run) {
  const original = globalThis.fetch;
  globalThis.fetch = mock;
  return Promise.resolve().then(run).finally(() => { globalThis.fetch = original; });
}

test('Supabase health reports an unconfigured Worker without making a request', async () => {
  assert.deepEqual(await checkSupabaseHealth({}), {
    configured: false,
    connected: false,
    reason: 'not_configured',
  });
});

test('Supabase health validates the secret key against the Data API without exposing it', async () => {
  await withFetch(async (input, init) => {
    assert.equal(new URL(input).href, 'https://pfpzmmsosyetkcoymoct.supabase.co/rest/v1/');
    assert.equal(init.headers.get('apikey'), env.SUPABASE_SECRET_KEY);
    assert.equal(init.headers.get('authorization'), null);
    assert.equal(init.headers.get('accept'), 'application/openapi+json');
    assert.equal(init.redirect, 'error');
    return new Response('{}', { status: 200 });
  }, async () => {
    assert.deepEqual(await checkSupabaseHealth(env), { configured: true, connected: true });
  });
});

test('Supabase health reports rejected credentials without returning upstream content', async () => {
  await withFetch(async () => new Response('sensitive upstream detail', { status: 401 }), async () => {
    assert.deepEqual(await checkSupabaseHealth(env), {
      configured: true,
      connected: false,
      reason: 'invalid_credentials',
      status: 401,
    });
  });
});

test('Supabase REST helper rejects paths outside the Data API', async () => {
  await assert.rejects(() => supabaseRest(env, 'https://attacker.example/rest/v1/items'), TypeError);
  await assert.rejects(() => supabaseRest(env, '/rest/v1/../auth/v1/admin'), TypeError);
});

test('Supabase REST helper removes caller-supplied Authorization and keeps the secret server-side', async () => {
  await withFetch(async (input, init) => {
    assert.equal(new URL(input).origin, 'https://pfpzmmsosyetkcoymoct.supabase.co');
    assert.equal(init.headers.get('apikey'), env.SUPABASE_SECRET_KEY);
    assert.equal(init.headers.get('authorization'), null);
    assert.equal(init.redirect, 'error');
    return new Response('[]', { status: 200 });
  }, async () => {
    const response = await supabaseRest(env, '/rest/v1/auction_events?select=id', {
      headers: { authorization: 'Bearer untrusted-client-token' },
    });
    assert.equal(response.status, 200);
    await response.body.cancel();
  });
});
