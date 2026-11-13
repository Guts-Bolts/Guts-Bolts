// Downloads: the Player, Studio and the Android app, served from Cloudflare R2
// (the DOWNLOADS bucket), so they come from gutsandbolts.net and nowhere else.
//
//   GET  /download/latest.json         the newest version and its files (the apps' update check)
//   GET  /download/<file>              the newest copy of that file
//   PUT  /download/upload/<file>?version=X   (the release workflow, with its GitHub token)
//   POST /download/publish             (the release workflow) {version, name, notes, files}
//
// Nightly test builds (the Player's opt-in "Nightly" channel) live apart from the
// releases, so a test build can never replace or block the normal download:
//   GET  /download/nightly.json                 the newest main build {version, commit, commitTime, date, files}
//   GET  /download/nightly/<commit>/<file>      its files
//   PUT  /download/upload/<file>?channel=nightly&commit=<sha>    (build.yml's nightly job, main only)
//   POST /download/publish?channel=nightly      {version, commit, commitTime, files}
// Only the newest two nightly builds are kept (older ones are deleted when a new one
// comes out), so they take a fixed, small amount of the free R2 space.
import { readLimitedText } from './http.js';

const FILE = /^[A-Za-z0-9][A-Za-z0-9._-]{0,80}$/;
const MAX_RELEASE_FILE = 90 * 1024 * 1024;
const MAX_MANIFEST = 64 * 1024;

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), { status, headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });
}

// Who may upload: the release workflow of this repository, building main or a version tag.
// GitHub hands each workflow run a short-lived token it signs (OpenID Connect); we check
// GitHub's signature and what the token says, so no password has to be stored anywhere.
const REPO = 'Guts-Bolts/Guts-Bolts';
const ISSUER = 'https://token.actions.githubusercontent.com';
const AUDIENCE = 'gutsandbolts.net';
const REPOSITORY_ID = '1266919304';
const RELEASE_WORKFLOW = REPO + '/.github/workflows/build.yml';

// Only the repository's designated release workflow may publish downloads. A valid
// GitHub OIDC signature alone is not enough: other workflows can also request tokens.
export function isTrustedReleaseClaims(claims) {
  if (!claims || claims.repository !== REPO || claims.repository_id !== REPOSITORY_ID || claims.event_name !== 'push') return false;
  const ref = claims.ref || '';
  const versionTag = ref.startsWith('refs/tags/v') && /^[0-9]/.test(ref.slice('refs/tags/v'.length));
  if (!(ref === 'refs/heads/main' || versionTag)) return false;
  return claims.workflow_ref === RELEASE_WORKFLOW + '@' + ref;
}
export function limitUploadStream(stream, maxBytes) {
  if (!stream) throw new TypeError('A release upload body is required.');
  const reader = stream.getReader();
  let total = 0;
  return new ReadableStream({
    async pull(controller) {
      try {
        const { value, done } = await reader.read();
        if (done) {
          reader.releaseLock();
          if (!total) { const error = new Error('Release upload is empty.'); error.code = 'EMPTY_UPLOAD'; controller.error(error); return; }
          controller.close();
          return;
        }
        total += value.byteLength;
        if (total > maxBytes) {
          await reader.cancel('Release upload exceeds the configured limit.');
          controller.error(new RangeError('Release upload is too large.'));
          return;
        }
        controller.enqueue(value);
      } catch (error) {
        controller.error(error);
      }
    },
    async cancel(reason) { await reader.cancel(reason); },
  });
}

// R2 only takes a stream when it knows how many bytes are coming. A stream we made
// ourselves (like the size-checking one above) doesn't say, so R2 threw and every
// release upload failed with a 500. FixedLengthStream tells R2 the size up front.
export function knownLength(stream, length) {
  if (typeof FixedLengthStream !== 'function') return stream;   // not on Cloudflare (tests)
  const fixed = new FixedLengthStream(length);
  stream.pipeTo(fixed.writable).catch(() => {});   // a failed pipe makes the put fail too
  return fixed.readable;
}

export function normalizeReleaseManifest(m) {
  if (!m || typeof m !== 'object' || Array.isArray(m) ||
      typeof m.version !== 'string' || typeof m.files !== 'object' || !m.files || Array.isArray(m.files)) return null;
  const version = m.version.replace(/^v/, '');
  if (!/^[0-9][0-9A-Za-z.-]{0,30}$/.test(version)) return null;
  const allowed = new Set(['android', 'quest', 'windows', 'mac', 'linux']);
  const files = {};
  for (const [kind, name] of Object.entries(m.files)) {
    if (!allowed.has(kind) || typeof name !== 'string' || !FILE.test(name)) return null;
    files[kind] = name;
  }
  if (!Object.keys(files).length || Object.keys(files).length > allowed.size) return null;
  if (m.name !== undefined && (typeof m.name !== 'string' || m.name.length > 160)) return null;
  if (m.notes !== undefined && (typeof m.notes !== 'string' || m.notes.length > 20000)) return null;
  if (m.commit !== undefined && (typeof m.commit !== 'string' || !COMMIT.test(m.commit))) return null;
  const out = { version, name: m.name || '', notes: m.notes || '', files };
  if (m.commit) out.commit = m.commit;   // lets the Player tell a release from a nightly of the same version
  return out;
}

