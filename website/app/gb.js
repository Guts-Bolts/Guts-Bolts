// Talking to a Guts&Bolts server from the browser, exactly like the apps do:
// every request is signed with the player's own key (Ed25519 + BLAKE2b, from
// Monocypher compiled to WebAssembly: gbcrypto.wasm), and logging in unlocks
// the account key with the password (Argon2id), which never leaves the page.

const enc = new TextEncoder();
const hex = (bytes) => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
const unhex = (s) => {
  if (!/^([0-9a-f]{2})*$/i.test(s)) return null;
  const out = new Uint8Array(s.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(s.substr(i * 2, 2), 16);
  return out;
};
const random = (n) => crypto.getRandomValues(new Uint8Array(n));
export const randomHex = (n) => hex(random(n));

// --- the crypto (WebAssembly) ---------------------------------------------

let wasm = null;
async function crypt() {
  if (wasm) return wasm;
  const res = await fetch(new URL('gbcrypto.wasm', import.meta.url));
  const { instance } = await WebAssembly.instantiate(await res.arrayBuffer());
  wasm = instance.exports;
  return wasm;
}
const io = () => new Uint8Array(wasm.memory.buffer, wasm.buf(), wasm.bufSize());

async function keyPair(seed) {
  await crypt();
  const m = io();
  m.set(seed, 0);
  wasm.keyPair();
  const secret = m.slice(32, 96), pub = m.slice(96, 128);
  wasm.wipe();
  return { secret, pub };
}

async function signBytes(secret, message) {
  await crypt();
  let m = io();
  if (message.length > m.length - 128) throw new Error('That is too big to send.');
  m.set(secret, 0);
  m.set(message, 128);
  wasm.sign(message.length);
  m = io();
  const sig = m.slice(64, 128);
  m.fill(0, 0, 128);
  return sig;
}

// Account::passwordKeys: Argon2id(password, salt) -> lock key (unlocks the
// account key) + auth (what the server checks).
async function passwordKeys(password, saltHex) {
  await crypt();
  const pw = enc.encode(password), salt = unhex(saltHex);
  if (!pw.length || pw.length > 900 || !salt || salt.length !== 16) return null;
  const m = io();
  m.set(salt, 0);
  m.set(pw, 64);
  wasm.argon2(pw.length);
  const out = io().slice(1024, 1088);
  wasm.wipe();
  return { lock: out.slice(0, 32), auth: hex(out.slice(32, 64)) };
}

// Account::backupKey: the secret key locked with the password's lock key.
async function lockKey(lock, secret) {
  await crypt();
  const nonce = random(24), m = io();
  m.set(lock, 0); m.set(nonce, 32); m.set(secret, 72);
  wasm.lock();
  const r = io();
  const blob = hex(nonce) + hex(r.slice(56, 72)) + hex(r.slice(136, 200));
  wasm.wipe();
  return blob;
}

// Account::restoreKey.
async function unlockKey(lock, blobHex) {
  await crypt();
  const blob = unhex(blobHex || '');
  if (!blob || blob.length !== 104) return null;
  const m = io();
  m.set(lock, 0); m.set(blob.subarray(0, 24), 32); m.set(blob.subarray(24, 40), 56); m.set(blob.subarray(40), 72);
  const ok = wasm.unlock() === 0;
  const secret = ok ? io().slice(136, 200) : null;
  wasm.wipe();
  return secret;
}

// --- this browser's account key ----------------------------------------------
// Like the app's account.key: a visitor gets a key straight away (so they can
// look around); signing up or logging in ties it to a username.

const KEY = 'gb.key';
const VAULT_DB = 'gb.keyvault';
const VAULT_STORE = 'keys';
const VAULT_ID = 'browser-signing-key';
const VAULT_AAD = enc.encode('gutsandbolts:gb.key:v1');
let secret = null;

// The signing key is encrypted in localStorage with a non-extractable AES-GCM
// CryptoKey kept in IndexedDB. This protects a copied localStorage value from
// being used on its own. It does not protect against active same-origin XSS:
// malicious page code can still ask this page to sign requests.
function openVault() {
  return new Promise((resolve, reject) => {
    if (!('indexedDB' in globalThis) || !crypto.subtle) return reject(new Error('Secure browser storage is unavailable.'));
    const req = indexedDB.open(VAULT_DB, 1);
    req.onupgradeneeded = () => {
      if (!req.result.objectStoreNames.contains(VAULT_STORE)) req.result.createObjectStore(VAULT_STORE);
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error || new Error('Could not open secure browser storage.'));
    req.onblocked = () => reject(new Error('Secure browser storage is blocked.'));
  });
}

async function vaultGet() {
  const db = await openVault();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(VAULT_STORE, 'readonly');
    const req = tx.objectStore(VAULT_STORE).get(VAULT_ID);
    let value;
    req.onsuccess = () => { value = req.result || null; };
    req.onerror = () => reject(req.error || new Error('Could not read secure browser storage.'));
    tx.oncomplete = () => { db.close(); resolve(value); };
    tx.onerror = () => { db.close(); reject(tx.error || new Error('Could not read secure browser storage.')); };
    tx.onabort = () => { db.close(); reject(tx.error || new Error('Secure browser storage was interrupted.')); };
  });
}

