import { DurableObject } from 'cloudflare:workers';

const enc = new TextEncoder();
const now = () => Math.floor(Date.now() / 1000);
const hex = (bytes) => [...bytes].map((b) => b.toString(16).padStart(2, '0')).join('');
const randomHex = (n) => hex(crypto.getRandomValues(new Uint8Array(n)));
const isHex = (value, length = 64) => typeof value === 'string' && value.length === length && /^[a-f0-9]+$/i.test(value);
const hash = async (value) => hex(new Uint8Array(await crypto.subtle.digest('SHA-256', enc.encode(value))));
const fail = (error, status = 400) => ({ status, body: { ok: false, error } });
const success = (body, status = 200) => ({ status, body: { ok: true, ...body } });

function send(ws, value) {
  try { ws.send(JSON.stringify(value)); } catch { /* connection is already closed */ }
}
function safeText(value, max, fallback = '') {
  return typeof value === 'string' ? value.replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, max) : fallback;
}
function safeStatus(raw) {
  if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return null;
  const number = (key, min, max) => Number.isFinite(raw[key]) && raw[key] >= min && raw[key] <= max
    ? Math.round(raw[key] * 10) / 10 : null;
  const total = number('memoryTotalMB', 1, 1048576);
  const used = number('memoryUsedMB', 0, 1048576);
  return {
    cpuPercent: number('cpuPercent', 0, 100),
    cpuCores: Number.isInteger(raw.cpuCores) && raw.cpuCores >= 1 && raw.cpuCores <= 512 ? raw.cpuCores : null,
    memoryPercent: number('memoryPercent', 0, 100),
    memoryUsedMB: used,
    memoryTotalMB: total,
    gpuPercent: number('gpuPercent', 0, 100),
    gpuTemperatureC: number('gpuTemperatureC', 0, 120),
    gpuMemoryUsedMB: number('gpuMemoryUsedMB', 0, 1048576),
    gpuMemoryTotalMB: number('gpuMemoryTotalMB', 0, 1048576),
    diskUsedGB: number('diskUsedGB', 0, 1048576),
    diskTotalGB: number('diskTotalGB', 0, 1048576),
    networkRxMbps: number('networkRxMbps', 0, 1000000),
    networkTxMbps: number('networkTxMbps', 0, 1000000),
    platform: safeText(raw.platform, 40, 'Unknown'),
    osVersion: safeText(raw.osVersion, 100),
    hostname: safeText(raw.hostname, 80, 'Unnamed machine'),
    agentVersion: safeText(raw.agentVersion, 24, 'unknown'),
    models: Array.isArray(raw.models)
      ? [...new Set(raw.models.filter((x) => typeof x === 'string').map((x) => safeText(x, 128)).filter((x) => /^[a-zA-Z0-9][a-zA-Z0-9._:/-]{0,127}$/.test(x)))].slice(0, 32)
      : [],
    capabilities: Array.isArray(raw.capabilities)
      ? [...new Set(raw.capabilities.filter((x) => typeof x === 'string').map((x) => safeText(x, 32)).filter(Boolean))].slice(0, 16)
      : [],
    services: Array.isArray(raw.services)
      ? raw.services.filter((x) => x && typeof x === 'object' && !Array.isArray(x)).slice(0, 32).map((x) => ({
          name: safeText(x.name, 48),
          state: ['running', 'stopped', 'busy', 'error', 'unknown'].includes(x.state) ? x.state : 'unknown',
          detail: safeText(x.detail, 120),
        }))
      : [],
    at: now(),
  };
}

