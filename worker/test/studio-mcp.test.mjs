// Studio's AI tools online for connected apps (like Lyreto): the permissions on "Log in with Guts&Bolts"
// (scopes), the /mcp endpoint (worker/mcp.js), the relay to the person's Studio (worker/mcp-relay.js),
// and the rule that publishing is never reachable. A pretend Studio on /ws answers like StudioMcp.cpp.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { makeServer, signUp, device } from './helpers.mjs';
import { handleMcp, ERR } from '../mcp.js';
import { canCall, filterTools, scopeForTool, READ_TOOLS, WRITE_TOOLS, isDenied } from '../mcp-policy.js';

const BACK = 'https://lyreto.com/api/connections/gutsandbolts/callback';
const TOOLS = ['get_game_tree', 'screenshot', 'insert_object', 'delete_object', 'publish_game', 'compare_scenes', 'secret_new_tool']
  .map((name) => ({ name, inputSchema: { type: 'object' } }));
let nextDevice = 40;

// A server, someone signed up from a real device (so Studio can sign mcp.serve), and an app token with `scope`.
function world(scope) {
  const s = makeServer();
  const secret = s.call(s.staff, 'admin.connectSecret', { id: 'lyreto' }).secret;
  const d = device(s, nextDevice++);
  const kai = signUp(s, 'Kai' + nextDevice, d.account);
  const app = s.call(kai, 'connect.app', { client_id: 'lyreto', redirect_uri: BACK, scope });
  const ok = s.call(kai, 'connect.approve', { client_id: 'lyreto', redirect_uri: BACK, scope });
  const t = ok.ok ? s.handle({ op: 'connect.token', args: { grant_type: 'authorization_code', code: ok.code, redirect_uri: BACK, client_id: 'lyreto', client_secret: secret } }) : {};
  const token = t.access_token;
  const env = { GB_SERVER_OBJECT: { idFromName: () => 'main', get: () => ({ fetch: (req) => s.fetch(req) }) } };
  const ask = async (_env, req) => s.handle(req);
  const mcp = (path, init = {}) => handleMcp(new Request('https://gutsandbolts.net' + path, init), env, new URL('https://gutsandbolts.net' + path), ask);
  const post = (body, tok = token, headers = {}) => mcp('/mcp', { method: 'POST', body: JSON.stringify(body),
    headers: Object.assign({ 'content-type': 'application/json', authorization: 'Bearer ' + tok }, headers) });
  return { s, secret, d, kai, app, ok, t, token, env, mcp, post };
}
const info = (s, token) => s.handle({ op: 'connect.tokenInfo', args: { token } });
const call = (id, name, args = {}) => ({ jsonrpc: '2.0', id, method: 'tools/call', params: { name, arguments: args } });
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

// A pretend Studio on /ws: says a real signed mcp.serve, then answers calls like StudioMcp::handle.
function openStudio(w, { silent = false } = {}) {
  const { s, d } = w, seen = [];
  const c = { id: 'studio' + Math.random(), mode: 'request', account: '', session: '', ticket: '', peer: '', since: 0, lastActive: 0, ip: '',
    ws: { send(text) {
      const m = JSON.parse(text); seen.push(m);
      if (m.t !== 'mcp.call' || silent) return;
      const { message } = m;
      let reply = { jsonrpc: '2.0', id: message.id, result: {} };
      if (message.method === 'tools/list') reply.result = { tools: TOOLS };
      else if (message.method === 'tools/call') reply.result = { content: [{ type: 'text', text: 'ran ' + message.params.name }], isError: false };
      else if (message.method === 'initialize') reply.result = { protocolVersion: '2025-06-18', serverInfo: { name: 'guts-and-bolts-studio' }, instructions: 'guide' };
      setTimeout(() => s.onMessage(c, JSON.stringify({ t: 'mcp.reply', id: m.id, reply })), 2);
    }, close() {} } };
  s.conns.set(c.id, c);
  const real = s.handle;
  let hello = null;
  s.handle = (req) => { hello = req; return {}; };   // (just to get the signed request the device makes)
  d.call('mcp.serve', { studio: '0.7.9', game: 'Obby Rush' });
  s.handle = real;
  s.onMessage(c, JSON.stringify(Object.assign(hello, { id: 1 })));
  return { c, seen };
}

