// Ban lists: creators keep lists of people who may not play their games, and use one list in
// as many of their games as they like (src/server/ServerBanLists.cpp has the same rules).
//
// Why lists and not "a ban per game": a creator with five games bans a cheater once, and the
// ban works in all five. A small team can share one list (the owner adds "editors").
//
// Safety (shared bans can be unfair, so every ban has to be explainable):
//   - Lists are never public. Only the owner and the editors they picked can add to a list,
//     and only games whose creator owns or edits a list can use it.
//   - Every ban says why (a reason is required), who added it and when.
//   - Every change is written in the list's log (who did what), which only the team and staff see.
//   - The banned person sees the reason, and can appeal once per ban. The list's team answers;
//     accepting an appeal takes the ban off.
//   - A game's own creator and staff can never be banned from a game this way.
//
// Requests (all need a signed-up account):
//   banlist.mine   {}                              -> {lists}           lists you own or edit
//   banlist.create {name}                          -> {list}
//   banlist.get    {id}                            -> {list}            entries, editors, appeals, log
//   banlist.rename {id, name}                      -> {list}            (owner)
//   banlist.delete {id}                            -> {}                (owner; its games stop using it)
//   banlist.add    {id, who, reason, days}         -> {list}            (owner or editor; days 0 = for good)
//   banlist.remove {id, who}                       -> {list}
//   banlist.share  {id, who, remove}               -> {list}            (owner: add or remove an editor)
//   banlist.answer {id, appeal, accept, note}      -> {list}            (owner or editor)
//   banlist.check  {game}                          -> {banned, ban?}    am I banned from this game?
//   banlist.appeal {game, text}                    -> {ban}             ask the list's team to unban you
//   game.banLists  {game, lists?}                  -> {lists}           which lists a game uses (its creator)
//   game.ban       {game, who, reason, days}       -> {list}            ban from this game: adds to the game's
//                                                                       first list (made for you if it has none)
import { filterText } from './textfilter.js';

export const kListName = 40, kReason = 200, kAppealText = 500, kAnswerNote = 200;
export const kMostListsEach = 10, kMostEntries = 2000, kMostEditors = 10, kListsPerGame = 5;
export const kMostDays = 3650, kLogKept = 300, kAppealsKept = 200, kAppealMin = 10;

const enc = new TextEncoder();
const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');
const now = () => Math.floor(Date.now() / 1000);

// Online::cleanText (control characters out, a cap in UTF-8 bytes, trimmed), then the text filter.
export function clean(s, maxLen, newlines = false) {
  let out = '', bytes = 0;
  for (const ch of String(s || '')) {
    const c = ch.codePointAt(0);
    const keep = ch === '\n' && newlines ? '\n' : (c >= 32 && c !== 127 ? ch : '');
    if (!keep) continue;
    const b = enc.encode(keep).length;
    if (bytes + b > maxLen) break;
    out += keep; bytes += b;
  }
  return filterText(out.replace(/^[ \n]+|[ \n]+$/g, ''));
}

// Is this ban still on at time t? (until 0 = for good)
export const banLive = (e, t) => !e.until || e.until > t;

