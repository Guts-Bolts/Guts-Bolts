// Journeys: the different ways players go through a game, not just one straight funnel. A creator
// lists the stages ("Pick a class", "First fight", "Shop") and, for each stage, the choices a player
// can make there (an event each: "ChoseWarrior", "ChoseMage", "SkippedClass"). The game's Configure
// page then draws where players went from stage to stage, and where they left (a Sankey chart),
// and says in plain words where the most players are lost. Free, like everything else for creators.
// (src/server/ServerJourneys.cpp has the same rules.)
//
// How it counts:
//   - Every journey starts with "Joined the game" (the game sends that by itself).
//   - A player's path is the first choice they made at each stage, in order: an event only counts
//     for the next stage they haven't done yet, so the chart always reads left to right. (A way
//     around a stage, like skipping the tutorial, is a choice of its own: "SkippedTutorial".)
//   - Events come from the same AnalyticsService calls as experiments (worker/experiments.js):
//       LogCustomEvent(player, "ChoseWarrior")      -> ChoseWarrior
//       LogFunnelStepEvent(player, "Tutorial", nil, 3) -> funnel:Tutorial:3
//       LogEconomyEvent(...Sink, "Coins", ...)      -> spent:Coins   (earned:Coins for Source)
//   - Only the game's own servers send events (analytics.log checks that), and only while a journey
//     or an experiment is running.
//
// Requests (the game's creator; staff can look):
//   journeys.list   {game}                       -> {journeys: [... with results]}
//   journeys.create {game, name, stages: [{name, nodes: [event, ...]}]} -> {journeys}
//   journeys.stop   {game, id}                   -> {journeys}   stops counting; the chart stays
//   journeys.remove {game, id}                   -> {journeys}   the numbers go too
import { metricProblem } from './experiments.js';

