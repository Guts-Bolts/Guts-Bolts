// The events calendar: game jams, art contests, live shows, meetups and launches, each with a
// start and an end, on one shared calendar (website/app/calendar.js draws it). The game "Events"
// with free rewards (server.js eventsOp) are something else. src/server/ServerCalendar.cpp has
// the same rules.
//
// How it works:
//   - Anyone signed up can put an event on the calendar. Staff events are marked "Official" and
//     come first on the same day.
//   - An event can be hosted by a group: you need to be its Owner or have "manage" in it.
//   - It starts between an hour ago and a year from now, and lasts from 15 minutes to 31 days
//     (a game jam can take a few weeks).
//   - The title and description go through the word filter; Fyxa looks at new ones and people can
//     report them. A link can point somewhere on Guts&Bolts ("#/game/12", "#/live" ...), nowhere else.
//   - People press "Going" to say they'll be there. If the time changes or the event is called
//     off, everyone going gets a notification.
//   - Only the person who made it (or staff) can change or delete it. Events that ended more
//     than a year ago are tidied away.
//   - kPerDay new events a day (Verified: kPerDayVerified), at most kMostUpcoming coming up each.
//
// Requests:
//   calendar.list   {from, to, kind, when}  -> {events, kinds}  from/to: a time range (seconds);
//                   or when: "upcoming" (not over yet, soonest first) or "past" (newest first)
//   calendar.get    {id}                    -> {event}  (with the description)
//   calendar.make   {title, text, kind, start, end, link, group}  -> {event}
//   calendar.edit   {id, ...the same}       -> {event}
//   calendar.delete {id}                    -> {}
//   calendar.going  {id, on}                -> {event}
import { clean } from './banlists.js';

