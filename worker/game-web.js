// Games talking to the web: HttpService:GetAsync / PostAsync / RequestAsync, and the secrets
// (API keys) a game uses for them. Made so games can bridge to Lyreto (lyreto.com), but safe for
// anything on the list below.
//
// Nothing goes straight from the host's computer to a website. A game's server script asks us
// (op http.request) and we make the request, so:
//   * only sites on WEB_HOSTS, only https, no redirects followed;
//   * only games whose creator turned on "Web requests" (game.settings { http: true });
//   * only someone running a live server of the game (relay.host), or its creator (Studio tests);
//   * secrets: the creator saves them on the website (game.secretSet); a script only names one
//     (HttpService:GetSecret("LyretoKey")) and we put the real value into the header right here.
//     The host never sees it, the website never shows it again, and each secret only goes to the
//     site it was saved for. If a site echoes it back, we blank it out of the answer.
//     A secret can also be "Guts&Bolts game servers only" (not when a player hosts).
//   * every request says which game and which kind of server sent it (X-GutsAndBolts-* headers,
//     which scripts can't set), so the site can trust them when the secret matches.
//
// The C++ server (src/server) keeps the same settings and secrets, but can't make web requests.

const fail = (error) => ({ ok: false, error });
const okay = (extra) => Object.assign({ ok: true }, extra || {});
const now = () => Math.floor(Date.now() / 1000);

// Sites games may reach (this host or anything under it). Add one only after asking its owners.
export const WEB_HOSTS = ['lyreto.com'];
export const kWebMethods = ['GET', 'POST', 'PUT', 'PATCH', 'DELETE'];
export const kWebUrl = 2048, kWebBody = 64 * 1024, kWebAnswer = 1024 * 1024, kWebHeaders = 30, kWebHeaderValue = 4096;
export const kWebPerMinute = 120;          // per server (host account + game)
export const kWebGamePerMinute = 500;      // per game, every server together
export const kWebTimeoutMs = 10000;
export const kSecretsPerGame = 20, kSecretValue = 2048;
export const kSecretName = /^[A-Za-z0-9_]{1,50}$/;
// Headers scripts can't send (we set some of them ourselves, the rest would confuse the site or Cloudflare).
const kBlockedHeaders = /^(host|content-length|connection|keep-alive|transfer-encoding|upgrade|te|trailer|proxy-.*|cookie|forwarded|via|x-forwarded-.*|x-real-ip|cf-.*|x-gutsandbolts-.*|x-gb-.*)$/i;

// A site on the list, or the reason it isn't (`url`: the cleaned-up URL).
export function webUrl(raw) {
  const s = typeof raw === 'string' ? raw.trim() : '';
  if (!s) return { error: 'the Url is empty' };
  if (s.length > kWebUrl) return { error: 'that Url is too long (' + kWebUrl + ' letters at most)' };
  let u;
  try { u = new URL(s); } catch { return { error: 'that isn\'t a web address (it should start with https://)' }; }
  if (u.protocol !== 'https:') return { error: 'only https:// addresses work' };
  if (u.username || u.password) return { error: 'addresses with a name or password in them don\'t work' };
  if (u.port && u.port !== '443') return { error: 'addresses with a port number don\'t work' };
  const host = u.hostname.toLowerCase();
  if (!WEB_HOSTS.some((h) => host === h || host.endsWith('.' + h)))
    return { error: host + ' isn\'t on Guts&Bolts\' list of sites games can reach (' + WEB_HOSTS.join(', ') + ')' };
  return { url: u.toString(), host };
}
// Does a secret saved for `secretHost` go to `host`? (The same site, or one under it.)
const secretFits = (secretHost, host) => host === secretHost || host.endsWith('.' + secretHost);