test('permissions: none asked = identify only; Studio ones are shown, approved and put on the token', () => {
  const plain = world('');
  assert.deepEqual(plain.app.scopes.map((x) => x.id), ['identify']);
  assert.equal(plain.t.scope, 'identify');
  const w = world('studio.write studio.read');
  assert.deepEqual(w.app.scopes.map((x) => x.id), ['identify', 'studio.read', 'studio.write']);
  assert.equal(w.t.scope, 'identify studio.read studio.write');
  const i = info(w.s, w.token);
  assert.ok(i.ok, i.error);
  assert.deepEqual(i.scopes, ['identify', 'studio.read', 'studio.write']);
  assert.equal(i.user.id, String(w.kai.userId));
  assert.equal(i.account, undefined);   // never the account's key
  assert.equal(JSON.stringify(i).includes(w.kai.id), false);
  assert.deepEqual(w.s.call(w.kai, 'connect.list').apps[0].scopes, ['identify', 'studio.read', 'studio.write']);
});

test('permissions that don\'t exist, or the app may not ask for, are refused', () => {
  const w = world('studio.read publish');
  assert.equal(w.app.ok, false);
  assert.equal(w.ok.ok, false);
  assert.match(w.app.error, /no permission called "publish"/);
});

test('revoke: the app hands the token back and it stops working; Settings > Disconnect does too', () => {
  const w = world('studio.read');
  const revoke = (secret, token = w.token) => w.s.handle({ op: 'connect.revoke', args: { token, client_id: 'lyreto', client_secret: secret } });
  assert.equal(revoke('gbs_wrong').oauth, 'invalid_client');
  assert.ok(revoke(w.secret).ok);
  assert.equal(info(w.s, w.token).oauth, 'invalid_token');
  assert.ok(revoke(w.secret, 'gbt_nope').ok);   // the same answer either way
  const w2 = world('studio.read');
  w2.s.call(w2.kai, 'connect.remove', { id: 'lyreto' });
  assert.equal(info(w2.s, w2.token).oauth, 'invalid_token');
});

test('the tool list: read tools need studio.read, changes studio.write, publishing never', () => {
  assert.equal(scopeForTool('get_game_tree'), 'studio.read');
  assert.equal(scopeForTool('insert_object'), 'studio.write');
  assert.ok(canCall('screenshot', ['identify', 'studio.read']).ok);
  assert.equal(canCall('set_property', ['identify', 'studio.read']).ok, false);
  assert.ok(canCall('read_script', ['studio.write']).ok);   // write includes read
  const all = ['identify', 'studio.read', 'studio.write'];
  for (const name of ['publish_game', 'compare_scenes', 'upload_model', 'PublishGame', 'set_price', 'brand_new_tool', null])
    assert.equal(canCall(name, all).ok, false, String(name));
  for (const t of [...READ_TOOLS, ...WRITE_TOOLS]) assert.equal(isDenied(t), false, t);
  assert.deepEqual(filterTools(TOOLS, ['studio.read']).map((t) => t.name), ['get_game_tree', 'screenshot']);
});

