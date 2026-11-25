// G&B Boards: plan things with cards in columns ("To do", "Doing", "Done"), alone or with
// friends. Good for planning a game, tracking bugs, a group's event or homework. Its own
// product on the site, in the Productivity group (website/app/boards.js draws its pages).
// src/server/ServerBoards.cpp has the same rules.
//
// How it works:
//   - A board has a title, a few words about it, a colour, and columns. Each column has
//     cards. A card has a title, notes, colour labels, a due date, a checklist, the people
//     doing it, and can be ticked done.
//   - The person who made it (the owner) can add up to kMembers people by name. Everyone on
//     the board can add, change and move columns and cards. Only the owner changes the
//     board's own settings, adds or removes people, or deletes it (staff can delete one too).
//     Anyone on it can leave.
//   - Who can look: 'private' (only the people on it), 'link' (anyone with the link can look,
//     not change), or 'public' (anyone, and it's in the list of public boards). Public and
//     link boards can be reported.
//   - Words go through the word filter like every other name and post.
//   - People are told (the bell) when they're added to a board or put on a card.
//   - Free. Up to kBoardsEach boards each, kColumns columns and kCards cards on a board.
//
// Requests:
//   board.mine   {}                         -> {boards}     yours and the ones you're on
//   board.list   {page}                     -> {boards, more, page}   public ones (guests too)
//   board.get    {id}                       -> {board}      (guests too, for link and public boards)
//   board.make   {title, template}          -> {board}
//   board.set    {id, title, text, visibility, color}  -> {board}   (the owner)
//   board.delete {id}                       -> {}           (the owner or staff)
//   board.member {id, who, on}              -> {board}      (the owner; or yourself with on: false, to leave)
//   board.column {id, do, column, title, to}  -> {board}    do: add, rename, move, delete
//   board.card   {id, do, card, column, to, title, text, labels, due, checks, people, done}  -> {board}
//                                                           do: add, edit, move, delete
import { clean } from './banlists.js';

