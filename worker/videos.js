// G&B Videos: upload a video, watch it, like it, comment on it. Its own product on the site
// (website/app/videos.js draws its pages), sharing the same accounts, follows, notifications,
// reports and staff tools. src/server/ServerVideo.cpp has the same rules.
//
// How it works:
//   - Uploading takes two steps, so a big file never goes through the JSON requests:
//       1. video.start {title, text, tags, cat, size, secs, w, h, thumb} checks everything and
//          gives back an upload address, /vidup/<id>?t=<token>, good for kUploadWait seconds.
//       2. The website PUTs the file itself there (with a progress bar). The server keeps it
//          in Cloudflare R2 (the VIDEOS bucket, or the DOWNLOADS one under videos/), checks its
//          first bytes really are a video (MP4, MOV or WebM), and the video is ready.
//     The website also sends a small picture (thumb) it took from the video, for the feed.
//   - Watching: /vid/<id> streams the file (with byte ranges, so you can skip ahead), and
//     /vidthumb/<id> is its picture. Anyone can watch a public video, even without an account.
//   - Like art, videos from people who aren't Verified wait for a staff check ('pending')
//     before anyone else can see them. Until then the uploader and staff watch it with the
//     video's own key (/vid/<id>?k=<key>), which only they get.
//   - When a video goes public, the uploader's followers are told (that's "subscribing").
//   - Likes, comments (the uploader is told), views. Only the uploader changes the words; the
//     uploader or staff can delete a video (the file in R2 goes too).
//   - It stays free: R2 keeps 10 GB for free, so videos together may use at most
//     VIDEO_STORAGE_GB (a setting, default kStorageGb). Over that, uploads wait for space.
//     Not-Verified people can upload kPerDay videos a day (Verified: kPerDayVerified).
//
// Requests:
//   video.list   {sort, cat, tag, by, q, page}   -> {videos, more, page, total}   (guests too)
//   video.get    {id}                            -> {video}                       (guests too; counts a view)
//   video.start  {title, text, tags, cat, size, secs, w, h, thumb} -> {video, upload}
//   video.edit   {id, title, text, tags, cat, thumb} -> {video}                    (the uploader)
//   video.delete {id}                            -> {}                            (the uploader or staff)
//   video.like   {id, on}                        -> {likes, liked}
//   video.comment {id, text}                     -> {video}
//   video.uncomment {id, comment}                -> {video}
//   video.pending {}                             -> {videos}                      (staff: waiting for a check)
//   video.review {id, ok, note}                  -> {video}                       (staff)
// And on the server on a computer (GB_SERVER), where worker/index.js passes the file on in pieces:
//   video.chunk {id, t, at, data, last}  video.bytes {id, k, from}  video.thumb {id, k}  (ServerVideo.cpp)
import { clean } from './banlists.js';