async function vaultPut(value) {
  const db = await openVault();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(VAULT_STORE, 'readwrite');
    tx.objectStore(VAULT_STORE).put(value, VAULT_ID);
    tx.oncomplete = () => { db.close(); resolve(); };
    tx.onerror = () => { db.close(); reject(tx.error || new Error('Could not save secure browser storage.')); };
    tx.onabort = () => { db.close(); reject(tx.error || new Error('Secure browser storage was interrupted.')); };
  });
}

async function getVaultKey(create) {
  const saved = await vaultGet();
  if (saved) return saved;
  if (!create) return null;
  const key = await crypto.subtle.generateKey({ name: 'AES-GCM', length: 256 }, false, ['encrypt', 'decrypt']);
  await vaultPut(key);
  return key;
}

function storedKey() {
  try { return localStorage.getItem(KEY) || ''; } catch { return ''; }
}
function clearStoredKey() {
  try { localStorage.removeItem(KEY); } catch { /* key remains in memory for this visit */ }
}

async function saveKey(newSecret) {
  try {
    const key = await getVaultKey(true);
    const iv = random(12);
    const cipher = await crypto.subtle.encrypt({ name: 'AES-GCM', iv, additionalData: VAULT_AAD }, key, newSecret);
    localStorage.setItem(KEY, 'v1.' + hex(iv) + '.' + hex(new Uint8Array(cipher)));
  } catch {
    // Never fall back to writing a plaintext signing key. The account remains
    // usable for this page visit and can be unlocked again with its password.
    clearStoredKey();
  }
}

async function restoreStoredKey(value) {
  if (value.startsWith('v1.')) {
    const parts = value.split('.');
    const iv = parts.length === 3 ? unhex(parts[1]) : null;
    const cipher = parts.length === 3 ? unhex(parts[2]) : null;
    if (!iv || iv.length !== 12 || !cipher || cipher.length !== 80) return null;
    try {
      const key = await getVaultKey(false);
      if (!key) return null;
      return new Uint8Array(await crypto.subtle.decrypt({ name: 'AES-GCM', iv, additionalData: VAULT_AAD }, key, cipher));
    } catch { return null; }
  }
  // One-time migration for existing installations whose browser key was stored
  // as plaintext hex. Preserve the same account key, then replace the old value.
  const legacy = unhex(value);
  if (legacy && legacy.length === 64) {
    await saveKey(legacy);
    return legacy;
  }
  return null;
}

let loadingKey = null;
async function loadKey() {
  if (secret) return secret;
  if (loadingKey) return loadingKey;
  loadingKey = (async () => {
    secret = await restoreStoredKey(storedKey());
    if (!secret || secret.length !== 64) {
      secret = (await keyPair(random(32))).secret;
      await saveKey(secret);
    }
    return secret;
  })();
  try { return await loadingKey; }
  finally { loadingKey = null; }
}

