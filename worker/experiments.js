// Experiments (A/B tests): a creator tries two to four versions of something in their game (a new
// shop, a shorter tutorial, a bigger reward) and sees which one players like more. Free, like
// everything else for creators. (src/server/ServerExperiments.cpp has the same rules.)
//
// How it works:
//   - The creator makes an experiment on the game's Configure page: a name ("Shop"), the versions
//     ("Old", "New") and up to kMostMetrics things to count, each one of:
//       an event name          the game's AnalyticsService:LogCustomEvent(player, "BoughtSword", 1)
//       spent:<currency>       AnalyticsService:LogEconomyEvent(player, Enum.AnalyticsEconomyFlowType.Sink, "Coins", 50, ...)
//       earned:<currency>      ... the same with Source
//       funnel:<name>:<step>   AnalyticsService:LogFunnelStepEvent(player, "Tutorial", nil, 3, "Jumped") reached step 3
//   - Each player always gets the same version: variantOf(experiment id, user number). The game asks
//     ExperimentService:GetVariantAsync(player, "Shop") and does what that version says.
//   - The game's server sends what happened (analytics.log). Every experiment also counts, by itself,
//     the players, the minutes they played, and how many came back on another day.
//   - The Configure page shows each version side by side, and says when a difference is big enough
//     to trust (a two-proportion z-test, 95%), so creators don't fall for luck.
//
// Requests:
//   experiments.list    {game}                             -> {experiments: [... with results]}   (the creator or staff)
//   experiments.create  {game, name, variants[], metrics[]} -> {experiments}                       (the creator)
//   experiments.stop    {game, id}                          -> {experiments}   stops counting; results stay
//   experiments.remove  {game, id}                          -> {experiments}   results go too
//   experiments.forGame {game}                              -> {experiments: [{id, name, variants}]}   the running ones, for the game
//   analytics.log       {game, events: [{player, name, value}]} -> {kept}
//     Only the game's own servers: its creator, staff, or the host of a live server of it (and then only
//     for the players in that server).
export const kMostExperiments = 10, kMostRunning = 5, kMostMetrics = 3, kMostVariants = 4, kMostEvents = 200;
export const kMostPlayersCounted = 50000;   // in one experiment (after that, new players aren't counted)
export const EXPERIMENTS_COSTLY = { 'experiments.create': [20, 60, 3600], 'analytics.log': [1200, 2400, 600] };
export const EXPERIMENTS_GUEST_OK = ['experiments.forGame', 'analytics.log'];   // (guests can host games too)

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const today = () => Math.floor(Date.now() / 86400000);

// Which version a player gets: FNV-1a of "<experiment id>:<user number>", the same in the game
// (src/scripting/Experiments.cpp) and src/server, so the game can't lie about it.
export function variantOf(expId, userId, count) {
  let h = 0x811c9dc5;
  const s = new TextEncoder().encode(expId + ':' + Math.trunc(userId));
  for (const b of s) { h ^= b; h = Math.imul(h, 0x01000193) >>> 0; }
  return count > 0 ? h % count : 0;
}

// A name or metric: plain letters, numbers, spaces and a few marks.
const tidy = (s, most) => String(s || '').replace(/[^A-Za-z0-9 _\-.:]/g, '').replace(/\s+/g, ' ').trim().slice(0, most);

export function metricProblem(m) {
  if (!m) return 'A metric needs a name.';
  if (/^(spent|earned):/.test(m)) return m.split(':')[1] ? '' : 'Say which currency, like spent:Coins.';
  if (/^funnel:/.test(m)) {
    const p = m.split(':');
    return p.length === 3 && p[1] && /^[0-9]{1,3}$/.test(p[2]) ? '' : 'A funnel metric looks like funnel:Tutorial:3 (the funnel\'s name and a step number).';
  }
  if (m.startsWith('__')) return 'Metric names can\'t start with two underscores.';
  return '';
}

