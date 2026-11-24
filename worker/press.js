// Press: news from the Guts&Bolts team, creator spotlights and behind-the-scenes posts, written
// by staff and read by everyone (website/app/press.js draws the pages, seo.js gives each post a
// real address search engines can read: /press/<id>). src/server/ServerPress.cpp has the same rules.
//
// How it works:
//   - Only staff write posts. A post has a title, a one-line summary, the text (paragraphs split by
//     an empty line; a line starting "## " is a small heading) and a kind: news, spotlight or devlog.
//   - A creator spotlight can name the person it's about (they get a notification when it's
//     published, once) and up to kMostGames games, shown as cards under the post.
//   - Posts can be saved as drafts first: only staff see drafts. Publishing sets the date.
//   - The id is made from the title ("big-winter-jam-3f2a1c") so the address reads well.
//   - Anyone can read; reading counts a view (not for search engines: quiet).
//
// Requests:
//   press.list   {kind, page, drafts}   -> {posts, more, page}  newest first; drafts: staff only
//   press.get    {id, quiet}            -> {post}   (with the text and the games)
//   press.write  {title, summary, text, kind, creator, games, published}  -> {post}   staff
//   press.edit   {id, ...the same}      -> {post}   staff
//   press.delete {id}                   -> {}       staff
import { clean } from './banlists.js';