export async function accountId() { return hex((await loadKey()).slice(32, 64)); }

async function setKey(newSecret) {
  secret = newSecret;
  await saveKey(secret);
}

export async function forgetKey() {   // log out: this browser becomes a new visitor
  secret = (await keyPair(random(32))).secret;
  await saveKey(secret);
}

// --- requests ------------------------------------------------------------------

// The server checks the signature against nlohmann::json::dump() of the args:
// keys sorted, no spaces. Numbers must be whole (the site never sends fractions).
function canon(v) {
  if (Array.isArray(v)) return '[' + v.map(canon).join(',') + ']';
  if (v && typeof v === 'object') {
    return '{' + Object.keys(v).filter((k) => v[k] !== undefined).sort()
      .map((k) => JSON.stringify(k) + ':' + canon(v[k])).join(',') + '}';
  }
  if (typeof v === 'number') return Number.isFinite(v) ? String(Math.trunc(v)) : 'null';   // (NaN would make the request unreadable)
  return JSON.stringify(v === undefined ? null : v);
}

let clockOffset = 0;   // server time - our time (a wrong clock would fail every request)
let clockChecked = false;
async function checkClock() {
  if (clockChecked) return;
  clockChecked = true;
  try {
    const r = await (await fetch('/api/info', { cache: 'no-store' })).json();
    if (r && r.time) clockOffset = r.time - Math.floor(Date.now() / 1000);
  } catch { /* offline: requests will say so */ }
}

// The "are you a person?" check (Cloudflare Turnstile). The server asks for it when a request
// looks doubtful (worker/shield.js) and always before signing up on the website. A popup shows
// the check (usually it ticks itself), the server hands back a pass for an hour, and the
// request is sent again. Resolves true once passed, false if the popup was closed.
let checking = null;
function loadTurnstile() {
  if (window.turnstile) return Promise.resolve();
  return new Promise((ok, no) => {
    const tag = document.createElement('script');
    tag.src = 'https://challenges.cloudflare.com/turnstile/v0/api.js?render=explicit';
    tag.async = true;
    tag.onload = ok;
    tag.onerror = no;
    document.head.appendChild(tag);
  });
}
function personCheck(sitekey) {
  if (checking) return checking;   // (one popup, however many requests asked)
  checking = new Promise((done) => {
    const box = document.createElement('div');
    box.className = 'modal';
    box.innerHTML = '<div class="popup"><button class="popup-x" aria-label="Close">\u00d7</button>' +
      '<div class="popup-title">Quick check</div><p>Making sure you\'re a person, not a bot. This usually takes a second.</p>' +
      '<div class="person-check" style="display:flex;justify-content:center;min-height:65px"></div><p class="small person-error"></p></div>';
    document.body.appendChild(box);
    let widget = null;
    const finish = (passed) => {
      try { if (widget !== null) window.turnstile.remove(widget); } catch { /* already gone */ }
      box.remove();
      checking = null;
      done(passed);
    };
    box.querySelector('.popup-x').onclick = () => finish(false);
    const say = (text) => { box.querySelector('.person-error').textContent = text; };
    loadTurnstile().then(() => {
      widget = window.turnstile.render(box.querySelector('.person-check'), {
        sitekey, action: 'gb_check',
        callback: async (token) => {
          try {
            const r = await (await fetch('/api/turnstile', { method: 'POST', body: JSON.stringify({ token }), headers: { 'content-type': 'application/json' } })).json();
            if (r && r.ok) return finish(true);
            say((r && r.error) || 'That didn\'t work. Try again.');
          } catch { say('Couldn\'t reach the website\'s server. Check your internet.'); }
          window.turnstile.reset(widget);   // (each check works once)
        },
        'error-callback': () => say('The check couldn\'t load. Try again in a moment.'),
      });
    }).catch(() => say('The check couldn\'t load. Is something blocking challenges.cloudflare.com?'));
  });
  return checking;
}

