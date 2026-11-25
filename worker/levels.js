// Levels, daily streaks, profile walls and showcases. Things that make the site worth coming
// back to every day. src/server/ServerLevels.cpp has the same rules.
//
// Levels and XP:
//   - You earn XP for doing things on Guts&Bolts: playing games, posting (forum, comments,
//     walls), making things (uploads, art, videos, pastes, courses) and claiming your daily
//     Bolts. Each thing has a daily cap (XP_FOR), so spamming one button doesn't level you up.
//   - Level L needs 50 * L * (L - 1) XP in total: level 2 at 100, level 3 at 300, level 10 at
//     4,500 ... up to kMaxLevel. Every new level gives free Bolts (levelReward), once.
//   - XP comes in after a request worked: installLevels wraps op() and looks the request up in
//     XP_FOR. Nothing else in server.js has to remember to give XP.
//
// Daily streak:
//   - bolts.daily (server.js) asks dailyClaim(): 25 Bolts, plus 5 more for every day in a row
//     you've claimed (up to +35 on day 8 and after). Miss a day and the streak starts again.
//
// Profile wall:
//   - People post short messages on someone's profile. Who can post is up to the owner:
//     'friends' (the start), 'everyone' (anyone signed up) or 'nobody'. The owner can always.
//     Blocked people can't, and words are filtered like everywhere else.
//   - The owner, the person who wrote it, and staff can delete a post. The owner gets a bell.
//
// Showcase:
//   - Up to kShowcase things you made or own, picked by you, shown at the top of your profile.
//
// Everything lives in one object on the account, u.levels:
//   {xp, day, got: {op: times today}, streak, best, lastDaily, wall: [{id, by, text, at}],
//    wallWho, wallDay, wallToday, showcase: [asset ids]}
//
// Requests:
//   levels.me     {}                 -> {levels}            your level, XP and streak
//   wall.list     {id, offset}       -> {posts, more, canPost, who}   (guests too)
//   wall.post     {id, text}         -> {post}              id = whose wall (user number or key)
//   wall.delete   {id, post}         -> {}
//   wall.who      {who}              -> {who}               friends / everyone / nobody
//   showcase.set  {ids: [...]}       -> {showcase}
import { clean } from './banlists.js';