// How sure we can be that `a` (did / n) and `b` differ: a two-proportion z-test.
export function zScore(didA, nA, didB, nB) {
  if (nA < 1 || nB < 1) return 0;
  const p = (didA + didB) / (nA + nB);
  const se = Math.sqrt(p * (1 - p) * (1 / nA + 1 / nB));
  return se > 0 ? (didB / nB - didA / nA) / se : 0;
}

export function installExperiments(Cls) {
  const P = Cls.prototype;

  P.experimentsInit = function () {
    if (this.experimentsReady) return;
    this.sql.exec(`CREATE TABLE IF NOT EXISTS expdata (game TEXT NOT NULL, exp TEXT NOT NULL, player INTEGER NOT NULL,
      metric TEXT NOT NULL, value REAL NOT NULL, PRIMARY KEY (game, exp, player, metric))`);
    this.experimentsReady = true;
  };

  // One experiment with its numbers, version by version.
  P.experimentResults = function (game, e) {
    this.experimentsInit();
    const per = e.variants.map((v) => ({ name: v, players: 0, minutes: 0, cameBack: 0, metrics: e.metrics.map((m) => ({ metric: m, did: 0, total: 0 })) }));
    const rows = this.sql.exec('SELECT player, metric, value FROM expdata WHERE game = ? AND exp = ?', game, e.id);
    for (const r of rows) {
      const v = per[variantOf(e.id, r.player, e.variants.length)];
      if (r.metric === '__days') { v.players++; if (r.value >= 2) v.cameBack++; continue; }
      if (r.metric === '__minutes') { v.minutes += r.value; continue; }
      const m = v.metrics.find((x) => x.metric === r.metric);
      if (m) { m.total += r.value; if (r.value > 0) m.did++; }
    }
    const first = per[0];
    for (const v of per) {
      v.minutes = Math.round(v.minutes);
      v.cameBackSure = v !== first && Math.abs(zScore(first.cameBack, first.players, v.cameBack, v.players)) >= 1.96;
      for (let i = 0; i < v.metrics.length; i++) {
        const m = v.metrics[i], f = first.metrics[i];
        m.total = Math.round(m.total * 100) / 100;
        m.sure = v !== first && Math.abs(zScore(f.did, first.players, m.did, v.players)) >= 1.96;
      }
    }
    return Object.assign({}, e, { results: per });
  };

  P.experimentsOp = function (name, me, args) {
    const a = this.assets.get(this.assets.resolve(str(args, 'game')));
    if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
    const list = Array.isArray(a.experiments) ? a.experiments : [];
    const running = list.filter((e) => !e.stopped);

    if (name === 'experiments.forGame')
      return okay({ experiments: running.map((e) => ({ id: e.id, name: e.name, variants: e.variants })) });

    if (name === 'analytics.log') return this.analyticsLog(a, running, me, args);

    const mine = a.creator === me.id;
    if (!mine && !(this.isStaff(me) && name === 'experiments.list')) return fail('Only the game\'s creator can run its experiments.');
    const answer = () => okay({ experiments: (a.experiments || []).map((e) => this.experimentResults(a.id, e)),
      most: kMostExperiments, mostRunning: kMostRunning });
    if (name === 'experiments.list') return answer();

    if (name === 'experiments.create') {
      if (me.userId === 0) return fail('Sign up to run experiments.');
      const title = tidy(args.name, 40);
      if (!title) return fail('Give the experiment a name, like "Shop".');
      if (running.some((e) => e.name.toLowerCase() === title.toLowerCase())) return fail('There\'s already a running experiment called ' + title + '.');
      if (running.length >= kMostRunning) return fail('A game can run ' + kMostRunning + ' experiments at once. Stop one first.');
      if (list.length >= kMostExperiments) return fail('A game can keep ' + kMostExperiments + ' experiments. Remove an old one first.');
      const variants = (Array.isArray(args.variants) ? args.variants : []).map((v) => tidy(v, 20)).filter(Boolean);
      if (variants.length < 2 || variants.length > kMostVariants) return fail('An experiment needs 2 to ' + kMostVariants + ' versions, like "Old" and "New".');
      if (new Set(variants.map((v) => v.toLowerCase())).size !== variants.length) return fail('Give each version a different name.');
      const metrics = (Array.isArray(args.metrics) ? args.metrics : []).map((m) => tidy(m, 60)).filter(Boolean);
      if (metrics.length > kMostMetrics) return fail('Pick up to ' + kMostMetrics + ' things to count.');
      for (const m of metrics) { const bad = metricProblem(m); if (bad) return fail(bad); }
      const id = [...crypto.getRandomValues(new Uint8Array(6))].map((b) => b.toString(16).padStart(2, '0')).join('');
      a.experiments = [...list, { id, name: title, variants, metrics: [...new Set(metrics)], started: now(), stopped: 0 }];
      this.saveAsset(a);
      return answer();
    }
    const e = list.find((x) => x.id === str(args, 'id'));
    if (!e) return fail('That experiment isn\'t there.');
    if (name === 'experiments.stop') {
      if (e.stopped) return fail('It\'s already stopped.');
      e.stopped = now();
      this.saveAsset(a);
      return answer();
    }
    if (name === 'experiments.remove') {
      this.experimentsInit();
      a.experiments = list.filter((x) => x !== e);
      this.sql.exec('DELETE FROM expdata WHERE game = ? AND exp = ?', a.id, e.id);
      this.saveAsset(a);
      return answer();
    }
    return fail('Unknown request.');
  };

  // What happened in the game, from its server.
  P.analyticsLog = function (a, running, me, args) {
    const trusted = this.isStaff(me) || a.creator === me.id;
    let allowed = null;   // user numbers the sender may speak for (null: anyone)
    if (!trusted) {
      const mine = [...this.sessions.values()].filter((s) => s.host === me.id && this.assets.resolve(s.game) === a.id);
      if (!mine.length) return fail('Only the game\'s own servers can send its analytics.');
      allowed = new Set([me.userId]);
      for (const s of mine)
        for (const p of s.players) { const c = this.conns.get(p); const u = c && c.account ? this.users.get(c.account) : null; if (u) allowed.add(u.userId); }
    }
    const events = Array.isArray(args.events) ? args.events.slice(0, kMostEvents) : [];
    if (!running.length || !events.length) return okay({ kept: 0 });
    this.experimentsInit();
    const day = today();
    let kept = 0;
    for (const ev of events) {
      if (!ev || typeof ev !== 'object') continue;
      const player = typeof ev.player === 'number' ? Math.trunc(ev.player) : 0;
      const what = typeof ev.name === 'string' ? ev.name.slice(0, 60) : '';
      let value = typeof ev.value === 'number' && Number.isFinite(ev.value) ? ev.value : 1;
      if (player <= 0 || !what || (allowed && !allowed.has(player))) continue;
      for (const e of running) {
        const get = (metric) => {
          const r = [...this.sql.exec('SELECT value FROM expdata WHERE game = ? AND exp = ? AND player = ? AND metric = ?', a.id, e.id, player, metric)][0];
          return r ? r.value : null;
        };
        const put = (metric, v) => this.sql.exec('INSERT OR REPLACE INTO expdata (game, exp, player, metric, value) VALUES (?, ?, ?, ?, ?)', a.id, e.id, player, metric, v);
        if (what === '__join') {
          const last = get('__lastday');
          if (last === day) continue;
          if (last === null) {   // a new player in this experiment
            const n = [...this.sql.exec("SELECT COUNT(*) AS n FROM expdata WHERE game = ? AND exp = ? AND metric = '__days'", a.id, e.id)][0].n;
            if (n >= kMostPlayersCounted) continue;
          }
          put('__days', (get('__days') || 0) + 1);
          put('__lastday', day);
          kept++;
          continue;
        }
        if (get('__days') === null) continue;   // (only players the experiment has seen join)
        if (what === '__minutes') { put('__minutes', (get('__minutes') || 0) + Math.min(Math.max(value, 0), 600)); kept++; continue; }
        if (!e.metrics.includes(what)) continue;
        value = Math.min(Math.max(value, -1e9), 1e9);
        put(what, (get(what) || 0) + value);
        kept++;
      }
    }
    return okay({ kept });
  };
}