export const BOARD_COLORS = ['red', 'orange', 'yellow', 'green', 'blue', 'purple', 'gray'];
export const BOARD_SEES = ['private', 'link', 'public'];
export const kTitle = 80, kText = 500, kColTitle = 40, kCardTitle = 120, kCardText = 2000, kChecks = 30, kCheckText = 120;
export const kBoardsEach = 50, kColumns = 20, kCards = 500, kMembers = 30, kPageSize = 30;
// The boards you can start from: column names, and a few starter cards.
export const BOARD_TEMPLATES = {
  blank: { name: 'To do, Doing, Done', columns: [['To do', []], ['Doing', []], ['Done', []]] },
  game: { name: 'Make a game', columns: [
    ['Ideas', ['Write down what the game is about', 'Pick a name']],
    ['To build', ['Make the map', 'Script the main idea', 'Add sounds']],
    ['Testing', ['Play it with friends']],
    ['Done', []]] },
  bugs: { name: 'Bug tracker', columns: [['Reported', []], ['Fixing', []], ['Check the fix', []], ['Fixed', []]] },
  event: { name: 'Plan an event', columns: [
    ['Plan', ['Pick a day and time', 'Pick the game or place']],
    ['Tell people', ['Post it in the group']],
    ['On the day', []],
    ['Done', []]] },
};
export const BOARDS_LOOK_ONLY = ['board.list', 'board.get'];
export const BOARDS_COSTLY = { 'board.make': [30, 90, 3600], 'board.card': [600, 1800, 3600], 'board.column': [200, 600, 3600], 'board.member': [60, 180, 3600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');

// Labels: colours from the list, no repeats.
export function boardLabels(list) {
  const out = [];
  for (const x of Array.isArray(list) ? list : []) if (BOARD_COLORS.includes(x) && !out.includes(x)) out.push(x);
  return out;
}
// A due date: "2026-12-31" or "" (no date).
export function boardDue(s) {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(String(s || ''));
  if (!m) return '';
  const d = new Date(Date.UTC(+m[1], +m[2] - 1, +m[3]));
  return d.getUTCFullYear() === +m[1] && d.getUTCMonth() === +m[2] - 1 && d.getUTCDate() === +m[3] && +m[1] >= 2000 && +m[1] <= 2200 ? m[0] : '';
}
const countCards = (b) => b.columns.reduce((n, c) => n + c.cards.length, 0);
const place = (list, x, to) => {   // put x into list at spot `to` (the end if it's missing or too big)
  const i = Math.trunc(Number(to));
  list.splice(Number.isFinite(i) && i >= 0 && i <= list.length ? i : list.length, 0, x);
};

export function installBoards(Cls) {
  const P = Cls.prototype;

  P.boardInit = function () {
    if (this.boards) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS boards (id TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.boards = new Map();
    for (const r of this.sql.exec('SELECT id, data FROM boards')) this.boards.set(r.id, JSON.parse(r.data));
  };
  P.boardSave = function (b) { this.sql.exec('INSERT OR REPLACE INTO boards (id, data) VALUES (?, ?)', b.id, JSON.stringify(b)); };
  P.boardOn = function (b, me) { return !!me && (b.by === me.id || b.members.includes(me.id)); };
  P.boardCanSee = function (b, me) { return !!b && (b.visibility !== 'private' || this.boardOn(b, me) || (!!me && this.isStaff(me))); };
  P.boardPerson = function (id) {
    const u = this.users.get(id);
    return u ? { id: u.id, name: u.name, userId: u.userId, verified: this.isVerified(u) } : { id, name: '?', userId: 0, verified: false };
  };
  // The short version for lists.
  P.boardSummary = function (b) {
    let done = 0;
    for (const c of b.columns) for (const k of c.cards) if (k.done) done++;
    return { id: b.id, title: b.title, color: b.color, visibility: b.visibility, by: this.boardPerson(b.by), people: b.members.length + 1,
      columns: b.columns.length, cards: countCards(b), done, edited: b.edited || b.at, at: b.at };
  };
  P.boardJson = function (b, me) {
    const on = this.boardOn(b, me);
    return Object.assign(this.boardSummary(b), {
      text: b.text, owner: !!me && b.by === me.id, canEdit: on, canDelete: !!me && (b.by === me.id || this.isStaff(me)),
      members: b.members.map((id) => this.boardPerson(id)),
      lists: b.columns.map((c) => ({ id: c.id, title: c.title, cards: c.cards.map((k) => ({
        id: k.id, title: k.title, text: k.text, labels: k.labels, due: k.due, done: !!k.done, at: k.at, by: this.boardPerson(k.by),
        checks: k.checks.map((x) => ({ id: x.id, text: x.text, done: !!x.done })),
        people: k.people.map((id) => this.boardPerson(id)) })) })),
    });
  };
  // For report.send (server.js): only boards people other than its own can see.
  P.boardReportTarget = function (id, me) {
    this.boardInit();
    const b = this.boards.get(String(id || ''));
    if (!b || !this.boardCanSee(b, me)) return 'That board isn\'t there any more.';
    if (b.by === me.id) return 'You can\'t report yourself.';
    const words = [b.text, ...b.columns.flatMap((c) => [c.title + ':', ...c.cards.map((k) => '- ' + k.title + (k.text ? ': ' + k.text : ''))])];
    return { target: b.id, about: b.by, copy: { subject: 'Board: ' + b.title, body: words.filter(Boolean).join('\n').slice(0, 2000), at: b.at } };
  };
  P.boardReportName = function (target) { this.boardInit(); const b = this.boards.get(String(target || '')); return b ? b.title : '(deleted)'; };

  P.boardOp = function (name, me, args) {
    this.boardInit();
    const t = now();
    if (name === 'board.list') {
      const page = Math.max(1, Math.min(1000, Math.trunc(Number(args.page) || 1)));
      const list = [...this.boards.values()].filter((b) => b.visibility === 'public').sort((x, y) => (y.edited || y.at) - (x.edited || x.at));
      const from = (page - 1) * kPageSize;
      return okay({ boards: list.slice(from, from + kPageSize).map((b) => this.boardSummary(b)), more: list.length > from + kPageSize, page });
    }
    const b = this.boards.get(str(args, 'id'));
    if (name === 'board.get') {
      if (!this.boardCanSee(b, me)) return fail('That board isn\'t here, or it\'s private.');
      return okay({ board: this.boardJson(b, me) });
    }

    if (!me || me.userId === 0) return fail('Sign up to make boards.');
    if (name === 'board.mine') {
      const list = [...this.boards.values()].filter((x) => this.boardOn(x, me)).sort((x, y) => (y.edited || y.at) - (x.edited || x.at));
      return okay({ boards: list.map((x) => this.boardSummary(x)) });
    }
    if (name === 'board.make') {
      const title = clean(str(args, 'title'), kTitle);
      if (!title) return fail('Give your board a name.');
      if ([...this.boards.values()].filter((x) => x.by === me.id).length >= kBoardsEach) return fail('You have ' + kBoardsEach + ' boards. Delete an old one first.');
      const tpl = BOARD_TEMPLATES[str(args, 'template')] || BOARD_TEMPLATES.blank;
      const board = { id: 'bd-' + hex(6), by: me.id, title, text: '', visibility: 'private', color: 'red', members: [], at: t, edited: t,
        columns: tpl.columns.map(([ct, cards]) => ({ id: hex(4), title: ct, cards: cards.map((k) => ({
          id: hex(4), title: k, text: '', labels: [], due: '', checks: [], people: [], done: false, at: t, by: me.id })) })) };
      this.boards.set(board.id, board);
      this.boardSave(board);
      return okay({ board: this.boardJson(board, me) });
    }

    if (!this.boardCanSee(b, me)) return fail('That board isn\'t here (any more).');
    const owner = b.by === me.id;
    if (name === 'board.delete') {
      if (!owner && !this.isStaff(me)) return fail('Only the board\'s owner, or staff, can delete it.');
      this.boards.delete(b.id);
      this.sql.exec('DELETE FROM boards WHERE id = ?', b.id);
      return okay();
    }
    if (name === 'board.member') {
      let who = null;
      if (str(args, 'who') === me.id) who = me;
      else if (args.who !== undefined && args.who !== '') who = this.findPerson(args.who);
      if (!who) return fail('There\'s nobody with that name or number.');
      const on = args.on !== false;
      if (who.id === me.id && !on) {   // leaving
        if (owner) return fail('You own this board. Delete it instead, if you\'re done with it.');
        if (!b.members.includes(me.id)) return fail('You\'re not on this board.');
        b.members = b.members.filter((x) => x !== me.id);
      } else {
        if (!owner) return fail('Only the board\'s owner can add or remove people.');
        if (who.id === b.by) return fail('That\'s you: you own this board.');
        if (on) {
          if (who.userId === 0) return fail('They need to sign up first.');
          if (b.members.includes(who.id)) return fail(who.name + ' is already on this board.');
          if (b.members.length >= kMembers) return fail('A board can have ' + kMembers + ' people on it at most.');
          b.members.push(who.id);
          this.notify(who, 'board', me.name + ' added you to the board "' + b.title + '".', b.id);
        } else {
          if (!b.members.includes(who.id)) return fail(who.name + ' isn\'t on this board.');
          b.members = b.members.filter((x) => x !== who.id);
        }
      }
      if (!on) for (const c of b.columns) for (const k of c.cards) k.people = k.people.filter((x) => x !== who.id);   // (off their cards too)
      b.edited = t;
      this.boardSave(b);
      return okay({ board: this.boardJson(b, me) });
    }
    if (!this.boardOn(b, me)) return fail('Only people on this board can change it. Ask its owner to add you.');
    if (name === 'board.set') {
      if (!owner) return fail('Only the board\'s owner can change its settings.');
      if (args.title !== undefined) {
        const title = clean(str(args, 'title'), kTitle);
        if (!title) return fail('Give your board a name.');
        b.title = title;
      }
      if (args.text !== undefined) b.text = clean(str(args, 'text'), kText, true);
      if (args.visibility !== undefined) b.visibility = BOARD_SEES.includes(str(args, 'visibility')) ? str(args, 'visibility') : 'private';
      if (args.color !== undefined) b.color = BOARD_COLORS.includes(str(args, 'color')) ? str(args, 'color') : 'red';
      b.edited = t;
      this.boardSave(b);
      return okay({ board: this.boardJson(b, me) });
    }
    const what = str(args, 'do');
    if (name === 'board.column') {
      const col = b.columns.find((c) => c.id === str(args, 'column'));
      if (what === 'add') {
        const title = clean(str(args, 'title'), kColTitle);
        if (!title) return fail('Give the column a name.');
        if (b.columns.length >= kColumns) return fail('A board can have ' + kColumns + ' columns at most.');
        place(b.columns, { id: hex(4), title, cards: [] }, args.to);
      } else {
        if (!col) return fail('That column isn\'t there any more.');
        if (what === 'rename') {
          const title = clean(str(args, 'title'), kColTitle);
          if (!title) return fail('Give the column a name.');
          col.title = title;
        } else if (what === 'move') {
          b.columns = b.columns.filter((c) => c !== col);
          place(b.columns, col, args.to);
        } else if (what === 'delete') {
          b.columns = b.columns.filter((c) => c !== col);
        } else return fail('Unknown request.');
      }
      b.edited = t;
      this.boardSave(b);
      return okay({ board: this.boardJson(b, me) });
    }
    if (name === 'board.card') {
      const people = new Set([b.by, ...b.members]);
      let col = b.columns.find((c) => c.cards.some((k) => k.id === str(args, 'card')));
      let card = col ? col.cards.find((k) => k.id === str(args, 'card')) : null;
      const target = b.columns.find((c) => c.id === str(args, 'column'));
      // The bits of a card that were sent (the ones left out stay as they are).
      const fill = (k) => {
        if (args.title !== undefined) {
          const title = clean(str(args, 'title'), kCardTitle);
          if (!title) return 'Give the card a name.';
          k.title = title;
        }
        if (args.text !== undefined) k.text = clean(str(args, 'text'), kCardText, true);
        if (args.labels !== undefined) k.labels = boardLabels(args.labels);
        if (args.due !== undefined) k.due = boardDue(args.due);
        if (args.done !== undefined) k.done = args.done === true;
        if (args.checks !== undefined) {
          const list = Array.isArray(args.checks) ? args.checks : [];
          if (list.length > kChecks) return 'A checklist can have ' + kChecks + ' things on it at most.';
          k.checks = list.map((x) => ({ id: typeof x.id === 'string' && /^[0-9a-f]{8}$/.test(x.id) ? x.id : hex(4),
            text: clean(typeof x.text === 'string' ? x.text : '', kCheckText), done: x.done === true })).filter((x) => x.text);
        }
        if (args.people !== undefined) {
          const before = new Set(k.people);
          k.people = [...new Set(Array.isArray(args.people) ? args.people : [])].filter((id) => people.has(id));
          for (const id of k.people) if (!before.has(id) && id !== me.id)
            this.notify(this.users.get(id), 'board', me.name + ' put you on the card "' + k.title + '" in "' + b.title + '".', b.id);
        }
        return '';
      };
      if (what === 'add') {
        if (!target) return fail('That column isn\'t there any more.');
        if (countCards(b) >= kCards) return fail('A board can have ' + kCards + ' cards at most. Delete some done ones first.');
        if (!clean(str(args, 'title'), kCardTitle)) return fail('Give the card a name.');
        card = { id: hex(4), title: '', text: '', labels: [], due: '', checks: [], people: [], done: false, at: t, by: me.id };
        const why = fill(card);
        if (why) return fail(why);
        place(target.cards, card, args.to);
      } else {
        if (!card) return fail('That card isn\'t there any more.');
        if (what === 'edit') {
          const before = JSON.stringify(card), why = fill(card);
          if (why) { Object.assign(card, JSON.parse(before)); return fail(why); }
        } else if (what === 'move') {
          if (!target) return fail('That column isn\'t there any more.');
          col.cards = col.cards.filter((k) => k !== card);
          place(target.cards, card, args.to);
        } else if (what === 'delete') {
          col.cards = col.cards.filter((k) => k !== card);
        } else return fail('Unknown request.');
      }
      b.edited = t;
      this.boardSave(b);
      return okay({ board: this.boardJson(b, me) });
    }
    return fail('Unknown request.');
  };
}