// What gives XP: op -> [XP, how many times a day it counts].
export const XP_FOR = {
  'bolts.play': [10, 12],      // every 5 minutes in a game
  'forum.post': [15, 4], 'forum.reply': [5, 20], 'comments.post': [5, 10], 'wall.post': [5, 10],
  'groups.post': [3, 10], 'art.comment': [3, 10], 'video.comment': [3, 10],
  'upload': [25, 8], 'update': [10, 3], 'art.post': [20, 4], 'video.start': [20, 3], 'paste.make': [5, 4],
  'academy.publish': [30, 2], 'academy.done': [10, 5], 'gamelog.post': [10, 3],
  'friends.accept': [10, 5],
};
export const kDailyXp = 20, kMaxLevel = 100, kShowcase = 6, kWallKept = 200, kWallText = 300, kWallPerDay = 30, kWallPage = 20;
export const WALL_WHO = ['friends', 'everyone', 'nobody'];
export const LEVELS_LOOK_ONLY = ['wall.list'];
export const LEVELS_COSTLY = { 'wall.post': [20, 60, 600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const utcDay = (t) => new Date(t * 1000).toISOString().slice(0, 10);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');

// The XP you need in total to be level L.
export const xpForLevel = (L) => 50 * L * (L - 1);
export function levelOf(xp) {
  let L = 1;
  while (L < kMaxLevel && xp >= xpForLevel(L + 1)) L++;
  return L;
}
// Free Bolts for reaching level L.
export const levelReward = (L) => Math.min(100, 5 * L);
// Daily Bolts on the streak's day n (1 = first day).
export const dailyBolts = (n) => 25 + 5 * Math.min(Math.max(n, 1) - 1, 7);

export function installLevels(Cls) {
  const P = Cls.prototype;

  P.levelsOf = function (u) {
    if (!u.levels || typeof u.levels !== 'object') u.levels = {};
    const lv = u.levels;
    if (!Number.isFinite(lv.xp)) lv.xp = 0;
    if (!Array.isArray(lv.wall)) lv.wall = [];
    if (!Array.isArray(lv.showcase)) lv.showcase = [];
    if (!WALL_WHO.includes(lv.wallWho)) lv.wallWho = 'friends';
    return lv;
  };
  // The streak as it stands today: 0 when they missed yesterday.
  P.levelsStreak = function (u, today) {
    const lv = this.levelsOf(u);
    const yesterday = utcDay(Date.parse(today + 'T00:00:00Z') / 1000 - 86400);
    return lv.lastDaily === today || lv.lastDaily === yesterday ? (lv.streak || 0) : 0;
  };
  P.levelsJson = function (u) {
    const lv = this.levelsOf(u), level = levelOf(lv.xp), t = now(), today = utcDay(t);
    const streak = this.levelsStreak(u, today);
    return { level, xp: lv.xp, from: xpForLevel(level), to: level >= kMaxLevel ? xpForLevel(level) : xpForLevel(level + 1),
      max: level >= kMaxLevel, streak, best: lv.best || 0,
      nextDaily: dailyBolts(streak + 1) };   // the next claim (today's, or tomorrow's if today's is done)
  };

  // Give XP (and Bolts for each new level). Returns the XP given.
  P.giveXp = function (u, amount, why = '') {
    if (!u || u.userId === 0 || u.banned || !(amount > 0)) return 0;
    const lv = this.levelsOf(u), before = levelOf(lv.xp);
    lv.xp += amount;
    const after = levelOf(lv.xp);
    for (let L = before + 1; L <= after; L++) {
      if (!this.hasRef(u, 'level:' + L)) this.add(u, levelReward(L), 'Reached level ' + L, 'level:' + L);
    }
    if (after > before) this.notify(u, 'level', 'You reached level ' + after + '! Here are ' + levelReward(after) + ' Bolts.', String(u.userId));
    this.saveUser(u);
    return amount;
  };
  // After a request worked: XP for it, if it gives any and today's cap isn't reached.
  P.levelsEarn = function (name, me) {
    const rule = XP_FOR[name];
    if (!rule || !me || me.userId === 0) return 0;
    const lv = this.levelsOf(me), today = utcDay(now());
    if (lv.day !== today) { lv.day = today; lv.got = {}; }
    if (!lv.got || typeof lv.got !== 'object') lv.got = {};
    if ((lv.got[name] || 0) >= rule[1]) return 0;
    lv.got[name] = (lv.got[name] || 0) + 1;
    return this.giveXp(me, rule[0], name);
  };

  // bolts.daily: today's Bolts with the streak bonus, and some XP.
  P.dailyClaim = function (me, today) {
    if (this.hasRef(me, 'daily:' + today)) return fail('You already got today\'s Bolts. Come back tomorrow!');
    const lv = this.levelsOf(me);
    const streak = this.levelsStreak(me, today) + 1;
    lv.streak = streak; lv.lastDaily = today; lv.best = Math.max(lv.best || 0, streak);
    const got = dailyBolts(streak);
    this.add(me, got, streak > 1 ? 'Daily reward (' + streak + ' days in a row)' : 'Daily reward', 'daily:' + today);
    this.giveXp(me, kDailyXp, 'daily');
    this.saveUser(me);
    return okay({ me: this.meJson(me), got, streak });
  };

  // ---------------------------------------------------------------- the wall
  P.wallCanPost = function (me, owner) {
    if (!me || me.userId === 0 || !owner || owner.userId === 0) return false;
    if (me.banned || owner.banned) return false;
    if (me.id === owner.id) return true;
    if (this.blocks(me, owner)) return false;
    const who = this.levelsOf(owner).wallWho;
    return who === 'everyone' || (who === 'friends' && owner.friends.includes(me.id));
  };
  P.wallPostJson = function (p, owner, me) {
    const u = this.users.get(p.by);
    return { id: p.id, text: p.text, at: p.at,
      by: u ? { id: u.id, name: u.username || u.name, userId: u.userId, verified: this.isVerified(u), avatar: u.avatar || null } : { id: p.by, name: '?', userId: 0, verified: false, avatar: null },
      canDelete: !!me && me.userId > 0 && (p.by === me.id || owner.id === me.id || this.isStaff(me)) };
  };

  // What the profile page shows: level, streak, showcase, and wall settings.
  P.levelsProfile = function (u, me) {
    const lv = this.levelsOf(u);
    const showcase = lv.showcase.map((id) => this.assets.get(id)).filter((a) => a && this.canPlay(a, me)).map((a) => this.publicAsset(a, me));
    return Object.assign(this.levelsJson(u), { showcase, wallWho: lv.wallWho, wallCount: lv.wall.length, canPost: this.wallCanPost(me, u) });
  };

  P.levelsOp = function (name, me, args) {
    const t = now(), today = utcDay(t);
    const owner = () => {
      const want = str(args, 'id') || (typeof args.id === 'number' ? String(args.id) : '');
      const u = want && want.length < 12 && /^[0-9]+$/.test(want) ? this.findUserId(Number(want)) : this.findPerson(want);
      return u && u.userId > 0 ? u : null;
    };
    if (name === 'levels.me') return okay({ levels: this.levelsJson(me) });
    if (name === 'wall.list') {
      const u = owner();
      if (!u) return fail('There\'s no account with that ID on this server.');
      const offset = Math.max(0, Math.trunc(Number(args.offset)) || 0);
      const all = this.levelsOf(u).wall.filter((p) => { const by = this.users.get(p.by); return !(by && me && me.userId > 0 && this.blocks(me, by)); });
      return okay({ posts: all.slice(offset, offset + kWallPage).map((p) => this.wallPostJson(p, u, me)), more: all.length > offset + kWallPage,
        total: all.length, canPost: this.wallCanPost(me, u), who: this.levelsOf(u).wallWho });
    }
    if (me.userId === 0) return fail('Sign up first.');
    if (name === 'wall.post') {
      const u = owner();
      if (!u) return fail('There\'s no account with that ID on this server.');
      if (!this.wallCanPost(me, u)) {
        const who = this.levelsOf(u).wallWho;
        return fail(who === 'nobody' ? 'This wall is closed.' : who === 'friends' ? 'Only their friends can post here.' : 'You can\'t post here.');
      }
      const text = clean(str(args, 'text'), kWallText, true).replace(/\n{3,}/g, '\n\n');
      if (!text) return fail('Write something first.');
      const mine = this.levelsOf(me);
      if (mine.wallDay !== today) { mine.wallDay = today; mine.wallToday = 0; }
      if (mine.wallToday >= kWallPerDay) return fail('That\'s enough wall posts for today. Come back tomorrow!');
      mine.wallToday++;
      const p = { id: hex(6), by: me.id, text, at: t };
      const lv = this.levelsOf(u);
      lv.wall.unshift(p);
      if (lv.wall.length > kWallKept) lv.wall.length = kWallKept;
      this.saveUser(me, u);
      if (u.id !== me.id) this.notify(u, 'wall', (me.username || me.name) + ' wrote on your wall.', String(u.userId));
      return okay({ post: this.wallPostJson(p, u, me) });
    }
    if (name === 'wall.delete') {
      const u = owner();
      if (!u) return fail('There\'s no account with that ID on this server.');
      const lv = this.levelsOf(u), i = lv.wall.findIndex((p) => p.id === str(args, 'post'));
      if (i < 0) return fail('That post is gone already.');
      const p = lv.wall[i];
      if (p.by !== me.id && u.id !== me.id && !this.isStaff(me)) return fail('You can only delete your own posts, or posts on your wall.');
      lv.wall.splice(i, 1);
      this.saveUser(u);
      return okay({});
    }
    if (name === 'wall.who') {
      const who = str(args, 'who');
      if (!WALL_WHO.includes(who)) return fail('Pick friends, everyone or nobody.');
      this.levelsOf(me).wallWho = who;
      this.saveUser(me);
      return okay({ who });
    }
    if (name === 'showcase.set') {
      const ids = Array.isArray(args.ids) ? [...new Set(args.ids.filter((x) => typeof x === 'string'))] : [];
      if (ids.length > kShowcase) return fail('You can show up to ' + kShowcase + ' things.');
      for (const id of ids) {
        const a = this.assets.get(id);
        if (!a || (a.creator !== me.id && !me.owned.includes(id))) return fail('You can only show things you made or own.');
      }
      this.levelsOf(me).showcase = ids;
      this.saveUser(me);
      return okay({ showcase: this.levelsProfile(me, me).showcase });
    }
    return fail('Unknown request.');
  };

  // XP after every request that worked (see the top). op() is also called from inside the
  // server for lookups, but those ('list'...) never give XP.
  const op = P.op;
  P.op = function (name, me, args) {
    const reply = op.call(this, name, me, args);
    if (reply && reply.ok === true && XP_FOR[name]) this.levelsEarn(name, me);
    // The profile page gets their level, showcase and wall settings.
    if (name === 'profile' && reply && reply.ok === true && reply.user) {
      const u = this.users.get(reply.user.id);
      if (u) reply.levels = this.levelsProfile(u, me);
    }
    return reply;
  };
  // Your level and streak come with "me" everywhere.
  const meJson = P.meJson;
  P.meJson = function (u) { return Object.assign(meJson.call(this, u), { levels: this.levelsJson(u) }); };
}
