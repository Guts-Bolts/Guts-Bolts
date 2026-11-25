// Audio: the Guts&Bolts audio portal. Music, loops and sound effects people uploaded, to listen
// to on the website and to use in games. It's its own product (website/app/audio.js draws its
// pages), sharing the same accounts, notifications, reports and staff tools.
// src/server/ServerAudio.cpp has the same rules.
//
// How it works:
//   - A track IS a Library audio asset (kind 'audio', uploaded with the normal 'upload' request).
//     So every sound in the portal already has an ID a game can use (Sound.SoundId), and every
//     public sound people uploaded before the portal existed shows up in it straight away.
//   - The portal keeps its own extra bits for each track in its own table: a category (music,
//     loop, sound effect...), a genre, tags, how many times it was played, faves and comments.
//   - Only public sounds that passed the staff check are in the portal lists. Private ones still
//     show up in "My audio" for the person who made them.
//   - Plays: one per person per track per day, and the creator's own don't count.
//   - Faves: a heart. Lists can sort by newest, best this week, most faved and most played.
//   - Comments: up to kCommentText letters; the creator is told. The writer, the creator and staff
//     can delete one.
//
// Requests:
//   audio.list    {sort, cat, genre, tag, by, q, page} -> {tracks, more, page, total}   (guests too)
//   audio.get     {id}                         -> {track}            (guests too)
//   audio.listen  {id}                         -> {plays}            (guests too; counts a play)
//   audio.file    {id}                         -> {data, ext}        (guests too; the website uses /sound/<id>)
//   audio.tag     {id, cat, genre, tags}       -> {track}            (the creator or staff)
//   audio.fave    {id, on}                     -> {faves, faved}
//   audio.comment {id, text}                   -> {track}
//   audio.uncomment {id, comment}              -> {track}
import { clean } from './banlists.js';

export const AUDIO_CATS = ['music', 'loop', 'sfx', 'ambience', 'voice', 'other'];
export const AUDIO_GENRES = ['electronic', 'chiptune', 'rock', 'metal', 'hiphop', 'pop', 'orchestral', 'ambient', 'lofi', 'jazz', 'funk', 'other'];
export const kTag = 20, kMostTags = 8, kCommentText = 500, kCommentsKept = 500, kPageSize = 30, kHeardKept = 500;
// Requests guests can make, and how often people can do things (server.js adds these to its lists).
export const AUDIO_LOOK_ONLY = ['audio.list', 'audio.get', 'audio.listen', 'audio.file'];
export const AUDIO_COSTLY = { 'audio.comment': [30, 90, 600], 'audio.fave': [240, 720, 600], 'audio.listen': [240, 1200, 3600] };
export const AUDIO_MIME = { mp3: 'audio/mpeg', wav: 'audio/wav', ogg: 'audio/ogg', flac: 'audio/flac' };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');
const utcDay = (t) => Math.floor(t / 86400);

function toB64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

