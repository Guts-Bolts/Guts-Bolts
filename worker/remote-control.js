import { DurableObject } from 'cloudflare:workers';

const enc = new TextEncoder();
const now = () => Math.floor(Date.now() / 1000);
const fail = (error, status = 400) => Response.json({ ok: false, error }, { status, headers: { 'cache-control': 'no-store' } });
const hex = (bytes) => [...bytes].map((b) => b.toString(16).padStart(2, '0')).join('');
const randomHex = (n) => hex(crypto.getRandomValues(new Uint8Array(n)));
const isHex = (value, length = 64) => typeof value === 'string' && value.length === length && /^[a-f0-9]+$/i.test(value);

async function hash(value) {
  return hex(new Uint8Array(await crypto.subtle.digest('SHA-256', enc.encode(value))));
}
function equal(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string') return false;
  const length = Math.max(a.length, b.length);
  let diff = a.length ^ b.length;
  for (let i = 0; i < length; i++) diff |= (a.charCodeAt(i) || 0) ^ (b.charCodeAt(i) || 0);
  return diff === 0;
}
function jsonMessage(ws, value) {
  try { ws.send(JSON.stringify(value)); } catch { /* connection already closed */ }
}
function safeStatus(raw) {
  if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return null;
  const number = (key, min, max) => Number.isFinite(raw[key]) && raw[key] >= min && raw[key] <= max ? Math.round(raw[key] * 10) / 10 : null;
  const text = (key, max) => typeof raw[key] === 'string' ? raw[key].slice(0, max) : '';
  const cpu = number('cpuPercent', 0, 100);
  const ram = number('memoryPercent', 0, 100);
  const slots = Number.isInteger(raw.slots) && raw.slots >= 1 && raw.slots <= 50 ? raw.slots : null;
  return {
    cpuPercent: cpu, memoryPercent: ram,
    memoryUsedMB: number('memoryUsedMB', 0, 1048576),
    memoryTotalMB: number('memoryTotalMB', 1, 1048576),
    gpuPercent: number('gpuPercent', 0, 100),
    gpuTemperatureC: number('gpuTemperatureC', 0, 120),
    gpuMemoryUsedMB: number('gpuMemoryUsedMB', 0, 1048576),
    gpuMemoryTotalMB: number('gpuMemoryTotalMB', 0, 1048576),
    serverRunning: raw.serverRunning === true,
    serverPid: Number.isInteger(raw.serverPid) && raw.serverPid > 0 ? raw.serverPid : null,
    serverUptimeSeconds: number('serverUptimeSeconds', 0, 315360000),
    slots, priority: ['Normal', 'AboveNormal', 'High'].includes(raw.priority) ? raw.priority : 'Normal',
    autoRestart: raw.autoRestart === true,
    agentVersion: text('agentVersion', 24),
    lastError: text('lastError', 240),
    at: now(),
  };
}
const COMMANDS = new Set(['start', 'stop', 'restart', 'set-slots', 'set-priority', 'set-auto-restart']);
function validCommand(command, args) {
  if (!COMMANDS.has(command) || !args || typeof args !== 'object' || Array.isArray(args)) return false;
  if (command === 'set-slots') return Object.keys(args).length === 1 && Number.isInteger(args.slots) && args.slots >= 1 && args.slots <= 50;
  if (command === 'set-priority') return Object.keys(args).length === 1 && ['Normal', 'AboveNormal', 'High'].includes(args.priority);
  if (command === 'set-auto-restart') return Object.keys(args).length === 1 && typeof args.enabled === 'boolean';
  return Object.keys(args).length === 0;
}