export async function signedRequest(op, args = {}) {
  await checkClock();
  const key = await loadKey();
  const account = hex(key.slice(32, 64));
  const time = Math.floor(Date.now() / 1000) + clockOffset;
  const nonce = randomHex(8);
  const text = 'gb-req:' + op + '\n' + account + '\n' + time + '\n' + nonce + '\n' + canon(args);
  const sig = hex(await signBytes(key, enc.encode(text)));
  return { op, account, time, nonce, args, sig };
}

export async function call(op, args = {}, again = true) {
  try {
    const request = await signedRequest(op, args);
    const res = await fetch('/api', { method: 'POST', body: JSON.stringify(request), headers: { 'content-type': 'application/json' } });
    const r = await res.json();
    if (typeof r !== 'object' || r === null) return { ok: false, error: 'The server sent back something odd.' };
    if (!('ok' in r)) r.ok = false;
    if (r.challenge && r.sitekey && again && await personCheck(r.sitekey)) return call(op, args, false);
    return r;
  } catch {
    return { ok: false, error: 'Couldn\'t reach the website\'s server. Check your internet.' };
  }
}

// --- signing up and logging in (like PlayerLogin.cpp) ---------------------------

export async function signUp(username, password) {
  const salt = randomHex(16);
  const keys = await passwordKeys(password, salt);
  if (!keys) return { ok: false, error: 'Something went wrong. Try again.' };
  const blob = await lockKey(keys.lock, await loadKey());
  const r = await call('account.signup', { username, salt, auth: keys.auth, key: blob });
  if (r.ok) await call('hello', { name: username, grants: [], protocol: 1 });
  return r;
}

// `code`: the emailed code, for accounts with two-step verification (r.needCode asks for it).
export async function logIn(username, password, code = '') {
  const s = await call('account.salt', { username });
  if (!s.ok) return s;
  const keys = await passwordKeys(password, s.salt || '');
  if (!keys) return { ok: false, error: 'Something went wrong. Try again.' };
  const r = await call('account.login', code ? { username, auth: keys.auth, code } : { username, auth: keys.auth });
  if (!r.ok) return r;
  const key = await unlockKey(keys.lock, r.key);
  if (!key || hex(key.slice(32, 64)) !== String(r.keyAccount || r.account).toLowerCase())
    return { ok: false, error: 'Couldn\'t unlock your account in this browser.' };
  await setKey(key);
  return r;
}

// Proof that you know your password (for changing your email or two-step settings).
export async function passwordProof(username, password) {
  const s = await call('account.salt', { username });
  if (!s.ok) return '';
  const keys = await passwordKeys(password, s.salt || '');
  return keys ? keys.auth : '';
}

// Change your password: this browser has the key, so it locks it with the new one.
export async function changePassword(username, current, next) {
  const auth = await passwordProof(username, current);
  if (!auth) return { ok: false, error: 'Something went wrong. Try again.' };
  const salt = randomHex(16);
  const keys = await passwordKeys(next, salt);
  if (!keys) return { ok: false, error: 'Something went wrong. Try again.' };
  return call('account.password', { auth, salt, newAuth: keys.auth, key: await lockKey(keys.lock, await loadKey()) });
}

// Forgot password: with the emailed code, this browser's key takes over the account.
export async function resetPassword(username, code, password) {
  const salt = randomHex(16);
  const keys = await passwordKeys(password, salt);
  if (!keys) return { ok: false, error: 'Something went wrong. Try again.' };
  return call('account.reset', { username, code, salt, auth: keys.auth, key: await lockKey(keys.lock, await loadKey()) });
}

// Files for uploads, as base64.
export async function fileBase64(file) {
  const bytes = new Uint8Array(await file.arrayBuffer());
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

export function base64Bytes(b64) {
  const s = atob(b64 || '');
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
  return out;
}

// Sign a message with this browser's account key (hex), like Account::sign.
// Staff use it to give badges: the server checks it against their account.
export async function sign(message) {
  return hex(await signBytes(await loadKey(), enc.encode(message)));
}
