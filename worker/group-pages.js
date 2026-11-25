// Group pages that look like a real community's page (website/app/groups.js draws them), and the
// People page's cards. src/server/ServerGroupPages.cpp has the same rules.
//
// What a group gets here (the rest of groups lives in server.js groupOp):
//   - An icon and a banner. Both are decals from the Library (free to use, and every new one gets
//     a staff check before it shows, so nobody needs a second picture check). A decal still
//     waiting for its check is saved but only shows once it passes.
//   - Announcements: longer posts with a title, pinned above the wall. Ranks that can Shout can
//     post them; the writer, staff and ranks with "manage" can delete them. The newest kNewsKept stay.
//   - A member list per rank, with avatars (the Roblox-style "pick a rank, see who has it").
//   - How many people are in each rank (groups.get answers rankCounts).
//
// Requests:
//   groups.pictures       {id, icon?, banner?}   -> {group}   a decal's number or ID ('' takes it off)
//   groups.announce       {id, title, text}      -> {news}
//   groups.announceDelete {id, post}             -> {news}
//   groups.members        {id, rank, offset, limit} -> {rank, total, people}  (rank '' = everyone)
//   users.search          {query, show}  show: '' (everyone), 'online', 'verified' or 'club' (server.js)
//
// People cards (users.search answers these): someone's public info plus their avatar, whether
// they're online (only when their privacy settings let you see it), what they're playing, their
// level and how many groups they're in.
import { levelOf } from './levels.js';

export const kNewsKept = 30, kNewsTitle = 80, kNewsText = 2000, kMembersPage = 60, kNotifyMost = 1000;
export const GROUP_PAGE_OPS = new Set(['groups.pictures', 'groups.announce', 'groups.announceDelete', 'groups.members']);
export const GROUP_PAGE_LOOK_ONLY = ['groups.members'];
export const GROUP_PAGE_COSTLY = { 'groups.announce': [20, 60, 3600], 'groups.pictures': [30, 90, 3600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const int = (args, k) => (typeof args[k] === 'number' && isFinite(args[k]) ? Math.trunc(args[k]) : 0);
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');

export function installGroupPages(Cls, say) {
  const P = Cls.prototype;

  // A group picture ({id, num} of a decal that passed its staff check), or null.
  P.groupPicture = function (id) {
    const a = id ? this.assets.get(id) : null;
    return a && a.kind === 'decal' && !a.review ? { id: a.id, num: a.num || 0 } : null;
  };

  P.groupNewsJson = function (g) {
    return (Array.isArray(g.news) ? g.news : []).slice().reverse().map((n) => {
      const u = this.users.get(n.by);
      return { id: n.id, title: n.title, text: n.text, time: n.time, by: n.by, name: u ? u.name : '?', verified: !!u && this.isVerified(u) };
    });
  };

  // Extra bits for groups.get: announcements, and how many people have each rank.
  P.groupExtras = function (g) {
    const rankCounts = {};
    for (const id of Object.keys(g.members)) {
      const r = this.rankIn(g, id);
      if (r) rankCounts[r.id] = (rankCounts[r.id] || 0) + 1;
    }
    return { news: this.groupNewsJson(g), rankCounts };
  };

  // A person as the People page and member lists show them.
  P.personCard = function (u, viewer) {
    const seen = this.presence(viewer, u);
    return Object.assign(this.publicUser(u), {
      avatar: u.avatar || null, wearing: this.wornItems(u), online: !!seen.online, playing: seen.playing ? seen.playing.title : '',
      level: levelOf((u.levels && Number.isFinite(u.levels.xp)) ? u.levels.xp : 0), groups: this.groupsOf(u.id).length });
  };

  // The People page's tabs: everyone, who's online now, Verified people, or Club members.
  P.peopleShown = function (show, u, viewer) {
    if (show === 'online') return !!this.presence(viewer, u).online;
    if (show === 'verified') return this.isVerified(u);
    if (show === 'club') return !!this.clubOf(u);
    return true;
  };

  P.groupPageOp = function (name, me, args, g) {
    const t = now();
    const can = (perm) => this.groupCan(g, me, perm);
    const staff = this.isStaff(me);

    if (name === 'groups.members') {
      const rank = str(args, 'rank');
      const ranks = this.ranksOf(g);
      if (rank && !ranks.some((r) => r.id === rank)) return fail('That rank is gone.');
      let ids = Object.keys(g.members).filter((id) => this.users.has(id));
      if (rank) ids = ids.filter((id) => { const r = this.rankIn(g, id); return r && r.id === rank; });
      ids.sort((a, b) => (this.rankIn(g, b).level - this.rankIn(g, a).level) || (this.users.get(a).userId - this.users.get(b).userId));
      const offset = Math.max(0, int(args, 'offset'));
      const limit = 'limit' in args ? Math.min(100, Math.max(1, int(args, 'limit'))) : kMembersPage;
      const people = ids.slice(offset, offset + limit).map((id) => {
        const r = this.rankIn(g, id);
        return Object.assign(this.personCard(this.users.get(id), me), { role: r.name, rank: r.id, rankLevel: r.level });
      });
      return okay({ rank, total: ids.length, people });
    }
    if (name === 'groups.pictures') {
      if (!can('manage') && !staff) return fail('Your rank can\'t change this group.');
      const pick = (key) => {
        if (!(key in args)) return '';
        const want = String(args[key] ?? '').trim().replace(/^gb:/, '');
        if (!want) { delete g[key]; return ''; }
        const a = this.assets.get(this.assets.resolve(want));
        if (!a || a.kind !== 'decal') return 'That isn\'t a decal. Pick a decal from the Library (or upload your own).';
        g[key] = a.id;
        return '';
      };
      const bad = pick('icon') || pick('banner');
      if (bad) return fail(bad);
      this.saveGroup(g);
      const waiting = [g.icon, g.banner].some((id) => { const a = id && this.assets.get(id); return a && a.review; });
      return okay({ group: this.publicGroup(g), waiting });
    }
    if (name === 'groups.announce') {
      if (!can('shout')) return fail('Your rank can\'t post announcements in this group.');
      const title = say(str(args, 'title'), kNewsTitle);
      const text = say(str(args, 'text'), kNewsText, true);
      if (!title) return fail('Give the announcement a title.');
      if (!text) return fail('Write the announcement first.');
      if (!Array.isArray(g.news)) g.news = [];
      g.news.push({ id: 'n-' + hex(4), by: me.id, title, text, time: t });
      if (g.news.length > kNewsKept) g.news.splice(0, g.news.length - kNewsKept);
      // Everyone in the group hears about it (the first kNotifyMost, so a huge group stays quick).
      for (const id of Object.keys(g.members).slice(0, kNotifyMost)) {
        if (id !== me.id) this.notify(this.users.get(id), 'group', g.name + ': ' + title, g.id);
      }
      this.saveGroup(g);
      return okay({ news: this.groupNewsJson(g) });
    }
    if (name === 'groups.announceDelete') {
      const list = Array.isArray(g.news) ? g.news : [];
      const i = list.findIndex((n) => n.id === str(args, 'post'));
      if (i < 0) return fail('That announcement is already gone.');
      if (list[i].by !== me.id && !can('manage') && !staff) return fail('Only the writer or a group admin can delete that.');
      list.splice(i, 1);
      this.saveGroup(g);
      return okay({ news: this.groupNewsJson(g) });
    }
    return fail('Unknown request.');
  };
}