// ---- Nightly test builds ----
const COMMIT = /^[0-9a-f]{40}$/;
const NIGHTLY_KINDS = new Set(['windows', 'mac', 'linux']);   // phones update from the stores/APK instead
export const NIGHTLY_KEEP = 2;
export const nightlyKey = (commit, name) => 'nightly/' + commit + '/' + name;

export function normalizeNightlyManifest(m) {
  if (!m || typeof m !== 'object' || Array.isArray(m) || typeof m.version !== 'string' ||
      typeof m.commit !== 'string' || !COMMIT.test(m.commit) ||
      !Number.isSafeInteger(m.commitTime) || m.commitTime <= 0 ||
      typeof m.files !== 'object' || !m.files || Array.isArray(m.files)) return null;
  const version = m.version.replace(/^v/, '');
  if (!/^[0-9][0-9A-Za-z.-]{0,30}$/.test(version)) return null;
  const files = {};
  for (const [kind, name] of Object.entries(m.files)) {
    if (!NIGHTLY_KINDS.has(kind) || typeof name !== 'string' || !FILE.test(name)) return null;
    files[kind] = name;
  }
  if (!Object.keys(files).length) return null;
  const date = new Date(m.commitTime * 1000).toISOString().slice(0, 10);
  return { version, commit: m.commit, commitTime: m.commitTime, date, files };
}

// Publishing a nightly: every file must already be uploaded, and an older build
// (a slow run finishing after a newer one) never replaces a newer one.
export async function publishNightly(env, safe) {
  for (const name of Object.values(safe.files)) {
    if (!(await env.DOWNLOADS.head(nightlyKey(safe.commit, name)))) return json({ ok: false, error: 'Missing file: ' + name }, 400);
  }
  let prev = null;
  const old = await env.DOWNLOADS.get('nightly.json');
  if (old) { try { prev = JSON.parse(await old.text()); } catch { prev = null; } }
  if (prev && prev.commit !== safe.commit && Number(prev.commitTime) > safe.commitTime) {
    return json({ ok: false, error: 'A newer nightly is already out.', current: prev.commit }, 409);
  }
  const manifest = { ...safe, channel: 'nightly', published: Math.floor(Date.now() / 1000) };
  await env.DOWNLOADS.put('nightly.json', JSON.stringify(manifest), { httpMetadata: { contentType: 'application/json' } });
  const keep = new Set([safe.commit]);
  if (prev && typeof prev.commit === 'string' && COMMIT.test(prev.commit)) keep.add(prev.commit);
  await pruneNightlies(env, keep);
  return json({ ok: true, manifest });
}

// Deletes every nightly file whose build isn't in `keep`.
export async function pruneNightlies(env, keep) {
  let cursor;
  const gone = [];
  do {
    const page = await env.DOWNLOADS.list({ prefix: 'nightly/', cursor });
    for (const o of page.objects || []) {
      const commit = o.key.split('/')[1];
      if (!keep.has(commit)) gone.push(o.key);
    }
    cursor = page.truncated ? page.cursor : undefined;
  } while (cursor);
  for (let i = 0; i < gone.length; i += 1000) await env.DOWNLOADS.delete(gone.slice(i, i + 1000));
  return gone.length;
}

let jwksCache = null, jwksAt = 0;

const b64url = (s) => Uint8Array.from(atob(s.replace(/-/g, '+').replace(/_/g, '/') + '==='.slice((s.length + 3) % 4)), (c) => c.charCodeAt(0));

async function githubKeys(force) {
  if (!force && jwksCache && Date.now() - jwksAt < 3600 * 1000) return jwksCache;
  const r = await fetch(ISSUER + '/.well-known/jwks');
  if (!r.ok) throw new Error('jwks');
  jwksCache = (await r.json()).keys || [];
  jwksAt = Date.now();
  return jwksCache;
}

