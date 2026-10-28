// The online MCP endpoint: https://gutsandbolts.net/mcp (MCP "Streamable HTTP", JSON answers, no SSE stream).
// Based on the plan the Lyreto team sent (MIT).
//
// A connected app (like Lyreto) calls it with the access token it got from "Log in with Guts&Bolts":
//     POST /mcp   Authorization: Bearer gbt_...   body: one JSON-RPC message (or a batch)
// We check the token and its permissions (worker/mcp-policy.js), then pass the message on to that
// person's own Studio through the relay in the server (worker/mcp-relay.js) and send back Studio's
// answer, or a clear "Studio is offline" error when their Studio isn't open with connected apps allowed.
//
//     GET  /mcp/status                              { online, game, studio }
//     GET  /.well-known/oauth-protected-resource    where to get a token (RFC 9728), for MCP clients
//     GET  /.well-known/oauth-authorization-server  the "Log in with Guts&Bolts" addresses (RFC 8414)
//
// Errors apps can act on (JSON-RPC, in the -32000 "server" range):
//   -32010 Studio is offline (data.reason "studio_offline")   -32011 Studio didn't answer in time
//   -32012 Studio is busy                                     -32003 that tool isn't allowed
// publish_game is refused here, again in the relay, and again in Studio itself.

import { canCall, canUseMethod, filterTools, hasStudio } from './mcp-policy.js';

const kMaxBody = 1 << 20, kMaxBatch = 10;
const PROTOCOLS = ['2025-06-18', '2025-03-26', '2024-11-05'];
// How long Studio gets per tool, in seconds (others: 30; methods that aren't tools: 15).
const TOOL_TIMEOUTS = { playtest: 120, multiplayer_test: 120, physics_replay: 90, generate_level: 90, bake_navmesh: 90, run_lua: 60, screenshot: 45 };
export const ERR = { offline: -32010, timeout: -32011, busy: -32012, denied: -32003 };

const json = (o, status = 200, extra = {}) =>
  new Response(JSON.stringify(o), { status, headers: Object.assign({ 'content-type': 'application/json', 'cache-control': 'no-store' }, extra) });
const rpcError = (id, code, message, data) => ({ jsonrpc: '2.0', id: id === undefined ? null : id, error: Object.assign({ code, message }, data ? { data } : {}) });

// serverAsk(env, req): worker/index.js's way to ask the server one thing (connect.tokenInfo here).
export async function handleMcp(request, env, url, serverAsk) {
  const origin = url.origin;
  if (url.pathname.startsWith('/.well-known/oauth-protected-resource'))
    return json({ resource: origin + '/mcp', authorization_servers: [origin], scopes_supported: ['studio.read', 'studio.write'], bearer_methods_supported: ['header'] });
  if (url.pathname === '/.well-known/oauth-authorization-server')
    return json({ issuer: origin, authorization_endpoint: origin + '/api/oauth/authorize', token_endpoint: origin + '/api/oauth/token',
      revocation_endpoint: origin + '/api/oauth/revoke', response_types_supported: ['code'], grant_types_supported: ['authorization_code'],
      scopes_supported: ['identify', 'studio.read', 'studio.write'], token_endpoint_auth_methods_supported: ['client_secret_post', 'client_secret_basic'] });

  // Apps call this from their own server. Web pages (anything a browser sends with an Origin) can't.
  if (request.headers.get('origin')) return json({ error: 'forbidden', error_description: 'Web pages can\'t use this. Call it from your app\'s server.' }, 403);
  if (url.pathname !== '/mcp' && url.pathname !== '/mcp/status') return json({ error: 'not_found' }, 404);
  if (url.pathname === '/mcp' && request.method === 'DELETE') return new Response(null, { status: 204 });   // "end session": we keep none
  if (url.pathname === '/mcp' && request.method !== 'POST')
    return new Response('Send JSON-RPC with POST. (This server doesn\'t open an SSE stream.)', { status: 405, headers: { allow: 'POST, DELETE' } });
  if (url.pathname === '/mcp/status' && request.method !== 'GET') return new Response(null, { status: 405, headers: { allow: 'GET' } });

  // --- who is it, and what may they do? ---
  const bearer = /^Bearer\s+(\S+)$/i.exec(request.headers.get('authorization') || '');
  const unauthorized = (why) => json({ error: 'invalid_token', error_description: why }, 401,
    { 'www-authenticate': 'Bearer error="invalid_token", resource_metadata="' + origin + '/.well-known/oauth-protected-resource"' });
  if (!bearer) return unauthorized('Send the access token as "Authorization: Bearer <token>".');
  const token = bearer[1];
  const who = await serverAsk(env, { op: 'connect.tokenInfo', args: { token } });
  if (!who.ok) return who.oauth === 'temporarily_unavailable' ? json({ error: 'temporarily_unavailable' }, 503) : unauthorized(who.error || 'Bad token.');
  const scopes = Array.isArray(who.scopes) ? who.scopes : [];
  if (!hasStudio(scopes))
    return json({ error: 'insufficient_scope', error_description: 'Connect the app again and allow it to use your Studio (studio.read or studio.write).' }, 403,
      { 'www-authenticate': 'Bearer error="insufficient_scope", scope="studio.read studio.write"' });
  if (env.GB_SERVER) return json({ error: 'temporarily_unavailable', error_description: 'Connected apps can only use Studio through the gutsandbolts.net server.' }, 503);

  if (url.pathname === '/mcp/status') {
    const r = await relay(env, { token, status: true });
    return r.ok ? json({ online: r.online, game: r.game, studio: r.studio }) : json({ error: 'temporarily_unavailable', error_description: r.error || '' }, 503);
  }

  // --- the JSON-RPC message(s) ---
  if (Number(request.headers.get('content-length') || 0) > kMaxBody) return json(rpcError(null, -32600, 'That request is too big.'), 413);
  const text = await request.text();
  if (text.length > kMaxBody) return json(rpcError(null, -32600, 'That request is too big.'), 413);
  let body;
  try { body = JSON.parse(text); } catch { return json(rpcError(null, -32700, 'That wasn\'t valid JSON.'), 400); }
  const batch = Array.isArray(body), messages = batch ? body : [body];
  if (!messages.length || messages.length > kMaxBatch) return json(rpcError(null, -32600, 'Send 1 to ' + kMaxBatch + ' messages at once.'), 400);
  const answers = [];
  for (const m of messages) {   // in order (Studio runs one tool at a time anyway)
    const a = await one(env, token, scopes, m);
    if (a) answers.push(a);
  }
  if (!answers.length) return new Response(null, { status: 202 });   // only notifications
  return json(batch ? answers : answers[0]);
}