export const VIDEO_CATS = ['gaming', 'music', 'funny', 'howto', 'animation', 'vlog', 'sports', 'science', 'news', 'other'];
export const kTitle = 100, kText = 3000, kTag = 20, kMostTags = 10, kCommentText = 1000, kCommentsKept = 1000;
// Cloudflare takes at most 100 MB in one request (on the free plan), so one video is a bit less.
export const kMostBytes = 95 * 1024 * 1024, kThumbBytes = 200 * 1024, kPageSize = 24, kChunk = 1024 * 1024;
export const kMostSecs = 60 * 60;   // an hour (only what the browser says: it's for the label)
export const kPerDay = 3, kPerDayVerified = 15, kUploadWait = 3600, kStorageGb = 5, kTellFollowers = 500;
// Requests guests can make, and how often people can do things (server.js adds these to its lists).
export const VIDEO_LOOK_ONLY = ['video.list', 'video.get'];
export const VIDEO_COSTLY = { 'video.start': [10, 30, 3600], 'video.comment': [30, 90, 600], 'video.like': [240, 720, 600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const num = (args, k) => (typeof args[k] === 'number' && Number.isFinite(args[k]) ? args[k] : 0);
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');
const utcDay = (t) => Math.floor(t / 86400);
const json = (body, status = 200) => new Response(JSON.stringify(body), { status, headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });

function fromB64(s) {
  const bin = atob(String(s || '').replace(/^data:[^,]+,/, ''));
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
const isJpg = (d) => d && d.length > 3 && d[0] === 0xff && d[1] === 0xd8 && d[2] === 0xff;
const isPng = (d) => d && d.length > 8 && [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a].every((v, i) => d[i] === v);

// What kind of video these bytes are, by their first bytes (never by what the sender says).
//   MP4 / MOV: "ftyp" at byte 4 (MOV's brand is "qt  "). WebM: the EBML start 1A 45 DF A3.
export function videoType(d) {
  if (!d || d.length < 12) return '';
  if (d[4] === 0x66 && d[5] === 0x74 && d[6] === 0x79 && d[7] === 0x70)
    return d[8] === 0x71 && d[9] === 0x74 ? 'mov' : 'mp4';
  if (d[0] === 0x1a && d[1] === 0x45 && d[2] === 0xdf && d[3] === 0xa3) return 'webm';
  return '';
}
export const VIDEO_MIME = { mp4: 'video/mp4', mov: 'video/quicktime', webm: 'video/webm' };

// Tags: lower case letters, numbers and dashes, no repeats.
export function videoTags(list) {
  const out = [];
  for (const t of Array.isArray(list) ? list : String(list || '').split(/[,\s]+/)) {
    const tag = String(t || '').toLowerCase().replace(/^#/, '').replace(/[^a-z0-9-]/g, '').slice(0, kTag);
    if (tag && !out.includes(tag)) out.push(tag);
    if (out.length >= kMostTags) break;
  }
  return out;
}

// A byte range header ("bytes=100-", "bytes=100-199", "bytes=-500") as {offset, length}, or null.
export function videoRange(header, size) {
  const m = /^bytes=(\d*)-(\d*)$/.exec(String(header || '').trim());
  if (!m || (m[1] === '' && m[2] === '')) return null;
  let from, to;
  if (m[1] === '') { from = Math.max(0, size - Number(m[2])); to = size - 1; }
  else { from = Number(m[1]); to = m[2] === '' ? size - 1 : Math.min(size - 1, Number(m[2])); }
  if (!(from >= 0) || from >= size || to < from) return 'bad';
  return { offset: from, length: to - from + 1 };
}

export function installVideos(Cls) {
  const P = Cls.prototype;

  // The videos, kept in their own table (one row each) and in memory.
  P.videoInit = function () {
    if (this.videos) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS videos (id TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.videos = new Map();
    for (const r of this.sql.exec('SELECT id, data FROM videos')) this.videos.set(r.id, JSON.parse(r.data));
  };
  P.videoSave = function (v) {
    if (v.comments.length > kCommentsKept) v.comments = v.comments.slice(-kCommentsKept);
    this.sql.exec('INSERT OR REPLACE INTO videos (id, data) VALUES (?, ?)', v.id, JSON.stringify(v));
  };
  P.videoForget = function (v) {
    this.videos.delete(v.id);
    this.sql.exec('DELETE FROM videos WHERE id = ?', v.id);
    this.sql.exec('DELETE FROM files WHERE id = ?', 'vidthumb:' + v.id);
    const bucket = this.videoBucket();
    if (bucket) Promise.resolve(bucket.delete(this.videoKey(v.id))).catch(() => {});
  };
  // Where the files live: a VIDEOS bucket if there is one, or the downloads bucket under videos/.
  P.videoBucket = function () { return (this.env && (this.env.VIDEOS || this.env.DOWNLOADS)) || null; };
  P.videoKey = function (id) { return (this.env && this.env.VIDEOS ? '' : 'videos/') + id; };
  P.videoRoom = function () { return Math.max(0, Number(this.env && this.env.VIDEO_STORAGE_GB) || kStorageGb) * 1024 * 1024 * 1024; };
  P.videoUsed = function () { let n = 0; for (const v of this.videos.values()) n += v.size || 0; return n; };
  // Public: ready and passed (or didn't need) the staff check.
  P.videoPublic = function (v) { return !!v && v.state === 'ready' && !v.review; };
  P.videoCanSee = function (v, me) {
    return !!v && (this.videoPublic(v) || (!!me && (v.by === me.id || (this.isStaff(me) && v.state === 'ready'))));
  };
  P.videoPerson = function (id) {
    const u = this.users.get(id);
    return u ? { id: u.id, name: u.name, username: u.username || '', userId: u.userId, verified: this.isVerified(u), followers: (u.followers || []).length }
      : { id, name: '?', username: '', userId: 0, verified: false, followers: 0 };
  };
  // Its addresses: public ones are plain; the rest need the video's key.
  P.videoLinks = function (v) {
    const k = this.videoPublic(v) ? '' : '?k=' + v.key;
    return { src: '/vid/' + encodeURIComponent(v.id) + k, poster: v.thumb ? '/vidthumb/' + encodeURIComponent(v.id) + k : '' };
  };
  // A video as the website sees it (full: with comments and whether you liked it).
  P.videoJson = function (v, me, full = false) {
    const out = Object.assign({ id: v.id, title: v.title, cat: v.cat, tags: v.tags, secs: v.secs || 0, at: v.at, by: this.videoPerson(v.by),
      likes: v.likes.length, views: v.views || 0, comments: v.comments.length, state: v.state, review: v.review || undefined },
    v.state === 'ready' ? this.videoLinks(v) : { src: '', poster: '' });
    if (!full) return out;
    const mine = !!me && v.by === me.id, staff = !!me && this.isStaff(me);
    const uploader = this.users.get(v.by);
    return Object.assign(out, {
      text: v.text, edited: v.edited || 0, w: v.w || 0, h: v.h || 0, type: v.type || '', size: v.size || 0,
      reviewNote: (mine || staff) ? v.reviewNote || undefined : undefined,
      liked: !!me && v.likes.includes(me.id), mine, canDelete: mine || staff,
      following: !!me && !!uploader && (uploader.followers || []).includes(me.id),
      commentList: v.comments.map((c) => ({ id: c.id, text: c.text, at: c.at, by: this.videoPerson(c.by),
        canDelete: !!me && (c.by === me.id || mine || staff) })).reverse(),   // newest first
    });
  };
  // It's public now: tell the people who follow the uploader (not Guts, who follows everyone).
  P.videoTellFollowers = function (v) {
    const u = this.users.get(v.by);
    if (!u || v.told) return;
    v.told = true;
    for (const id of (u.followers || []).slice(0, kTellFollowers)) {
      const f = this.users.get(id);
      if (f && !this.isOfficial(f)) this.notify(f, 'video', u.name + ' posted a new video: "' + v.title + '"', v.id);
    }
  };
  // For report.send (server.js): what's being reported, and a copy for staff.
  P.videoReportTarget = function (id, me) {
    this.videoInit();
    const [vid, cid] = String(id || '').split(':');
    const v = this.videos.get(vid);
    if (!v || !this.videoCanSee(v, me)) return 'That isn\'t there any more.';
    if (!cid) {
      if (v.by === me.id) return 'You can\'t report yourself.';
      return { target: v.id, about: v.by, copy: { subject: 'Video: ' + v.title, body: v.text || '(no description)', at: v.at } };
    }
    const c = v.comments.find((x) => x.id === cid);
    if (!c) return 'That comment isn\'t there any more.';
    if (c.by === me.id) return 'You can\'t report yourself.';
    return { target: v.id + ':' + c.id, about: c.by, copy: { subject: 'Comment on video: ' + v.title, body: c.text, at: c.at } };
  };
  P.videoReportName = function (target) {
    this.videoInit();
    const v = this.videos.get(String(target || '').split(':')[0]);
    return v ? v.title : '(deleted)';
  };

  // The addresses that aren't JSON requests (server.js fetch sends them here):
  //   PUT /vidup/<id>?t=<token>  the file itself (step 2 of an upload)
  //   GET /vid/<id>[?k=<key>]    the video, with byte ranges
  //   GET /vidthumb/<id>[?k=]    its picture
  P.videoFetch = async function (request, url) {
    this.videoInit();
    const [, kind, raw] = /^\/(vidup|vid|vidthumb)\/(.*)$/.exec(url.pathname) || [];
    let id = '';
    try { id = decodeURIComponent(raw || ''); } catch { id = ''; }
    const v = this.videos.get(id);
    if (kind === 'vidup') return this.videoUpload(request, url, v);
    const keyOk = !!v && (this.videoPublic(v) || (v.state === 'ready' && url.searchParams.get('k') === v.key));
    if (!keyOk) return new Response('No video.', { status: 404 });
    const cache = this.videoPublic(v) ? 'public, max-age=3600' : 'private, no-store';
    if (kind === 'vidthumb') {
      const data = v.thumb ? this.readFile('vidthumb:' + v.id) : null;
      if (!data) return new Response('No picture.', { status: 404 });
      return new Response(data, { headers: { 'content-type': isPng(data) ? 'image/png' : 'image/jpeg', 'cache-control': cache, 'x-content-type-options': 'nosniff' } });
    }
    const bucket = this.videoBucket();
    if (!bucket) return new Response('Videos aren\'t set up on this server.', { status: 404 });
    const range = videoRange(request.headers.get('range'), v.size);
    if (range === 'bad') return new Response('Bad range.', { status: 416, headers: { 'content-range': 'bytes */' + v.size } });
    const obj = request.method === 'HEAD' ? await bucket.head(this.videoKey(v.id)) : await bucket.get(this.videoKey(v.id), range ? { range } : undefined);
    if (!obj) return new Response('No video.', { status: 404 });
    const h = { 'content-type': VIDEO_MIME[v.type] || 'application/octet-stream', 'accept-ranges': 'bytes', 'cache-control': cache, 'x-content-type-options': 'nosniff' };
    if (range) {
      h['content-range'] = 'bytes ' + range.offset + '-' + (range.offset + range.length - 1) + '/' + v.size;
      h['content-length'] = String(range.length);
      return new Response(request.method === 'HEAD' ? null : obj.body, { status: 206, headers: h });
    }
    h['content-length'] = String(v.size);
    return new Response(request.method === 'HEAD' ? null : obj.body, { headers: h });
  };
  P.videoUpload = async function (request, url, v) {
    if (request.method !== 'PUT') return json(fail('Use PUT.'), 405);
    if (!v || v.state !== 'uploading' || !v.up || url.searchParams.get('t') !== v.up.token) return json(fail('That upload isn\'t open (any more). Start again.'), 404);
    if (v.up.until < now()) { this.videoForget(v); return json(fail('That upload took too long. Start again.'), 410); }
    const size = Number(request.headers.get('content-length') || 0);
    if (!size || size !== v.size) return json(fail('The file isn\'t the size you said it was. Start again.'), 400);
    const bucket = this.videoBucket();
    if (!bucket) return json(fail('Videos aren\'t set up on this server yet.'), 503);
    const key = this.videoKey(v.id);
    v.up.token = '';   // (one try per address)
    try {
      await bucket.put(key, request.body, { httpMetadata: { contentType: 'application/octet-stream' } });
      const head = await bucket.get(key, { range: { offset: 0, length: 16 } });
      const first = head ? new Uint8Array(await head.arrayBuffer()) : null;
      const type = videoType(first);
      if (!type) { this.videoForget(v); return json(fail('That isn\'t a video this site can play. Use .mp4, .mov or .webm.'), 400); }
      v.type = type;
      v.size = head.size || size;
    } catch {
      this.videoForget(v);
      return json(fail('The video didn\'t arrive. Check your connection and try again.'), 500);
    }
    v.state = 'ready';
    delete v.up;
    if (this.videoPublic(v)) this.videoTellFollowers(v);
    this.videoSave(v);
    this.flush();
    return json(okay({ video: this.videoJson(v, this.users.get(v.by), true) }));
  };

  P.videoOp = function (name, me, args) {
    this.videoInit();
    const t = now();
    if (name === 'video.list') {
      const sort = ['new', 'week', 'top', 'views'].includes(str(args, 'sort')) ? str(args, 'sort') : 'new';
      const cat = VIDEO_CATS.includes(str(args, 'cat')) ? str(args, 'cat') : '';
      const tag = videoTags([str(args, 'tag')])[0] || '';
      const q = str(args, 'q').trim().toLowerCase().slice(0, 60);
      let by = null;
      if (args.by !== undefined && args.by !== '') {
        by = this.findPerson(args.by);
        if (!by) return fail('There\'s nobody with that name or number.');
      }
      const page = Math.max(1, Math.min(1000, Math.trunc(Number(args.page) || 1)));
      const mine = !!me && !!by && by.id === me.id;
      let list = [...this.videos.values()].filter((v) =>
        (mine ? v.by === me.id : by ? v.by === by.id && this.videoPublic(v) : this.videoPublic(v)) &&
        (!cat || v.cat === cat) && (!tag || v.tags.includes(tag)) &&
        (!q || v.title.toLowerCase().includes(q) || v.tags.some((x) => x.includes(q))));
      if (sort === 'week') list = list.filter((v) => v.at > t - 7 * 86400);
      const score = sort === 'views' ? (v) => v.views || 0 : (v) => v.likes.length * 10 + (v.views || 0);   // (a like counts as ten views)
      list.sort(sort === 'new' ? (x, y) => y.at - x.at : (x, y) => score(y) - score(x) || y.at - x.at);
      const from = (page - 1) * kPageSize;
      return okay({ videos: list.slice(from, from + kPageSize).map((v) => this.videoJson(v, me)), more: list.length > from + kPageSize, page,
        total: list.length, cats: VIDEO_CATS });
    }
    const v = this.videos.get(str(args, 'id'));
    if (name === 'video.get') {
      if (!this.videoCanSee(v, me)) return fail('That video isn\'t here (any more).');
      if (v.state === 'ready' && (!me || v.by !== me.id)) { v.views = (v.views || 0) + 1; this.videoSave(v); }
      return okay({ video: this.videoJson(v, me, true) });
    }

    if (!me || me.userId === 0) return fail('Sign up to upload videos, like and comment.');
    const staff = this.isStaff(me), verified = this.isVerified(me);

    if (name === 'video.start' || name === 'video.edit') {
      const title = clean(str(args, 'title'), kTitle);
      if (!title) return fail('Give your video a title.');
      const text = clean(str(args, 'text'), kText, true);
      const cat = VIDEO_CATS.includes(str(args, 'cat')) ? str(args, 'cat') : 'other';
      const tags = videoTags(args.tags);
      let thumb = null;
      if (str(args, 'thumb')) {
        try { thumb = fromB64(str(args, 'thumb')); } catch { thumb = null; }
        if (!thumb || !(isJpg(thumb) || isPng(thumb)) || thumb.length > kThumbBytes) return fail('The video\'s picture must be a .jpg or .png under ' + (kThumbBytes >> 10) + ' KB.');
      }
      if (name === 'video.edit') {
        if (!v || !this.videoCanSee(v, me)) return fail('That video isn\'t here (any more).');
        if (v.by !== me.id) return fail('Only the uploader can change this.');
        Object.assign(v, { title, text, cat, tags, edited: t });
        if (thumb) { this.writeFile('vidthumb:' + v.id, thumb); v.thumb = true; }
        this.videoSave(v);
        return okay({ video: this.videoJson(v, me, true) });
      }
      // Uploads that never finished are let go first (they hold space and a place in today's count).
      for (const old of [...this.videos.values()]) if (old.state === 'uploading' && old.up && old.up.until < t) this.videoForget(old);
      const today = utcDay(t);
      if (me.videoDay !== today) { me.videoDay = today; me.videoToday = 0; }
      const most = verified || staff ? kPerDayVerified : kPerDay;
      if (me.videoToday >= most) return fail('You\'ve uploaded ' + most + ' videos today. Come back tomorrow!');
      const size = Math.trunc(num(args, 'size'));
      if (size <= 0) return fail('Pick a video first.');
      if (size > kMostBytes) return fail('That video is too big (the most is ' + (kMostBytes >> 20) + ' MB). Try a shorter one, or save it smaller.');
      if (this.videoUsed() + size > this.videoRoom()) return fail('Videos are full for now (they have to fit in the free space). Staff are on it: try again later.');
      const video = { id: 'vid-' + hex(6), by: me.id, title, text, cat, tags, type: '', size, at: t, edited: 0, views: 0, likes: [], comments: [],
        secs: Math.max(0, Math.min(kMostSecs, Math.round(num(args, 'secs')))), w: Math.max(0, Math.min(8192, Math.trunc(num(args, 'w')))),
        h: Math.max(0, Math.min(8192, Math.trunc(num(args, 'h')))), thumb: !!thumb, key: hex(12), state: 'uploading',
        up: { token: hex(16), until: t + kUploadWait } };
      if (!verified && !staff) video.review = 'pending';
      if (thumb) this.writeFile('vidthumb:' + video.id, thumb);
      this.videos.set(video.id, video);
      this.videoSave(video);
      me.videoToday += 1;
      this.saveUser(me);
      return okay({ video: this.videoJson(video, me, true), upload: '/vidup/' + video.id + '?t=' + video.up.token, most: kMostBytes });
    }
    if (name === 'video.pending') {
      if (!staff) return fail('Only staff can do that.');
      return okay({ videos: [...this.videos.values()].filter((x) => x.review === 'pending' && x.state === 'ready').sort((x, y) => x.at - y.at).map((x) => this.videoJson(x, me)) });
    }

    if (!v || !this.videoCanSee(v, me)) return fail('That video isn\'t here (any more).');
    if (name === 'video.delete') {
      if (v.by !== me.id && !staff) return fail('Only the uploader or staff can delete this.');
      this.videoForget(v);
      return okay();
    }
    if (name === 'video.review') {
      if (!staff) return fail('Only staff can do that.');
      if (!v.review) return fail('That isn\'t waiting for a check any more.');
      const uploader = this.users.get(v.by);
      if (args.ok === true) {
        delete v.review; delete v.reviewNote;
        this.notify(uploader, 'video', 'Your video "' + v.title + '" passed the staff check. Everyone can watch it now!', v.id);
        this.videoTellFollowers(v);
      } else {
        v.review = 'rejected'; v.reviewNote = clean(str(args, 'note'), 200);
        this.notify(uploader, 'video', 'Your video "' + v.title + '" didn\'t pass the staff check' + (v.reviewNote ? ': ' + v.reviewNote : '.'), v.id);
      }
      this.videoSave(v);
      return okay({ video: this.videoJson(v, me, true) });
    }
    if (!this.videoPublic(v)) return fail(v.state !== 'ready' ? 'This video is still uploading.' : 'This hasn\'t passed the staff check yet.');
    if (name === 'video.like') {
      const on = args.on !== false, had = v.likes.includes(me.id);
      if (on && !had) v.likes.push(me.id);
      if (!on && had) v.likes = v.likes.filter((x) => x !== me.id);
      if (on !== had) this.videoSave(v);
      return okay({ likes: v.likes.length, liked: on });
    }
    if (name === 'video.comment') {
      const text = clean(str(args, 'text'), kCommentText, true);
      if (!text) return fail('Write something first.');
      v.comments.push({ id: hex(5), by: me.id, text, at: t });
      this.videoSave(v);
      if (v.by !== me.id) this.notify(this.users.get(v.by), 'video', me.name + ' commented on your video "' + v.title + '".', v.id);
      return okay({ video: this.videoJson(v, me, true) });
    }
    if (name === 'video.uncomment') {
      const c = v.comments.find((x) => x.id === str(args, 'comment'));
      if (!c) return fail('That comment isn\'t there any more.');
      if (c.by !== me.id && v.by !== me.id && !staff) return fail('You can only delete your own comments, or ones on your video.');
      v.comments = v.comments.filter((x) => x !== c);
      this.videoSave(v);
      return okay({ video: this.videoJson(v, me, true) });
    }
    return fail('Unknown request.');
  };
}