export const PRESS_KINDS = { news: 'News', spotlight: 'Creator spotlight', devlog: 'Behind the scenes' };
export const kTitle = 120, kSummary = 300, kText = 20000, kMostGames = 6, kPageSize = 20;
export const PRESS_LOOK_ONLY = ['press.list', 'press.get'];
export const PRESS_COSTLY = { 'press.write': [30, 90, 3600], 'press.edit': [120, 360, 3600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');

// "Big Winter Jam!" -> "big-winter-jam" (letters and numbers only, at most 50).
export function pressSlug(title) {
  const s = String(title || '').normalize('NFKD').replace(/[̀-ͯ]/g, '').toLowerCase()
    .replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 50).replace(/-+$/, '');
  return s || 'post';
}
// The text in blocks: [{h: 'heading'} | {p: 'paragraph'}]. Both the app and seo.js draw these.
export function pressBlocks(text) {
  const out = [];
  for (const part of String(text || '').split(/\n\s*\n/)) {
    const s = part.trim();
    if (!s) continue;
    if (s.startsWith('## ')) {
      const [head, ...rest] = s.split('\n');
      out.push({ h: head.slice(3).trim() });
      if (rest.join('\n').trim()) out.push({ p: rest.join('\n').trim() });
    } else out.push({ p: s });
  }
  return out;
}
// About how long it takes to read (minutes, at least 1).
export const readMinutes = (text) => Math.max(1, Math.round(String(text || '').split(/\s+/).filter(Boolean).length / 220));

export function installPress(Cls) {
  const P = Cls.prototype;

  P.pressInit = function () {
    if (this.press) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS press (id TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.press = new Map();
    for (const r of this.sql.exec('SELECT id, data FROM press')) this.press.set(r.id, JSON.parse(r.data));
  };
  P.pressSave = function (p) { this.sql.exec('INSERT OR REPLACE INTO press (id, data) VALUES (?, ?)', p.id, JSON.stringify(p)); };

  P.pressJson = function (p, me, full = false) {
    const who = (id) => { const u = id ? this.users.get(id) : null; return u ? { id: u.id, name: u.name, userId: u.userId, verified: this.isVerified(u) } : null; };
    const out = { id: p.id, title: p.title, summary: p.summary, kind: p.kind, kindName: PRESS_KINDS[p.kind] || 'News',
      published: !!p.published, at: p.published || p.at, edited: p.edited || 0, views: p.views || 0, minutes: readMinutes(p.text),
      by: who(p.by) || { id: p.by, name: 'Guts&Bolts', userId: 0, verified: true }, creator: who(p.creator) };
    const games = (p.games || []).map((id) => this.assets.get(id)).filter((a) => a && a.kind === 'game' && this.canPlay(a, me));
    out.picture = games[0] ? games[0].id : '';
    if (full) Object.assign(out, { text: p.text, blocks: pressBlocks(p.text), games: games.map((a) => this.publicAsset(a, me)),
      gameIds: p.games || [], canEdit: !!me && this.isStaff(me) });
    return out;
  };

  // The fields of press.write / press.edit, checked: { fields } or { error }.
  P.pressFields = function (args) {
    const title = clean(str(args, 'title'), kTitle);
    if (!title) return { error: 'Give the post a title.' };
    const text = clean(str(args, 'text'), kText, true);
    if (!text) return { error: 'Write the post first.' };
    const summary = clean(str(args, 'summary'), kSummary) || pressBlocks(text).map((b) => b.p || '').find(Boolean).replace(/\s+/g, ' ').slice(0, kSummary);
    const kind = Object.hasOwn(PRESS_KINDS, str(args, 'kind')) ? str(args, 'kind') : 'news';
    let creator = '';
    if (args.creator !== undefined && args.creator !== '' && args.creator !== null) {
      const u = this.findPerson(args.creator);
      if (!u) return { error: 'There\'s nobody with that name or number to spotlight.' };
      creator = u.id;
    }
    const raw = Array.isArray(args.games) ? args.games : typeof args.games === 'string' ? args.games.split(/[\s,]+/) : [];
    const games = [];
    for (const g of raw.slice(0, 20)) {
      const a = this.assets.get(String(g).trim());
      if (!String(g).trim()) continue;
      if (!a || a.kind !== 'game') return { error: '"' + String(g).slice(0, 30) + '" isn\'t a game\'s ID.' };
      if (!games.includes(a.id)) games.push(a.id);
    }
    if (games.length > kMostGames) return { error: 'A post can show at most ' + kMostGames + ' games.' };
    return { fields: { title, summary, text, kind, creator, games } };
  };

  P.pressOp = function (name, me, args) {
    this.pressInit();
    const t = now();
    const staff = !!me && me.userId !== 0 && this.isStaff(me);
    if (name === 'press.list') {
      const kind = Object.hasOwn(PRESS_KINDS, str(args, 'kind')) ? str(args, 'kind') : '';
      const drafts = staff && args.drafts === true;
      const list = [...this.press.values()].filter((p) => (drafts ? !p.published : !!p.published) && (!kind || p.kind === kind))
        .sort((x, y) => (y.published || y.at) - (x.published || x.at));
      const page = Math.max(1, Math.min(1000, Math.trunc(Number(args.page) || 1)));
      const from = (page - 1) * kPageSize;
      return okay({ posts: list.slice(from, from + kPageSize).map((p) => this.pressJson(p, me)), more: list.length > from + kPageSize, page,
        kinds: PRESS_KINDS, canWrite: staff });
    }
    const p = this.press.get(str(args, 'id'));
    if (name === 'press.get') {
      if (!p || (!p.published && !staff)) return fail('That post isn\'t here (any more).');
      if (args.quiet !== true && p.published) { p.views = (p.views || 0) + 1; this.pressSave(p); }
      return okay({ post: this.pressJson(p, me, true) });
    }
    if (!staff) return fail('Only Guts&Bolts staff write Press posts.');
    // A creator spotlight that just came out: tell the creator.
    const tellCreator = (post) => {
      const u = post.creator ? this.users.get(post.creator) : null;
      if (u && post.published && post.toldAbout !== u.id) { this.notify(u, 'press', 'You\'re in a Guts&Bolts creator spotlight: "' + post.title.slice(0, 60) + '"', post.id); post.toldAbout = u.id; }
    };
    if (name === 'press.write') {
      const f = this.pressFields(args);
      if (f.error) return fail(f.error);
      let id = pressSlug(f.fields.title) + '-' + hex(3);
      while (this.press.has(id)) id = pressSlug(f.fields.title) + '-' + hex(3);
      const post = Object.assign({ id, by: me.id, at: t, published: args.published === false ? 0 : t, views: 0 }, f.fields);
      tellCreator(post);
      this.press.set(id, post);
      this.pressSave(post);
      this.staffDid(me, 'press', (post.published ? 'Published' : 'Saved a draft of') + ' the Press post "' + post.title + '"');
      return okay({ post: this.pressJson(post, me, true) });
    }
    if (!p) return fail('That post isn\'t here (any more).');
    if (name === 'press.edit') {
      const f = this.pressFields(args);
      if (f.error) return fail(f.error);
      Object.assign(p, f.fields, { edited: t });
      if (args.published === false) p.published = 0;
      else if (args.published === true && !p.published) p.published = t;
      tellCreator(p);
      this.pressSave(p);
      this.staffDid(me, 'press', 'Changed the Press post "' + p.title + '"');
      return okay({ post: this.pressJson(p, me, true) });
    }
    if (name === 'press.delete') {
      this.press.delete(p.id);
      this.sql.exec('DELETE FROM press WHERE id = ?', p.id);
      this.staffDid(me, 'press', 'Deleted the Press post "' + p.title + '"');
      return okay();
    }
    return fail('Unknown request.');
  };
}