export const CALENDAR_KINDS = { jam: 'Game jam', art: 'Art contest', live: 'Live show', meetup: 'Meetup', launch: 'Launch', other: 'Other' };
export const kTitle = 80, kText = 3000, kLink = 200, kShortest = 15 * 60, kLongest = 31 * 86400, kAhead = 366 * 86400, kLate = 3600;
export const kPerDay = 5, kPerDayVerified = 20, kMostUpcoming = 20, kMostGoing = 5000, kKeepOver = 365 * 86400, kMostInList = 300;
export const CALENDAR_LOOK_ONLY = ['calendar.list', 'calendar.get'];
export const CALENDAR_COSTLY = { 'calendar.make': [20, 60, 3600], 'calendar.edit': [60, 180, 3600], 'calendar.going': [120, 360, 600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const int = (args, k) => (typeof args[k] === 'number' && isFinite(args[k]) ? Math.trunc(args[k]) : 0);
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');

// A link to somewhere on the site, as the app's "#/..." address ('' when there isn't one, null when
// it's somewhere else). "https://gutsandbolts.net/app/#/game/12" and "/app/#/game/12" are fine too.
export function calendarLink(s) {
  let l = String(s || '').trim();
  if (!l) return '';
  l = l.replace(/^https:\/\/(www\.)?gutsandbolts\.net/i, '').replace(/^\/app\/?/, '');
  if (!/^#\/[A-Za-z0-9\-_/?=&.%]{1,200}$/.test(l) || l.includes('//')) return null;
  return l;
}

// What's wrong with these times ('' when they're fine).
export function calendarTimesProblem(start, end, t) {
  if (!start || !end) return 'Pick when it starts and ends.';
  if (start < t - kLate) return 'That start time has already gone by.';
  if (start > t + kAhead) return 'Events can be at most a year away.';
  if (end - start < kShortest) return 'An event lasts at least 15 minutes.';
  if (end - start > kLongest) return 'An event can last at most 31 days.';
  return '';
}

export function installCalendar(Cls) {
  const P = Cls.prototype;

  P.calendarInit = function () {
    if (this.calendar) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS calendar (id TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.calendar = new Map();
    for (const r of this.sql.exec('SELECT id, data FROM calendar')) this.calendar.set(r.id, JSON.parse(r.data));
  };
  P.calendarSave = function (e) { this.sql.exec('INSERT OR REPLACE INTO calendar (id, data) VALUES (?, ?)', e.id, JSON.stringify(e)); };
  P.calendarDrop = function (e) { this.calendar.delete(e.id); this.sql.exec('DELETE FROM calendar WHERE id = ?', e.id); };
  P.calendarTidy = function (t) { for (const e of [...this.calendar.values()]) if (e.end + kKeepOver < t) this.calendarDrop(e); };

  P.calendarJson = function (e, me, t, full = false) {
    const u = this.users.get(e.by), g = e.group ? this.groups.get(e.group) : null;
    const going = e.going || [];
    const out = { id: e.id, title: e.title, kind: e.kind, kindName: CALENDAR_KINDS[e.kind] || 'Other', start: e.start, end: e.end,
      link: e.link || '', official: !!e.official, going: going.length, amGoing: !!me && me.userId !== 0 && going.includes(me.id),
      state: t < e.start ? 'soon' : t < e.end ? 'now' : 'over', at: e.at,
      by: u ? { id: u.id, name: u.name, userId: u.userId, verified: this.isVerified(u) } : { id: e.by, name: '?', userId: 0, verified: false },
      group: g ? { id: g.id, name: g.name } : null };
    if (full) Object.assign(out, { text: e.text, edited: e.edited || 0, mine: !!me && e.by === me.id,
      canEdit: !!me && me.userId !== 0 && (e.by === me.id || this.isStaff(me)) });
    return out;
  };
  // For report.send (server.js).
  P.calendarReportTarget = function (id, me) {
    this.calendarInit();
    const e = this.calendar.get(String(id || ''));
    if (!e) return 'That event isn\'t on the calendar any more.';
    if (e.by === me.id) return 'You can\'t report yourself.';
    return { target: e.id, about: e.by, copy: { subject: 'Event: ' + e.title, body: e.text.slice(0, 2000), at: e.at } };
  };
  P.calendarReportName = function (target) { this.calendarInit(); const e = this.calendar.get(target); return e ? e.title : '(deleted)'; };

  // The fields of calendar.make / calendar.edit, checked: { fields } or { error }. `old` is the event being changed.
  P.calendarFields = function (me, args, t, old) {
    const title = clean(str(args, 'title'), kTitle);
    if (!title) return { error: 'Give your event a name.' };
    const text = clean(str(args, 'text'), kText, true);
    const kind = Object.hasOwn(CALENDAR_KINDS, str(args, 'kind')) ? str(args, 'kind') : 'other';
    const start = int(args, 'start'), end = int(args, 'end');
    // (an event that's already on keeps its start when you only fix the words)
    const bad = old && start === old.start && end === old.end ? '' : calendarTimesProblem(start, end, t);
    if (bad) return { error: bad };
    const link = calendarLink(str(args, 'link'));
    if (link === null) return { error: 'Links can only go somewhere on Guts&Bolts (like #/game/12 or #/live).' };
    let group = old ? old.group || '' : '';
    if ('group' in args) {
      group = str(args, 'group');
      if (group) {
        const g = this.groups.get(group);
        if (!g) return { error: 'That group doesn\'t exist (any more).' };
        const r = this.rankIn(g, me.id);
        if (!this.isStaff(me) && !(r && (r.id === 'Owner' || r.perms.includes('manage')))) return { error: 'You need "manage" in ' + g.name + ' to put events on its behalf.' };
      }
    }
    return { fields: { title, text, kind, start, end, link, group } };
  };

  P.calendarOp = function (name, me, args) {
    this.calendarInit();
    const t = now();
    const hidden = (e) => { const u = this.users.get(e.by); return !!u && !!me && me.userId !== 0 && this.blocks(me, u); };
    if (name === 'calendar.list') {
      this.calendarTidy(t);
      const kind = Object.hasOwn(CALENDAR_KINDS, str(args, 'kind')) ? str(args, 'kind') : '';
      let list = [...this.calendar.values()].filter((e) => (!kind || e.kind === kind) && !hidden(e));
      const from = int(args, 'from'), to = int(args, 'to');
      if (from || to) {
        if (!(to > from) || to - from > 62 * 86400) return fail('Ask for at most two months at a time.');
        list = list.filter((e) => e.start < to && e.end > from).sort((x, y) => x.start - y.start || (y.official ? 1 : 0) - (x.official ? 1 : 0));
      } else if (str(args, 'when') === 'past') {
        list = list.filter((e) => e.end <= t).sort((x, y) => y.end - x.end);
      } else {
        list = list.filter((e) => e.end > t).sort((x, y) => x.start - y.start || (y.official ? 1 : 0) - (x.official ? 1 : 0));
      }
      return okay({ events: list.slice(0, kMostInList).map((e) => this.calendarJson(e, me, t)), kinds: CALENDAR_KINDS });
    }
    const getIt = () => this.calendar.get(str(args, 'id'));
    if (name === 'calendar.get') {
      const e = getIt();
      if (!e || hidden(e)) return fail('That event isn\'t on the calendar (any more).');
      return okay({ event: this.calendarJson(e, me, t, true) });
    }
    if (!me || me.userId === 0) return fail('Sign up to use the calendar.');
    const staff = this.isStaff(me);
    if (name === 'calendar.make') {
      const f = this.calendarFields(me, args, t, null);
      if (f.error) return fail(f.error);
      const today = Math.floor(t / 86400);
      if (me.calendarDay !== today) { me.calendarDay = today; me.calendarToday = 0; }
      const most = this.isVerified(me) || staff ? kPerDayVerified : kPerDay;
      if (!staff && me.calendarToday >= most) return fail('You\'ve put ' + most + ' events on the calendar today. Come back tomorrow!');
      const coming = [...this.calendar.values()].filter((e) => e.by === me.id && e.end > t).length;
      if (!staff && coming >= kMostUpcoming) return fail('You already have ' + kMostUpcoming + ' events coming up.');
      const e = Object.assign({ id: 'ev-' + hex(5), by: me.id, at: t, official: staff, going: [me.id] }, f.fields);
      this.calendar.set(e.id, e);
      this.calendarSave(e);
      me.calendarToday = (me.calendarToday || 0) + 1;
      this.saveUser(me);
      return okay({ event: this.calendarJson(e, me, t, true) });
    }
    const e = getIt();
    if (!e) return fail('That event isn\'t on the calendar (any more).');
    const tellGoing = (text) => { for (const id of e.going || []) { const u = this.users.get(id); if (u && id !== me.id) this.notify(u, 'calendar', text, e.id); } };
    if (name === 'calendar.going') {
      if (t >= e.end) return fail('That event is over.');
      if (hidden(e)) return fail('You can\'t go to this event.');
      const going = e.going || (e.going = []);
      const i = going.indexOf(me.id);
      if (args.on === false) { if (i >= 0) going.splice(i, 1); }
      else if (i < 0) { if (going.length >= kMostGoing) return fail('This event is full.'); going.push(me.id); }
      this.calendarSave(e);
      return okay({ event: this.calendarJson(e, me, t, true) });
    }
    if (e.by !== me.id && !staff) return fail('Only the person who made it, or staff, can change it.');
    if (name === 'calendar.edit') {
      const f = this.calendarFields(me, args, t, e);
      if (f.error) return fail(f.error);
      const moved = f.fields.start !== e.start || f.fields.end !== e.end;
      Object.assign(e, f.fields, { edited: t });
      this.calendarSave(e);
      if (e.by !== me.id) this.staffDid(me, 'calendar', 'Changed the event "' + e.title + '"', e.by);
      if (moved) tellGoing('"' + e.title.slice(0, 60) + '" moved to a new time.');
      return okay({ event: this.calendarJson(e, me, t, true) });
    }
    if (name === 'calendar.delete') {
      if (e.by !== me.id) this.staffDid(me, 'calendar', 'Deleted the event "' + e.title + '"', e.by);
      if (t < e.end) tellGoing('"' + e.title.slice(0, 60) + '" was called off.');
      this.calendarDrop(e);
      return okay();
    }
    return fail('Unknown request.');
  };
}
