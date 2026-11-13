// MCP 2026-07-28 on https://gutsandbolts.net/mcp: no handshake, server/discover, the headers that must
// match the body, resultType on answers, and old (2025) apps still working the old way.
// The pretend Studio here is the same as in studio-mcp.test.mjs.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp, device } from './helpers.mjs';
import { handleMcp } from '../mcp.js';
import { checkRequest, decodeHeaderValue, modernReply, MODERN, SUPPORTED, CODES } from '../mcp-protocol.js';

const BACK = 'https://lyreto.com/api/connections/gutsandbolts/callback';
const TOOLS = ['get_game_tree', 'screenshot', 'insert_object', 'delete_object', 'publish_game', 'compare_scenes', 'secret_new_tool']
  .map((name) => ({ name, inputSchema: { type: 'object' } }));
let nextDevice = 140;

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


const CAPS = { 'io.modelcontextprotocol/protocolVersion': MODERN, 'io.modelcontextprotocol/clientCapabilities': {},
  'io.modelcontextprotocol/clientInfo': { name: 'test-app', version: '1' } };
// A 2026-07-28 request: the body with _meta, and the matching headers.
function modern(id, method, params = {}, nameHeader) {
  const body = { jsonrpc: '2.0', id, method, params: Object.assign({}, params, { _meta: Object.assign({}, CAPS) }) };
  if (id === undefined) delete body.id;
  const headers = { 'mcp-protocol-version': MODERN, 'mcp-method': method };
  const name = nameHeader !== undefined ? nameHeader : params.name || params.uri;
  if (name) headers['mcp-name'] = name;
  return { body, headers };
}
const send = (w, { body, headers }) => w.post(body, w.token, headers);
const hdr = (h) => (name) => (name in h ? h[name] : null);
const b64 = (text) => '=?base64?' + Buffer.from(text, 'utf8').toString('base64') + '?=';

test('server/discover works with no handshake, online or offline, and lists every version we speak', async () => {
  const w = world('studio.read');
  const off = await send(w, modern(1, 'server/discover'));
  assert.equal(off.status, 200);
  const a = await off.json();
  assert.equal(a.result.resultType, 'complete');
  assert.deepEqual(a.result.supportedVersions, SUPPORTED);
  assert.equal(a.result.supportedVersions[0], '2026-07-28');
  assert.ok(a.result.capabilities.tools);
  assert.match(a.result.instructions, /offline/);
  assert.equal(a.result._meta['io.modelcontextprotocol/serverInfo'].name, 'guts-and-bolts-studio');
  assert.equal(off.headers.get('mcp-session-id'), null);   // no sessions any more
  openStudio(w);
  const on = await (await send(w, modern(2, 'server/discover'))).json();
  assert.equal(on.result.instructions, 'guide');   // Studio's own guide
});

test('tools/list and tools/call work the new way, with resultType and the same permissions', async () => {
  const w = world('studio.read');
  const { seen } = openStudio(w);
  const list = await (await send(w, modern(1, 'tools/list'))).json();
  assert.equal(list.result.resultType, 'complete');
  assert.deepEqual(list.result.tools.map((t) => t.name), ['get_game_tree', 'screenshot']);
  const ok = await (await send(w, modern(2, 'tools/call', { name: 'get_game_tree', arguments: {} }))).json();
  assert.equal(ok.result.content[0].text, 'ran get_game_tree');
  assert.equal(ok.result.resultType, 'complete');
  const sent = seen.find((m) => m.t === 'mcp.call' && m.message.method === 'tools/call');
  assert.equal('_meta' in sent.message.params, false);   // Studio gets a plain message
  const no = await (await send(w, modern(3, 'tools/call', { name: 'insert_object', arguments: {} }))).json();
  assert.equal(no.error.code, -32003);
  const pub = await (await send(w, modern(4, 'tools/call', { name: 'publish_game', arguments: {} }))).json();
  assert.equal(pub.error.code, -32003);
  assert.equal(seen.some((m) => m.t === 'mcp.call' && m.message.params && m.message.params.name === 'publish_game'), false);
});

test('headers that don\'t match the body are refused with 400 and -32020', async () => {
  const w = world('studio.read');
  openStudio(w);
  const cases = [
    (r) => { delete r.headers['mcp-protocol-version']; r.body.params._meta['io.modelcontextprotocol/protocolVersion'] = MODERN; },
    (r) => { r.headers['mcp-method'] = 'tools/list'; },
    (r) => { delete r.headers['mcp-method']; },
    (r) => { r.headers['mcp-name'] = 'screenshot'; },
    (r) => { delete r.headers['mcp-name']; },
    (r) => { r.headers['mcp-protocol-version'] = '2025-06-18'; },
  ];
  for (const change of cases) {
    const r = modern(7, 'tools/call', { name: 'get_game_tree', arguments: {} });
    change(r);
    const res = await send(w, r);
    assert.equal(res.status, 400, change.toString());
    const a = await res.json();
    assert.equal(a.error.code, CODES.headerMismatch, change.toString());
    assert.equal(a.id, 7);
  }
});

