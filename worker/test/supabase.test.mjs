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
    assert.equal(init.redirect, 'manual');
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
    assert.equal(init.redirect, 'manual');
    return new Response('[]', { status: 200 });
  }, async () => {
    const response = await supabaseRest(env, '/rest/v1/auction_events?select=id', {
      headers: { authorization: 'Bearer untrusted-client-token' },
    });
    assert.equal(response.status, 200);
    await response.body.cancel();
  });
});

test('Supabase event writes use the server key and idempotent conflict handling', async () => {
  const { insertSupabaseRows } = await import('../supabase.js');
  await withFetch(async (input, init) => {
    assert.equal(new URL(input).pathname, '/rest/v1/gb_auction_events');
    assert.equal(new URL(input).searchParams.get('on_conflict'), 'id');
    assert.equal(init.headers.get('apikey'), env.SUPABASE_SECRET_KEY);
    assert.match(init.headers.get('prefer'), /resolution=ignore-duplicates/);
    assert.equal(init.headers.get('authorization'), null);
    assert.equal(init.method, 'POST');
    return new Response(null, { status: 204 });
  }, async () => {
    await insertSupabaseRows(env, 'gb_auction_events', [{ id: 'event-1', event_type: 'bid_placed' }]);
  });
});

test('notification archive reads and read-state updates are scoped to one account', async () => {
  const { listSupabaseNotifications, markSupabaseNotificationsRead } = await import('../supabase.js');
  const account = 'a'.repeat(64);
  const calls = [];
  await withFetch(async (input, init) => {
    calls.push({ url: new URL(input), init });
    if (init.method === 'PATCH') return new Response(null, { status: 204 });
    return Response.json([{ id: 'note-1', kind: 'friend', message: 'Hello', about: '',
      created_at: '2026-11-01T00:00:00Z', read_at: null }]);
  }, async () => {
    const notes = await listSupabaseNotifications(env, account, 200);
    assert.equal(notes.length, 1);
    assert.equal(calls[0].url.searchParams.get('account_id'), 'eq.' + account);
    await markSupabaseNotificationsRead(env, account);
    assert.equal(calls[1].init.method, 'PATCH');
    assert.equal(calls[1].url.searchParams.get('account_id'), 'eq.' + account);
    assert.equal(calls[1].url.searchParams.get('read_at'), 'is.null');
  });
});

test('public activity reads are bounded and reject private notification tables', async () => {
  const { listSupabaseActivity } = await import('../supabase.js');
  await withFetch(async (input) => {
    const url = new URL(input);
    assert.equal(url.pathname, '/rest/v1/gb_forum_activity');
    assert.equal(url.searchParams.get('limit'), '20');
    return Response.json([]);
  }, async () => {
    assert.deepEqual(await listSupabaseActivity(env, 'gb_forum_activity', 999), []);
    await assert.rejects(() => listSupabaseActivity(env, 'gb_notifications', 10), /Unsupported activity feed/);
  });
});


test('Supabase REST helper never asks Cloudflare for the unsupported "error" redirect mode', async () => {
  // Cloudflare Workers throw "Invalid redirect value" for redirect: 'error',
  // which made every Supabase call fail on the live site.
  await withFetch(async (input, init) => {
    assert.ok(['follow', 'manual'].includes(init.redirect));
    return new Response(null, { status: 302, headers: { location: 'https://elsewhere.example/' } });
  }, async () => {
    const health = await checkSupabaseHealth(env);
    assert.equal(health.connected, false);
    assert.equal(health.reason, 'api_error');
  });
});