// Tags: lower case letters, numbers and dashes, no repeats.
export function audioTags(list) {
  const out = [];
  for (const t of Array.isArray(list) ? list : String(list || '').split(/[,\s]+/)) {
    const tag = String(t || '').toLowerCase().replace(/^#/, '').replace(/[^a-z0-9-]/g, '').slice(0, kTag);
    if (tag && !out.includes(tag)) out.push(tag);
    if (out.length >= kMostTags) break;
  }
  return out;
}

// GET /sound/<id>: the sound file, with Range so the website's player can jump around in it.
export function soundResponse(request, bytes, ext) {
  const type = AUDIO_MIME[ext] || 'application/octet-stream';
  const base = { 'content-type': type, 'accept-ranges': 'bytes', 'cache-control': 'public, max-age=3600', 'x-content-type-options': 'nosniff' };
  const m = /^bytes=(\d*)-(\d*)$/.exec(request.headers.get('range') || '');
  if (m && (m[1] || m[2])) {
    const size = bytes.length;
    let start = m[1] ? Number(m[1]) : Math.max(0, size - Number(m[2]));
    let end = m[1] && m[2] ? Math.min(Number(m[2]), size - 1) : size - 1;
    if (start >= size || start > end) return new Response(null, { status: 416, headers: { 'content-range': 'bytes */' + size } });
    return new Response(bytes.subarray(start, end + 1), { status: 206,
      headers: Object.assign(base, { 'content-range': 'bytes ' + start + '-' + end + '/' + size }) });
  }
  return new Response(bytes, { headers: base });
}

export function installAudio(Cls) {
  const P = Cls.prototype;

  // The portal's extra bits for each sound, kept in their own table (one row each) and in memory.
  P.audioInit = function () {
    if (this.audioInfo) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS audio_portal (id TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.audioInfo = new Map();
    for (const r of this.sql.exec('SELECT id, data FROM audio_portal')) this.audioInfo.set(r.id, JSON.parse(r.data));
  };
  P.audioSave = function (x) {
    if (x.comments.length > kCommentsKept) x.comments = x.comments.slice(-kCommentsKept);
    this.audioInfo.set(x.id, x);
    this.sql.exec('INSERT OR REPLACE INTO audio_portal (id, data) VALUES (?, ?)', x.id, JSON.stringify(x));
  };
  // A sound's extra bits (made the first time anyone asks: older sounds don't have any yet).
  P.audioExtra = function (a) {
    return this.audioInfo.get(a.id) || { id: a.id, cat: 'other', genre: '', tags: [], plays: 0, faves: [], comments: [] };
  };
  // The sound itself (an audio asset), found by its id or its number.
  P.audioAsset = function (id) {
    const key = String(id || '').trim().replace(/^gb:/, '');
    let a = this.assets.get(key);
    if (!a && /^\d+$/.test(key)) a = [...this.assets.values()].find((x) => x.kind === 'audio' && String(x.num) === key);
    return a && a.kind === 'audio' ? a : null;
  };
  // In the portal for everyone: public, and passed the staff check.
  P.audioListed = function (a) {
    return !!a && !a.review && (a.access || 'public') === 'public' && !a.exclusive && !a.promoOnly && this.assets.get(a.id) === a;
  };
  P.audioCanSee = function (a, me) {
    return !!a && (this.audioListed(a) || (!!me && (a.creator === me.id || this.isStaff(me))));
  };
  P.audioPerson = function (id) {
    const u = this.users.get(id);
    return u ? { id: u.id, name: u.name, username: u.username || '', userId: u.userId, verified: this.isVerified(u) }
      : { id, name: 'Guts', username: '', userId: 0, verified: false };
  };
  // A track as the website sees it (full: with comments and whether you faved it).
  P.audioJson = function (a, me, full = false) {
    const x = this.audioExtra(a);
    const out = { id: a.id, num: a.num || 0, title: a.name, cat: x.cat, genre: x.genre, tags: x.tags, ext: (a.meta && a.meta.ext) || '',
      size: a.size || 0, at: a.created || 0, by: this.audioPerson(a.creator), plays: x.plays, faves: x.faves.length,
      comments: x.comments.length, access: a.access || 'public', review: a.review || undefined };
    if (!full) return out;
    const mine = !!me && a.creator === me.id, staff = !!me && this.isStaff(me);
    return Object.assign(out, {
      text: a.description || '', faved: !!me && x.faves.includes(me.id), mine, canTag: mine || staff,
      commentList: x.comments.map((c) => ({ id: c.id, text: c.text, at: c.at, by: this.audioPerson(c.by),
        canDelete: !!me && (c.by === me.id || mine || staff) })).reverse(),   // newest first
    });
  };
  // For report.send (server.js): "<id>" is the sound, "<id>:<comment>" a comment under it.
  P.audioReportTarget = function (id, me) {
    this.audioInit();
    const [aid, cid] = String(id || '').split(':');
    const a = this.audioAsset(aid);
    if (!a || !this.audioCanSee(a, me)) return 'That isn\'t there any more.';
    if (!cid) {
      if (a.creator === me.id) return 'You can\'t report yourself.';
      return { target: a.id, about: a.creator, copy: { subject: 'Audio: ' + a.name, body: a.description || '(no description)', at: a.created || 0 } };
    }
    const c = this.audioExtra(a).comments.find((x) => x.id === cid);
    if (!c) return 'That comment isn\'t there any more.';
    if (c.by === me.id) return 'You can\'t report yourself.';
    return { target: a.id + ':' + c.id, about: c.by, copy: { subject: 'Comment on audio: ' + a.name, body: c.text, at: c.at } };
  };
  P.audioReportName = function (target) {
    const a = this.audioAsset(String(target || '').split(':')[0]);
    return a ? a.name : '(deleted)';
  };
  // GET /sound/<id>: the sound itself, for <audio> tags. Like decals, any sound that passed the
  // staff check plays by its ID (private ones too: games that use them keep working).
  P.audioSoundResponse = function (request, path) {
    let id = '';
    try { id = decodeURIComponent(path); } catch { id = ''; }
    const r = this.audioOp('audio.file', null, { id });
    if (!r.ok) return new Response('No sound.', { status: 404 });
    return soundResponse(request, r.bytes, r.ext);
  };

  P.audioOp = function (name, me, args) {
    this.audioInit();
    const t = now();
    if (name === 'audio.list') {
      const sort = ['new', 'week', 'top', 'plays'].includes(str(args, 'sort')) ? str(args, 'sort') : 'new';
      const cat = AUDIO_CATS.includes(str(args, 'cat')) ? str(args, 'cat') : '';
      const genre = AUDIO_GENRES.includes(str(args, 'genre')) ? str(args, 'genre') : '';
      const tag = audioTags([str(args, 'tag')])[0] || '';
      const q = str(args, 'q').trim().toLowerCase().slice(0, 60);
      let by = null;
      if (args.by !== undefined && args.by !== '') {
        by = this.findPerson(args.by);
        if (!by) return fail('There\'s nobody with that name or number.');
      }
      const page = Math.max(1, Math.min(1000, Math.trunc(Number(args.page) || 1)));
      const list = [];
      for (const a of this.assets.values()) {
        if (a.kind !== 'audio') continue;
        if (by ? !(a.creator === by.id && this.audioCanSee(a, me)) : !this.audioListed(a)) continue;
        const x = this.audioExtra(a);
        if (cat && x.cat !== cat) continue;
        if (genre && x.genre !== genre) continue;
        if (tag && !x.tags.includes(tag)) continue;
        if (q && !String(a.name || '').toLowerCase().includes(q) && !x.tags.some((g) => g.includes(q))) continue;
        if (sort === 'week' && (a.created || 0) <= t - 7 * 86400) continue;
        list.push([a, x]);
      }
      const when = (p) => p[0].created || 0;
      list.sort(sort === 'new' ? (p, q2) => when(q2) - when(p)
        : sort === 'plays' ? (p, q2) => q2[1].plays - p[1].plays || when(q2) - when(p)
          : (p, q2) => q2[1].faves.length - p[1].faves.length || q2[1].plays - p[1].plays || when(q2) - when(p));
      const from = (page - 1) * kPageSize;
      return okay({ tracks: list.slice(from, from + kPageSize).map(([a]) => this.audioJson(a, me)), more: list.length > from + kPageSize,
        page, total: list.length, cats: AUDIO_CATS, genres: AUDIO_GENRES });
    }
    const a = this.audioAsset(str(args, 'id'));
    if (name === 'audio.file') {
      if (!a || a.review) return fail('No sound.');
      const bytes = this.readFile(a.id);
      if (!bytes) return fail('No sound.');
      const r = okay({ ext: (a.meta && a.meta.ext) || '' });
      Object.defineProperty(r, 'bytes', { value: bytes, enumerable: false });   // (for /sound, not sent as JSON)
      if (me) r.data = toB64(bytes);
      return r;
    }
    if (!a || !this.audioCanSee(a, me)) return fail('That sound isn\'t here (any more), or it\'s private.');
    if (name === 'audio.get') return okay({ track: this.audioJson(a, me, true) });
    if (name === 'audio.listen') {
      const x = this.audioExtra(a);
      if (me && a.creator !== me.id && !a.review) {
        const today = utcDay(t);
        const heard = me.audioHeard && me.audioHeard.day === today ? me.audioHeard : { day: today, ids: [] };
        if (!heard.ids.includes(a.id)) {
          heard.ids.push(a.id);
          if (heard.ids.length > kHeardKept) heard.ids.shift();
          me.audioHeard = heard;
          this.saveUser(me);
          x.plays += 1;
          this.audioSave(x);
        }
      }
      return okay({ plays: x.plays });
    }

    if (!me || me.userId === 0) return fail('Sign up to fave sounds and comment.');
    const staff = this.isStaff(me);
    const x = this.audioExtra(a);
    if (name === 'audio.tag') {
      if (a.creator !== me.id && !staff) return fail('Only the person who uploaded it can change this.');
      x.cat = AUDIO_CATS.includes(str(args, 'cat')) ? str(args, 'cat') : 'other';
      x.genre = AUDIO_GENRES.includes(str(args, 'genre')) && (x.cat === 'music' || x.cat === 'loop') ? str(args, 'genre') : '';
      x.tags = audioTags(args.tags);
      this.audioSave(x);
      return okay({ track: this.audioJson(a, me, true) });
    }
    if (a.review) return fail('This hasn\'t passed the staff check yet.');
    if (name === 'audio.fave') {
      const on = args.on !== false, had = x.faves.includes(me.id);
      if (on && !had) x.faves.push(me.id);
      if (!on && had) x.faves = x.faves.filter((y) => y !== me.id);
      if (on !== had) this.audioSave(x);
      return okay({ faves: x.faves.length, faved: on });
    }
    if (name === 'audio.comment') {
      const text = clean(str(args, 'text'), kCommentText, true);
      if (!text) return fail('Write something first.');
      x.comments.push({ id: hex(5), by: me.id, text, at: t });
      this.audioSave(x);
      if (a.creator !== me.id) this.notify(this.users.get(a.creator), 'audio', me.name + ' commented on your sound "' + a.name + '".', a.id);
      return okay({ track: this.audioJson(a, me, true) });
    }
    if (name === 'audio.uncomment') {
      const c = x.comments.find((y) => y.id === str(args, 'comment'));
      if (!c) return fail('That comment isn\'t there any more.');
      if (c.by !== me.id && a.creator !== me.id && !staff) return fail('You can only delete your own comments, or ones on your sounds.');
      x.comments = x.comments.filter((y) => y !== c);
      this.audioSave(x);
      return okay({ track: this.audioJson(a, me, true) });
    }
    return fail('Unknown request.');
  };
}
