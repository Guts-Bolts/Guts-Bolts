# Supabase integration

Guts&Bolts uses Supabase as a supporting backend service. The Cloudflare Worker remains the trusted integration point; the existing Durable Object remains authoritative for player accounts, sign-in, Bolts, inventory, and core game state.

## Current integration

- The Worker has a server-only PostgREST helper in `worker/supabase.js`.
- `GET /api/supabase/health` checks the configured Supabase Data API connection. The endpoint returns only whether the connection is configured and reachable; it does not return credentials, schema, or database rows.
- The Worker uses the Supabase secret API key only in its server-side `apikey` header. It is never bundled into `website/` or shipped to the Player/Studio apps.
- No player tables, duplicate authentication, or currency records are created by this foundation change.

## Configure the Worker

In the Cloudflare dashboard, open **Workers & Pages → project-guts-and-bolts → Settings → Variables and Secrets**.

1. Confirm the plain-text variable `SUPABASE_URL` is set to `https://pfpzmmsosyetkcoymoct.supabase.co`. It is also declared in `wrangler.jsonc`.
2. In Supabase, open **Project Settings → API Keys** and create a dedicated **Secret key** for the Guts&Bolts Worker (for example, name it `guts-and-bolts-worker`).
3. Add that value to Cloudflare as a **Secret** named `SUPABASE_SECRET_KEY`.
4. Do not paste the key into chat, commit it to Git, add it to `wrangler.jsonc`, or expose it to browser code. If it is ever exposed, rotate/delete it in Supabase and replace the Cloudflare secret.
5. Redeploy the Worker if the dashboard does not activate the new binding immediately.

The Worker deployment uses `keep_vars: true`, so dashboard-managed variables and secrets are retained across normal deployments.

## Verify the connection

After the secret is configured and the Worker is deployed, request:

```http
GET https://gutsandbolts.net/api/supabase/health
```

A successful response is similar to:

```json
{
  "ok": true,
  "supabase": {
    "configured": true,
    "connected": true
  }
}
```

A `503` response means the Worker has not been configured, cannot reach the Data API, or Supabase rejected the key. The response gives a safe reason code and never echoes the key or raw upstream response.

## Security model

Supabase secret keys have elevated `service_role` access and bypass Row Level Security. They are therefore restricted to this trusted Worker. Any future operation that reads or changes player-related data must first authorize the caller using Guts&Bolts' existing signed-request/session rules. Never accept an arbitrary table name, SQL fragment, or unrestricted PostgREST path from a client.

For every future exposed table, enable RLS and add narrowly scoped policies. Also grant only the roles that need Data API access. Supabase's October 30, 2026 rollout means new tables are not automatically exposed through the Data API; exposure must be explicitly granted. Keep internal-only tables out of exposed schemas where practical.

## Intended use

Good candidates for later Supabase-backed features include live auction broadcasts, forum activity, player notifications, and supporting relational data. Each feature should be introduced with its own reviewed schema/migration and authorization model. The existing Guts&Bolts database remains the source of truth for accounts, balances, ownership, and core transactions unless a deliberate migration is designed and tested.