test('Studio\'s own lists match: every tool exists, read tools are Studio\'s read-only ones, and StudioMcp.cpp has the same lists', () => {
  const tools = readFileSync(new URL('../../src/editor/AiTools.cpp', import.meta.url), 'utf8');
  const flags = new Map([...tools.matchAll(/\{"([a-z_]+)", "[^"]*", "[A-Z_ ]+", (true|false), (true|false)/g)].map((m) => [m[1], m[2] === 'true']));
  for (const t of READ_TOOLS) assert.equal(flags.get(t), true, t + ' should be a read-only Studio tool');
  for (const t of WRITE_TOOLS) assert.equal(flags.get(t), false, t + ' should be a Studio tool that changes things');
  const studio = readFileSync(new URL('../../src/editor/StudioMcp.cpp', import.meta.url), 'utf8');
  const list = (name) => [...(new RegExp(name + '\\[\\] = \\{([^}]*)\\}').exec(studio) || ['', ''])[1].matchAll(/"([a-z_]+)"/g)].map((m) => m[1]);
  assert.deepEqual(list('kAppReadTools'), READ_TOOLS);
  assert.deepEqual(list('kAppWriteTools'), WRITE_TOOLS);
});

test('Studio offline: a clear error, but initialize still works', async () => {
  const { post } = world('studio.read');
  const init = await (await post({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { protocolVersion: '2025-03-26' } })).json();
  assert.equal(init.result.protocolVersion, '2025-03-26');
  assert.match(init.result.instructions, /offline/);
  const r = await (await post(call(2, 'get_game_tree'))).json();
  assert.equal(r.error.code, ERR.offline);
  assert.match(r.error.message, /Allow connected apps/);
});

test('Studio online: calls go through; the list is cut to the permission and never shows publish_game', async () => {
  const w = world('studio.read');
  const { seen } = openStudio(w);
  assert.equal(seen[0].t, 'mcp.ready', JSON.stringify(seen[0]));
  const list = await (await w.post({ jsonrpc: '2.0', id: 1, method: 'tools/list' })).json();
  assert.deepEqual(list.result.tools.map((t) => t.name), ['get_game_tree', 'screenshot']);
  const ok = await (await w.post(call(2, 'get_game_tree'))).json();
  assert.equal(ok.result.content[0].text, 'ran get_game_tree');
  const sent = seen.find((m) => m.t === 'mcp.call' && m.message.method === 'tools/call');
  assert.equal(sent.app, 'Lyreto');
  assert.deepEqual(sent.scopes, ['identify', 'studio.read']);   // Studio checks them again
  const no = await (await w.post(call(3, 'insert_object'))).json();
  assert.equal(no.error.code, ERR.denied);
  assert.match(no.error.message, /studio\.write/);
  const st = await (await w.mcp('/mcp/status', { headers: { authorization: 'Bearer ' + w.token } })).json();
  assert.deepEqual(st, { online: true, game: 'Obby Rush', studio: '0.7.9' });
});

test('publish_game is refused at the endpoint and at the relay, even with every permission', async () => {
  const w = world('studio.read studio.write');
  const { seen } = openStudio(w);
  const r = await (await w.post(call(9, 'publish_game', { confirm: true }))).json();
  assert.equal(r.error.code, ERR.denied);
  const direct = await (await w.s.fetch(new Request('https://gb.internal/mcp-call', { method: 'POST',
    body: JSON.stringify({ token: w.token, message: call(1, 'publish_game') }) }))).json();
  assert.equal(direct.denied, true);
  assert.equal(seen.some((m) => m.t === 'mcp.call' && m.message.params && m.message.params.name === 'publish_game'), false);
  const ok = await (await w.post(call(10, 'insert_object'))).json();
  assert.equal(ok.result.content[0].text, 'ran insert_object');
});

test('tokens: none, wrong, identify-only, web pages and notifications', async () => {
  const w = world('');
  assert.equal((await w.post({ jsonrpc: '2.0', id: 1, method: 'ping' }, 'gbt_wrong')).status, 401);
  assert.equal((await w.post({ jsonrpc: '2.0', id: 1, method: 'ping' })).status, 403);   // identify only
  const v = world('studio.read');
  assert.equal((await v.post({ jsonrpc: '2.0', id: 1, method: 'ping' }, v.token, { origin: 'https://gutsandbolts.net' })).status, 403);
  assert.deepEqual((await (await v.post({ jsonrpc: '2.0', id: 1, method: 'ping' })).json()).result, {});
  assert.equal((await v.post({ jsonrpc: '2.0', method: 'notifications/initialized' })).status, 202);
  assert.equal((await v.mcp('/mcp', { headers: { authorization: 'Bearer ' + v.token } })).status, 405);
  const meta = await (await v.mcp('/.well-known/oauth-protected-resource')).json();
  assert.equal(meta.resource, 'https://gutsandbolts.net/mcp');
});

test('a guest or someone else\'s signature can\'t serve; a banned account\'s token stops working', async () => {
  const w = world('studio.read');
  const c = { id: 'x', mode: 'request', ws: { send(t) { c.got = JSON.parse(t); }, close() {} } };
  w.s.conns.set(c.id, c);
  w.s.onMessage(c, JSON.stringify({ op: 'mcp.serve', account: w.kai.id, nonce: 'abcdef12', time: Math.floor(Date.now() / 1000), sig: '00', args: {}, id: 1 }));
  assert.equal(c.got.ok, false);
  assert.equal(c.mode, 'request');
  w.kai.banned = true;
  assert.equal(info(w.s, w.token).oauth, 'invalid_token');
});

test('Studio closes mid-call -> offline; silent -> timeout; a second Studio replaces the first', async () => {
  const w = world('studio.read');
  const a = openStudio(w, { silent: true });
  const ask = (timeoutMs) => w.s.fetch(new Request('https://gb.internal/mcp-call', { method: 'POST',
    body: JSON.stringify({ token: w.token, timeoutMs, message: call(1, 'get_errors') }) })).then((r) => r.json());
  const pending = ask(30000);
  await wait(10);
  w.s.drop(a.c.id);
  assert.equal((await pending).offline, true);
  const b = openStudio(w, { silent: true });
  assert.equal((await ask(1000)).timeout, true);
  const c2 = openStudio(w);
  assert.ok(b.seen.some((m) => m.t === 'mcp.replaced'));
  assert.equal(w.s.conns.has(b.c.id), false);
  assert.ok(c2.seen[0].ok);
});