export const kMostJourneys = 5, kMostRunningJourneys = 3, kMostStages = 10, kMostNodes = 10;
export const kMostPlayersInJourney = 50000;
export const JOURNEYS_COSTLY = { 'journeys.create': [20, 60, 3600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const tidy = (s, most) => String(s || '').replace(/[^A-Za-z0-9 _\-.:]/g, '').replace(/\s+/g, ' ').trim().slice(0, most);

// The running journeys of a game (asset `a`).
export const runningJourneys = (a) => (Array.isArray(a.journeys) ? a.journeys : []).filter((j) => !j.stopped);

// The stages a creator typed, checked. Returns {stages} or {error}.
export function cleanStages(raw) {
  const list = Array.isArray(raw) ? raw : [];
  const stages = [];
  for (const s of list) {
    if (!s || typeof s !== 'object') continue;
    const name = tidy(s.name, 40);
    const nodes = [...new Set((Array.isArray(s.nodes) ? s.nodes : []).map((n) => tidy(n, 60)).filter(Boolean))];
    if (!name && !nodes.length) continue;
    if (!name) return { error: 'Give each stage a name, like "Pick a class".' };
    if (!nodes.length) return { error: 'Stage "' + name + '" needs at least one event (a choice players can make there).' };
    if (nodes.length > kMostNodes) return { error: 'A stage can have up to ' + kMostNodes + ' choices.' };
    for (const n of nodes) { const bad = metricProblem(n); if (bad) return { error: bad }; }
    stages.push({ name, nodes });
  }
  if (!stages.length) return { error: 'Add at least one stage after "Joined the game".' };
  if (stages.length > kMostStages) return { error: 'A journey can have up to ' + kMostStages + ' stages.' };
  return { stages };
}

// A path ("" or "0,2,1": the choice made at each stage so far) after one event: the new path, or
// null if the event isn't the next stage's.
export function stepPath(stages, path, event) {
  const done = path === '' ? [] : path.split(',');
  const next = stages[done.length];
  if (!next) return null;
  const i = next.nodes.indexOf(event);
  if (i < 0) return null;
  return [...done, String(i)].join(',');
}

// The chart's numbers from every player's path: how many reached each choice, the flows between
// stages (from -1 = "Joined the game"), and how many left at each choice.
export function journeyResults(j, paths) {
  const stages = j.stages.map((s) => ({ name: s.name, nodes: s.nodes.map((n) => ({ name: n, players: 0, left: 0 })) }));
  const flows = new Map();
  let players = 0, leftAtJoin = 0;
  for (const path of paths) {
    players++;
    const p = path === '' ? [] : path.split(',').map(Number);
    if (!p.length) { leftAtJoin++; continue; }
    let from = -1;
    p.forEach((node, s) => {
      if (!stages[s] || !stages[s].nodes[node]) return;
      stages[s].nodes[node].players++;
      const key = s + ':' + from + ':' + node;
      flows.set(key, (flows.get(key) || 0) + 1);
      from = node;
    });
    const last = p.length - 1;
    if (last < stages.length - 1 && stages[last] && stages[last].nodes[p[last]]) stages[last].nodes[p[last]].left++;
  }
  return Object.assign({}, j, {
    players, leftAtJoin, stages,
    flows: [...flows.entries()].map(([k, n]) => { const [stage, from, to] = k.split(':').map(Number); return { stage, from, to, players: n }; }),
  });
}

export function installJourneys(Cls) {
  const P = Cls.prototype;

  P.journeysInit = function () {
    if (this.journeysReady) return;
    this.sql.exec(`CREATE TABLE IF NOT EXISTS journeydata (game TEXT NOT NULL, journey TEXT NOT NULL, player INTEGER NOT NULL,
      path TEXT NOT NULL, PRIMARY KEY (game, journey, player))`);
    this.journeysReady = true;
  };

  P.journeyWithResults = function (game, j) {
    this.journeysInit();
    const rows = this.sql.exec('SELECT path FROM journeydata WHERE game = ? AND journey = ?', game, j.id);
    return journeyResults(j, [...rows].map((r) => r.path));
  };

  // One event for every running journey of game `a` (from analytics.log). Returns how many kept it.
  P.journeyEvent = function (a, player, what) {
    const running = runningJourneys(a);
    if (!running.length) return 0;
    this.journeysInit();
    let kept = 0;
    for (const j of running) {
      const row = [...this.sql.exec('SELECT path FROM journeydata WHERE game = ? AND journey = ? AND player = ?', a.id, j.id, player)][0];
      if (what === '__join') {
        if (row) continue;   // (a player's journey starts once)
        const n = [...this.sql.exec('SELECT COUNT(*) AS n FROM journeydata WHERE game = ? AND journey = ?', a.id, j.id)][0].n;
        if (n >= kMostPlayersInJourney) continue;
        this.sql.exec('INSERT INTO journeydata (game, journey, player, path) VALUES (?, ?, ?, ?)', a.id, j.id, player, '');
        kept++;
        continue;
      }
      if (!row) continue;   // (only players the journey has seen join)
      const next = stepPath(j.stages, row.path, what);
      if (next === null) continue;
      this.sql.exec('UPDATE journeydata SET path = ? WHERE game = ? AND journey = ? AND player = ?', next, a.id, j.id, player);
      kept++;
    }
    return kept;
  };

  P.journeysOp = function (name, me, args) {
    const a = this.assets.get(this.assets.resolve(str(args, 'game')));
    if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
    const list = Array.isArray(a.journeys) ? a.journeys : [];
    const mine = a.creator === me.id;
    if (!mine && !(this.isStaff(me) && name === 'journeys.list')) return fail('Only the game\'s creator can see its journeys.');
    const answer = () => okay({ journeys: (a.journeys || []).map((j) => this.journeyWithResults(a.id, j)),
      most: kMostJourneys, mostRunning: kMostRunningJourneys, mostStages: kMostStages, mostNodes: kMostNodes });
    if (name === 'journeys.list') return answer();

    if (name === 'journeys.create') {
      if (me.userId === 0) return fail('Sign up to track journeys.');
      const title = tidy(args.name, 40);
      if (!title) return fail('Give the journey a name, like "First ten minutes".');
      const running = runningJourneys(a);
      if (running.some((j) => j.name.toLowerCase() === title.toLowerCase())) return fail('There\'s already a running journey called ' + title + '.');
      if (running.length >= kMostRunningJourneys) return fail('A game can track ' + kMostRunningJourneys + ' journeys at once. Stop one first.');
      if (list.length >= kMostJourneys) return fail('A game can keep ' + kMostJourneys + ' journeys. Remove an old one first.');
      const got = cleanStages(args.stages);
      if (got.error) return fail(got.error);
      const id = [...crypto.getRandomValues(new Uint8Array(6))].map((b) => b.toString(16).padStart(2, '0')).join('');
      a.journeys = [...list, { id, name: title, stages: got.stages, started: now(), stopped: 0 }];
      this.saveAsset(a);
      return answer();
    }
    const j = list.find((x) => x.id === str(args, 'id'));
    if (!j) return fail('That journey isn\'t there.');
    if (name === 'journeys.stop') {
      if (j.stopped) return fail('It\'s already stopped.');
      j.stopped = now();
      this.saveAsset(a);
      return answer();
    }
    if (name === 'journeys.remove') {
      this.journeysInit();
      a.journeys = list.filter((x) => x !== j);
      this.sql.exec('DELETE FROM journeydata WHERE game = ? AND journey = ?', a.id, j.id);
      this.saveAsset(a);
      return answer();
    }
    return fail('Unknown request.');
  };
}
