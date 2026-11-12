// G&B Paste: share code, logs and settings files with a link. A small free tool for
// developers, its own product on the site (website/app/paste.js draws its pages).
// src/server/ServerPaste.cpp has the same rules.
//
// How it works:
//   - A paste is some text (up to kMostBytes) with a title and a language (for the label and
//     the file name when you download it). Anyone signed up can make one.
//   - "public" pastes show in the Recent list; "unlisted" ones only open with their link (the id
//     is long and random so nobody can guess it). Both can be reported.
//   - A paste can run out: after 1 hour, 1 day, 1 week or 1 month, or never. Run-out pastes are
//     gone for good (they're deleted the next time anyone looks).
//   - The text is kept exactly as typed (no word filter: that would break code and logs), but
//     Fyxa looks at new pastes and flags anything bad for staff, and people can report them.
//     The title goes through the filter like every other name.
//   - Only the person who made it (or staff) can delete it. Pastes can't be changed: make a new
//     one instead ("Copy and edit" on the website).
//   - Not-Verified people can make kPerDay pastes a day (Verified: kPerDayVerified).
//
// Requests:
//   paste.make   {title, text, lang, visibility, expires}  -> {paste}   expires: hours (0 = never)
//   paste.get    {id}                       -> {paste}    (guests too; counts a view)
//   paste.list   {page, lang, by}           -> {pastes, more}   public ones, newest first (guests too)
//   paste.mine   {}                         -> {pastes}   yours, unlisted ones too
//   paste.delete {id}                       -> {}
// GET /pasteraw/<id> is the text alone (text/plain), for curl and scripts.
import { clean } from './banlists.js';

