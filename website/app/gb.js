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
let secret = null;

function store(get, value) {
  try {
    if (get) return localStorage.getItem(KEY);
    if (value === null) localStorage.removeItem(KEY); else localStorage.setItem(KEY, value);
  } catch { /* private mode: the key only lasts this visit */ }
  return null;
}

async function loadKey() {
  if (secret) return secret;
  const saved = unhex(store(true) || '');
  if (saved && saved.length === 64) { secret = saved; return secret; }
  secret = (await keyPair(random(32))).secret;
  store(false, hex(secret));
  return secret;
}

export async function accountId() { return hex((await loadKey()).slice(32, 64)); }

async function setKey(newSecret) {
  secret = newSecret;
  store(false, hex(secret));
}

export async function forgetKey() {   // log out: this browser becomes a new visitor
  secret = (await keyPair(random(32))).secret;
  store(false, hex(secret));
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

export async function call(op, args = {}, again = true) {
  await checkClock();
  const key = await loadKey();
  const account = hex(key.slice(32, 64));
  const time = Math.floor(Date.now() / 1000) + clockOffset;
  const nonce = randomHex(8);
  const text = 'gb-req:' + op + '\n' + account + '\n' + time + '\n' + nonce + '\n' + canon(args);
  const sig = hex(await signBytes(key, enc.encode(text)));
  const body = '{"op":' + JSON.stringify(op) + ',"account":"' + account + '","time":' + time +
               ',"nonce":"' + nonce + '","args":' + canon(args) + ',"sig":"' + sig + '"}';
  try {
    const res = await fetch('/api', { method: 'POST', body, headers: { 'content-type': 'application/json' } });
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
