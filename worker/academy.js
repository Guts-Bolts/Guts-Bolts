// G&B Academy: courses anyone can write, about scripting, building, modeling, game design and
// more. Its own product on the site (website/app/academy.js draws its pages); it grows Studio
// Learn (the 20 lessons built into Studio) into courses written by the community.
// src/server/ServerAcademy.cpp has the same rules.
//
// How it works:
//   - A course has a title, a short summary, a topic, a level and up to kMostLessons lessons.
//     Each lesson is a title and some text (``` code blocks show as code on the website).
//   - Courses start as drafts: only the writer (and staff) see them until they press Publish.
//   - Learners tick lessons off as they finish them; that's kept per account (the "academy"
//     progress table), so it follows you to any device. Finishing every lesson of someone
//     else's course tells its writer.
//   - People can like courses ("helpful"), report them, and the writer or staff can delete them.
//   - Text goes through the word filter like the forum. Fyxa looks at published courses too.
//   - kPerDay new courses a day each (Verified: kPerDayVerified), at most kMostEach courses each.
//
// Requests:
//   academy.list    {topic, level, q, sort, by, page}  -> {courses, more}   published ones (guests too)
//   academy.get     {id}                     -> {course}   with its lessons' titles and your progress (guests too)
//   academy.lesson  {id, lesson}             -> {course, lesson}  one lesson's text (guests too)
//   academy.save    {id?, title, summary, topic, level, lessons: [{id?, title, text}]} -> {course}
//   academy.publish {id, on}                 -> {course}
//   academy.delete  {id}                     -> {}
//   academy.done    {id, lesson, on}         -> {done, finished}
//   academy.like    {id, on}                 -> {likes, liked}
//   academy.mine    {}                       -> {written, learning}
import { clean } from './banlists.js';

