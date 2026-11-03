// Three website features that live together here (src/server/ServerExtras.cpp has the same rules):
//
// 1. Achievements on profiles: platform achievements like "Play 10 games" or "Make your first
//    item". Nothing is stored: they're worked out from the account each time (profile shows them).
//
// 2. Game update logs: a game's creator posts "what's new" notes on the game's page. People who
//    favourited the game see them in My Feed.
//      gamelog.list   {id}                 -> {entries}                 (anyone)
//      gamelog.post   {id, title, text}    -> {entries}                 (the game's creator, or staff)
//      gamelog.delete {id, entry}          -> {entries}
//
// 3. Events: staff set up a timed event (a name, start and end, and a free catalog item you earn
//    by playing for a number of minutes while it's on). Minutes count from the Player's "still
//    playing" reports (bolts.play, every 5 minutes or so).
//      events.list       {}                -> {events, now}             (anyone; signed-in people see their minutes)
//      events.claim      {id}              -> {events, got}             (signed up; played enough)
//      admin.eventSave   {id?, name, text, start, end, item, minutes}   (staff; no id = a new event)
//      admin.eventRemove {id}

export const kLogTitle = 80, kLogText = 1000, kLogKept = 30, kLogPerDay = 5, kLogFeedDays = 30, kLogFeedEach = 2;
export const kEventName = 60, kEventText = 600, kMostEvents = 50, kEventMaxMinutes = 600, kEventMaxDays = 60;
export const kPlayReport = 5;   // minutes each bolts.play counts for (the Player reports about every 5 minutes)

const fail = (error) => ({ ok: false, error });

// ---------------------------------------------------------------- achievements
// stat: what to count (achievementStats); need: how many.
export const ACHIEVEMENTS = [
  { id: 'first-play',   name: 'First Steps',     text: 'Play your first game',                  stat: 'played',     need: 1 },
  { id: 'explorer',     name: 'Explorer',        text: 'Play 10 different games',               stat: 'played',     need: 10 },
  { id: 'globetrotter', name: 'Globetrotter',    text: 'Play 50 different games',               stat: 'played',     need: 50 },
  { id: 'game-maker',   name: 'Game Maker',      text: 'Publish your first game',               stat: 'gamesMade',  need: 1 },
  { id: 'designer',     name: 'Designer',        text: 'Make your first catalog item',          stat: 'itemsMade',  need: 1 },
  { id: 'crowd',        name: 'Crowd Pleaser',   text: 'Get 100 visits on your games',          stat: 'visits',     need: 100 },
  { id: 'friendly',     name: 'Friendly',        text: 'Have 5 friends',                        stat: 'friends',    need: 5 },
  { id: 'popular',      name: 'Popular',         text: 'Have 25 friends',                       stat: 'friends',    need: 25 },
  { id: 'badge-hunter', name: 'Badge Hunter',    text: 'Earn a badge in a game',                stat: 'gameBadges', need: 1 },
  { id: 'dressed-up',   name: 'Dressed Up',      text: 'Save an outfit',                        stat: 'outfits',    need: 1 },
  { id: 'party-goer',   name: 'Party Goer',      text: 'Get a free item from an event',         stat: 'events',     need: 1 },
  { id: 'one-year',     name: 'One Year Club',   text: 'Have your account for a whole year',    stat: 'days',       need: 365 },
];

// The numbers achievements look at. `made`: the assets this person created.
export function achievementStats(u, made, t) {
  return {
    played: (u.played || []).length,
    gamesMade: made.filter((a) => a.kind === 'game').length,
    itemsMade: made.filter((a) => a.kind !== 'game' && a.kind !== 'model' && a.kind !== 'devproduct' && a.kind !== 'pass').length,
    visits: made.filter((a) => a.kind === 'game').reduce((n, a) => n + (a.plays || 0), 0),
    friends: (u.friends || []).length,
    gameBadges: (u.gameBadges || []).length,
    outfits: (u.outfits || []).length,
    events: (u.eventClaims || []).length,
    days: Math.floor(Math.max(0, t - (u.created || t)) / 86400),
  };
}

// [{id, name, text, earned, have, need}], earned ones first (in list order).
export function achievementsOf(stats) {
  const all = ACHIEVEMENTS.map((x) => ({ id: x.id, name: x.name, text: x.text, have: Math.min(stats[x.stat] || 0, x.need),
    need: x.need, earned: (stats[x.stat] || 0) >= x.need }));
  return [...all.filter((x) => x.earned), ...all.filter((x) => !x.earned)];
}

// ---------------------------------------------------------------- game update logs
export function gameLogView(a) {
  return (a.updateLog || []).map((e) => ({ id: e.id, title: e.title, text: e.text, at: e.at }));
}