async function verifyGithubToken(token) {
  const parts = token.split('.');
  if (parts.length !== 3) return null;
  let header, claims;
  try {
    header = JSON.parse(new TextDecoder().decode(b64url(parts[0])));
    claims = JSON.parse(new TextDecoder().decode(b64url(parts[1])));
  } catch { return null; }
  if (header.alg !== 'RS256' || !header.kid) return null;
  let jwk = (await githubKeys(false)).find((k) => k.kid === header.kid);
  if (!jwk) jwk = (await githubKeys(true)).find((k) => k.kid === header.kid);   // GitHub rotated its keys
  if (!jwk) return null;
  const key = await crypto.subtle.importKey('jwk', { kty: jwk.kty, n: jwk.n, e: jwk.e, alg: 'RS256', ext: true },
    { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' }, false, ['verify']);
  const ok = await crypto.subtle.verify('RSASSA-PKCS1-v1_5', key, b64url(parts[2]), new TextEncoder().encode(parts[0] + '.' + parts[1]));
  if (!ok) return null;
  const now = Math.floor(Date.now() / 1000);
  const aud = Array.isArray(claims.aud) ? claims.aud : [claims.aud];
  if (claims.iss !== ISSUER || !aud.includes(AUDIENCE) || !(claims.exp > now) || (claims.nbf && claims.nbf > now + 60)) return null;
  if (!isTrustedReleaseClaims(claims)) return null;
  return claims;
}

// The checked token's claims, or null. Nightly builds may only come from main.
async function allowed(request) {
  const got = (request.headers.get('authorization') || '').replace(/^Bearer\s+/i, '');
  if (!got) return null;
  try { return await verifyGithubToken(got); } catch { return null; }
}
export const mayPublishNightly = (claims) => !!claims && isTrustedReleaseClaims(claims) && claims.ref === 'refs/heads/main';

// The size of one upload request, which must be sent and fit under the limit.
function uploadLength(request) {
  const header = request.headers.get('content-length');
  if (header === null) return { error: json({ ok: false, error: 'Send the file size (Content-Length).' }, 411) };
  const value = Number(header);
  if (!Number.isSafeInteger(value) || value > MAX_RELEASE_FILE) return { error: json({ ok: false, error: 'Release file is too large.' }, 413) };
  if (!value || !request.body) return { error: json({ ok: false, error: 'Empty file.' }, 400) };
  return { value };
}

// After a version's file is saved, it also becomes the newest copy people download.
async function copyToLatest(env, versionKey, name, meta, toLatest = true) {
  const stored = await env.DOWNLOADS.get(versionKey);
  if (!stored) return json({ ok: false, error: 'The release upload could not be verified.' }, 500);
  if (!toLatest) return json({ ok: true, size: stored.size });   // nightly files stay in nightly/
  await env.DOWNLOADS.put('latest/' + name, knownLength(stored.body, stored.size), meta);
  return json({ ok: true, size: stored.size });
}

// Files bigger than one request may be (Cloudflare stops requests at 100 MB, and the
// Windows download is bigger) go up in pieces with R2's multipart upload:
//   POST upload/<file>/start?version=X                  -> { uploadId }
//   PUT  upload/<file>/part?version=X&uploadId=U&n=1..   (one piece, up to 90 MB) -> { part }
//   POST upload/<file>/finish?version=X&uploadId=U       { parts: [...] } -> { ok, size }
// Every piece but the last must be the same size (split -b does that).
export const MAX_RELEASE_PARTS = 40;
export async function handleMultipartStep(request, env, url, step, name, versionKey, meta, toLatest = true) {
  if (step === 'start' && request.method === 'POST') {
    const upload = await env.DOWNLOADS.createMultipartUpload(versionKey, meta);
    return json({ ok: true, uploadId: upload.uploadId });
  }
  const uploadId = url.searchParams.get('uploadId') || '';
  if (!uploadId || uploadId.length > 1024) return json({ ok: false, error: 'Bad upload.' }, 400);
  const upload = env.DOWNLOADS.resumeMultipartUpload(versionKey, uploadId);
  if (step === 'part' && request.method === 'PUT') {
    const n = Number(url.searchParams.get('n'));
    if (!Number.isInteger(n) || n < 1 || n > MAX_RELEASE_PARTS) return json({ ok: false, error: 'Bad part number.' }, 400);
    const length = uploadLength(request);
    if (length.error) return length.error;
    const part = await upload.uploadPart(n, knownLength(request.body, length.value));
    return json({ ok: true, part: { partNumber: part.partNumber, etag: part.etag } });
  }
  if (step === 'finish' && request.method === 'POST') {
    const text = await readLimitedText(request, MAX_MANIFEST);
    let parts;
    try { parts = JSON.parse(text || '').parts; } catch { parts = null; }
    if (!Array.isArray(parts) || !parts.length || parts.length > MAX_RELEASE_PARTS ||
        !parts.every((p) => p && Number.isInteger(p.partNumber) && typeof p.etag === 'string')) {
      return json({ ok: false, error: 'Bad part list.' }, 400);
    }
    await upload.complete(parts.map((p) => ({ partNumber: p.partNumber, etag: p.etag })));
    return copyToLatest(env, versionKey, name, meta, toLatest);
  }
  return json({ ok: false, error: 'Bad upload.' }, 400);
}

export async function handleDownload(request, env, url) {
  if (!env.DOWNLOADS) return json({ ok: false, error: 'Downloads aren\'t set up yet.' }, 503);
  const rest = url.pathname.slice('/download/'.length);
  if (rest.startsWith('upload/') || rest === 'publish') {
    const claims = await allowed(request);
    if (!claims) return json({ ok: false, error: 'Not allowed.' }, 403);
    const nightly = url.searchParams.get('channel') === 'nightly';
    if (nightly && !mayPublishNightly(claims)) return json({ ok: false, error: 'Nightly builds only come from main.' }, 403);
    if (nightly && rest === 'publish' && request.method === 'POST') {
      const text = await readLimitedText(request, MAX_MANIFEST);
      if (text === null) return json({ ok: false, error: 'Manifest is too large.' }, 413);
      let m;
      try { m = JSON.parse(text); } catch { return json({ ok: false, error: 'Bad manifest.' }, 400); }
      const safe = normalizeNightlyManifest(m);
      if (!safe) return json({ ok: false, error: 'Bad manifest.' }, 400);
      return publishNightly(env, safe);
    }
    if (rest === 'publish' && request.method === 'POST') {
      const text = await readLimitedText(request, MAX_MANIFEST);
      if (text === null) return json({ ok: false, error: 'Manifest is too large.' }, 413);
      let m;
      try { m = JSON.parse(text); } catch { return json({ ok: false, error: 'Bad manifest.' }, 400); }
      const safe = normalizeReleaseManifest(m);
      if (!safe) return json({ ok: false, error: 'Bad manifest.' }, 400);
      const manifest = { ...safe, date: Math.floor(Date.now() / 1000) };
      await env.DOWNLOADS.put('latest.json', JSON.stringify(manifest), { httpMetadata: { contentType: 'application/json' } });
      return json({ ok: true, manifest });
    }
    const [name, step = ''] = rest.slice('upload/'.length).split('/');
    const version = (url.searchParams.get('version') || '').replace(/^v/, '');
    const commit = url.searchParams.get('commit') || '';
    if (!FILE.test(name) || (nightly ? !COMMIT.test(commit) : !/^[0-9][0-9A-Za-z.-]{0,30}$/.test(version))) return json({ ok: false, error: 'Bad upload.' }, 400);
    const meta = { httpMetadata: { contentType: 'application/octet-stream', contentDisposition: 'attachment; filename="' + name + '"' } };
    const versionKey = nightly ? nightlyKey(commit, name) : version + '/' + name;
    if (step) return handleMultipartStep(request, env, url, step, name, versionKey, meta, !nightly);
    if (request.method !== 'PUT') return json({ ok: false, error: 'Bad upload.' }, 400);
    const length = uploadLength(request);
    if (length.error) return length.error;
    try {
      await env.DOWNLOADS.put(versionKey, knownLength(limitUploadStream(request.body, MAX_RELEASE_FILE), length.value), meta);
    } catch (error) {
      if (error instanceof RangeError) return json({ ok: false, error: 'Release file is too large.' }, 413);
      if (error && error.code === 'EMPTY_UPLOAD') return json({ ok: false, error: 'Empty file.' }, 400);
      throw error;
    }
    return copyToLatest(env, versionKey, name, meta, !nightly);
  }
  if (request.method !== 'GET' && request.method !== 'HEAD') return json({ ok: false, error: 'Use GET.' }, 405);
  if (rest === 'latest.json') {
    const obj = await env.DOWNLOADS.get('latest.json');
    if (!obj) return json({ ok: false, error: 'No release yet.' }, 404);
    return new Response(obj.body, { headers: { 'content-type': 'application/json', 'cache-control': 'public, max-age=60' } });
  }
  if (rest === 'nightly.json') {
    const obj = await env.DOWNLOADS.get('nightly.json');
    if (!obj) return json({ ok: false, error: 'No nightly build yet.' }, 404);
    return new Response(obj.body, { headers: { 'content-type': 'application/json', 'cache-control': 'public, max-age=60' } });
  }
  const nightlyFile = /^nightly\/([0-9a-f]{40})\/([^/]+)$/.exec(rest);
  if (nightlyFile && !FILE.test(nightlyFile[2])) return new Response('Not found.', { status: 404 });
  if (!nightlyFile && !FILE.test(rest)) return new Response('Not found.', { status: 404 });
  const obj = await env.DOWNLOADS.get(nightlyFile ? rest : 'latest/' + rest);
  if (!obj) return new Response('That download isn\'t there yet.', { status: 404 });
  const headers = new Headers();
  obj.writeHttpMetadata(headers);
  headers.set('etag', obj.httpEtag);
  headers.set('content-length', String(obj.size));
  headers.set('cache-control', 'public, max-age=300');
  return new Response(request.method === 'HEAD' ? null : obj.body, { headers });
}
