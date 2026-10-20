// A real website server (worker/server.js) running in plain Node for the tests:
// an in-memory SQLite database stands in for Cloudflare's, and people are made
// signed-up directly (no keys or passwords needed).
import { DatabaseSync } from 'node:sqlite';
import { GbServerObject } from '../server.js';
import wasmModule from '../../website/app/gbcrypto.wasm';

export const STAFF = 'a'.repeat(64);   // the staff account ("Guts", user #1)

// Cloudflare's ctx.storage.sql, on node:sqlite.
function fakeSql() {
  const db = new DatabaseSync(':memory:');
  return {
    exec(query, ...bind) {
      if (!bind.length && query.split(';').filter((s) => s.trim()).length > 1) { db.exec(query); return []; }
      const st = db.prepare(query);
      if (/^\s*select/i.test(query)) return st.all(...bind);
      st.run(...bind);
      return [];
    },
  };
}

export function makeServer() {
  const s = new GbServerObject({ storage: { sql: fakeSql() } }, { OFFICIAL: STAFF });
  s.signUp = (name, id) => signUp(s, name, id);
  s.call = (me, op, args = {}) => { try { return s.op(op, me, args); } finally { s.flush(); } };
  s.addGame = (creator, name, extra = {}) => addGame(s, creator, name, extra);
  s.staff = signUp(s, 'Guts', STAFF);
  return s;
}

let nextKey = 1;
// Someone with a signed-up account (like account.signup, without the password bits).
export function signUp(s, name, id) {
  const me = s.user(id || (nextKey++).toString(16).padStart(64, 'b'));
  if (s.isOfficial(me)) { me.username = 'Guts'; me.name = 'Guts'; me.userId = 1; }
  else {
    me.userId = s.nextUserId++;
    me.username = name;
    me.name = name;
    s.takenNames.add(name.toLowerCase());
  }
  s.add(me, 100, 'Welcome to Guts&Bolts!', 'welcome');
  s.saveUser(me);
  s.flush();
  return me;
}

// A published game (what Studio's Publish makes).
export function addGame(s, creator, name, extra = {}) {
  const id = String(s.nextAssetNum + 1000);
  const a = Object.assign({ id, num: s.nextAssetNum++, kind: 'game', name, description: '', creator: creator.id, created: Math.floor(Date.now() / 1000),
    updated: Math.floor(Date.now() / 1000), price: 0, sales: 0, plays: 0, meta: {}, access: 'public' }, extra);
  s.assets.set(id, a);
  s.saveAsset(a);
  s.flush();
  return a;
}

// rot13: the filter's word list is kept scrambled so the code doesn't spell them out.
export const rot13 = (w) => w.replace(/[a-z]/gi, (c) => String.fromCharCode((c <= 'Z' ? 65 : 97) + (c.toLowerCase().charCodeAt(0) - 97 + 13) % 26));

// A device that sends real signed requests through s.handle (checkRequest and all), like the
// website does: device(s, n) makes key pair number n; d.call(op, args) signs and sends one.
let wasm = null;
const hexOf = (b) => Array.from(b, (x) => x.toString(16).padStart(2, '0')).join('');
function canon(v) {
  if (Array.isArray(v)) return '[' + v.map(canon).join(',') + ']';
  if (v && typeof v === 'object') return '{' + Object.keys(v).sort().map((k) => JSON.stringify(k) + ':' + canon(v[k])).join(',') + '}';
  return JSON.stringify(v);
}
export function device(s, n) {
  if (!wasm) wasm = new WebAssembly.Instance(wasmModule, {}).exports;
  const io = () => new Uint8Array(wasm.memory.buffer, wasm.buf(), wasm.bufSize());
  io().set(new Uint8Array(32).fill(n), 0);
  wasm.keyPair();
  const secret = io().slice(32, 96), account = hexOf(io().slice(96, 128));
  let nonce = 0;
  return {
    account,
    call(op, args = {}) {
      const time = Math.floor(Date.now() / 1000), nn = (++nonce).toString(16).padStart(16, '0');
      const msg = new TextEncoder().encode('gb-req:' + op + '\n' + account + '\n' + time + '\n' + nn + '\n' + canon(args));
      io().set(secret, 0);
      io().set(msg, 128);
      wasm.sign(msg.length);
      const sig = hexOf(io().slice(64, 128));
      return s.handle({ op, account, time, nonce: nn, args, sig });
    },
  };
}

// A key pair that signs any text (like badge grants from the website): signer(n).sign(text).
export function signer(n) {
  if (!wasm) wasm = new WebAssembly.Instance(wasmModule, {}).exports;
  const io = () => new Uint8Array(wasm.memory.buffer, wasm.buf(), wasm.bufSize());
  io().set(new Uint8Array(32).fill(n), 0);
  wasm.keyPair();
  const secret = io().slice(32, 96), account = hexOf(io().slice(96, 128));
  return {
    account,
    sign(text) {
      const msg = new TextEncoder().encode(text);
      io().set(secret, 0);
      io().set(msg, 128);
      wasm.sign(msg.length);
      return hexOf(io().slice(64, 128));
    },
  };
}
