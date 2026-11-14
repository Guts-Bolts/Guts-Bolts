// Art: the Guts&Bolts art portal. People post drawings, pixel art, comics, renders and
// screenshots; everyone can look, fave and comment. It's its own product (website/app/art.js
// draws its pages), sharing the same accounts, notifications, reports and staff tools.
// src/server/ServerArt.cpp has the same rules.
//
// How it works:
//   - A piece is one picture (PNG, JPG, GIF or WebP, up to kMostBytes) with a title, a few words,
//     up to kMostTags tags and one category. The website also sends a small preview picture
//     (thumb) so the gallery doesn't load every full-size picture.
//   - Like decals, pictures from people who aren't Verified wait for a staff check before anyone
//     else can see them ('pending'). Verified creators and staff go straight to the gallery.
//     Fyxa also looks at every new picture and flags anything bad for staff (server.js).
//   - Faves: a heart. The gallery can sort by newest, best this week, or most faved ever.
//   - Comments: up to kCommentText letters; the artist is told. The writer, the artist and staff
//     can delete one.
//   - Only the artist can change the words; the artist or staff can delete a piece.
//   - Posting is free. Not-Verified people can post kPostsPerDay a day (Verified: kPostsPerDayVerified).
//
// Requests:
//   art.list    {sort, cat, tag, by, q, page}   -> {pieces, more, page}    (guests too)
//   art.get     {id}                            -> {piece}                 (guests too; counts a view)
//   art.picture {id, thumb}                     -> {data, type}            (guests too; the website uses /artpic/<id>)
//   art.post    {title, text, tags, cat, data, thumb}  -> {piece}
//   art.edit    {id, title, text, tags, cat}    -> {piece}                 (the artist)
//   art.delete  {id}                            -> {}                      (the artist or staff)
//   art.fave    {id, on}                        -> {faves, faved}
//   art.comment {id, text}                      -> {piece}
//   art.uncomment {id, comment}                 -> {piece}
//   art.pending {}                              -> {pieces}                (staff: waiting for a check)
//   art.review  {id, ok, note}                  -> {piece}                 (staff)
import { clean } from './banlists.js';