export const PASTE_LANGS = ['text', 'lua', 'luau', 'python', 'javascript', 'json', 'cpp', 'c', 'csharp', 'html', 'css', 'bash', 'log', 'markdown', 'other'];
export const PASTE_EXPIRES = [0, 1, 24, 168, 720];   // hours: never, 1 hour, 1 day, 1 week, 1 month
export const kTitle = 80, kMostBytes = 100 * 1024, kPageSize = 30, kPerDay = 30, kPerDayVerified = 200, kMostEach = 2000;
export const PASTE_LOOK_ONLY = ['paste.get', 'paste.list'];
export const PASTE_COSTLY = { 'paste.make': [60, 180, 3600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');
const enc = new TextEncoder();

// The text as typed: no control characters except tab and new lines, \r\n made \n, at most
// kMostBytes (null when it's bigger). Not trimmed inside, only spaces and empty lines at the very end.
export function pasteText(s) {
  let out = '', bytes = 0;
  for (const ch of String(s || '').replace(/\r\n?/g, '\n')) {
    const c = ch.codePointAt(0);
    if (c < 32 && ch !== '\n' && ch !== '\t') continue;
    if (c === 127) continue;
    const b = enc.encode(ch).length;
    if (bytes + b > kMostBytes) return null;   // too big: say so, don't cut someone's log in half
    out += ch; bytes += b;
  }
  return out.replace(/[ \t\n]+$/, '');
}

export function installPaste(Cls) {
  const P = Cls.prototype;

  P.pasteInit = function () {
    if (this.pastes) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS pastes (id TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.pastes = new Map();
    for (const r of this.sql.exec('SELECT id, data FROM pastes')) this.pastes.set(r.id, JSON.parse(r.data));
  };
  P.pasteSave = function (p) { this.sql.exec('INSERT OR REPLACE INTO pastes (id, data) VALUES (?, ?)', p.id, JSON.stringify(p)); };
  P.pasteDrop = function (p) { this.pastes.delete(p.id); this.sql.exec('DELETE FROM pastes WHERE id = ?', p.id); };
  // The paste, unless it has run out (then it's deleted now).
  P.pasteLive = function (id) {
    this.pasteInit();
    const p = this.pastes.get(String(id || ''));
    if (p && p.until && p.until <= now()) { this.pasteDrop(p); return null; }
    return p || null;
  };
  P.pasteJson = function (p, me, full = false) {
    const u = this.users.get(p.by);
    const out = { id: p.id, title: p.title, lang: p.lang, visibility: p.visibility, at: p.at, until: p.until || 0,
      lines: p.lines, size: p.size, views: p.views || 0,
      by: u ? { id: u.id, name: u.name, userId: u.userId, verified: this.isVerified(u) } : { id: p.by, name: '?', userId: 0, verified: false } };
    if (full) Object.assign(out, { text: p.text, mine: !!me && p.by === me.id, canDelete: !!me && (p.by === me.id || this.isStaff(me)) });
    return out;
  };
  // For report.send (server.js).
  P.pasteReportTarget = function (id, me) {
    const p = this.pasteLive(id);
    if (!p) return 'That paste isn\'t there any more.';
    if (p.by === me.id) return 'You can\'t report yourself.';
    return { target: p.id, about: p.by, copy: { subject: 'Paste: ' + p.title, body: p.text.slice(0, 2000), at: p.at } };
  };
  P.pasteReportName = function (target) { const p = this.pasteLive(target); return p ? p.title : '(deleted)'; };
  // GET /pasteraw/<id>
  P.pasteRawResponse = function (id) {
    let key = '';
    try { key = decodeURIComponent(id || ''); } catch { key = ''; }
    const p = this.pasteLive(key);
    if (!p) return new Response('No paste.', { status: 404, headers: { 'content-type': 'text/plain; charset=utf-8' } });
    return new Response(p.text, { headers: { 'content-type': 'text/plain; charset=utf-8', 'x-content-type-options': 'nosniff',
      'cache-control': 'public, max-age=60', 'content-security-policy': "default-src 'none'; sandbox" } });
  };

  P.pasteOp = function (name, me, args) {
    this.pasteInit();
    const t = now();
    if (name === 'paste.list') {
      for (const p of [...this.pastes.values()]) if (p.until && p.until <= t) this.pasteDrop(p);
      const lang = PASTE_LANGS.includes(str(args, 'lang')) ? str(args, 'lang') : '';
      let by = null;
      if (args.by !== undefined && args.by !== '') {
        by = this.findPerson(args.by);
        if (!by) return fail('There\'s nobody with that name or number.');
      }
      const page = Math.max(1, Math.min(1000, Math.trunc(Number(args.page) || 1)));
      const list = [...this.pastes.values()].filter((p) => p.visibility === 'public' && (!lang || p.lang === lang) && (!by || p.by === by.id))
        .sort((x, y) => y.at - x.at);
      const from = (page - 1) * kPageSize;
      return okay({ pastes: list.slice(from, from + kPageSize).map((p) => this.pasteJson(p, me)), more: list.length > from + kPageSize, page,
        langs: PASTE_LANGS });
    }
    if (name === 'paste.get') {
      const p = this.pasteLive(str(args, 'id'));
      if (!p) return fail('That paste isn\'t here. It may have run out or been deleted.');
      if (!me || p.by !== me.id) { p.views = (p.views || 0) + 1; this.pasteSave(p); }
      return okay({ paste: this.pasteJson(p, me, true) });
    }
    if (!me || me.userId === 0) return fail('Sign up to make pastes.');
    if (name === 'paste.mine') {
      const list = [...this.pastes.values()].filter((p) => p.by === me.id && !(p.until && p.until <= t)).sort((x, y) => y.at - x.at);
      return okay({ pastes: list.map((p) => this.pasteJson(p, me)) });
    }
    if (name === 'paste.make') {
      const text = pasteText(str(args, 'text'));
      if (text === null) return fail('That\'s too long (the most is ' + (kMostBytes >> 10) + ' KB).');
      if (!text.trim()) return fail('Paste something first.');
      const title = clean(str(args, 'title'), kTitle) || 'Untitled';
      const lang = PASTE_LANGS.includes(str(args, 'lang')) ? str(args, 'lang') : 'text';
      const visibility = str(args, 'visibility') === 'unlisted' ? 'unlisted' : 'public';
      const hours = Math.trunc(Number(args.expires) || 0);
      if (!PASTE_EXPIRES.includes(hours)) return fail('Pick when it runs out: never, 1 hour, 1 day, 1 week or 1 month.');
      const today = Math.floor(t / 86400);
      if (me.pasteDay !== today) { me.pasteDay = today; me.pastesToday = 0; }
      const most = this.isVerified(me) || this.isStaff(me) ? kPerDayVerified : kPerDay;
      if (me.pastesToday >= most) return fail('You\'ve made ' + most + ' pastes today. Come back tomorrow!');
      const mine = [...this.pastes.values()].filter((p) => p.by === me.id && !(p.until && p.until <= t));
      if (mine.length >= kMostEach) return fail('You have ' + kMostEach + ' pastes. Delete some old ones first.');
      const p = { id: hex(6), by: me.id, title, lang, visibility, text, at: t, until: hours ? t + hours * 3600 : 0,
        lines: text.split('\n').length, size: enc.encode(text).length, views: 0 };
      this.pastes.set(p.id, p);
      this.pasteSave(p);
      me.pastesToday += 1;
      this.saveUser(me);
      return okay({ paste: this.pasteJson(p, me, true) });
    }
    if (name === 'paste.delete') {
      const p = this.pasteLive(str(args, 'id'));
      if (!p) return fail('That paste isn\'t here (any more).');
      if (p.by !== me.id && !this.isStaff(me)) return fail('Only the person who made it, or staff, can delete it.');
      this.pasteDrop(p);
      return okay();
    }
    return fail('Unknown request.');
  };
}