export function installGameWeb(Cls) {
  const P = Cls.prototype;

  P.secretsInit = function () {
    this.sql.exec(`CREATE TABLE IF NOT EXISTS gamesecrets (game TEXT NOT NULL, name TEXT NOT NULL, value TEXT NOT NULL,
      host TEXT NOT NULL, dedicated INTEGER NOT NULL, updated INTEGER NOT NULL, PRIMARY KEY (game, name))`);
  };
  P.gameSecret = function (game, name) {
    return [...this.sql.exec('SELECT value, host, dedicated FROM gamesecrets WHERE game = ? AND name = ?', game, name)][0] || null;
  };
  // What the website shows: names and where they go, never the values.
  P.secretList = function (game) {
    return [...this.sql.exec('SELECT name, host, dedicated, updated FROM gamesecrets WHERE game = ? ORDER BY name', game)]
      .map((r) => ({ name: r.name, host: r.host, dedicatedOnly: !!r.dedicated, updated: r.updated }));
  };

  // game.secrets {id} / game.secretSet {id, name, value, host, dedicatedOnly} / game.secretRemove {id, name}
  // Only the game's creator. Values go in and never come back out.
  P.secretOp = function (name, me, args) {
    const a = this.assets.get(typeof args.id === 'string' ? args.id : '');
    if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
    if (a.creator !== me.id) return fail('Only the game\'s creator can see or change its secrets.');
    const list = () => okay({ http: !!a.httpOn, secrets: this.secretList(a.id), hosts: WEB_HOSTS });
    if (name === 'game.secrets') return list();
    const key = typeof args.name === 'string' ? args.name.trim() : '';
    if (!kSecretName.test(key)) return fail('Secret names are 1-50 letters, numbers and _ (like LyretoKey).');
    if (name === 'game.secretRemove') {
      this.sql.exec('DELETE FROM gamesecrets WHERE game = ? AND name = ?', a.id, key);
      return list();
    }
    if (name === 'game.secretSet') {
      const value = typeof args.value === 'string' ? args.value : '';
      if (!value) return fail('Paste the secret\'s value.');
      if (value.length < 8) return fail('That secret is too short (8 letters at least): real API keys are longer.');
      if (value.length > kSecretValue) return fail('That secret is too long (' + kSecretValue + ' letters at most).');
      if (/[\r\n\0]/.test(value)) return fail('A secret can\'t have line breaks in it.');
      const host = (typeof args.host === 'string' ? args.host : '').trim().toLowerCase();
      if (!WEB_HOSTS.includes(host)) return fail('Pick the site this secret is for (' + WEB_HOSTS.join(', ') + ').');
      const count = [...this.sql.exec('SELECT COUNT(*) AS n FROM gamesecrets WHERE game = ? AND name <> ?', a.id, key)][0].n;
      if (count >= kSecretsPerGame) return fail('A game can have ' + kSecretsPerGame + ' secrets. Remove one first.');
      this.sql.exec('INSERT OR REPLACE INTO gamesecrets (game, name, value, host, dedicated, updated) VALUES (?, ?, ?, ?, ?, ?)',
        a.id, key, value, host, args.dedicatedOnly === true ? 1 : 0, now());
      return list();
    }
    return fail('Unknown request.');
  };

  // http.request {game, url, method, headers, body}: checks everything, then the request itself
  // happens after this answer (webFetch). The finished request, secrets and all, rides along in
  // a hidden property so it can never be sent back by mistake.
  P.webOp = function (name, me, args) {
    if (name !== 'http.request') return fail('Unknown request.');
    const game = this.assets.resolve(typeof args.game === 'string' ? args.game.slice(0, 80) : '');
    const a = this.assets.get(game);
    if (!a || a.kind !== 'game') return fail('that game isn\'t there');
    if (!a.httpOn) return fail('web requests are off for this game. Its creator can turn them on in the game\'s Settings on the website.');
    // Who's asking: a live server of the game (the host's account), or the creator testing in Studio.
    const mine = [...this.sessions.values()].filter((s) => s.host === me.id && this.assets.resolve(s.game) === game);
    const dedicated = mine.some((s) => s.dedicated);
    if (!mine.length && a.creator !== me.id) return fail('only a running server of this game can make web requests');
    const kind = dedicated ? 'dedicated' : mine.length ? 'player' : 'studio';

    const where = webUrl(args.url);
    if (where.error) return fail(where.error);
    const method = typeof args.method === 'string' ? args.method.toUpperCase() : 'GET';
    if (!kWebMethods.includes(method)) return fail('Method must be one of ' + kWebMethods.join(', '));
    let body;
    if (args.body !== undefined && args.body !== null && args.body !== '') {
      if (typeof args.body !== 'string') return fail('Body must be text');
      if (method === 'GET') return fail('a GET request can\'t have a Body (use POST)');
      if (new TextEncoder().encode(args.body).length > kWebBody) return fail('the Body is too big (64 KB at most)');
      body = args.body;
    }
    const raw = args.headers && typeof args.headers === 'object' && !Array.isArray(args.headers) ? args.headers : {};
    const names = Object.keys(raw);
    if (names.length > kWebHeaders) return fail('too many headers (' + kWebHeaders + ' at most)');
    const headers = {}, hidden = [];
    for (const k of names) {
      if (!/^[A-Za-z0-9!#$%&'*+.^_`|~-]{1,64}$/.test(k)) return fail('"' + k.slice(0, 64) + '" isn\'t a header name');
      if (kBlockedHeaders.test(k)) return fail('games can\'t set the ' + k + ' header');
      let v = raw[k];
      if (v && typeof v === 'object' && typeof v.secret === 'string') {
        const s = kSecretName.test(v.secret) ? this.gameSecret(a.id, v.secret) : null;
        if (!s) return fail('this game has no secret called "' + String(v.secret).slice(0, 50) + '" (add it in the game\'s Settings on the website)');
        if (!secretFits(s.host, where.host)) return fail('the secret "' + v.secret + '" only goes to ' + s.host);
        if (s.dedicated && !dedicated) return fail('the secret "' + v.secret + '" only works on Guts&Bolts game servers, not when a player hosts');
        const prefix = typeof v.prefix === 'string' ? v.prefix : '', suffix = typeof v.suffix === 'string' ? v.suffix : '';
        v = prefix + s.value + suffix;
        hidden.push(s.value);
      } else if (typeof v === 'number') v = String(v);
      if (typeof v !== 'string') return fail('header ' + k + ' must be text or a Secret');
      if (v.length > kWebHeaderValue || /[\r\n\0]/.test(v)) return fail('header ' + k + ' is too long or has a line break');
      headers[k] = v;
    }
    // Limits: per server, and per game.
    const t = Date.now() / 1000;
    if (!this.webSent) this.webSent = new Map();
    if (this.webSent.size > 2000)   // (forget servers that went quiet)
      for (const [k, times] of this.webSent) if (!times.length || times[times.length - 1] <= t - 60) this.webSent.delete(k);
    const count = (key, most) => {
      const times = (this.webSent.get(key) || []).filter((x) => x > t - 60);
      if (times.length >= most) return false;
      times.push(t);
      this.webSent.set(key, times);
      return true;
    };
    if (!count('s\n' + me.id + '\n' + game, kWebPerMinute)) return fail('too many web requests (' + kWebPerMinute + ' a minute is the most for one server). Wait a moment.');
    if (!count('g\n' + game, kWebGamePerMinute)) return fail('too many web requests from this game\'s servers right now (' + kWebGamePerMinute + ' a minute). Wait a moment.');

    headers['User-Agent'] = 'GutsAndBolts-HttpService/1.0';
    headers['X-GutsAndBolts-Game'] = String(a.num || a.id);
    headers['X-GutsAndBolts-Server'] = kind;          // dedicated (a Guts&Bolts game server), player (someone hosting) or studio
    headers['X-GutsAndBolts-Host'] = String(me.userId || 0);   // the account running it (0 = a guest)
    const answer = okay();
    Object.defineProperty(answer, 'webJob', { value: { url: where.url, method, headers, body, hidden }, enumerable: false });
    return answer;
  };

  // The second half of http.request: make the request and hand back what the site said.
  P.webFetch = async function (answer) {
    const job = answer.webJob;
    const scrub = (s) => { let out = s; for (const v of job.hidden) if (v) out = out.split(v).join('[secret]'); return out; };
    let res;
    try {
      res = await fetch(job.url, { method: job.method, headers: job.headers, body: job.body, redirect: 'manual',
        signal: AbortSignal.timeout(kWebTimeoutMs) });
    } catch (e) {
      return fail(e && e.name === 'TimeoutError' ? 'the site didn\'t answer in time (10 seconds)' : 'couldn\'t reach the site');
    }
    let bytes = new Uint8Array(0);
    if (res.body) {
      const reader = res.body.getReader();
      const parts = [];
      let total = 0;
      try {
        for (;;) {
          const { value, done } = await reader.read();
          if (done) break;
          total += value.byteLength;
          if (total > kWebAnswer) { try { await reader.cancel(); } catch { /* fine */ } return fail('the site\'s answer was bigger than 1 MB'); }
          parts.push(value);
        }
      } catch { return fail('the site stopped answering halfway'); }
      bytes = new Uint8Array(total);
      let at = 0;
      for (const p of parts) { bytes.set(p, at); at += p.byteLength; }
    }
    const headers = {};
    for (const [k, v] of res.headers) if (k !== 'set-cookie') headers[k] = scrub(v);
    return okay({ status: res.status, statusText: res.statusText || '', headers, body: scrub(new TextDecoder().decode(bytes)) });
  };
}