async function one(env, token, scopes, m) {
  if (!m || typeof m !== 'object' || Array.isArray(m) || m.jsonrpc !== '2.0' || typeof m.method !== 'string')
    return rpcError(m && typeof m === 'object' ? m.id : null, -32600, 'Not a JSON-RPC 2.0 request.');
  if (!('id' in m)) return null;   // notifications (notifications/initialized ...): nothing to pass on
  const { id, method } = m;
  const params = m.params && typeof m.params === 'object' && !Array.isArray(m.params) ? m.params : {};
  if (method === 'ping') return { jsonrpc: '2.0', id, result: {} };
  if (method === 'initialize') {
    // Studio's own answer (with its guide) when it's online; else ours, so the app can still connect.
    const r = await relay(env, { token, message: { jsonrpc: '2.0', id, method, params }, timeoutMs: 8000 });
    if (r.ok && r.reply && r.reply.result) return r.reply;
    const want = typeof params.protocolVersion === 'string' ? params.protocolVersion : PROTOCOLS[0];
    return { jsonrpc: '2.0', id, result: {
      protocolVersion: PROTOCOLS.includes(want) ? want : PROTOCOLS[0],
      capabilities: { tools: { listChanged: false }, resources: { listChanged: false }, prompts: { listChanged: false } },
      serverInfo: { name: 'guts-and-bolts-studio', title: 'Guts&Bolts Studio', version: '1' },
      instructions: 'These are the AI tools of the person\'s own Guts&Bolts Studio. Studio is offline right now: the tools work while '
        + 'Studio is open and "Allow connected apps to use AI tools" is ticked in its Assistant tab. Publishing is never available here.',
    } };
  }
  if (method === 'tools/call') {
    const verdict = canCall(params.name, scopes);
    if (!verdict.ok) return rpcError(id, ERR.denied, verdict.message);
    return passOn(env, token, { jsonrpc: '2.0', id, method, params }, (TOOL_TIMEOUTS[params.name] || 30) * 1000);
  }
  if (!canUseMethod(method, scopes)) return rpcError(id, -32601, 'Method not available: ' + method.slice(0, 60));
  const reply = await passOn(env, token, { jsonrpc: '2.0', id, method, params }, 15000);
  if (method === 'tools/list' && reply.result) reply.result.tools = filterTools(reply.result.tools, scopes);
  return reply;
}

async function passOn(env, token, message, timeoutMs) {
  const r = await relay(env, { token, message, timeoutMs });
  if (r.ok) return r.reply && typeof r.reply === 'object' ? r.reply : rpcError(message.id, -32603, 'Studio sent no answer.');
  if (r.offline) return rpcError(message.id, ERR.offline, r.error || 'Studio is offline.', { reason: 'studio_offline' });
  if (r.timeout) return rpcError(message.id, ERR.timeout, r.error || 'Studio didn\'t answer in time.', { reason: 'studio_timeout' });
  if (r.busy) return rpcError(message.id, ERR.busy, r.error, { reason: 'studio_busy' });
  if (r.denied) return rpcError(message.id, ERR.denied, r.error);
  return rpcError(message.id, -32603, r.error || 'Something went wrong reaching Studio.');
}

// Ask the relay in the Durable Object (a little longer than Studio's own limit, so Studio's timeout wins).
async function relay(env, body) {
  const stub = env.GB_SERVER_OBJECT.get(env.GB_SERVER_OBJECT.idFromName('main'));
  const ms = (Number(body.timeoutMs) || 15000) + 5000;
  let timer;
  try {
    const r = await Promise.race([
      stub.fetch(new Request('https://gb.internal/mcp-call', { method: 'POST', body: JSON.stringify(body) })),
      new Promise((_, no) => { timer = setTimeout(() => no(new Error('timeout')), ms); }),
    ]);
    return await r.json();
  } catch (e) {
    return e && e.message === 'timeout' ? { ok: false, timeout: true, error: 'Studio didn\'t answer in time.' } : { ok: false, error: 'Couldn\'t reach the Guts&Bolts server.' };
  } finally {
    clearTimeout(timer);
  }
}