// title / text already cleaned and filtered. Returns null when it worked, else fail(...).
export function gameLogPost(a, title, text, id, t, day) {
  if (!title) return fail('Give the update a short title.');
  const log = a.updateLog || (a.updateLog = []);
  if (a.updateLogDay !== day) { a.updateLogDay = day; a.updateLogToday = 0; }
  if (a.updateLogToday >= kLogPerDay) return fail('That\'s ' + kLogPerDay + ' updates for this game today. Post again tomorrow.');
  a.updateLogToday++;
  log.unshift({ id, title, text, at: t });
  if (log.length > kLogKept) log.length = kLogKept;
  return null;
}

export function gameLogDelete(a, entry) {
  const before = (a.updateLog || []).length;
  a.updateLog = (a.updateLog || []).filter((e) => e.id !== entry);
  return a.updateLog.length === before ? fail('That update isn\'t there any more.') : null;
}

// My Feed: the newest notes of the games you favourited (the last 30 days, 2 a game).
export function gameLogFeed(games, t) {
  const out = [];
  for (const a of games)
    for (const e of (a.updateLog || []).slice(0, kLogFeedEach))
      if (t - e.at <= kLogFeedDays * 86400)
        out.push({ text: e.title + (e.text ? ': ' + e.text : ''), title: e.title, at: e.at, kind: 'gamelog', game: { id: a.id, name: a.name } });
  return out;
}

// ---------------------------------------------------------------- events
export class Events {
  constructor(saved) {
    this.list = Array.isArray(saved) ? saved : [];   // {id, name, text, start, end, item, minutes, at}
  }
  toJSON() { return this.list; }

  live(t) { return this.list.filter((e) => e.start <= t && t < e.end); }

  // What everyone sees: live and upcoming events, then ones that ended in the last week.
  // itemOf(id) -> {id, name, kind} or null. u: the signed-in person (or null).
  view(t, u, itemOf) {
    const shown = this.list.filter((e) => e.end > t - 7 * 86400).sort((a, b) => a.start - b.start);
    return {
      ok: true, now: t,
      events: shown.map((e) => ({
        id: e.id, name: e.name, text: e.text, start: e.start, end: e.end, minutes: e.minutes, item: itemOf(e.item),
        state: t < e.start ? 'soon' : t < e.end ? 'live' : 'over',
        played: u ? Math.min((u.eventPlay || {})[e.id] || 0, e.minutes) : 0,
        claimed: u ? (u.eventClaims || []).includes(e.id) : false,
      })),
    };
  }

  // The Player said "still playing": every live event gets kPlayReport more minutes.
  played(u, t) {
    const live = this.live(t);
    if (!live.length) return false;
    const play = u.eventPlay || (u.eventPlay = {});
    for (const e of live) play[e.id] = Math.min((play[e.id] || 0) + kPlayReport, kEventMaxMinutes);
    // (forget events that are gone)
    for (const id of Object.keys(play)) if (!this.list.some((e) => e.id === id)) delete play[id];
    return true;
  }

  // Can `u` claim event `id` now? Returns the event, or fail(...).
  claimable(u, id, t) {
    const e = this.list.find((x) => x.id === id);
    if (!e) return fail('That event isn\'t there any more.');
    if (t < e.start) return fail('That event hasn\'t started yet.');
    if (t >= e.end + 86400) return fail('That event is over.');   // (a day to claim after it ends)
    if ((u.eventClaims || []).includes(e.id)) return fail('You already got this event\'s item.');
    const have = (u.eventPlay || {})[e.id] || 0;
    if (have < e.minutes) return fail('Play ' + (e.minutes - have) + ' more minutes during the event to earn it.');
    return e;
  }

  // fields: {id?, name, text, start, end, item, minutes}, already cleaned. Returns the event or fail(...).
  save(fields, newId, t) {
    if (!fields.name) return fail('Give the event a name.');
    if (!(fields.start > 0) || !(fields.end > fields.start)) return fail('The event has to end after it starts.');
    if (fields.end - fields.start > kEventMaxDays * 86400) return fail('An event can last ' + kEventMaxDays + ' days at most.');
    const minutes = Math.round(fields.minutes);
    if (!(minutes >= 0 && minutes <= kEventMaxMinutes)) return fail('Minutes of playing must be between 0 and ' + kEventMaxMinutes + '.');
    const old = fields.id ? this.list.find((e) => e.id === fields.id) : null;
    if (fields.id && !old) return fail('That event isn\'t there any more.');
    if (!old && this.list.length >= kMostEvents) {
      const over = this.list.filter((e) => e.end < t).sort((a, b) => a.end - b.end)[0];
      if (!over) return fail('There are ' + kMostEvents + ' events already. Remove one first.');
      this.list = this.list.filter((e) => e !== over);
    }
    const e = old || { id: newId, at: t };
    Object.assign(e, { name: fields.name, text: fields.text, start: Math.floor(fields.start), end: Math.floor(fields.end), item: fields.item, minutes });
    if (!old) this.list.push(e);
    return e;
  }

  remove(id) {
    const before = this.list.length;
    this.list = this.list.filter((e) => e.id !== id);
    return this.list.length === before ? fail('That event isn\'t there any more.') : null;
  }
}