export function installBanLists(Cls) {
  const P = Cls.prototype;

  // The lists, kept in their own table (one row each) and in memory.
  P.banListsInit = function () {
    if (this.banLists) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS banlists (id TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.banLists = new Map();
    for (const r of this.sql.exec('SELECT id, data FROM banlists')) this.banLists.set(r.id, JSON.parse(r.data));
  };
  P.banListSave = function (l) {
    if (l.log.length > kLogKept) l.log = l.log.slice(-kLogKept);
    if (l.appeals.length > kAppealsKept) {
      const extra = l.appeals.length - kAppealsKept;
      let dropped = 0;
      l.appeals = l.appeals.filter((a) => a.status === 'open' || dropped++ >= extra);
    }
    this.sql.exec('INSERT OR REPLACE INTO banlists (id, data) VALUES (?, ?)', l.id, JSON.stringify(l));
  };
  P.banListLog = function (l, me, did, who = '', note = '') {
    l.log.push({ at: now(), by: me.id, did, who, note });
  };
  P.banListRole = function (l, me) {
    if (l.owner === me.id) return 'owner';
    if (l.editors.includes(me.id)) return 'editor';
    if (this.isStaff(me)) return 'staff';
    return '';
  };
  P.banListPerson = function (id) {
    const u = this.users.get(id);
    return u ? { id: u.id, name: u.name, username: u.username || '', userId: u.userId } : { id, name: '?', username: '', userId: 0 };
  };

  // The ban (and its list) that keeps `me` out of game `a`, or null. Expired bans don't count.
  P.banFor = function (a, me) {
    if (!a || !me || !Array.isArray(a.banLists) || !a.banLists.length) return null;
    if (a.creator === me.id || this.isStaff(me)) return null;
    this.banListsInit();
    const t = now();
    for (const id of a.banLists) {
      const l = this.banLists.get(id);
      const e = l && l.entries.find((x) => x.who === me.id && banLive(x, t));
      if (e) return { list: l, entry: e };
    }
    return null;
  };
  // What a banned player is told when they try to join.
  P.banMessageFor = function (a, me) {
    const b = this.banFor(a, me);
    if (!b) return '';
    const until = b.entry.until ? ' until ' + new Date(b.entry.until * 1000).toISOString().slice(0, 10) : '';
    return 'You\'re banned from ' + (a.name || 'this game') + until + '. Reason: ' + b.entry.reason +
      ' (You can appeal on the game\'s page.)';
  };

  P.banListSummary = function (l, me) {
    const t = now();
    const games = [];
    for (const a of this.assets.values()) if (a.kind === 'game' && Array.isArray(a.banLists) && a.banLists.includes(l.id)) games.push({ id: a.id, name: a.name });
    return { id: l.id, name: l.name, owner: this.banListPerson(l.owner), role: this.banListRole(l, me), created: l.created,
      bans: l.entries.filter((e) => banLive(e, t)).length, openAppeals: l.appeals.filter((x) => x.status === 'open').length, games };
  };
  P.banListFull = function (l, me) {
    const t = now();
    const person = (id) => this.banListPerson(id);
    return Object.assign(this.banListSummary(l, me), {
      editors: l.editors.map(person),
      entries: l.entries.filter((e) => banLive(e, t)).map((e) => ({ who: person(e.who), reason: e.reason, by: person(e.by), at: e.at, until: e.until })),
      appeals: l.appeals.slice().reverse().map((x) => ({ id: x.id, who: person(x.who), game: x.game, text: x.text, at: x.at, status: x.status,
        note: x.note || '', answeredBy: x.answeredBy ? person(x.answeredBy) : null, answeredAt: x.answeredAt || 0 })),
      log: l.log.slice(-100).reverse().map((x) => ({ at: x.at, by: person(x.by), did: x.did, who: x.who ? person(x.who) : null, note: x.note })),
    });
  };

  P.banListMake = function (me, name) {
    let owned = 0;
    for (const l of this.banLists.values()) if (l.owner === me.id) owned++;
    if (owned >= kMostListsEach) return fail('You can have ' + kMostListsEach + ' ban lists at most. Delete one first.');
    const l = { id: 'bl' + hex(6), name, owner: me.id, editors: [], entries: [], appeals: [], log: [], created: now() };
    this.banLists.set(l.id, l);
    this.banListLog(l, me, 'create', '', name);
    this.banListSave(l);
    return l;
  };

  // Add (or change) a ban. Returns an error answer, or null.
  P.banListAdd = function (l, me, args) {
    const who = this.findPerson(args.who);
    if (!who) return fail('There\'s nobody with that name or number.');
    if (who.id === me.id) return fail('You can\'t ban yourself.');
    if (who.id === l.owner || l.editors.includes(who.id)) return fail('That person looks after this list, so they can\'t be on it.');
    if (this.isStaff(who)) return fail('Guts&Bolts staff can\'t be banned from games.');
    const reason = clean(str(args, 'reason'), kReason);
    if (!reason) return fail('Say why. Every ban needs a reason, so the person (and your team) know what happened.');
    const days = typeof args.days === 'number' && Number.isFinite(args.days) ? Math.trunc(args.days) : 0;
    if (days < 0 || days > kMostDays) return fail('A ban lasts 1 to ' + kMostDays + ' days, or 0 for good.');
    const t = now();
    const entry = { who: who.id, reason, by: me.id, at: t, until: days ? t + days * 86400 : 0 };
    const old = l.entries.findIndex((e) => e.who === who.id);
    if (old >= 0) l.entries.splice(old, 1);
    else {
      l.entries = l.entries.filter((e) => banLive(e, t));   // make room: old bans that ran out go
      if (l.entries.length >= kMostEntries) return fail('This list is full (' + kMostEntries + ' people). Start another one.');
    }
    l.entries.push(entry);
    this.banListLog(l, me, old >= 0 ? 'change' : 'add', who.id, reason + (days ? ' (' + days + ' days)' : ' (for good)'));
    return null;
  };

  P.banListOp = function (name, me, args) {
    this.banListsInit();
    if (me.userId === 0) return fail('Sign up to use ban lists.');
    const t = now();

    if (name === 'banlist.check' || name === 'banlist.appeal') {
      const a = this.assets.get(this.assets.resolve(str(args, 'game')));
      if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
      const b = this.banFor(a, me);
      if (name === 'banlist.check') {
        if (!b) return okay({ banned: false });
        const mine = b.list.appeals.slice().reverse().find((x) => x.who === me.id && x.banAt === b.entry.at);
        return okay({ banned: true, ban: { reason: b.entry.reason, at: b.entry.at, until: b.entry.until, list: b.list.name,
          owner: this.banListPerson(b.list.owner), appeal: mine ? { status: mine.status, note: mine.note || '', at: mine.at } : null,
          canAppeal: !mine || mine.status === 'denied' } });
      }
      if (!b) return fail('You aren\'t banned from this game.');
      const text = clean(str(args, 'text'), kAppealText, true);
      if (text.length < kAppealMin) return fail('Write a little more: say why you should be let back in.');
      const mine = b.list.appeals.find((x) => x.who === me.id && x.banAt === b.entry.at && x.status === 'open');
      if (mine) return fail('You already appealed this ban. Wait for an answer.');
      const ap = { id: 'ap' + hex(5), who: me.id, game: a.id, text, at: t, status: 'open', banAt: b.entry.at };
      b.list.appeals.push(ap);
      this.banListLog(b.list, me, 'appeal', '', a.name);
      this.banListSave(b.list);
      for (const id of [b.list.owner, ...b.list.editors])
        this.notify(this.users.get(id), 'banlist', me.name + ' appealed their ban from ' + a.name + ' (list "' + b.list.name + '").', b.list.id);
      return okay({ ban: { status: 'open', at: t } });
    }

    if (name === 'banlist.mine') {
      const lists = [];
      for (const l of this.banLists.values()) if (l.owner === me.id || l.editors.includes(me.id)) lists.push(this.banListSummary(l, me));
      lists.sort((x, y) => (x.role === y.role ? y.created - x.created : x.role === 'owner' ? -1 : 1));
      return okay({ lists });
    }
    if (name === 'banlist.create') {
      const listName = clean(str(args, 'name'), kListName);
      if (!listName) return fail('Give the list a name.');
      const l = this.banListMake(me, listName);
      if (l.ok === false) return l;
      return okay({ list: this.banListFull(l, me) });
    }

    // Game side: which lists a game uses, and "ban from this game".
    if (name === 'game.banLists' || name === 'game.ban') {
      const a = this.assets.get(this.assets.resolve(str(args, 'game')));
      if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
      if (a.creator !== me.id && !this.isStaff(me)) return fail('Only the game\'s creator can change who\'s banned from it.');
      const listsOf = () => (a.banLists || []).map((id) => this.banLists.get(id)).filter(Boolean).map((l) => this.banListSummary(l, me));
      if (name === 'game.banLists') {
        if (!Array.isArray(args.lists)) return okay({ lists: listsOf() });
        const want = [...new Set(args.lists.filter((x) => typeof x === 'string'))];
        if (want.length > kListsPerGame) return fail('A game can use ' + kListsPerGame + ' ban lists at most.');
        for (const id of want) {
          const l = this.banLists.get(id);
          // A list you were never given can't be put on your game (no borrowing strangers' bans).
          if (!l || (this.banListRole(l, me) !== 'owner' && this.banListRole(l, me) !== 'editor' && !(l.owner === a.creator || l.editors.includes(a.creator))))
            return fail('You can only use ban lists you own or were added to.');
        }
        const before = a.banLists || [];
        a.banLists = want;
        this.saveAsset(a);
        for (const id of want) if (!before.includes(id)) { const l = this.banLists.get(id); this.banListLog(l, me, 'use', '', a.name); this.banListSave(l); }
        for (const id of before) if (!want.includes(id)) { const l = this.banLists.get(id); if (l) { this.banListLog(l, me, 'unuse', '', a.name); this.banListSave(l); } }
        return okay({ lists: listsOf() });
      }
      // game.ban: onto the game's first list you can edit; a new "<game> bans" list if there's none.
      let l = (a.banLists || []).map((id) => this.banLists.get(id)).find((x) => x && (x.owner === me.id || x.editors.includes(me.id) || this.isStaff(me)));
      if (!l) {
        if ((a.banLists || []).length >= kListsPerGame) return fail('This game already uses ' + kListsPerGame + ' ban lists you can\'t add to.');
        l = this.banListMake(me, clean((a.name || 'Game') + ' bans', kListName));
        if (l.ok === false) return l;
        a.banLists = [...(a.banLists || []), l.id];
        this.saveAsset(a);
        this.banListLog(l, me, 'use', '', a.name);
      }
      const bad = this.banListAdd(l, me, args);
      if (bad) return bad;
      this.banListSave(l);
      return okay({ list: this.banListFull(l, me) });
    }

    const l = this.banLists.get(str(args, 'id'));
    const role = l ? this.banListRole(l, me) : '';
    if (!l || !role) return fail('There\'s no ban list like that (or it isn\'t yours).');
    if (name === 'banlist.get') return okay({ list: this.banListFull(l, me) });
    const ownerOnly = name === 'banlist.rename' || name === 'banlist.delete' || name === 'banlist.share';
    if (ownerOnly && role !== 'owner' && role !== 'staff') return fail('Only the list\'s owner can do that.');

    if (name === 'banlist.rename') {
      const listName = clean(str(args, 'name'), kListName);
      if (!listName) return fail('Give the list a name.');
      this.banListLog(l, me, 'rename', '', listName);
      l.name = listName;
    } else if (name === 'banlist.delete') {
      for (const a of this.assets.values())
        if (Array.isArray(a.banLists) && a.banLists.includes(l.id)) { a.banLists = a.banLists.filter((x) => x !== l.id); this.saveAsset(a); }
      this.banLists.delete(l.id);
      this.sql.exec('DELETE FROM banlists WHERE id = ?', l.id);
      return okay({});
    } else if (name === 'banlist.share') {
      const who = this.findPerson(args.who);
      if (!who || who.userId === 0) return fail('There\'s nobody with that name or number.');
      if (args.remove === true) {
        if (!l.editors.includes(who.id)) return fail('They aren\'t an editor of this list.');
        l.editors = l.editors.filter((x) => x !== who.id);
        this.banListLog(l, me, 'unshare', who.id);
      } else {
        if (who.id === l.owner) return fail('That\'s the list\'s owner.');
        if (l.editors.includes(who.id)) return fail('They\'re already an editor.');
        if (l.editors.length >= kMostEditors) return fail('A list can have ' + kMostEditors + ' editors at most.');
        l.entries = l.entries.filter((e) => e.who !== who.id);   // a team member can't be on the list
        l.editors.push(who.id);
        this.banListLog(l, me, 'share', who.id);
        this.notify(who, 'banlist', me.name + ' added you as an editor of the ban list "' + l.name + '".', l.id);
      }
    } else if (name === 'banlist.add') {
      const bad = this.banListAdd(l, me, args);
      if (bad) return bad;
    } else if (name === 'banlist.remove') {
      const who = this.findPerson(args.who);
      const i = who ? l.entries.findIndex((e) => e.who === who.id) : -1;
      if (i < 0) return fail('They aren\'t on this list.');
      l.entries.splice(i, 1);
      this.banListLog(l, me, 'remove', who.id, clean(str(args, 'note'), kAnswerNote));
    } else if (name === 'banlist.answer') {
      const ap = l.appeals.find((x) => x.id === str(args, 'appeal'));
      if (!ap) return fail('That appeal isn\'t there.');
      if (ap.status !== 'open') return fail('Someone already answered that appeal.');
      ap.status = args.accept === true ? 'accepted' : 'denied';
      ap.note = clean(str(args, 'note'), kAnswerNote);
      ap.answeredBy = me.id; ap.answeredAt = t;
      if (ap.status === 'accepted') l.entries = l.entries.filter((e) => e.who !== ap.who);
      this.banListLog(l, me, ap.status === 'accepted' ? 'accept' : 'deny', ap.who, ap.note);
      const g = this.assets.get(ap.game);
      this.notify(this.users.get(ap.who), 'banappeal', (ap.status === 'accepted' ? 'Your ban appeal was accepted: you can play ' : 'Your ban appeal was turned down for ') +
        (g ? g.name : 'that game') + (ap.note ? '. "' + ap.note + '"' : '.'), ap.game);
    } else {
      return fail('Unknown request.');
    }
    this.banListSave(l);
    return okay({ list: this.banListFull(l, me) });
  };
}