export class RemoteControlObject extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.ctx = ctx;
    this.env = env;
    this.sql = ctx.storage.sql;
    this.connections = new Map();
    this.agent = null;
    this.sql.exec(`
      CREATE TABLE IF NOT EXISTS remote_devices (
        id TEXT PRIMARY KEY, owner TEXT NOT NULL, token_hash TEXT NOT NULL,
        created INTEGER NOT NULL, revoked INTEGER NOT NULL DEFAULT 0
      );
      CREATE TABLE IF NOT EXISTS remote_pairings (
        code_hash TEXT PRIMARY KEY, expires INTEGER NOT NULL, conn_id TEXT NOT NULL, ip TEXT NOT NULL
      );
      CREATE TABLE IF NOT EXISTS remote_status (
        id INTEGER PRIMARY KEY CHECK (id = 1), data TEXT NOT NULL, seen INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS remote_audit (
        id TEXT PRIMARY KEY, at INTEGER NOT NULL, actor TEXT NOT NULL,
        action TEXT NOT NULL, result TEXT NOT NULL
      );
      CREATE INDEX IF NOT EXISTS remote_audit_at ON remote_audit(at);
      CREATE TABLE IF NOT EXISTS remote_attempts (
        id INTEGER PRIMARY KEY AUTOINCREMENT, ip TEXT NOT NULL, at INTEGER NOT NULL
      );
      CREATE INDEX IF NOT EXISTS remote_attempts_ip_at ON remote_attempts(ip, at);
    `);
  }

  audit(actor, action, result) {
    this.sql.exec('INSERT INTO remote_audit (id, at, actor, action, result) VALUES (?, ?, ?, ?, ?)',
      randomHex(16), now(), String(actor).slice(0, 80), String(action).slice(0, 40), String(result).slice(0, 240));
    this.sql.exec('DELETE FROM remote_audit WHERE at < ?', now() - 90 * 86400);
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === '/agent' || url.pathname === '/ws/remote') return this.agentSocket(request);
    if (url.pathname === '/control' && request.method === 'POST') return this.control(request);
    return new Response('Not found.', { status: 404 });
  }

  agentSocket(request) {
    if (request.headers.get('Upgrade')?.toLowerCase() !== 'websocket') return new Response('Expected a WebSocket.', { status: 426 });
    const pair = new WebSocketPair();
    const ws = pair[1];
    ws.accept();
    const c = { id: randomHex(12), ws, ip: request.headers.get('cf-connecting-ip') || '', authed: false, attempts: 0, pending: false };
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
        if (text.length > 8192) return this.drop(c);
        let message;
        try { message = JSON.parse(text); } catch { return this.drop(c); }
        if (!message || typeof message !== 'object' || Array.isArray(message)) return this.drop(c);
        await this.onAgentMessage(c, message);
      }).catch(() => this.drop(c));
    });
    const gone = () => this.drop(c);
    ws.addEventListener('close', gone);
    ws.addEventListener('error', gone);
    return new Response(null, { status: 101, webSocket: pair[0] });
  }

  async onAgentMessage(c, message) {
    if (message.t === 'pair.begin') {
      if (c.authed || c.pending || !isHex(message.code) || typeof message.device !== 'string') return this.drop(c);
      const count = [...this.sql.exec('SELECT COUNT(*) AS n FROM remote_attempts WHERE ip = ? AND at > ?', c.ip, now() - 3600)][0]?.n || 0;
      if (count >= 5) return this.drop(c);
      this.sql.exec('INSERT INTO remote_attempts (ip, at) VALUES (?, ?)', c.ip, now());
      this.sql.exec('DELETE FROM remote_pairings WHERE expires <= ?', now());
      const codeHash = await hash(message.code.toLowerCase());
      this.sql.exec('INSERT OR REPLACE INTO remote_pairings (code_hash, expires, conn_id, ip) VALUES (?, ?, ?, ?)',
        codeHash, now() + 300, c.id, c.ip);
      c.pending = true;
      c.deviceName = message.device.slice(0, 60);
      jsonMessage(c.ws, { t: 'pair.waiting', expiresIn: 300 });
      return;
    }
    if (message.t === 'device.auth') {
      if (c.authed || c.pending || !isHex(message.token)) return this.drop(c);
      c.attempts++;
      if (c.attempts > 5) return this.drop(c);
      const device = [...this.sql.exec("SELECT owner, token_hash, revoked FROM remote_devices WHERE id = 'home'")][0];
      const tokenHash = await hash(message.token.toLowerCase());
      if (!device || device.revoked || !equal(tokenHash, device.token_hash)) return this.drop(c);
      c.authed = true;
      c.owner = device.owner;
      this.attachAgent(c);
      jsonMessage(c.ws, { t: 'device.ready', heartbeatSeconds: 10 });
      this.audit('agent', 'device.connected', 'Home server agent connected');
      return;
    }
    if (!c.authed || this.agent !== c) return this.drop(c);
    if (message.t === 'status') {
      const status = safeStatus(message.status);
      if (!status) return this.drop(c);
      this.sql.exec('INSERT OR REPLACE INTO remote_status (id, data, seen) VALUES (1, ?, ?)', JSON.stringify(status), now());
      this.checkAlerts(status);
      return;
    }
    if (message.t === 'ack') {
      const id = typeof message.id === 'string' && /^[a-f0-9-]{16,64}$/i.test(message.id) ? message.id : '';
      if (!id) return this.drop(c);
      this.audit('agent', 'command.ack', (message.ok === true ? 'ok: ' : 'failed: ') + String(message.message || '').slice(0, 180));
      jsonMessage(c.ws, { t: 'ack.received', id });
      return;
    }
    if (message.t === 'agent.error') {
      this.audit('agent', 'agent.error', String(message.message || 'Agent reported an error').slice(0, 200));
      return;
    }
    return this.drop(c);
  }

  attachAgent(c) {
    if (this.agent && this.agent !== c) {
      jsonMessage(this.agent.ws, { t: 'device.replaced' });
      try { this.agent.ws.close(4001, 'Replaced by a newer connection'); } catch { /* already closed */ }
    }
    this.agent = c;
  }

  drop(c) {
    if (!c || !this.connections.has(c.id)) return;
    this.connections.delete(c.id);
    if (this.agent === c) {
      this.agent = null;
      this.audit('agent', 'device.disconnected', 'Home server agent disconnected');
    }
    try { c.ws.close(1000, 'bye'); } catch { /* already closed */ }
  }

  checkAlerts(status) {
    const previous = this.alertState || {};
    const next = {
      cpu: status.cpuPercent !== null && status.cpuPercent >= 90,
      memory: status.memoryPercent !== null && status.memoryPercent >= 90,
      gpu: status.gpuTemperatureC !== null && status.gpuTemperatureC >= 88,
    };
    const labels = { cpu: 'CPU usage reached 90%', memory: 'Memory usage reached 90%', gpu: 'GPU temperature reached 88°C' };
    for (const key of Object.keys(next)) {
      if (next[key] && !previous[key]) this.audit('monitor', 'alert.' + key, labels[key]);
    }
    this.alertState = next;
  }

  async control(request) {
    if (request.headers.get('content-type')?.split(';')[0] !== 'application/json') return fail('Expected JSON.', 415);
    const actor = (request.headers.get('x-gb-staff-id') || '').toLowerCase();
    if (!isHex(actor)) return fail('Not authorized.', 401);
    let body;
    try { body = await request.json(); } catch { return fail('Bad request.', 400); }
    if (!body || typeof body !== 'object' || Array.isArray(body) || typeof body.action !== 'string') return fail('Bad request.', 400);
    const device = [...this.sql.exec("SELECT owner, revoked FROM remote_devices WHERE id = 'home'")][0];
    if (body.action === 'pair') {
      if (device && !device.revoked) return fail('A device is already paired. Revoke it before pairing another.', 409);
      const code = typeof body.code === 'string' ? body.code.toLowerCase() : '';
      if (!isHex(code)) return fail('Paste the complete pairing code shown on the home PC.', 400);
      const codeHash = await hash(code);
      const pairing = [...this.sql.exec('SELECT expires, conn_id FROM remote_pairings WHERE code_hash = ?', codeHash)][0];
      const conn = pairing && this.connections.get(pairing.conn_id);
      if (!pairing || pairing.expires <= now() || !conn || !conn.pending) {
        this.sql.exec('DELETE FROM remote_pairings WHERE code_hash = ?', codeHash);
        this.audit(actor, 'pair.failed', 'Invalid, expired or disconnected pairing code');
        return fail('That pairing code is invalid or expired. Generate a new one on the home PC.', 400);
      }
      const token = randomHex(32);
      const tokenHash = await hash(token);
      this.sql.exec("INSERT OR REPLACE INTO remote_devices (id, owner, token_hash, created, revoked) VALUES ('home', ?, ?, ?, 0)",
        actor, tokenHash, now());
      this.sql.exec('DELETE FROM remote_pairings');
      conn.pending = false;
      conn.authed = true;
      conn.owner = actor;
      this.attachAgent(conn);
      jsonMessage(conn.ws, { t: 'pair.complete', token });
      this.audit(actor, 'device.paired', 'Home server agent paired');
      return Response.json({ ok: true, paired: true }, { headers: { 'cache-control': 'no-store' } });
    }
    if (!device || device.revoked || device.owner !== actor) return fail('This account is not the paired server owner.', 403);
    if (body.action === 'status') {
      const row = [...this.sql.exec('SELECT data, seen FROM remote_status WHERE id = 1')][0];
      let status = null;
      try { status = row ? JSON.parse(row.data) : null; } catch { status = null; }
      return Response.json({ ok: true, online: !!(this.agent && this.agent.authed && now() - (row?.seen || 0) < 45),
        lastSeen: row?.seen || 0, status }, { headers: { 'cache-control': 'no-store' } });
    }
    if (body.action === 'audit') {
      const rows = [...this.sql.exec('SELECT id, at, actor, action, result FROM remote_audit ORDER BY at DESC LIMIT 100')];
      return Response.json({ ok: true, entries: rows }, { headers: { 'cache-control': 'no-store' } });
    }
    if (body.action === 'revoke') {
      this.sql.exec("UPDATE remote_devices SET revoked = 1 WHERE id = 'home'");
      if (this.agent) this.drop(this.agent);
      this.audit(actor, 'device.revoked', 'Home server device credential revoked');
      return Response.json({ ok: true, revoked: true }, { headers: { 'cache-control': 'no-store' } });
    }
    if (body.action === 'command') {
      const command = body.command;
      const args = body.args || {};
      if (!validCommand(command, args)) return fail('That server command is not allowed.', 400);
      const recent = [...this.sql.exec('SELECT COUNT(*) AS n FROM remote_audit WHERE actor = ? AND at > ? AND action LIKE ?',
        actor, now() - 60, 'command.%')][0]?.n || 0;
      if (recent >= 20) return fail('Too many remote commands. Wait a minute.', 429);
      if (!this.agent || !this.agent.authed || now() - (([...this.sql.exec('SELECT seen FROM remote_status WHERE id = 1')][0] || {}).seen || 0) > 45)
        return fail('The home PC is offline. No command was sent.', 503);
      const id = crypto.randomUUID();
      const commandMessage = { t: 'command', id, command, args, expires: now() + 30 };
      jsonMessage(this.agent.ws, commandMessage);
      this.audit(actor, 'command.' + command, 'sent');
      return Response.json({ ok: true, queued: true, id }, { status: 202, headers: { 'cache-control': 'no-store' } });
    }
    return fail('Unknown remote-control action.', 400);
  }
}
