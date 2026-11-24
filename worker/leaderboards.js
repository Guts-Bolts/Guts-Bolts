// Leaderboards: the site-wide top lists on #/leaderboards (src/server/ServerLeaderboards.cpp
// has the same rules). Three lists, each for "this week" (the last 7 days) or "all time":
//
//   - Top creators: points = visits on their public games + faves on their public art.
//     (This week: visits in the last 7 days, and faves on art posted in the last 7 days.)
//   - Most played games: visits (plays) on public games. This week uses the per-day counts
//     that creator stats already keep (a.days["YYYY-MM-DD"].plays, 60 days).
//   - Most faved art: faves on public pieces (this week: pieces posted in the last 7 days,
//     the same as the Art gallery's "Best this week").
//
// Only public things count: no games waiting for a staff check, no private / friends / testers
// games, no art waiting for a check, and nobody who's banned. A creator's own visits never
// counted in the first place (server.js doesn't count a play by the game's creator).
// The lists are worked out from what's already kept, so nothing new is stored. They're
// kept in memory for kCacheSecs so a busy page doesn't add everything up on every visit.
//
//   leaders.get {period: 'week' | 'all'}  -> {period, creators, games, art, at}   (guests too)
//
// uploadCounts(id) is also here: how much someone has posted outside the catalog (art, videos,
// pastes) and how many faves their art has. The profile's achievements use it (extras.js).

export const kTopSize = 10, kCacheSecs = 300, kWeekDays = 7;
export const LEADER_LOOK_ONLY = ['leaders.get'];

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const now = () => Math.floor(Date.now() / 1000);
const dayKey = (t) => new Date(t * 1000).toISOString().slice(0, 10);

// The last `n` days' keys, today first ("YYYY-MM-DD", like server.js tally()).
export function lastDays(t, n = kWeekDays) {
  const out = [];
  for (let i = 0; i < n; i++) out.push(dayKey(t - i * 86400));
  return out;
}
// A game's visits over those days (from its per-day counts).
export function playsIn(a, days) {
  let n = 0;
  for (const d of days) n += (a.days && a.days[d] && a.days[d].plays) || 0;
  return n;
}
const isPublicGame = (a) => a.kind === 'game' && !a.review && (!a.access || a.access === 'public');

export function installLeaderboards(Cls) {
  const P = Cls.prototype;

  P.leaderPerson = function (u) {
    return { id: u.id, userId: u.userId, name: u.name, verified: this.isVerified(u), avatar: u.avatar || null };
  };

  // Everything for one period, freshly added up.
  P.leadersBuild = function (period, t) {
    const week = period === 'week', days = lastDays(t);
    const fine = (id) => { const u = this.users.get(id); return u && u.userId > 0 && !u.banned ? u : null; };
    const points = new Map();   // account id -> {visits, faves}
    const add = (id, field, n) => {
      if (!n) return;
      const p = points.get(id) || { visits: 0, faves: 0 };
      p[field] += n;
      points.set(id, p);
    };
    const games = [];
    for (const a of this.assets.values()) {
      if (!isPublicGame(a) || !fine(a.creator)) continue;
      const plays = week ? playsIn(a, days) : (a.plays || 0);
      if (!plays) continue;
      games.push({ a, plays });
      add(a.creator, 'visits', plays);
    }
    if (this.artInit) this.artInit();
    const art = [];
    for (const p of (this.art ? this.art.values() : [])) {
      if (p.review || !fine(p.by) || (week && p.at <= t - kWeekDays * 86400)) continue;
      const faves = p.faves.length;
      if (!faves) continue;
      art.push({ p, faves });
      add(p.by, 'faves', faves);
    }
    games.sort((x, y) => y.plays - x.plays || (x.a.created || 0) - (y.a.created || 0));
    art.sort((x, y) => y.faves - x.faves || x.p.at - y.p.at);
    const creators = [...points.entries()].map(([id, p]) => ({ u: this.users.get(id), visits: p.visits, faves: p.faves, points: p.visits + p.faves }))
      .sort((x, y) => y.points - x.points || x.u.userId - y.u.userId);
    return {
      period, at: t,
      creators: creators.slice(0, kTopSize).map((c) => Object.assign(this.leaderPerson(c.u), { points: c.points, visits: c.visits, faves: c.faves })),
      games: games.slice(0, kTopSize).map((g) => Object.assign(this.publicAsset(g.a), { visits: g.plays, by: this.leaderPerson(this.users.get(g.a.creator)) })),
      art: art.slice(0, kTopSize).map((x) => Object.assign(this.artJson(x.p, null), { faves: x.faves })),
    };
  };

  P.leadersOp = function (name, me, args) {
    if (name !== 'leaders.get') return { ok: false, error: 'Unknown request.' };
    const period = args.period === 'all' ? 'all' : 'week';
    const t = now();
    this.leadersCache = this.leadersCache || {};
    let got = this.leadersCache[period];
    if (!got || t - got.at >= kCacheSecs) got = this.leadersCache[period] = this.leadersBuild(period, t);
    return okay(got);
  };

  // {art, artFaves, videos, pastes}: what `id` has posted outside the catalog (for achievements).
  P.uploadCounts = function (id) {
    const out = { art: 0, artFaves: 0, videos: 0, pastes: 0 };
    if (this.artInit) this.artInit();
    for (const p of (this.art ? this.art.values() : [])) if (p.by === id) { out.art++; out.artFaves += p.faves.length; }
    if (this.videoInit) this.videoInit();
    for (const v of (this.videos ? this.videos.values() : [])) if (v.by === id && v.state === 'ready') out.videos++;
    if (this.pasteInit) this.pasteInit();
    for (const p of (this.pastes ? this.pastes.values() : [])) if (p.by === id) out.pastes++;
    return out;
  };
}