export const ART_CATS = ['drawing', 'painting', 'pixel', 'comic', 'fanart', '3d', 'screenshot', 'photo', 'other'];
export const kTitle = 60, kText = 1000, kTag = 20, kMostTags = 8, kCommentText = 500, kCommentsKept = 500;
export const kMostBytes = 2 * 1024 * 1024, kThumbBytes = 200 * 1024, kPageSize = 24;
export const kPostsPerDay = 5, kPostsPerDayVerified = 25;
// Requests guests can make, and how often people can post (server.js adds these to its lists).
export const ART_LOOK_ONLY = ['art.list', 'art.get', 'art.picture'];
export const ART_COSTLY = { 'art.post': [30, 90, 3600], 'art.comment': [30, 90, 600], 'art.fave': [240, 720, 600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');
const utcDay = (t) => Math.floor(t / 86400);

function fromB64(s) {
  const bin = atob(String(s || '').replace(/^data:[^,]+,/, ''));
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
function toB64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

// What kind of picture these bytes are, by their first bytes (never by what the sender says).
export function artType(d) {
  if (!d || d.length < 12) return '';
  if ([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a].every((v, i) => d[i] === v)) return 'png';
  if (d[0] === 0xff && d[1] === 0xd8 && d[2] === 0xff) return 'jpg';
  if (d[0] === 0x47 && d[1] === 0x49 && d[2] === 0x46 && d[3] === 0x38) return 'gif';
  if (d[0] === 0x52 && d[1] === 0x49 && d[2] === 0x46 && d[3] === 0x46 && d[8] === 0x57 && d[9] === 0x45 && d[10] === 0x42 && d[11] === 0x50) return 'webp';
  return '';
}
export const ART_MIME = { png: 'image/png', jpg: 'image/jpeg', gif: 'image/gif', webp: 'image/webp' };

// Tags: lower case letters, numbers and dashes, no repeats.
export function artTags(list) {
  const out = [];
  for (const t of Array.isArray(list) ? list : String(list || '').split(/[,\s]+/)) {
    const tag = String(t || '').toLowerCase().replace(/^#/, '').replace(/[^a-z0-9-]/g, '').slice(0, kTag);
    if (tag && !out.includes(tag)) out.push(tag);
    if (out.length >= kMostTags) break;
  }
  return out;
}

export function installArt(Cls) {
  const P = Cls.prototype;

  // The pieces, kept in their own table (one row each) and in memory.
  P.artInit = function () {
    if (this.art) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS art (id TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.art = new Map();
    for (const r of this.sql.exec('SELECT id, data FROM art')) this.art.set(r.id, JSON.parse(r.data));
  };
  P.artSave = function (p) {
    if (p.comments.length > kCommentsKept) p.comments = p.comments.slice(-kCommentsKept);
    this.sql.exec('INSERT OR REPLACE INTO art (id, data) VALUES (?, ?)', p.id, JSON.stringify(p));
  };
  P.artCanSee = function (p, me) {
    return !!p && (!p.review || (me && (p.by === me.id || this.isStaff(me))));
  };
  P.artPerson = function (id) {
    const u = this.users.get(id);
    return u ? { id: u.id, name: u.name, username: u.username || '', userId: u.userId, verified: this.isVerified(u) }
      : { id, name: '?', username: '', userId: 0, verified: false };
  };
  // A piece as the website sees it (full: with comments and whether you faved it).
  P.artJson = function (p, me, full = false) {
    const out = { id: p.id, title: p.title, cat: p.cat, tags: p.tags, type: p.type, at: p.at, by: this.artPerson(p.by),
      faves: p.faves.length, views: p.views || 0, comments: p.comments.length, review: p.review || undefined };
    if (!full) return out;
    const mine = !!me && p.by === me.id, staff = !!me && this.isStaff(me);
    return Object.assign(out, {
      text: p.text, edited: p.edited || 0, reviewNote: (mine || staff) ? p.reviewNote || undefined : undefined,
      faved: !!me && p.faves.includes(me.id), mine, canDelete: mine || staff,
      commentList: p.comments.map((c) => ({ id: c.id, text: c.text, at: c.at, by: this.artPerson(c.by),
        canDelete: !!me && (c.by === me.id || mine || staff) })).reverse(),   // newest first
    });
  };
  // For report.send (server.js): what's being reported, and a copy for staff.
  P.artReportTarget = function (id, me) {
    this.artInit();
    const [pid, cid] = String(id || '').split(':');
    const p = this.art.get(pid);
    if (!p || !this.artCanSee(p, me)) return 'That isn\'t there any more.';
    if (!cid) {
      if (p.by === me.id) return 'You can\'t report yourself.';
      return { target: p.id, about: p.by, copy: { subject: 'Art: ' + p.title, body: p.text || '(no description)', at: p.at } };
    }
    const c = p.comments.find((x) => x.id === cid);
    if (!c) return 'That comment isn\'t there any more.';
    if (c.by === me.id) return 'You can\'t report yourself.';
    return { target: p.id + ':' + c.id, about: c.by, copy: { subject: 'Comment on art: ' + p.title, body: c.text, at: c.at } };
  };
  P.artReportName = function (target) {
    this.artInit();
    const p = this.art.get(String(target || '').split(':')[0]);
    return p ? p.title : '(deleted)';
  };
  // GET /artpic/<id> (and /artpic/<id>/thumb): the picture itself, for <img> tags.
  P.artPictureResponse = function (path) {
    const [id, which] = path.split('/').map((x) => { try { return decodeURIComponent(x || ''); } catch { return ''; } });
    const r = this.artOp('art.picture', null, { id, thumb: which === 'thumb' });
    if (!r.ok) return new Response('No picture.', { status: 404 });
    return new Response(r.bytes, { headers: { 'content-type': ART_MIME[r.type] || 'application/octet-stream',
      'cache-control': 'public, max-age=86400', 'x-content-type-options': 'nosniff' } });
  };

  P.artOp = function (name, me, args) {
    this.artInit();
    const t = now();
    if (name === 'art.list') {
      const sort = ['new', 'week', 'top'].includes(str(args, 'sort')) ? str(args, 'sort') : 'new';
      const cat = ART_CATS.includes(str(args, 'cat')) ? str(args, 'cat') : '';
      const tag = artTags([str(args, 'tag')])[0] || '';
      const q = str(args, 'q').trim().toLowerCase().slice(0, 60);
      let by = null;
      if (args.by !== undefined && args.by !== '') {
        by = this.findPerson(args.by);
        if (!by) return fail('There\'s nobody with that name or number.');
      }
      const page = Math.max(1, Math.min(1000, Math.trunc(Number(args.page) || 1)));
      let list = [...this.art.values()].filter((p) =>
        (by ? p.by === by.id && this.artCanSee(p, me) : !p.review) &&
        (!cat || p.cat === cat) && (!tag || p.tags.includes(tag)) &&
        (!q || p.title.toLowerCase().includes(q) || p.tags.some((x) => x.includes(q))));
      if (sort === 'week') list = list.filter((p) => p.at > t - 7 * 86400);
      list.sort(sort === 'new' ? (x, y) => y.at - x.at : (x, y) => y.faves.length - x.faves.length || y.at - x.at);
      const from = (page - 1) * kPageSize;
      return okay({ pieces: list.slice(from, from + kPageSize).map((p) => this.artJson(p, me)), more: list.length > from + kPageSize, page,
        total: list.length, cats: ART_CATS });
    }
    const p = this.art.get(str(args, 'id'));
    if (name === 'art.picture') {
      if (!this.artCanSee(p, me)) return fail('No picture.');
      const thumb = args.thumb === true && p.thumb;
      const bytes = this.readFile((thumb ? 'artthumb:' : 'art:') + p.id);
      if (!bytes) return fail('No picture.');
      const r = okay({ type: thumb ? p.thumb : p.type });
      Object.defineProperty(r, 'bytes', { value: bytes, enumerable: false });   // (for /artpic, not sent as JSON)
      if (me) r.data = toB64(bytes);
      return r;
    }
    if (name === 'art.get') {
      if (!this.artCanSee(p, me)) return fail('That piece isn\'t here (any more).');
      if (!me || p.by !== me.id) { p.views = (p.views || 0) + 1; this.artSave(p); }
      return okay({ piece: this.artJson(p, me, true) });
    }

    if (!me || me.userId === 0) return fail('Sign up to post art, fave and comment.');
    const staff = this.isStaff(me), verified = this.isVerified(me);

    if (name === 'art.post' || name === 'art.edit') {
      const title = clean(str(args, 'title'), kTitle);
      if (!title) return fail('Give your piece a title.');
      const text = clean(str(args, 'text'), kText, true);
      const cat = ART_CATS.includes(str(args, 'cat')) ? str(args, 'cat') : 'other';
      const tags = artTags(args.tags);
      if (name === 'art.edit') {
        if (!p || !this.artCanSee(p, me)) return fail('That piece isn\'t here (any more).');
        if (p.by !== me.id) return fail('Only the artist can change this.');
        Object.assign(p, { title, text, cat, tags, edited: t });
        this.artSave(p);
        return okay({ piece: this.artJson(p, me, true) });
      }
      const today = utcDay(t);
      if (me.artDay !== today) { me.artDay = today; me.artToday = 0; }
      const most = verified || staff ? kPostsPerDayVerified : kPostsPerDay;
      if (me.artToday >= most) return fail('You\'ve posted ' + most + ' pieces today. Come back tomorrow!');
      let data, thumb = null;
      try { data = fromB64(str(args, 'data')); } catch { return fail('The picture got scrambled. Try again.'); }
      const type = artType(data);
      if (!type) return fail('Art must be a .png, .jpg, .gif or .webp picture.');
      if (data.length > kMostBytes) return fail('That picture is too big (the most is ' + (kMostBytes >> 20) + ' MB).');
      if (str(args, 'thumb')) {
        try { thumb = fromB64(str(args, 'thumb')); } catch { thumb = null; }
        if (!thumb || !artType(thumb) || thumb.length > kThumbBytes) thumb = null;   // (the gallery uses the full picture instead)
      }
      const piece = { id: 'art-' + hex(6), by: me.id, title, text, cat, tags, type, thumb: thumb ? artType(thumb) : '',
        size: data.length, at: t, edited: 0, views: 0, faves: [], comments: [] };
      if (!verified && !staff) piece.review = 'pending';
      this.writeFile('art:' + piece.id, data);
      if (thumb) this.writeFile('artthumb:' + piece.id, thumb);
      this.art.set(piece.id, piece);
      this.artSave(piece);
      me.artToday += 1;
      this.saveUser(me);
      return okay({ piece: this.artJson(piece, me, true) });
    }
    if (name === 'art.pending') {
      if (!staff) return fail('Only staff can do that.');
      return okay({ pieces: [...this.art.values()].filter((x) => x.review === 'pending').sort((x, y) => x.at - y.at).map((x) => this.artJson(x, me)) });
    }

    if (!p || !this.artCanSee(p, me)) return fail('That piece isn\'t here (any more).');
    if (name === 'art.delete') {
      if (p.by !== me.id && !staff) return fail('Only the artist or staff can delete this.');
      this.art.delete(p.id);
      this.sql.exec('DELETE FROM art WHERE id = ?', p.id);
      this.sql.exec('DELETE FROM files WHERE id = ? OR id = ?', 'art:' + p.id, 'artthumb:' + p.id);
      return okay();
    }
    if (name === 'art.review') {
      if (!staff) return fail('Only staff can do that.');
      if (!p.review) return fail('That isn\'t waiting for a check any more.');
      const artist = this.users.get(p.by);
      if (args.ok === true) {
        delete p.review; delete p.reviewNote;
        this.notify(artist, 'art', 'Your art "' + p.title + '" passed the staff check. Everyone can see it now!', p.id);
      } else {
        p.review = 'rejected'; p.reviewNote = clean(str(args, 'note'), 200);
        this.notify(artist, 'art', 'Your art "' + p.title + '" didn\'t pass the staff check' + (p.reviewNote ? ': ' + p.reviewNote : '.'), p.id);
      }
      this.artSave(p);
      return okay({ piece: this.artJson(p, me, true) });
    }
    if (p.review) return fail('This hasn\'t passed the staff check yet.');
    if (name === 'art.fave') {
      const on = args.on !== false, had = p.faves.includes(me.id);
      if (on && !had) p.faves.push(me.id);
      if (!on && had) p.faves = p.faves.filter((x) => x !== me.id);
      if (on !== had) this.artSave(p);
      return okay({ faves: p.faves.length, faved: on });
    }
    if (name === 'art.comment') {
      const text = clean(str(args, 'text'), kCommentText, true);
      if (!text) return fail('Write something first.');
      p.comments.push({ id: hex(5), by: me.id, text, at: t });
      this.artSave(p);
      if (p.by !== me.id) this.notify(this.users.get(p.by), 'art', me.name + ' commented on your art "' + p.title + '".', p.id);
      return okay({ piece: this.artJson(p, me, true) });
    }
    if (name === 'art.uncomment') {
      const c = p.comments.find((x) => x.id === str(args, 'comment'));
      if (!c) return fail('That comment isn\'t there any more.');
      if (c.by !== me.id && p.by !== me.id && !staff) return fail('You can only delete your own comments, or ones on your art.');
      p.comments = p.comments.filter((x) => x !== c);
      this.artSave(p);
      return okay({ piece: this.artJson(p, me, true) });
    }
    return fail('Unknown request.');
  };
}