export const ACADEMY_TOPICS = ['scripting', 'building', 'modeling', 'animation', 'gamedesign', 'art', 'audio', 'studio', 'other'];
export const ACADEMY_LEVELS = ['beginner', 'intermediate', 'advanced'];
export const kTitle = 80, kSummary = 300, kLessonTitle = 80, kLessonText = 20000, kMostLessons = 30;
export const kPageSize = 24, kPerDay = 5, kPerDayVerified = 20, kMostEach = 100;
export const ACADEMY_LOOK_ONLY = ['academy.list', 'academy.get', 'academy.lesson'];
export const ACADEMY_COSTLY = { 'academy.save': [60, 180, 3600], 'academy.like': [240, 720, 600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');
// Lesson text: tabs become 4 spaces (so code keeps its shape), then the usual clean-up.
export const lessonText = (s) => clean(String(s || '').replace(/\t/g, '    '), kLessonText, true);

export function installAcademy(Cls) {
  const P = Cls.prototype;

  P.academyInit = function () {
    if (this.courses) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS courses (id TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.sql.exec('CREATE TABLE IF NOT EXISTS academy_progress (account TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.courses = new Map();
    this.academyProgress = new Map();
    for (const r of this.sql.exec('SELECT id, data FROM courses')) this.courses.set(r.id, JSON.parse(r.data));
    for (const r of this.sql.exec('SELECT account, data FROM academy_progress')) this.academyProgress.set(r.account, JSON.parse(r.data));
  };
  P.academySave = function (c) { this.sql.exec('INSERT OR REPLACE INTO courses (id, data) VALUES (?, ?)', c.id, JSON.stringify(c)); };
  // Your progress: { courseId: [lesson ids done] }.
  P.academyMine = function (me) { return (me && this.academyProgress.get(me.id)) || {}; };
  P.academySaveProgress = function (me, prog) {
    this.academyProgress.set(me.id, prog);
    this.sql.exec('INSERT OR REPLACE INTO academy_progress (account, data) VALUES (?, ?)', me.id, JSON.stringify(prog));
  };
  P.academyCanSee = function (c, me) { return c.published || (!!me && (c.by === me.id || this.isStaff(me))); };
  P.academyJson = function (c, me, full = false) {
    const u = this.users.get(c.by);
    const done = (this.academyMine(me)[c.id] || []).filter((id) => c.lessons.some((l) => l.id === id));
    const out = { id: c.id, title: c.title, summary: c.summary, topic: c.topic, level: c.level, published: !!c.published,
      at: c.at, edited: c.edited || 0, lessons: c.lessons.length, likes: c.likes.length, learners: c.learners || 0, done: done.length,
      by: u ? { id: u.id, name: u.name, userId: u.userId, verified: this.isVerified(u) } : { id: c.by, name: '?', userId: 0, verified: false } };
    if (full) Object.assign(out, {
      lessonList: c.lessons.map((l) => ({ id: l.id, title: l.title, done: done.includes(l.id) })),
      liked: !!me && c.likes.includes(me.id), mine: !!me && c.by === me.id, canDelete: !!me && (c.by === me.id || this.isStaff(me)) });
    return out;
  };
  // For report.send (server.js).
  P.academyReportTarget = function (id, me) {
    this.academyInit();
    const c = this.courses.get(id);
    if (!c || !this.academyCanSee(c, me)) return 'That course isn\'t there any more.';
    if (c.by === me.id) return 'You can\'t report yourself.';
    return { target: c.id, about: c.by, copy: { subject: 'Course: ' + c.title, body: c.summary || '(no summary)', at: c.at } };
  };
  P.academyReportName = function (target) { this.academyInit(); const c = this.courses.get(target); return c ? c.title : '(deleted)'; };

  P.academyOp = function (name, me, args) {
    this.academyInit();
    const t = now();
    if (name === 'academy.list') {
      const topic = ACADEMY_TOPICS.includes(str(args, 'topic')) ? str(args, 'topic') : '';
      const level = ACADEMY_LEVELS.includes(str(args, 'level')) ? str(args, 'level') : '';
      const sort = str(args, 'sort') === 'top' ? 'top' : 'new';
      const q = str(args, 'q').toLowerCase().slice(0, 60);
      let by = null;
      if (args.by !== undefined && args.by !== '') {
        by = this.findPerson(args.by);
        if (!by) return fail('There\'s nobody with that name or number.');
      }
      const page = Math.max(1, Math.min(1000, Math.trunc(Number(args.page) || 1)));
      const list = [...this.courses.values()].filter((c) => c.published && c.lessons.length && (!topic || c.topic === topic) && (!level || c.level === level)
        && (!by || c.by === by.id) && (!q || c.title.toLowerCase().includes(q) || c.summary.toLowerCase().includes(q)))
        .sort((x, y) => (sort === 'top' && y.likes.length !== x.likes.length ? y.likes.length - x.likes.length : y.at - x.at));
      const from = (page - 1) * kPageSize;
      return okay({ courses: list.slice(from, from + kPageSize).map((c) => this.academyJson(c, me)), more: list.length > from + kPageSize, page,
        total: list.length, topics: ACADEMY_TOPICS, levels: ACADEMY_LEVELS });
    }
    const c = this.courses.get(str(args, 'id')) || null;
    if (name === 'academy.get' || name === 'academy.lesson') {
      if (!c || !this.academyCanSee(c, me)) return fail('That course isn\'t here (any more).');
      if (name === 'academy.get') return okay({ course: this.academyJson(c, me, true) });
      const i = c.lessons.findIndex((l) => l.id === str(args, 'lesson'));
      if (i < 0) return fail('That lesson isn\'t in this course (any more).');
      const l = c.lessons[i];
      return okay({ course: this.academyJson(c, me, true), lesson: { id: l.id, title: l.title, text: l.text, n: i + 1,
        prev: i > 0 ? c.lessons[i - 1].id : '', next: i + 1 < c.lessons.length ? c.lessons[i + 1].id : '' } });
    }
    if (!me || me.userId === 0) return fail('Sign up to write courses and keep your progress.');
    const staff = this.isStaff(me);

    if (name === 'academy.mine') {
      const prog = this.academyMine(me);
      const written = [...this.courses.values()].filter((x) => x.by === me.id).sort((x, y) => y.at - x.at).map((x) => this.academyJson(x, me));
      const learning = Object.keys(prog).map((id) => this.courses.get(id)).filter((x) => x && x.published && x.by !== me.id)
        .map((x) => this.academyJson(x, me));
      return okay({ written, learning });
    }
    if (name === 'academy.save') {
      if (c && c.by !== me.id) return fail('Only the writer can change this course.');
      if (!c && str(args, 'id')) return fail('That course isn\'t here (any more).');
      const title = clean(str(args, 'title'), kTitle);
      if (!title) return fail('Give your course a title.');
      const summary = clean(str(args, 'summary'), kSummary, true);
      const topic = ACADEMY_TOPICS.includes(str(args, 'topic')) ? str(args, 'topic') : 'other';
      const level = ACADEMY_LEVELS.includes(str(args, 'level')) ? str(args, 'level') : 'beginner';
      const given = Array.isArray(args.lessons) ? args.lessons : [];
      if (given.length > kMostLessons) return fail('A course can have up to ' + kMostLessons + ' lessons.');
      const lessons = [];
      for (const [i, x] of given.entries()) {
        if (!x || typeof x !== 'object') return fail('Lesson ' + (i + 1) + ' looks wrong.');
        const lt = clean(str(x, 'title'), kLessonTitle), text = lessonText(str(x, 'text'));
        if (!lt) return fail('Give lesson ' + (i + 1) + ' a title.');
        if (!text) return fail('Lesson ' + (i + 1) + ' ("' + lt + '") needs some text.');
        const old = c && c.lessons.find((l) => l.id === str(x, 'id'));   // (keeps its id, so ticks stay)
        const id = old && !lessons.some((l) => l.id === old.id) ? old.id : hex(4);
        lessons.push({ id, title: lt, text });
      }
      if (c) {
        if (c.published && !lessons.length) return fail('A published course needs at least one lesson.');
        Object.assign(c, { title, summary, topic, level, lessons, edited: t });
        this.academySave(c);
        return okay({ course: this.academyJson(c, me, true) });
      }
      const today = Math.floor(t / 86400);
      if (me.academyDay !== today) { me.academyDay = today; me.academyToday = 0; }
      const most = this.isVerified(me) || staff ? kPerDayVerified : kPerDay;
      if (me.academyToday >= most) return fail('You\'ve started ' + most + ' courses today. Come back tomorrow!');
      if ([...this.courses.values()].filter((x) => x.by === me.id).length >= kMostEach) return fail('You have ' + kMostEach + ' courses. Delete some first.');
      const fresh = { id: hex(5), by: me.id, title, summary, topic, level, lessons, published: false, at: t, edited: 0, likes: [], learners: 0 };
      this.courses.set(fresh.id, fresh);
      this.academySave(fresh);
      me.academyToday += 1;
      this.saveUser(me);
      return okay({ course: this.academyJson(fresh, me, true) });
    }

    if (!c || !this.academyCanSee(c, me)) return fail('That course isn\'t here (any more).');
    if (name === 'academy.publish') {
      if (c.by !== me.id) return fail('Only the writer can publish this course.');
      const on = args.on !== false;
      if (on && !c.lessons.length) return fail('Add at least one lesson first.');
      if (on && !c.published) c.at = t;   // (shows as new when it goes out)
      c.published = on;
      this.academySave(c);
      return okay({ course: this.academyJson(c, me, true) });
    }
    if (name === 'academy.delete') {
      if (c.by !== me.id && !staff) return fail('Only the writer or staff can delete this course.');
      this.courses.delete(c.id);
      this.sql.exec('DELETE FROM courses WHERE id = ?', c.id);
      return okay();
    }
    if (!c.published) return fail('This course isn\'t published yet.');
    if (name === 'academy.like') {
      const on = args.on !== false, had = c.likes.includes(me.id);
      if (c.by === me.id) return fail('You can\'t like your own course.');
      if (on && !had) c.likes.push(me.id);
      if (!on && had) c.likes = c.likes.filter((x) => x !== me.id);
      if (on !== had) this.academySave(c);
      return okay({ likes: c.likes.length, liked: on });
    }
    if (name === 'academy.done') {
      const lid = str(args, 'lesson');
      if (!c.lessons.some((l) => l.id === lid)) return fail('That lesson isn\'t in this course (any more).');
      const prog = Object.assign({}, this.academyMine(me));
      const started = !!prog[c.id];
      const list = (prog[c.id] || []).filter((x) => c.lessons.some((l) => l.id === x));
      const before = c.lessons.every((l) => list.includes(l.id));
      const on = args.on !== false;
      prog[c.id] = on ? (list.includes(lid) ? list : [...list, lid]) : list.filter((x) => x !== lid);
      this.academySaveProgress(me, prog);
      if (!started && c.by !== me.id) { c.learners = (c.learners || 0) + 1; this.academySave(c); }
      const finished = c.lessons.every((l) => prog[c.id].includes(l.id));
      if (finished && !before && c.by !== me.id)
        this.notify(this.users.get(c.by), 'academy', me.name + ' finished your course "' + c.title + '"!', c.id);
      return okay({ done: prog[c.id].length, finished });
    }
    return fail('Unknown request.');
  };
}
