// Downloads: the Player, Studio and the Android app, served from Cloudflare R2
// (the DOWNLOADS bucket), so they come from gutsandbolts.net and nowhere else.
//
//   GET  /download/latest.json         the newest version and its files (the apps' update check)
//   GET  /download/<file>              the newest copy of that file
//   PUT  /download/upload/<file>?version=X   (the release workflow, with its GitHub token)
//   POST /download/publish             (the release workflow) {version, name, notes, files}
const FILE = /^[A-Za-z0-9][A-Za-z0-9._-]{0,80}$/;

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
  if (!(ref === 'refs/heads/main' || /^refs\\/tags\\/v[0-9]/.test(ref))) return false;
  return claims.workflow_ref === RELEASE_WORKFLOW + '@' + ref;
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

async function allowed(request) {
  const got = (request.headers.get('authorization') || '').replace(/^Bearer\s+/i, '');
  if (!got) return false;
  try { return !!(await verifyGithubToken(got)); } catch { return false; }
}

export async function handleDownload(request, env, url) {
  if (!env.DOWNLOADS) return json({ ok: false, error: 'Downloads aren\'t set up yet.' }, 503);
  const rest = url.pathname.slice('/download/'.length);
  if (rest.startsWith('upload/') || rest === 'publish') {
    if (!(await allowed(request))) return json({ ok: false, error: 'Not allowed.' }, 403);
    if (rest === 'publish' && request.method === 'POST') {
      let m;
      try { m = await request.json(); } catch { return json({ ok: false, error: 'Bad manifest.' }, 400); }
      if (!m || typeof m.version !== 'string' || !m.files || typeof m.files !== 'object') return json({ ok: false, error: 'Bad manifest.' }, 400);
      const manifest = { version: m.version.replace(/^v/, ''), name: String(m.name || ''), notes: String(m.notes || '').slice(0, 20000),
        date: Math.floor(Date.now() / 1000), files: m.files };
      await env.DOWNLOADS.put('latest.json', JSON.stringify(manifest), { httpMetadata: { contentType: 'application/json' } });
      return json({ ok: true, manifest });
    }
    const name = rest.slice('upload/'.length), version = (url.searchParams.get('version') || '').replace(/^v/, '');
    if (request.method !== 'PUT' || !FILE.test(name) || !/^[0-9][0-9A-Za-z.-]{0,30}$/.test(version)) return json({ ok: false, error: 'Bad upload.' }, 400);
    const body = await request.arrayBuffer();
    if (!body.byteLength) return json({ ok: false, error: 'Empty file.' }, 400);
    const meta = { httpMetadata: { contentType: 'application/octet-stream', contentDisposition: 'attachment; filename="' + name + '"' } };
    await env.DOWNLOADS.put(version + '/' + name, body, meta);
    await env.DOWNLOADS.put('latest/' + name, body, meta);
    return json({ ok: true, size: body.byteLength });
  }
  if (request.method !== 'GET' && request.method !== 'HEAD') return json({ ok: false, error: 'Use GET.' }, 405);
  if (rest === 'latest.json') {
    const obj = await env.DOWNLOADS.get('latest.json');
    if (!obj) return json({ ok: false, error: 'No release yet.' }, 404);
    return new Response(obj.body, { headers: { 'content-type': 'application/json', 'cache-control': 'public, max-age=60' } });
  }
  if (!FILE.test(rest)) return new Response('Not found.', { status: 404 });
  const obj = await env.DOWNLOADS.get('latest/' + rest);
  if (!obj) return new Response('That download isn\'t there yet.', { status: 404 });
  const headers = new Headers();
  obj.writeHttpMetadata(headers);
  headers.set('etag', obj.httpEtag);
  headers.set('content-length', String(obj.size));
  headers.set('cache-control', 'public, max-age=300');
  return new Response(request.method === 'HEAD' ? null : obj.body, { headers });
}
