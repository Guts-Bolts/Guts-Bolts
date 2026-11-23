// G&B Live: go live from your browser, watch streams, and chat while you watch. It's part of
// G&B Videos (website/app/streams.js draws its pages), with the same accounts, follows,
// notifications, reports and staff tools. src/server/ServerStream.cpp has the same rules.
//
// How it works (all free, no paid streaming service):
//   - The streamer's browser records its camera or screen in short pieces (kSegSecs seconds
//     each, a whole little WebM video every time), and PUTs each one to /liveup/<id>?t=<token>&n=<n>.
//   - The server keeps the newest kKeep pieces in Cloudflare R2 (the same bucket as videos,
//     under live/) and lets older ones go, so a stream never takes up much space.
//   - Watchers ask stream.get every few seconds: it says which piece is newest, how many are
//     watching, and the new chat lines. They fetch /live/<id>/<n> for each new piece and play
//     them one after another (so they're about two pieces behind the streamer).
//   - Nothing gets checked by staff before it's shown (it's live), so only Verified people
//     and staff can go live. Anyone can report a stream or a chat line; the streamer and staff
//     can delete chat lines, give someone a time out, and turn on slow mode; staff can end any stream.
//   - A stream ends when the streamer presses End, after kQuiet seconds with no new piece
//     (the tab closed), or after kMostSecs. Then its pieces are deleted.
//   - Followers are told when someone goes live (at most once every kTellGap seconds).
//
// Requests:
//   stream.list    {}                       -> {live, recent}                 (guests too)
//   stream.get     {id, after, viewer}      -> {stream, chat}                 (guests too; counts you as watching)
//   stream.start   {title, cat, mime, thumb}      -> {stream, push, segSecs}        (Verified and staff)
//   stream.snap    {id, thumb}              -> {}                             (the streamer: a new picture for the list)
//   stream.end     {id}                     -> {stream}                       (the streamer or staff)
//   stream.chat    {id, text}               -> {chat}
//   stream.unchat  {id, chat}               -> {}                             (who wrote it, the streamer or staff)
//   stream.timeout {id, user}               -> {}                             (the streamer or staff: no chat for kTimeout seconds)
//   stream.slow    {id, secs}               -> {stream}                       (the streamer or staff: 0, 5, 15 or 60)
// And on the server on a computer (GB_SERVER), where worker/index.js passes the pieces on:
//   stream.push {id, t, n, data}  stream.piece {id, n}  stream.picture {id}   (ServerStream.cpp)
import { clean } from './banlists.js';
import { VIDEO_CATS } from './videos.js';