export class InfrastructureObject extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.ctx = ctx;
    this.env = env;
    this.sql = ctx.storage.sql;
    this.connections = new Map();
    this.nodes = new Map();
    this.busyNodes = new Set();
    this.pendingInference = new Map();
    this.requestWindows = new Map();
    const initialize = () => this.sql.exec(`
      CREATE TABLE IF NOT EXISTS infra_devices (
        id TEXT PRIMARY KEY, owner TEXT NOT NULL, name TEXT NOT NULL,
        platform TEXT NOT NULL, token_hash TEXT NOT NULL,
        created INTEGER NOT NULL, revoked INTEGER NOT NULL DEFAULT 0,
        last_seen INTEGER NOT NULL DEFAULT 0, status TEXT NOT NULL DEFAULT '{}'
      );
      CREATE TABLE IF NOT EXISTS infra_pairings (
        code_hash TEXT PRIMARY KEY, expires INTEGER NOT NULL, conn_id TEXT NOT NULL,
        name TEXT NOT NULL, platform TEXT NOT NULL
      );
      CREATE TABLE IF NOT EXISTS infra_pair_attempts (
        id INTEGER PRIMARY KEY AUTOINCREMENT, ip_hash TEXT NOT NULL, at INTEGER NOT NULL
      );
      CREATE INDEX IF NOT EXISTS infra_pair_attempts_ip_at ON infra_pair_attempts(ip_hash, at);
      CREATE TABLE IF NOT EXISTS infra_audit (
        id TEXT PRIMARY KEY, at INTEGER NOT NULL, actor TEXT NOT NULL,
        action TEXT NOT NULL, detail TEXT NOT NULL
      );
      CREATE INDEX IF NOT EXISTS infra_audit_at ON infra_audit(at);
    `);
    if (typeof ctx.blockConcurrencyWhile === 'function') ctx.blockConcurrencyWhile(initialize);
    else initialize();
  }

  audit(actor, action, detail) {
    this.sql.exec('INSERT INTO infra_audit (id, at, actor, action, detail) VALUES (?, ?, ?, ?, ?)',
      randomHex(16), now(), String(actor).slice(0, 80), String(action).slice(0, 40), String(detail).slice(0, 200));
    this.sql.exec('DELETE FROM infra_audit WHERE at < ?', now() - 90 * 86400);
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname !== '/agent' || request.headers.get('Upgrade')?.toLowerCase() !== 'websocket')
      return new Response('Not found.', { status: 404 });
    const origin = request.headers.get('Origin');
    if (origin && origin !== 'https://gutsandbolts.net') return new Response('Origin not allowed.', { status: 403 });
    const pair = new WebSocketPair();
    const ws = pair[1];
    ws.accept();
    const c = {
      id: randomHex(12), ws, ip: request.headers.get('x-gb-ip') || 'unknown',
      authed: false, pending: false, nodeId: '',
    };
    this.connections.set(c.id, c);
    let queue = Promise.resolve();
    ws.addEventListener('message', (event) => {
      queue = queue.then(async () => {
        if (!this.connections.has(c.id)) return;
        let text = event.data;
        if (typeof text !== 'string') {
          if (text && typeof text.text === 'function') text = await text.text();
          else return this.drop(c);
        }
        if (text.length > 450 * 1024) return this.drop(c);
        let message;
        try { message = JSON.parse(text); } catch { return this.drop(c); }
        if (!message || typeof message !== 'object' || Array.isArray(message)) return this.drop(c);
        await this.onNodeMessage(c, message);
      }).catch(() => this.drop(c));
    });
    ws.addEventListener('close', () => this.drop(c));
    ws.addEventListener('error', () => this.drop(c));
    return new Response(null, { status: 101, webSocket: pair[0] });
  }

  async onNodeMessage(c, message) {
    if (message.t === 'node.pair.begin') {
      if (c.authed || c.pending || !isHex(message.code) ||
          typeof message.name !== 'string' || typeof message.platform !== 'string') return this.drop(c);
      const codeHash = await hash(message.code.toLowerCase());
      const existing = [...this.sql.exec('SELECT expires FROM infra_pairings WHERE code_hash = ?', codeHash)][0];
      if (!existing) {
        this.sql.exec('DELETE FROM infra_pairings WHERE expires <= ?', now());
        this.sql.exec('DELETE FROM infra_pair_attempts WHERE at < ?', now() - 86400);
        const ipHash = await hash(c.ip || 'unknown');
        const attempts = [...this.sql.exec('SELECT COUNT(*) AS n FROM infra_pair_attempts WHERE ip_hash = ? AND at > ?',
          ipHash, now() - 3600)][0]?.n || 0;
        if (attempts >= 20) {
          send(c.ws, { t: 'pair.rate-limited' });
          return this.drop(c);
        }
        this.sql.exec('INSERT INTO infra_pair_attempts (ip_hash, at) VALUES (?, ?)', ipHash, now());
        this.sql.exec('INSERT INTO infra_pairings (code_hash, expires, conn_id, name, platform) VALUES (?, ?, ?, ?, ?)',
          codeHash, now() + 300, c.id, safeText(message.name, 80, 'Unnamed machine'), safeText(message.platform, 40, 'Unknown'));
      } else {
        this.sql.exec('UPDATE infra_pairings SET conn_id = ? WHERE code_hash = ?', c.id, codeHash);
      }
      c.pending = true;
      c.deviceName = safeText(message.name, 80, 'Unnamed machine');
      send(c.ws, { t: 'pair.waiting', expiresIn: 300 });
      return;
    }
    if (message.t === 'node.auth') {
      if (c.authed || c.pending || !isHex(message.id, 32) || !isHex(message.token)) return this.drop(c);
      const row = [...this.sql.exec('SELECT owner, token_hash, revoked FROM infra_devices WHERE id = ?', message.id.toLowerCase())][0];
      const tokenHash = await hash(message.token.toLowerCase());
      if (!row || row.revoked || !(await this.equalHash(tokenHash, row.token_hash))) {
        send(c.ws, { t: 'device.rejected' });
        return this.drop(c);
      }
      c.nodeId = message.id.toLowerCase();
      c.owner = row.owner;
      c.authed = true;
      const previous = this.nodes.get(c.nodeId);
      if (previous && previous !== c) this.drop(previous, false);
      this.nodes.set(c.nodeId, c);
      this.audit('agent', 'node.connected', 'Infrastructure node connected: ' + c.nodeId);
      send(c.ws, { t: 'node.ready', heartbeatSeconds: 10 });
      return;
    }
    if (!c.authed || this.nodes.get(c.nodeId) !== c) return this.drop(c);
    if (message.t === 'node.status') {
      const status = safeStatus(message.status);
      if (!status) return this.drop(c);
      this.sql.exec('UPDATE infra_devices SET last_seen = ?, status = ? WHERE id = ? AND revoked = 0',
        now(), JSON.stringify(status), c.nodeId);
      return;
    }
    if (message.t === 'node.infer.result' || message.t === 'node.infer.error') {
      const pending = typeof message.requestId === 'string' ? this.pendingInference.get(message.requestId) : null;
      if (!pending || pending.nodeId !== c.nodeId) return this.drop(c);
      this.pendingInference.delete(message.requestId);
      clearTimeout(pending.timer);
      this.busyNodes.delete(c.nodeId);
      if (message.t === 'node.infer.error') {
        pending.resolve(fail(safeText(message.error, 240, 'The local model could not answer.'), 502));
      } else {
        const answer = message.message;
        if (!answer || typeof answer !== 'object' || Array.isArray(answer) || answer.role !== 'assistant' ||
            typeof answer.content !== 'string' || answer.content.length > 200000 ||
            (answer.tool_calls !== undefined && (!Array.isArray(answer.tool_calls) || answer.tool_calls.length > 64))) {
          pending.resolve(fail('The local model returned an invalid response.', 502));
        } else pending.resolve(success({ message: answer, deviceId: c.nodeId }));
      }
      return;
    }
    if (message.t === 'node.error') {
      this.audit('agent', 'node.error', safeText(message.message, 180, 'Agent reported an error'));
      return;
    }
    return this.drop(c);
  }

  async equalHash(a, b) {
    if (typeof a !== 'string' || typeof b !== 'string') return false;
    const left = await crypto.subtle.digest('SHA-256', enc.encode(a));
    const right = await crypto.subtle.digest('SHA-256', enc.encode(b));
    const key = await crypto.subtle.importKey('raw', new Uint8Array(32), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign', 'verify']);
    const signature = await crypto.subtle.sign('HMAC', key, left);
    return crypto.subtle.verify('HMAC', key, signature, right);
  }

  drop(c, markOffline = true) {
    if (!c || !this.connections.has(c.id)) return;
    this.connections.delete(c.id);
    if (c.nodeId && this.nodes.get(c.nodeId) === c) {
      this.nodes.delete(c.nodeId);
      this.busyNodes.delete(c.nodeId);
      for (const [requestId, pending] of this.pendingInference) {
        if (pending.nodeId !== c.nodeId) continue;
        this.pendingInference.delete(requestId);
        clearTimeout(pending.timer);
        pending.resolve(fail('The inference machine disconnected before answering.', 503));
      }
      if (markOffline) this.audit('agent', 'node.disconnected', 'Infrastructure node disconnected: ' + c.nodeId);
    }
    try { c.ws.close(1000, 'bye'); } catch { /* already closed */ }
  }

  async infer(job) {
    if (!job || typeof job !== 'object' || Array.isArray(job) ||
        typeof job.account !== 'string' || !isHex(job.account) ||
        typeof job.model !== 'string' || !/^[a-zA-Z0-9][a-zA-Z0-9._:/-]{0,127}$/.test(job.model) ||
        !Array.isArray(job.messages) || !job.messages.length || job.messages.length > 80 ||
        (job.tools !== undefined && !Array.isArray(job.tools))) return fail('Invalid home inference request.', 400);
    let encoded;
    try { encoded = JSON.stringify(job); } catch { return fail('Invalid home inference request.', 400); }
    if (new TextEncoder().encode(encoded).length > 300 * 1024) return fail('The conversation is too large for home inference.', 413);
    const t = now();
    const window = this.requestWindows.get(job.account) || { start: t, count: 0 };
    if (t - window.start >= 60) { window.start = t; window.count = 0; }
    if (window.count >= 30) return fail('Home AI is rate-limited to 30 requests per minute per account.', 429);
    window.count++;
    this.requestWindows.set(job.account, window);
    const rows = [...this.sql.exec('SELECT id, status, last_seen, revoked FROM infra_devices WHERE revoked = 0')];
    const candidates = rows.filter((row) => {
      const connection = this.nodes.get(row.id);
      if (!connection || !connection.authed || this.busyNodes.has(row.id) || t - row.last_seen >= 45) return false;
      let status = {};
      try { status = JSON.parse(row.status || '{}'); } catch { return false; }
      return Array.isArray(status.models) && status.models.includes(job.model);
    }).sort((a, b) => {
      const load = (row) => { try { const status = JSON.parse(row.status || '{}'); return (Number(status.cpuPercent) || 0) + (Number(status.gpuPercent) || 0) * 0.25; } catch { return 1000; } };
      return load(a) - load(b);
    });
    if (!candidates.length) return fail('No online Nexus machine currently reports the requested Ollama model. Check that Ollama is running and the model is installed.', 503);
    const nodeId = candidates[0].id, connection = this.nodes.get(nodeId), requestId = randomHex(16);
    this.busyNodes.add(nodeId);
    return await new Promise((resolve) => {
      const timer = setTimeout(() => {
        this.pendingInference.delete(requestId);
        this.busyNodes.delete(nodeId);
        resolve(fail('The local model did not answer within 90 seconds.', 504));
      }, 90000);
      this.pendingInference.set(requestId, { resolve, timer, nodeId });
      send(connection.ws, { t: 'node.infer', requestId, model: job.model, messages: job.messages, tools: job.tools || [], options: { num_predict: 2048 } });
    });
  }

  async control(actor, body, ip = 'unknown') {
    actor = typeof actor === 'string' ? actor.toLowerCase() : '';
    if (!isHex(actor) || actor !== String(this.env.OFFICIAL || '').toLowerCase())
      return fail('Only the configured official Guts & Bolts owner may manage infrastructure.', 401);
    if (!body || typeof body !== 'object' || Array.isArray(body) || typeof body.action !== 'string')
      return fail('Bad request.', 400);
    const allowed = {
      list: ['action'],
      pair: ['action', 'code'],
      revoke: ['action', 'deviceId'],
      audit: ['action'],
    }[body.action];
    if (!allowed || Object.keys(body).some((key) => !allowed.includes(key)))
      return fail('Unexpected infrastructure request fields.', 400);

    if (body.action === 'pair') {
      ip = typeof ip === 'string' && ip.length <= 64 ? ip : 'unknown';
      this.sql.exec('DELETE FROM infra_pair_attempts WHERE at < ?', now() - 86400);
      const ipHash = await hash(ip);
      const attempts = [...this.sql.exec('SELECT COUNT(*) AS n FROM infra_pair_attempts WHERE ip_hash = ? AND at > ?',
        ipHash, now() - 3600)][0]?.n || 0;
      if (attempts >= 10) return fail('Too many pairing attempts. Wait an hour before trying again.', 429);
      this.sql.exec('INSERT INTO infra_pair_attempts (ip_hash, at) VALUES (?, ?)', ipHash, now());
      const code = typeof body.code === 'string' ? body.code.toLowerCase() : '';
      if (!isHex(code)) return fail('Paste the complete 64-character pairing code shown by the infrastructure agent.', 400);
      const codeHash = await hash(code);
      const pairing = [...this.sql.exec('SELECT expires, conn_id, name, platform FROM infra_pairings WHERE code_hash = ?', codeHash)][0];
      const conn = pairing && this.connections.get(pairing.conn_id);
      if (!pairing || pairing.expires <= now() || !conn || !conn.pending) {
        this.sql.exec('DELETE FROM infra_pairings WHERE code_hash = ?', codeHash);
        this.audit(actor, 'pair.failed', 'Invalid, expired or disconnected node pairing code');
        return fail('That pairing code is invalid or expired. Generate a new one on the machine.', 400);
      }
      const id = randomHex(16);
      const token = randomHex(32);
      this.sql.exec('INSERT INTO infra_devices (id, owner, name, platform, token_hash, created, revoked, last_seen, status) VALUES (?, ?, ?, ?, ?, ?, 0, 0, ?)',
        id, actor, pairing.name, pairing.platform, await hash(token), now(), '{}');
      this.sql.exec('DELETE FROM infra_pairings WHERE code_hash = ?', codeHash);
      conn.pending = false;
      conn.authed = true;
      conn.nodeId = id;
      conn.owner = actor;
      const previous = this.nodes.get(id);
      if (previous && previous !== conn) this.drop(previous, false);
      this.nodes.set(id, conn);
      send(conn.ws, { t: 'pair.complete', id, token });
      this.audit(actor, 'node.paired', 'Infrastructure node paired: ' + pairing.name);
      return success({ paired: true, deviceId: id, name: pairing.name, platform: pairing.platform });
    }

    if (body.action === 'list') {
      const rows = [...this.sql.exec('SELECT id, name, platform, created, revoked, last_seen, status FROM infra_devices WHERE owner = ? ORDER BY created DESC', actor)];
      const devices = rows.map((row) => {
        let status = {};
        try { status = JSON.parse(row.status || '{}'); } catch { status = {}; }
        const online = !row.revoked && this.nodes.has(row.id) && now() - row.last_seen < 45;
        return { id: row.id, name: row.name, platform: row.platform, created: row.created,
          revoked: !!row.revoked, online, lastSeen: row.last_seen, status };
      });
      const entries = [...this.sql.exec('SELECT id, at, actor, action, detail FROM infra_audit ORDER BY at DESC LIMIT 60')];
      return success({ devices, entries });
    }
    if (body.action === 'audit') {
      const entries = [...this.sql.exec('SELECT id, at, actor, action, detail FROM infra_audit ORDER BY at DESC LIMIT 100')];
      return success({ entries });
    }
    if (body.action === 'revoke') {
      const id = typeof body.deviceId === 'string' ? body.deviceId.toLowerCase() : '';
      if (!isHex(id, 32)) return fail('Invalid device ID.', 400);
      const row = [...this.sql.exec('SELECT id FROM infra_devices WHERE id = ? AND owner = ?', id, actor)][0];
      if (!row) return fail('Infrastructure node not found.', 404);
      this.sql.exec('UPDATE infra_devices SET revoked = 1 WHERE id = ?', id);
      const conn = this.nodes.get(id);
      if (conn) this.drop(conn);
      this.audit(actor, 'node.revoked', 'Infrastructure node access revoked: ' + id);
      return success({ revoked: true });
    }
    return fail('Unknown infrastructure action.', 400);
  }
}
