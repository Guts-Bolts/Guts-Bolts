// The MCP "2026-07-28" rules for our own MCP servers (https://gutsandbolts.net/mcp here, and Studio's
// built-in one in src/editor/McpServerProtocol.h, which follows the same rules).
//
// What changed in 2026-07-28, in short:
//   * No more handshake. There's no initialize / notifications/initialized and no Mcp-Session-Id.
//     Every request says its own protocol version and client details in params._meta.
//   * Every POST carries headers that repeat what's in the body, so gateways can route without
//     reading it: MCP-Protocol-Version, Mcp-Method, and Mcp-Name (for tools/call, resources/read,
//     prompts/get). If they don't match the body we refuse with 400 and -32020 (HeaderMismatch).
//   * server/discover tells a client what we support (it's optional for clients, required for servers).
//   * Every result says resultType: "complete" (or "input_required", which we never use).
//   * A missing resource is -32602 now (-32002 is gone), and an unknown method is HTTP 404.
//
// Old clients (2025-06-18 and before) still work exactly as before: an `initialize` request, or a
// request without the new version, is answered the old way.

export const MODERN = '2026-07-28';
export const LEGACY = ['2025-06-18', '2025-03-26', '2024-11-05'];
export const SUPPORTED = [MODERN, ...LEGACY];
export const CODES = { headerMismatch: -32020, missingCapability: -32021, unsupportedVersion: -32022, invalidParams: -32602, invalidRequest: -32600 };
export const META = {
  version: 'io.modelcontextprotocol/protocolVersion',
  capabilities: 'io.modelcontextprotocol/clientCapabilities',
  clientInfo: 'io.modelcontextprotocol/clientInfo',
  serverInfo: 'io.modelcontextprotocol/serverInfo',
};
// The methods whose name (or resource address) has to be in the Mcp-Name header too.
const NAMED = { 'tools/call': 'name', 'resources/read': 'uri', 'prompts/get': 'name' };

const isObject = (v) => !!v && typeof v === 'object' && !Array.isArray(v);
const fail = (status, code, message, data) => ({ ok: false, status, error: Object.assign({ code, message }, data ? { data } : {}) });

// A header value that can't be sent as plain text comes as =?base64?<base64 of the UTF-8 text>?=.
// Returns the text, or null if it's broken.
export function decodeHeaderValue(value) {
  const m = /^=\?base64\?([A-Za-z0-9+/]*={0,2})\?=$/.exec(value);
  if (!m) return value;
  try {
    const bin = atob(m[1]);
    return new TextDecoder('utf-8', { fatal: true }).decode(Uint8Array.from(bin, (c) => c.charCodeAt(0)));
  } catch { return null; }
}

// Which rules does this request follow, and does it follow them properly?
//   header(name)  reads a request header (any capitalisation), null if it isn't there
//   body          the parsed JSON body (one message, or an array for an old-style batch)
// Answers { ok: true, modern: false } (old rules), { ok: true, modern: true }, or
// { ok: false, status, error: { code, message, data? } } to send back as is.
export function checkRequest(header, body) {
  const headerVersion = header('mcp-protocol-version');
  const msg = isObject(body) ? body : null;
  const params = msg && isObject(msg.params) ? msg.params : null;
  const meta = params && isObject(params._meta) ? params._meta : null;
  const metaVersion = meta ? meta[META.version] : undefined;

  if (msg && msg.method === 'initialize') return { ok: true, modern: false };   // `initialize` = old rules
  const modern = headerVersion === MODERN || metaVersion === MODERN;
  if (!modern) {
    const asked = headerVersion || (typeof metaVersion === 'string' ? metaVersion : '');
    if (asked && !LEGACY.includes(asked))
      return fail(400, CODES.unsupportedVersion, 'Unsupported protocol version', { supported: SUPPORTED, requested: String(asked).slice(0, 64) });
    return { ok: true, modern: false };
  }

  if (!msg) return fail(400, CODES.invalidRequest, 'Send one JSON-RPC message per request (batches aren\'t part of ' + MODERN + ').');
  if (typeof msg.method !== 'string') return fail(400, CODES.invalidRequest, 'Not a JSON-RPC 2.0 request.');
  if (!headerVersion) return fail(400, CODES.headerMismatch, 'Header mismatch: the MCP-Protocol-Version header is missing.');
  if (metaVersion === undefined) return fail(400, CODES.invalidParams, 'params._meta["' + META.version + '"] is required.');
  if (headerVersion !== metaVersion)
    return fail(400, CODES.headerMismatch, 'Header mismatch: MCP-Protocol-Version header value \'' + String(headerVersion).slice(0, 64)
      + '\' does not match body value \'' + String(metaVersion).slice(0, 64) + '\'');

  const methodHeader = header('mcp-method');
  if (methodHeader === null || methodHeader === undefined) return fail(400, CODES.headerMismatch, 'Header mismatch: the Mcp-Method header is missing.');
  if (methodHeader !== msg.method)
    return fail(400, CODES.headerMismatch, 'Header mismatch: Mcp-Method header value \'' + String(methodHeader).slice(0, 60)
      + '\' does not match body value \'' + msg.method.slice(0, 60) + '\'');

  if (!('id' in msg)) return { ok: true, modern: true };   // notifications: nothing more to check
  if (!isObject(meta[META.capabilities])) return fail(400, CODES.invalidParams, 'params._meta["' + META.capabilities + '"] is required.');

  const field = NAMED[msg.method];
  if (field) {
    const raw = header('mcp-name');
    if (raw === null || raw === undefined) return fail(400, CODES.headerMismatch, 'Header mismatch: the Mcp-Name header is missing.');
    const name = decodeHeaderValue(raw);
    const want = params ? params[field] : undefined;
    if (name === null || typeof want !== 'string' || name !== want)
      return fail(400, CODES.headerMismatch, 'Header mismatch: Mcp-Name header value \'' + String(raw).slice(0, 80)
        + '\' does not match body value \'' + String(want === undefined ? '' : want).slice(0, 80) + '\'');
  }
  return { ok: true, modern: true };
}

// The _meta our answers carry, so clients know who answered.
export const serverMeta = (serverInfo) => ({ [META.serverInfo]: serverInfo });

// Make a reply follow the new rules: results say resultType, -32002 becomes -32602.
export function modernReply(reply, serverInfo) {
  if (!isObject(reply)) return reply;
  if (isObject(reply.result)) {
    const result = Object.assign({ resultType: 'complete' }, reply.result);
    result._meta = Object.assign({}, isObject(reply.result._meta) ? reply.result._meta : {}, serverMeta(serverInfo));
    return Object.assign({}, reply, { result });
  }
  if (isObject(reply.error) && reply.error.code === -32002)
    return Object.assign({}, reply, { error: Object.assign({}, reply.error, { code: CODES.invalidParams }) });
  return reply;
}

// The answer to server/discover.
export function discoverResult({ capabilities, instructions, serverInfo, ttlMs = 3600000 }) {
  const result = { resultType: 'complete', supportedVersions: SUPPORTED.slice(), capabilities: capabilities || {}, ttlMs, cacheScope: 'public' };
  if (instructions) result.instructions = instructions;
  result._meta = serverMeta(serverInfo);
  return result;
}

// The HTTP status for a single answer under the new rules (an unknown method is 404).
export const modernStatus = (reply) => (isObject(reply) && isObject(reply.error) && reply.error.code === -32601 ? 404 : 200);