export const kSegSecs = 4, kKeep = 15, kLeastSegBytes = 1000, kMostSegBytes = 4 * 1024 * 1024;
export const kQuiet = 30, kMostSecs = 4 * 3600, kPerDay = 10, kTitle = 100, kChatText = 300, kChatKept = 200, kChatPerGet = 50;
export const kTimeout = 600, kWatching = 15, kTellGap = 1800, kTellFollowers = 500, kEndedKept = 7 * 86400, kThumbBytes = 200 * 1024;
export const kSlowChoices = [0, 5, 15, 60];
// What the streamer's browser records in (watchers' browsers need to know to play it).
export const STREAM_MIMES = ['video/webm;codecs=vp8,opus', 'video/webm;codecs=vp9,opus', 'video/webm;codecs=vp8', 'video/webm;codecs=vp9', 'video/webm'];
export const STREAM_LOOK_ONLY = ['stream.list', 'stream.get'];
export const STREAM_COSTLY = { 'stream.start': [10, 30, 3600], 'stream.chat': [60, 180, 600], 'stream.snap': [120, 360, 3600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');
const json = (body, status = 200) => new Response(JSON.stringify(body), { status, headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });
function fromB64(s) {
  const bin = atob(String(s || '').replace(/^data:[^,]+,/, ''));
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
const isJpg = (d) => d && d.length > 3 && d[0] === 0xff && d[1] === 0xd8 && d[2] === 0xff;
// A piece of a stream is a whole small WebM video: it starts with the EBML mark 1A 45 DF A3.
export const isWebm = (d) => !!d && d.length >= 4 && d[0] === 0x1a && d[1] === 0x45 && d[2] === 0xdf && d[3] === 0xa3;

export function installStreams(Cls) {
  const P = Cls.prototype;

  P.streamInit = function () {
    if (this.streams) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS streams (id TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.streams = new Map();
    this.streamWatch = new Map();   // stream id -> Map(watcher -> last seen): only in memory
    for (const r of this.sql.exec('SELECT id, data FROM streams')) this.streams.set(r.id, JSON.parse(r.data));
  };
  P.streamSave = function (s) {
    if (s.chat.length > kChatKept) s.chat = s.chat.slice(-kChatKept);
    this.sql.exec('INSERT OR REPLACE INTO streams (id, data) VALUES (?, ?)', s.id, JSON.stringify(s));
  };
  P.streamBucket = function () { return (this.env && (this.env.VIDEOS || this.env.DOWNLOADS)) || null; };
  P.streamKey = function (id, n) { return (this.env && this.env.VIDEOS ? '' : 'videos/') + 'live/' + id + '/' + n; };
  P.streamDrop = function (s, from, to) {   // delete pieces from..to (both in) from R2
    const bucket = this.streamBucket();
    if (!bucket) return;
    for (let n = Math.max(0, from); n <= to; n++) Promise.resolve(bucket.delete(this.streamKey(s.id, n))).catch(() => {});
  };
  P.streamEnd = function (s, t, why) {
    if (s.state !== 'live') return;
    s.state = 'ended'; s.ended = t; s.why = why || 'ended';
    delete s.token;
    this.streamDrop(s, s.first, s.last);
    s.first = s.last + 1;
    this.streamWatch.delete(s.id);
    this.streamSave(s);
  };
  P.streamForget = function (s) {
    this.streamEnd(s, now(), 'gone');
    this.streams.delete(s.id);
    this.sql.exec('DELETE FROM streams WHERE id = ?', s.id);
    this.sql.exec('DELETE FROM files WHERE id = ?', 'livethumb:' + s.id);
  };
  // Streams whose tab went quiet, or that went on too long, end; old ended ones are let go.
  P.streamTidy = function (t) {
    for (const s of [...this.streams.values()]) {
      if (s.state === 'live' && (s.seen || s.at) < t - kQuiet) this.streamEnd(s, t, 'quiet');
      else if (s.state === 'live' && t - s.at > kMostSecs) this.streamEnd(s, t, 'long');
      else if (s.state === 'ended' && s.ended < t - kEndedKept) this.streamForget(s);
    }
  };
  P.streamViewers = function (s, t) {
    const w = this.streamWatch.get(s.id);
    if (!w) return 0;
    let n = 0;
    for (const [k, seen] of w) { if (seen >= t - kWatching) n++; else w.delete(k); }
    return n;
  };
  P.streamPerson = function (id) {
    const u = this.users.get(id);
    return u ? { id: u.id, name: u.name, userId: u.userId, verified: this.isVerified(u), followers: (u.followers || []).length }
      : { id, name: '?', userId: 0, verified: false, followers: 0 };
  };
  P.streamJson = function (s, me, t) {
    const mine = !!me && s.by === me.id, staff = !!me && this.isStaff(me);
    const viewers = s.state === 'live' ? this.streamViewers(s, t) : 0;
    const owner = this.users.get(s.by);
    return { id: s.id, title: s.title, cat: s.cat, at: s.at, state: s.state, ended: s.ended || 0, by: this.streamPerson(s.by),
      viewers, peak: s.peak || 0, first: s.first, last: s.last, segSecs: kSegSecs, slow: s.slow || 0, mime: s.mime || STREAM_MIMES[0],
      src: '/live/' + encodeURIComponent(s.id) + '/', poster: s.thumb ? '/live/' + encodeURIComponent(s.id) + '/pic?v=' + (s.thumbAt || 0) : '',
      mine, canMod: mine || staff, canEnd: (mine || staff) && s.state === 'live',
      following: !!me && !!owner && (owner.followers || []).includes(me.id) };
  };
  P.streamChatJson = function (s, c, me) {
    const staff = !!me && this.isStaff(me);
    return { n: c.n, id: c.id, text: c.text, at: c.at, by: this.streamPerson(c.by),
      canDelete: !!me && (c.by === me.id || s.by === me.id || staff) };
  };
  P.streamReportTarget = function (id, me) {
    this.streamInit();
    const [sid, cid] = String(id || '').split(':');
    const s = this.streams.get(sid);
    if (!s) return 'That isn\'t there any more.';
    if (!cid) {
      if (s.by === me.id) return 'You can\'t report yourself.';
      return { target: s.id, about: s.by, copy: { subject: 'Live stream: ' + s.title, body: '(a live stream: staff can watch it while it\'s on)', at: s.at } };
    }
    const c = s.chat.find((x) => x.id === cid);
    if (!c) return 'That chat line isn\'t there any more.';
    if (c.by === me.id) return 'You can\'t report yourself.';
    return { target: s.id + ':' + c.id, about: c.by, copy: { subject: 'Chat on live stream: ' + s.title, body: c.text, at: c.at } };
  };
  P.streamReportName = function (target) {
    this.streamInit();
    const s = this.streams.get(String(target || '').split(':')[0]);
    return s ? s.title : '(deleted)';
  };

  // The addresses that aren't JSON requests (server.js fetch sends them here):
  //   PUT /liveup/<id>?t=<token>&n=<n>   the next piece (the streamer)
  //   GET /live/<id>/<n>                 one piece
  //   GET /live/<id>/pic                 the stream's picture for the list
  P.streamFetch = async function (request, url) {
    this.streamInit();
    const t = now();
    this.streamTidy(t);
    const up = /^\/liveup\/([^/]+)$/.exec(url.pathname);
    const unpack = (x) => { try { return decodeURIComponent(x); } catch { return ''; } };
    if (up) return this.streamPush(request, url, unpack(up[1]), t);
    const m = /^\/live\/([^/]+)\/(\d{1,7}|pic)$/.exec(url.pathname);
    const s = m ? this.streams.get(unpack(m[1])) : null;
    if (!s) return new Response('No stream.', { status: 404 });
    if (m[2] === 'pic') {
      const data = s.thumb ? this.readFile('livethumb:' + s.id) : null;
      if (!data) return new Response('No picture.', { status: 404 });
      return new Response(data, { headers: { 'content-type': 'image/jpeg', 'cache-control': 'public, max-age=30', 'x-content-type-options': 'nosniff' } });
    }
    const n = Number(m[2]);
    const bucket = this.streamBucket();
    if (!bucket || n < s.first || n > s.last) return new Response('No piece.', { status: 404, headers: { 'cache-control': 'no-store' } });
    const obj = await bucket.get(this.streamKey(s.id, n));
    if (!obj) return new Response('No piece.', { status: 404, headers: { 'cache-control': 'no-store' } });
    // (a piece never changes once it's there, so browsers and Cloudflare may keep it a little while)
    return new Response(obj.body, { headers: { 'content-type': 'video/webm', 'cache-control': 'public, max-age=120', 'x-content-type-options': 'nosniff' } });
  };
  P.streamPush = async function (request, url, id, t) {
    if (request.method !== 'PUT') return json(fail('Use PUT.'), 405);
    const s = this.streams.get(id);
    if (!s || s.state !== 'live' || !s.token || url.searchParams.get('t') !== s.token) return json(fail('This stream has ended. Start a new one.'), 404);
    const n = Number(url.searchParams.get('n'));
    if (!Number.isInteger(n) || n <= s.last || n > s.last + 10) return json(fail('That piece is out of order.'), 409);
    const size = Number(request.headers.get('content-length') || 0);
    if (size > kMostSegBytes) return json(fail('That piece is too big. Lower the quality.'), 413);
    const data = new Uint8Array(await request.arrayBuffer());
    if (data.length < kLeastSegBytes || data.length > kMostSegBytes) return json(fail(data.length < kLeastSegBytes ? 'That piece is empty.' : 'That piece is too big. Lower the quality.'), 400);
    if (!isWebm(data)) return json(fail('Pieces must be WebM video.'), 400);
    const bucket = this.streamBucket();
    if (!bucket) return json(fail('Live streams aren\'t set up on this server yet.'), 503);
    try {
      await bucket.put(this.streamKey(s.id, n), data, { httpMetadata: { contentType: 'video/webm' } });
    } catch {
      return json(fail('The piece didn\'t save. Trying again with the next one.'), 500);
    }
    if (s.state !== 'live') return json(fail('This stream has ended.'), 404);
    s.last = n; s.seen = t;
    if (s.first < 0 || s.first > n) s.first = n;
    if (n - s.first >= kKeep) { this.streamDrop(s, s.first, n - kKeep); s.first = n - kKeep + 1; }
    if (t - s.at > kMostSecs) { this.streamEnd(s, t, 'long'); return json(fail('Streams can be ' + (kMostSecs / 3600) + ' hours long at most. Start a new one!'), 410); }
    s.peak = Math.max(s.peak || 0, this.streamViewers(s, t));
    this.streamSave(s);
    return json(okay({ n, viewers: this.streamViewers(s, t) }));
  };

  P.streamOp = function (name, me, args) {
    this.streamInit();
    const t = now();
    this.streamTidy(t);
    if (name === 'stream.list') {
      const all = [...this.streams.values()];
      const live = all.filter((s) => s.state === 'live' && s.last >= 0).map((s) => this.streamJson(s, me, t))
        .sort((x, y) => y.viewers - x.viewers || y.at - x.at);
      const recent = all.filter((s) => s.state === 'ended').sort((x, y) => y.ended - x.ended).slice(0, 12).map((s) => this.streamJson(s, me, t));
      return okay({ live, recent, cats: VIDEO_CATS });
    }
    const s = this.streams.get(str(args, 'id'));
    if (name === 'stream.get') {
      if (!s) return fail('That stream isn\'t here (any more).');
      if (s.state === 'live' && !(me && me.id === s.by)) {
        const who = me && me.userId ? me.id : 'guest:' + str(args, 'viewer').slice(0, 40);
        if (!this.streamWatch.has(s.id)) this.streamWatch.set(s.id, new Map());
        this.streamWatch.get(s.id).set(who, t);
        s.peak = Math.max(s.peak || 0, this.streamViewers(s, t));
      }
      const after = Math.max(0, Math.trunc(Number(args.after) || 0));
      const chat = s.chat.filter((c) => c.n > after).slice(-kChatPerGet).map((c) => this.streamChatJson(s, c, me));
      const out = okay({ stream: this.streamJson(s, me, t), chat });
      if (me && s.timeouts && s.timeouts[me.id] > t) out.timeoutUntil = s.timeouts[me.id];
      return out;
    }

    if (!me || me.userId === 0) return fail('Sign up to go live and chat.');
    const staff = this.isStaff(me);

    if (name === 'stream.start') {
      if (!this.isVerified(me) && !staff) return fail('Only Verified people can go live for now (nobody can check a live stream first). Get Verified, then come back!');
      const title = clean(str(args, 'title'), kTitle);
      if (!title) return fail('Give your stream a title.');
      const cat = VIDEO_CATS.includes(str(args, 'cat')) ? str(args, 'cat') : 'other';
      const mime = STREAM_MIMES.includes(str(args, 'mime')) ? str(args, 'mime') : STREAM_MIMES[0];
      let thumb = null;
      if (str(args, 'thumb')) {
        try { thumb = fromB64(str(args, 'thumb')); } catch { thumb = null; }
        if (!thumb || !isJpg(thumb) || thumb.length > kThumbBytes) return fail('The stream\'s picture must be a .jpg under ' + (kThumbBytes >> 10) + ' KB.');
      }
      const today = Math.floor(t / 86400);
      if (me.streamDay !== today) { me.streamDay = today; me.streamToday = 0; }
      if (me.streamToday >= kPerDay) return fail('You\'ve started ' + kPerDay + ' streams today. Come back tomorrow!');
      if (!this.streamBucket()) return fail('Live streams aren\'t set up on this server yet.');
      for (const old of this.streams.values()) if (old.by === me.id && old.state === 'live') this.streamEnd(old, t, 'replaced');   // one at a time
      const s2 = { id: 'live-' + hex(6), by: me.id, title, cat, mime, at: t, state: 'live', ended: 0, token: hex(16), first: -1, last: -1, seen: t,
        peak: 0, slow: 0, chat: [], chatN: 0, timeouts: {}, thumb: !!thumb, thumbAt: t };
      if (thumb) this.writeFile('livethumb:' + s2.id, thumb);
      this.streams.set(s2.id, s2);
      this.streamSave(s2);
      me.streamToday += 1;
      // Followers hear about it (but not every time someone restarts a stream).
      if (!me.streamToldAt || me.streamToldAt < t - kTellGap) {
        me.streamToldAt = t;
        for (const id of (me.followers || []).slice(0, kTellFollowers)) {
          const f = this.users.get(id);
          if (f && !this.isOfficial(f)) this.notify(f, 'stream', me.name + ' is live now: "' + title + '"', s2.id);
        }
      }
      this.saveUser(me);
      this.flush();
      return okay({ stream: this.streamJson(s2, me, t), push: '/liveup/' + s2.id + '?t=' + s2.token, segSecs: kSegSecs });
    }
    if (!s) return fail('That stream isn\'t here (any more).');
    const mod = s.by === me.id || staff;
    if (name === 'stream.snap') {
      if (s.by !== me.id) return fail('Only the streamer can do that.');
      if (s.state !== 'live') return fail('This stream has ended.');
      let thumb = null;
      try { thumb = fromB64(str(args, 'thumb')); } catch { thumb = null; }
      if (!thumb || !isJpg(thumb) || thumb.length > kThumbBytes) return fail('The stream\'s picture must be a .jpg under ' + (kThumbBytes >> 10) + ' KB.');
      this.writeFile('livethumb:' + s.id, thumb);
      s.thumb = true; s.thumbAt = t;
      this.streamSave(s);
      return okay();
    }
    if (name === 'stream.end') {
      if (!mod) return fail('Only the streamer or staff can end this.');
      if (s.state === 'live') {
        if (s.by !== me.id) this.notify(this.users.get(s.by), 'stream', 'Staff ended your stream "' + s.title + '".', s.id);
        this.streamEnd(s, t, s.by === me.id ? 'ended' : 'staff');
      }
      return okay({ stream: this.streamJson(s, me, t) });
    }
    if (name === 'stream.slow') {
      if (!mod) return fail('Only the streamer or staff can change this.');
      const secs = Math.trunc(Number(args.secs) || 0);
      s.slow = kSlowChoices.includes(secs) ? secs : 0;
      this.streamSave(s);
      return okay({ stream: this.streamJson(s, me, t) });
    }
    if (name === 'stream.timeout') {
      if (!mod) return fail('Only the streamer or staff can do that.');
      const who = this.users.get(str(args, 'user'));
      if (!who) return fail('There\'s nobody with that name or number.');
      if (who.id === s.by) return fail('You can\'t time out the streamer.');
      if (this.isStaff(who)) return fail('You can\'t time out staff.');
      s.timeouts = s.timeouts || {};
      s.timeouts[who.id] = t + kTimeout;
      s.chat = s.chat.filter((c) => c.by !== who.id);   // (their lines go too)
      this.streamSave(s);
      return okay({ until: s.timeouts[who.id] });
    }
    if (name === 'stream.unchat') {
      const c = s.chat.find((x) => x.id === str(args, 'chat'));
      if (!c) return fail('That chat line isn\'t there any more.');
      if (c.by !== me.id && !mod) return fail('You can only delete your own chat lines.');
      s.chat = s.chat.filter((x) => x !== c);
      this.streamSave(s);
      return okay();
    }
    if (name === 'stream.chat') {
      if (s.state !== 'live') return fail('This stream has ended, so its chat is closed.');
      if (s.timeouts && s.timeouts[me.id] > t) return fail('You\'re in a time out for this chat. Wait ' + Math.ceil((s.timeouts[me.id] - t) / 60) + ' minute(s).');
      const text = clean(str(args, 'text'), kChatText);
      if (!text) return fail('Write something first.');
      if (s.slow && !mod) {
        const last = [...s.chat].reverse().find((c) => c.by === me.id);
        if (last && t - last.at < s.slow) return fail('Slow mode is on: wait ' + (s.slow - (t - last.at)) + ' more second(s).');
      }
      s.chatN = (s.chatN || 0) + 1;
      const c = { n: s.chatN, id: hex(5), by: me.id, text, at: t };
      s.chat.push(c);
      this.streamSave(s);
      return okay({ chat: this.streamChatJson(s, c, me) });
    }
    return fail('Unknown request.');
  };
}