test('missing _meta fields are -32602; an unknown version is -32022 with the list; batches are refused', async () => {
  const w = world('studio.read');
  const noCaps = modern(1, 'tools/list');
  delete noCaps.body.params._meta['io.modelcontextprotocol/clientCapabilities'];
  const a = await send(w, noCaps);
  assert.equal(a.status, 400);
  assert.equal((await a.json()).error.code, -32602);
  const noMeta = modern(2, 'tools/list');
  delete noMeta.body.params._meta;
  assert.equal((await (await send(w, noMeta)).json()).error.code, -32602);
  const old = await w.post({ jsonrpc: '2.0', id: 3, method: 'tools/list' }, w.token, { 'mcp-protocol-version': '1900-01-01' });
  assert.equal(old.status, 400);
  const e = (await old.json()).error;
  assert.equal(e.code, CODES.unsupportedVersion);
  assert.deepEqual(e.data, { supported: SUPPORTED, requested: '1900-01-01' });
  const batch = await w.post([modern(4, 'tools/list').body], w.token, { 'mcp-protocol-version': MODERN, 'mcp-method': 'tools/list' });
  assert.equal(batch.status, 400);
});

test('an unknown method is HTTP 404; a notification is 202', async () => {
  const w = world('studio.read');
  const r = await send(w, modern(1, 'sampling/createMessage'));
  assert.equal(r.status, 404);
  assert.equal((await r.json()).error.code, -32601);
  assert.equal((await send(w, modern(undefined, 'notifications/cancelled', { requestId: 1 }))).status, 202);
});

test('old apps still work: initialize, then plain requests with no new headers', async () => {
  const w = world('studio.read');
  openStudio(w);
  const init = await (await w.post({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { protocolVersion: '2025-06-18' } })).json();
  assert.equal(init.result.protocolVersion, '2025-06-18');
  assert.equal(init.result.resultType, undefined);   // the old shape, untouched
  const list = await (await w.post({ jsonrpc: '2.0', id: 2, method: 'tools/list' }, w.token, { 'mcp-protocol-version': '2025-06-18', 'mcp-session-id': 'whatever' })).json();
  assert.deepEqual(list.result.tools.map((t) => t.name), ['get_game_tree', 'screenshot']);
  assert.equal(list.result.resultType, undefined);
});

test('checkRequest by itself: base64 names, initialize means old rules, -32002 becomes -32602', () => {
  const r = modern(1, 'resources/read', { uri: 'gutsbolts://scene/tree' });
  assert.deepEqual(checkRequest(hdr(r.headers), r.body), { ok: true, modern: true });
  const odd = modern(2, 'tools/call', { name: 'héllo,tool' }, b64('héllo,tool'));
  assert.deepEqual(checkRequest(hdr(odd.headers), odd.body), { ok: true, modern: true });
  odd.headers['mcp-name'] = b64('other');
  assert.equal(checkRequest(hdr(odd.headers), odd.body).error.code, CODES.headerMismatch);
  assert.equal(decodeHeaderValue('=?base64?!!!?='), '=?base64?!!!?=');   // not the pattern: taken as is
  assert.equal(decodeHeaderValue('=?base64?/w==?='), null);             // not UTF-8
  assert.deepEqual(checkRequest(hdr({ 'mcp-protocol-version': MODERN }), { jsonrpc: '2.0', id: 1, method: 'initialize', params: {} }), { ok: true, modern: false });
  assert.deepEqual(checkRequest(hdr({}), { jsonrpc: '2.0', id: 1, method: 'tools/list' }), { ok: true, modern: false });
  const info = { name: 'x', version: '1' };
  assert.equal(modernReply({ jsonrpc: '2.0', id: 1, error: { code: -32002, message: 'gone' } }, info).error.code, -32602);
  assert.equal(modernReply({ jsonrpc: '2.0', id: 1, result: { a: 1 } }, info).result._meta['io.modelcontextprotocol/serverInfo'], info);
});

test('Studio\'s C++ server uses the same version list and error numbers', async () => {
  const { readFileSync } = await import('node:fs');
  const cpp = readFileSync(new URL('../../src/editor/McpServerProtocol.h', import.meta.url), 'utf8');
  const versions = [...(/kSupportedVersions\[\] = \{([^}]*)\}/.exec(cpp) || ['', ''])[1].matchAll(/"([0-9-]+)"/g)].map((m) => m[1]);
  assert.deepEqual(versions, SUPPORTED);
  for (const [name, code] of Object.entries({ kHeaderMismatch: CODES.headerMismatch, kUnsupportedVersion: CODES.unsupportedVersion }))
    assert.match(cpp, new RegExp(name + ' = ' + code + ';'));
});
