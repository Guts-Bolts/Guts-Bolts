import { DurableObject } from 'cloudflare:workers';

const enc = new TextEncoder();
const now = () => Math.floor(Date.now() / 1000);
const fail = (error, status = 400) => ({ status, body: { ok: false, error } });
const success = (body, status = 200) => ({ status, body: { ok: true, ...body } });
const hex = (bytes) => [...bytes].map((b) => b.toString(16).padStart(2, '0')).join('');
const randomHex = (n) => hex(crypto.getRandomValues(new Uint8Array(n)));
const isHex = (value, length = 64) => typeof value === 'string' && value.length === length && /^[a-f0-9]+$/i.test(value);

async function hash(value) {
  return hex(new Uint8Array(await crypto.subtle.digest('SHA-256', enc.encode(value))));
}
async function equal(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string') return false;
  const [left, right] = await Promise.all([
    crypto.subtle.digest('SHA-256', enc.encode(a)),
    crypto.subtle.digest('SHA-256', enc.encode(b)),
  ]);
  if (typeof crypto.subtle.timingSafeEqual === 'function')
    return crypto.subtle.timingSafeEqual(left, right);
  const key = await crypto.subtle.importKey('raw', new Uint8Array(32), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign', 'verify']);
  const signature = await crypto.subtle.sign('HMAC', key, left);
  return crypto.subtle.verify('HMAC', key, signature, right);
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

const b64urlBytes = (value) => Uint8Array.from(atob(value.replace(/-/g, '+').replace(/_/g, '/') + '==='.slice((value.length + 3) % 4)), (c) => c.charCodeAt(0));
const toB64url = (bytes) => {
  let binary = '';
  for (let i = 0; i < bytes.length; i += 0x4000) binary += String.fromCharCode(...bytes.subarray(i, i + 0x4000));
  return btoa(binary).replace(/=/g, '').replace(/\+/g, '-').replace(/\//g, '_');
};
const concat = (...parts) => {
  const length = parts.reduce((sum, part) => sum + part.length, 0);
  const out = new Uint8Array(length);
  let offset = 0;
  for (const part of parts) { out.set(part, offset); offset += part.length; }
  return out;
};
async function hmac(keyBytes, data) {
  const key = await crypto.subtle.importKey('raw', keyBytes, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  return new Uint8Array(await crypto.subtle.sign('HMAC', key, data));
}
async function hkdfExpand(prk, info, length) {
  let previous = new Uint8Array(0), output = new Uint8Array(0), counter = 1;
  while (output.length < length) {
    previous = await hmac(prk, concat(previous, info, Uint8Array.of(counter++)));
    output = concat(output, previous);
  }
  return output.slice(0, length);
}
async function encryptSubscription(env, subscription) {
  const raw = b64urlBytes(env.REMOTE_PUSH_ENCRYPTION_KEY || '');
  if (raw.length !== 32) throw new Error('Remote push encryption key is not configured.');
  const key = await crypto.subtle.importKey('raw', raw, 'AES-GCM', false, ['encrypt']);
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const cipher = new Uint8Array(await crypto.subtle.encrypt({ name: 'AES-GCM', iv }, key, enc.encode(JSON.stringify(subscription))));
  return toB64url(iv) + '.' + toB64url(cipher);
}
async function decryptSubscription(env, stored) {
  const raw = b64urlBytes(env.REMOTE_PUSH_ENCRYPTION_KEY || '');
  if (raw.length !== 32) throw new Error('Remote push encryption key is not configured.');
  const [ivText, cipherText] = stored.split('.');
  const key = await crypto.subtle.importKey('raw', raw, 'AES-GCM', false, ['decrypt']);
  const plain = await crypto.subtle.decrypt({ name: 'AES-GCM', iv: b64urlBytes(ivText) }, key, b64urlBytes(cipherText));
  return JSON.parse(new TextDecoder().decode(plain));
}
async function vapidToken(env, audience) {
  const privateBytes = b64urlBytes(env.REMOTE_VAPID_PRIVATE_KEY || '');
  if (!privateBytes.length || !env.REMOTE_VAPID_PUBLIC_KEY) throw new Error('Web Push VAPID keys are not configured.');
  const key = await crypto.subtle.importKey('pkcs8', privateBytes, { name: 'ECDSA', namedCurve: 'P-256' }, false, ['sign']);
  const encode = (value) => toB64url(enc.encode(JSON.stringify(value)));
  const head = encode({ typ: 'JWT', alg: 'ES256' });
  const body = encode({ aud: audience, exp: now() + 3600, sub: 'mailto:security@gutsandbolts.net' });
  const input = head + '.' + body;
  const signature = new Uint8Array(await crypto.subtle.sign({ name: 'ECDSA', hash: 'SHA-256' }, key, enc.encode(input)));
  return input + '.' + toB64url(signature);
}
async function pushBody(subscription, payload) {
  const uaPublic = b64urlBytes(subscription.keys.p256dh);
  const authSecret = b64urlBytes(subscription.keys.auth);
  if (uaPublic.length !== 65 || authSecret.length !== 16) throw new Error('Invalid push subscription keys.');
  const ephemeral = await crypto.subtle.generateKey({ name: 'ECDH', namedCurve: 'P-256' }, true, ['deriveBits']);
  const asPublic = new Uint8Array(await crypto.subtle.exportKey('raw', ephemeral.publicKey));
  const uaKey = await crypto.subtle.importKey('raw', uaPublic, { name: 'ECDH', namedCurve: 'P-256' }, false, []);
  const shared = new Uint8Array(await crypto.subtle.deriveBits({ name: 'ECDH', public: uaKey }, ephemeral.privateKey, 256));
  const prkKey = await hmac(authSecret, shared);
  const keyInfo = concat(enc.encode('WebPush: info\0'), uaPublic, asPublic);
  const ikm = await hkdfExpand(prkKey, keyInfo, 32);
  const salt = crypto.getRandomValues(new Uint8Array(16));
  const prk = await hmac(salt, ikm);
  const cek = await hkdfExpand(prk, enc.encode('Content-Encoding: aes128gcm\0'), 16);
  const nonce = await hkdfExpand(prk, enc.encode('Content-Encoding: nonce\0'), 12);
  const plain = concat(enc.encode(JSON.stringify(payload)), Uint8Array.of(2));
  const aes = await crypto.subtle.importKey('raw', cek, 'AES-GCM', false, ['encrypt']);
  const encrypted = new Uint8Array(await crypto.subtle.encrypt({ name: 'AES-GCM', iv: nonce, tagLength: 128 }, aes, plain));
  return concat(salt, Uint8Array.of(0, 0, 16, 0), Uint8Array.of(asPublic.length), asPublic, encrypted);
}
\nexport class RemoteControlObject extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.ctx = ctx;
    this.env = env;
    this.sql = ctx.storage.sql;
    this.connections = new Map();
    this.agent = null;
    const initialize = () => this.sql.exec(`
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
      CREATE TABLE IF NOT EXISTS remote_alert_state (
        key TEXT PRIMARY KEY, started INTEGER NOT NULL, alerted INTEGER NOT NULL DEFAULT 0
      );
      CREATE TABLE IF NOT EXISTS remote_push_subscriptions (
        id TEXT PRIMARY KEY, encrypted TEXT NOT NULL, created INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS remote_push_state (
        id INTEGER PRIMARY KEY CHECK (id = 1), last_sent INTEGER NOT NULL DEFAULT 0
      );
      INSERT OR IGNORE INTO remote_push_state (id, last_sent) VALUES (1, 0);
    `);
    if (typeof ctx.blockConcurrencyWhile === 'function') ctx.blockConcurrencyWhile(initialize);
    else initialize();
  }

  audit(actor, action, result) {
    this.sql.exec('INSERT INTO remote_audit (id, at, actor, action, result) VALUES (?, ?, ?, ?, ?)',
      randomHex(16), now(), String(actor).slice(0, 80), String(action).slice(0, 40), String(result).slice(0, 240));
    this.sql.exec('DELETE FROM remote_audit WHERE at < ?', now() - 90 * 86400);
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === '/agent' || url.pathname === '/ws/remote') return this.agentSocket(request);
    
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
      const codeHash = await hash(message.code.toLowerCase());
      const existing = [...this.sql.exec('SELECT expires FROM remote_pairings WHERE code_hash = ?', codeHash)][0];
      if (existing && existing.expires <= now()) {
        this.sql.exec('DELETE FROM remote_pairings WHERE code_hash = ?', codeHash);
        jsonMessage(c.ws, { t: 'pair.expired' });
        return this.drop(c);
      }
      if (!existing) {
        this.sql.exec('DELETE FROM remote_attempts WHERE at < ?', now() - 86400);
        const count = [...this.sql.exec('SELECT COUNT(*) AS n FROM remote_attempts WHERE ip = ? AND at > ?', c.ip, now() - 3600)][0]?.n || 0;
        if (count >= 5) {
          jsonMessage(c.ws, { t: 'pair.rate-limited' });
          return this.drop(c);
        }
        this.sql.exec('INSERT INTO remote_attempts (ip, at) VALUES (?, ?)', c.ip, now());
        this.sql.exec('INSERT INTO remote_pairings (code_hash, expires, conn_id, ip) VALUES (?, ?, ?, ?)',
          codeHash, now() + 300, c.id, c.ip);
      } else {
        this.sql.exec('UPDATE remote_pairings SET conn_id = ?, ip = ? WHERE code_hash = ?', c.id, c.ip, codeHash);
      }
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
      if (!device || device.revoked || !(await equal(tokenHash, device.token_hash))) {
        jsonMessage(c.ws, { t: 'device.rejected' });
        return this.drop(c);
      }
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
      const wasOffline = [...this.sql.exec("SELECT key FROM remote_alert_state WHERE key = 'offline'")].length > 0;
      this.sql.exec("DELETE FROM remote_alert_state WHERE key = 'offline'");
      if (wasOffline) this.audit('agent', 'device.reconnected', 'Home server agent resumed heartbeats');
      if (this.ctx.storage && typeof this.ctx.storage.setAlarm === 'function')
        await this.ctx.storage.setAlarm(Date.now() + 45000);
      await this.checkAlerts(status);
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
      const detail = String(message.message || 'Agent reported an error').slice(0, 200);
      this.audit('agent', 'agent.error', detail);
      if (this.ctx.waitUntil) this.ctx.waitUntil(this.sendPush({ title: 'Guts & Bolts server agent error', body: detail }).catch(() => {}));
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
      this.sql.exec("INSERT OR REPLACE INTO remote_alert_state (key, started, alerted) VALUES ('offline', ?, 1)", now());
      this.audit('agent', 'device.disconnected', 'Home server agent disconnected');
      if (this.ctx.waitUntil) this.ctx.waitUntil(this.sendPush({ title: 'Guts & Bolts server offline', body: 'The home PC agent disconnected.' }).catch(() => {}));
    }
    try { c.ws.close(1000, 'bye'); } catch { /* already closed */ }
  }

  async checkAlerts(status) {
    const t = now();
    const checks = [
      { key: 'cpu', high: status.cpuPercent !== null && status.cpuPercent >= 90, seconds: 120, label: 'CPU usage stayed above 90% for two minutes' },
      { key: 'memory', high: status.memoryPercent !== null && status.memoryPercent >= 90, seconds: 120, label: 'Memory usage stayed above 90% for two minutes' },
      { key: 'gpu', high: status.gpuTemperatureC !== null && status.gpuTemperatureC >= 88, seconds: 60, label: 'GPU temperature stayed above 88°C for one minute' },
    ];
    for (const check of checks) {
      const row = [...this.sql.exec('SELECT started, alerted FROM remote_alert_state WHERE key = ?', check.key)][0];
      if (!check.high) {
        if (row) this.sql.exec('DELETE FROM remote_alert_state WHERE key = ?', check.key);
        continue;
      }
      if (!row) {
        this.sql.exec('INSERT INTO remote_alert_state (key, started, alerted) VALUES (?, ?, 0)', check.key, t);
        continue;
      }
      if (!row.alerted && t - row.started >= check.seconds) {
        this.audit('monitor', 'alert.' + check.key, check.label);
        this.sql.exec('UPDATE remote_alert_state SET alerted = 1 WHERE key = ?', check.key);
        await this.sendPush({ title: 'Guts & Bolts server alert', body: check.label });
      }
    }
  }

  async alarm() {
    const row = [...this.sql.exec('SELECT seen FROM remote_status WHERE id = 1')][0];
    if (!row || now() - row.seen < 45) {
      if (row && this.ctx.storage && typeof this.ctx.storage.setAlarm === 'function')
        await this.ctx.storage.setAlarm((row.seen + 45) * 1000);
      return;
    }
    const already = [...this.sql.exec("SELECT alerted FROM remote_alert_state WHERE key = 'offline'")][0];
    if (already && already.alerted) return;
    this.sql.exec("INSERT OR REPLACE INTO remote_alert_state (key, started, alerted) VALUES ('offline', ?, 1)", now());
    this.audit('monitor', 'alert.offline', 'Home server agent missed its heartbeat for more than 45 seconds');
    await this.sendPush({ title: 'Guts & Bolts server offline', body: 'The home PC agent has missed its heartbeat for more than 45 seconds.' });
  }

  async sendPush(payload) {
    if (!this.env.REMOTE_VAPID_PRIVATE_KEY || !this.env.REMOTE_VAPID_PUBLIC_KEY || !this.env.REMOTE_PUSH_ENCRYPTION_KEY) return;
    const lastSent = [...this.sql.exec('SELECT last_sent FROM remote_push_state WHERE id = 1')][0]?.last_sent || 0;
    if (now() - lastSent < 30) return;
    this.sql.exec('UPDATE remote_push_state SET last_sent = ? WHERE id = 1', now());
    const rows = [...this.sql.exec('SELECT id, encrypted FROM remote_push_subscriptions')];
    for (const row of rows) {
      try {
        const subscription = await decryptSubscription(this.env, row.encrypted);
        const endpoint = new URL(subscription.endpoint);
        const audience = endpoint.origin;
        const jwt = await vapidToken(this.env, audience);
        const body = await pushBody(subscription, payload);
        const response = await fetch(subscription.endpoint, {
          method: 'POST',
          headers: {
            authorization: 'vapid t=' + jwt + ', k=' + this.env.REMOTE_VAPID_PUBLIC_KEY,
            'content-type': 'application/octet-stream',
            'content-encoding': 'aes128gcm',
            ttl: '60',
            urgency: 'high',
          },
          body,
          redirect: 'manual',
          signal: AbortSignal.timeout(10000),
        });
        if (response.status === 404 || response.status === 410) {
          this.sql.exec('DELETE FROM remote_push_subscriptions WHERE id = ?', row.id);
        } else if (!response.ok) {
          this.audit('push', 'push.failed', 'Push provider returned HTTP ' + response.status);
        }
      } catch {
        this.audit('push', 'push.failed', 'Could not deliver a push notification');
      }
    }
  }

  async control(actor, body) {
    actor = typeof actor === 'string' ? actor.toLowerCase() : '';
    if (!isHex(actor)) return fail('Not authorized.', 401);
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
      return success({ paired: true });
    }
    if (!device || device.revoked || device.owner !== actor) return fail('This account is not the paired server owner.', 403);
    if (body.action === 'status') {
      const row = [...this.sql.exec('SELECT data, seen FROM remote_status WHERE id = 1')][0];
      let status = null;
      try { status = row ? JSON.parse(row.data) : null; } catch { status = null; }
      const entries = [...this.sql.exec('SELECT id, at, actor, action, result FROM remote_audit ORDER BY at DESC LIMIT 100')];
      return success({ online: !!(this.agent && this.agent.authed && now() - (row?.seen || 0) < 45),
        lastSeen: row?.seen || 0, status, entries });
    }
    if (body.action === 'audit') {
      const rows = [...this.sql.exec('SELECT id, at, actor, action, result FROM remote_audit ORDER BY at DESC LIMIT 100')];
      return success({ entries: rows });
    }
    if (body.action === 'push.config') return success({ pushPublicKey: this.env.REMOTE_VAPID_PUBLIC_KEY || '' });
    if (body.action === 'push.subscribe') {
      const sub = body.subscription;
      if (!sub || typeof sub !== 'object' || typeof sub.endpoint !== 'string' || !sub.keys ||
          typeof sub.keys.p256dh !== 'string' || typeof sub.keys.auth !== 'string') return fail('Invalid push subscription.', 400);
      try {
        if (b64urlBytes(sub.keys.p256dh).length !== 65 || b64urlBytes(sub.keys.auth).length !== 16)
          return fail('Invalid push subscription keys.', 400);
      } catch { return fail('Invalid push subscription keys.', 400); }
      let endpoint;
      try { endpoint = new URL(sub.endpoint); } catch { return fail('Invalid push endpoint.', 400); }
      const host = endpoint.hostname.toLowerCase();
      const allowedHost = host === 'fcm.googleapis.com' || host === 'web.push.apple.com' || host.endsWith('.push.services.mozilla.com');
      if (endpoint.protocol !== 'https:' || endpoint.username || endpoint.password || endpoint.port || !allowedHost)
        return fail('Unsupported push service endpoint.', 400);
      try {
        const subscriptionId = await hash(endpoint.toString());
        const exists = [...this.sql.exec('SELECT id FROM remote_push_subscriptions WHERE id = ?', subscriptionId)].length > 0;
        const count = [...this.sql.exec('SELECT COUNT(*) AS n FROM remote_push_subscriptions')][0]?.n || 0;
        if (!exists && count >= 5) return fail('A maximum of five phone/browser subscriptions is allowed.', 429);
        const encrypted = await encryptSubscription(this.env, { endpoint: endpoint.toString(), keys: { p256dh: sub.keys.p256dh, auth: sub.keys.auth } });
        this.sql.exec('INSERT OR REPLACE INTO remote_push_subscriptions (id, encrypted, created) VALUES (?, ?, ?)',
          subscriptionId, encrypted, now());
        this.audit(actor, 'push.subscribed', 'Phone push notifications enabled');
        return success({ subscribed: true });
      } catch (error) { return fail(error.message || 'Push setup failed.', 503); }
    }
    if (body.action === 'push.remove') {
      const endpoint = typeof body.endpoint === 'string' ? body.endpoint : '';
      if (!endpoint || endpoint.length > 2048) return fail('Invalid push endpoint.', 400);
      this.sql.exec('DELETE FROM remote_push_subscriptions WHERE id = ?', await hash(endpoint));
      this.audit(actor, 'push.unsubscribed', 'Phone push notifications disabled');
      return success({ unsubscribed: true });
    }
    if (body.action === 'revoke') {
      this.sql.exec("UPDATE remote_devices SET revoked = 1 WHERE id = 'home'");
      if (this.agent) this.drop(this.agent);
      this.audit(actor, 'device.revoked', 'Home server device credential revoked');
      return success({ revoked: true });
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
      return success({ queued: true, id }, 202);
    }
    return fail('Unknown remote-control action.', 400);
  }
}