// Share links: a creator makes a link to their game, item, profile or group to post somewhere else
// (a video, a Discord server, a forum), and sees what each link did: how many clicks, how many
// different people, and how many of them then played, bought, favourited, followed or joined.
// Links never run out; removing one leaves the thing itself alone. Free, like everything else.
// (src/server/ServerShareLinks.cpp has the same rules.)
//
// The link is https://gutsandbolts.net/s/<code> (worker/index.js sends it on to the website's #/s/<code>).
//
// How the counting works:
//   - share.open: someone opened the link. Their browser has a random visitor id (kept in it), so the
//     same person opening it twice is one "different person". If they're signed in we note what they had
//     already done (played it before, already owned it...) so that doesn't count as the link's work.
//   - share.done: the website asks this now and then for links the person opened in the last 7 days.
//     We look at what's true now (they played the game, own the item, favourited it, follow the person,
//     joined the group) and count each thing once per person per link. A purchase also adds its price.
//
// Requests:
//   share.create {kind, id, label}  -> {link}        the thing's owner; kind: game | item | profile | group
//   share.list   {kind, id}         -> {links, most} the owner (or staff): each link's totals and its last 90 days
//   share.remove {code}             -> {}            the owner of the link's thing (or staff)
//   share.open   {code, visitor}    -> {kind, id, path, name}   anyone, even without an account
//   share.done   {code}             -> {counted: [...]}         signed up
export const kMostLinks = 20, kKeepDays = 90, kLabel = 40;
export const SHARE_COSTLY = { 'share.create': [30, 90, 3600], 'share.open': [120, 240, 600], 'share.done': [120, 360, 600] };
export const SHARE_GUEST_OK = ['share.open'];
const KINDS = ['game', 'item', 'profile', 'group'];
const LIBRARY_KINDS = ['model', 'decal', 'audio', 'plugin', 'mesh', 'animation', 'video'];
const WHAT = { game: ['play', 'favorite'], item: ['buy', 'favorite'], profile: ['follow'], group: ['join'] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const dayNum = () => Math.floor(Date.now() / 86400000);
const CODE_CHARS = 'abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789';

export function installShareLinks(Cls) {
  const P = Cls.prototype;

  P.shareInit = function () {
    if (this.shareReady) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS sharelinks (code TEXT PRIMARY KEY, thing TEXT NOT NULL, data TEXT NOT NULL)');
    this.sql.exec('CREATE INDEX IF NOT EXISTS sharelinks_thing ON sharelinks (thing)');
    this.sql.exec('CREATE TABLE IF NOT EXISTS shareseen (code TEXT NOT NULL, who TEXT NOT NULL, what TEXT NOT NULL, PRIMARY KEY (code, who, what))');
    this.shareReady = true;
  };
  P.shareGet = function (code) {
    this.shareInit();
    const r = [...this.sql.exec('SELECT data FROM sharelinks WHERE code = ?', String(code || '').slice(0, 16))][0];
    return r ? JSON.parse(r.data) : null;
  };
  P.shareSave = function (l) {
    const old = dayNum() - kKeepDays;
    for (const d of Object.keys(l.days)) if (Number(d) <= old) delete l.days[d];
    this.sql.exec('INSERT OR REPLACE INTO sharelinks (code, thing, data) VALUES (?, ?, ?)', l.code, l.kind + ':' + l.id, JSON.stringify(l));
  };
  // Seen before? (and remember it now)
  P.shareFirst = function (code, who, what) {
    const had = [...this.sql.exec('SELECT 1 AS x FROM shareseen WHERE code = ? AND who = ? AND what = ?', code, who, what)].length > 0;
    if (!had) this.sql.exec('INSERT INTO shareseen (code, who, what) VALUES (?, ?, ?)', code, who, what);
    return !had;
  };

  // The thing a link points at: {name, path, owner (account id or null), price}, or null if it's gone.
  P.shareThing = function (kind, id) {
    if (kind === 'game' || kind === 'item') {
      const a = this.assets.get(this.assets.resolve(String(id || '')));
      if (!a || (kind === 'game') !== (a.kind === 'game')) return null;
      const page = kind === 'game' ? 'game' : LIBRARY_KINDS.includes(a.kind) ? 'library' : 'item';   // (models and decals live in the Library)
      return { id: a.id, name: a.name, path: '#/' + page + '/' + a.id, owner: a.creator, price: a.price || 0, asset: a };
    }
    if (kind === 'profile') {
      const u = this.findPerson(id);
      return u && u.userId > 0 ? { id: u.id, name: u.name, path: '#/user/' + u.userId, owner: u.id, user: u } : null;
    }
    if (kind === 'group') {
      const g = this.groups.get(String(id || ''));
      return g ? { id: g.id, name: g.name, path: '#/group/' + g.id, owner: g.owner, group: g } : null;
    }
    return null;
  };
  // Is `what` true for `me` now?
  P.shareIsDone = function (t, kind, what, me) {
    if (what === 'play') return (me.played || []).includes(t.id);
    if (what === 'buy') return (me.owned || []).includes(t.id);
    if (what === 'favorite') return (kind === 'game' ? me.favorites || [] : me.itemFavorites || []).includes(t.id);
    if (what === 'follow') return (me.following || []).includes(t.id) || (me.friends || []).includes(t.id);
    if (what === 'join') return !!(t.group && t.group.members[me.id]);
    return false;
  };
  P.shareDay = function (l) {
    const d = String(dayNum());
    return l.days[d] || (l.days[d] = { clicks: 0, people: 0, play: 0, buy: 0, favorite: 0, follow: 0, join: 0, spent: 0 });
  };
  P.shareJson = function (l) {
    const total = { clicks: 0, people: 0, play: 0, buy: 0, favorite: 0, follow: 0, join: 0, spent: 0 };
    for (const d of Object.values(l.days)) for (const k of Object.keys(total)) total[k] += d[k] || 0;
    const did = WHAT[l.kind].reduce((s, w) => Math.max(s, total[w]), 0);
    return { code: l.code, label: l.label, kind: l.kind, id: l.id, created: l.created, path: '/s/' + l.code, total,
      conversion: total.people > 0 ? Math.round((total[WHAT[l.kind][0]] / total.people) * 1000) / 10 : 0, did,
      days: Object.keys(l.days).sort().map((d) => Object.assign({ day: Number(d) }, l.days[d])) };
  };

  P.shareOp = function (name, me, args) {
    this.shareInit();
    if (name === 'share.open') {
      const l = this.shareGet(str(args, 'code'));
      const t = l && this.shareThing(l.kind, l.id);
      if (!t) return fail('That link doesn\'t go anywhere any more.');
      const visitor = str(args, 'visitor').replace(/[^A-Za-z0-9]/g, '').slice(0, 40) || 'anon';
      const d = this.shareDay(l);
      d.clicks++;
      if (this.shareFirst(l.code, 'v:' + (me.userId > 0 ? me.id : visitor), 'open')) d.people++;
      // Already done before the click: not the link's doing.
      if (me.userId > 0) for (const w of WHAT[l.kind]) if (this.shareIsDone(t, l.kind, w, me)) this.shareFirst(l.code, me.id, w);
      this.shareSave(l);
      return okay({ kind: l.kind, id: t.id, path: t.path, name: t.name });
    }
    if (name === 'share.done') {
      if (me.userId === 0) return okay({ counted: [] });
      const l = this.shareGet(str(args, 'code'));
      const t = l && this.shareThing(l.kind, l.id);
      if (!t) return okay({ counted: [] });
      if (t.owner === me.id) return okay({ counted: [] });   // (your own clicks don't count)
      const counted = [];
      for (const w of WHAT[l.kind]) {
        if (!this.shareIsDone(t, l.kind, w, me) || !this.shareFirst(l.code, me.id, w)) continue;
        const d = this.shareDay(l);
        d[w]++;
        if (w === 'buy') d.spent += t.price;
        counted.push(w);
      }
      if (counted.length) this.shareSave(l);
      return okay({ counted });
    }
    if (me.userId === 0) return fail('Sign up to make share links.');
    if (name === 'share.remove') {
      const l = this.shareGet(str(args, 'code'));
      if (!l) return fail('That link isn\'t there.');
      const t = this.shareThing(l.kind, l.id);
      if (!this.isStaff(me) && l.by !== me.id && !(t && t.owner === me.id)) return fail('Only its owner can remove that link.');
      this.sql.exec('DELETE FROM sharelinks WHERE code = ?', l.code);
      this.sql.exec('DELETE FROM shareseen WHERE code = ?', l.code);
      return okay({});
    }
    const kind = str(args, 'kind');
    if (!KINDS.includes(kind)) return fail('Share links are for games, items, profiles and groups.');
    const t = this.shareThing(kind, kind === 'profile' && !args.id ? me.id : args.id);
    if (!t) return fail('That isn\'t there.');
    const mine = t.owner === me.id;
    if (!mine && !(this.isStaff(me) && name === 'share.list')) return fail(kind === 'profile' ? 'You can only make links to your own profile.' : 'Only its owner can make share links for it.');
    const links = [...this.sql.exec('SELECT data FROM sharelinks WHERE thing = ?', kind + ':' + t.id)].map((r) => JSON.parse(r.data));
    if (name === 'share.list')
      return okay({ links: links.sort((a, b) => b.created - a.created).map((l) => this.shareJson(l)), most: kMostLinks });
    if (name === 'share.create') {
      if (kind === 'game' && t.asset.access === 'private') return fail('Make the game public (or friends-only) first: nobody could open it.');
      if (links.length >= kMostLinks) return fail('A thing can have ' + kMostLinks + ' links. Remove an old one first.');
      const label = String(args.label || '').replace(/[\u0000-\u001f<>]/g, '').trim().slice(0, kLabel) || 'Link ' + (links.length + 1);
      let code = '';
      do {
        const bytes = crypto.getRandomValues(new Uint8Array(8));
        code = [...bytes].map((b) => CODE_CHARS[b % CODE_CHARS.length]).join('');
      } while (this.shareGet(code));
      const l = { code, kind, id: t.id, by: me.id, label, created: now(), days: {} };
      this.shareSave(l);
      return okay({ link: this.shareJson(l) });
    }
    return fail('Unknown request.');
  };
}
