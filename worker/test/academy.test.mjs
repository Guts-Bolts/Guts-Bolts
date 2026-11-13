// G&B Academy (worker/academy.js): writing courses, drafts and publishing, lessons, progress,
// likes, reports, deletes, and Academy being its own product on the site.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { makeServer } from './helpers.mjs';
import { lessonText, kPerDay, kMostLessons } from '../academy.js';
import { PRODUCTS, GROUPS, productOf, activeLink } from '../../website/app/products.js';

const LESSONS = [{ title: 'Your first script', text: 'Make a Script.\n```lua\nprint("hi")\n\tif x then end\n```' }, { title: 'Variables', text: 'local x = 5' }];
function setup() {
  const s = makeServer();
  return { s, ann: s.signUp('ann'), bob: s.signUp('bob') };
}
const write = (s, who, extra = {}) => s.call(who, 'academy.save', Object.assign({ title: 'Lua for beginners', summary: 'Learn to script', topic: 'scripting', level: 'beginner', lessons: LESSONS }, extra));
const published = (s, who, extra) => { const c = write(s, who, extra).course; assert.ok(s.call(who, 'academy.publish', { id: c.id, on: true }).ok); return s.call(who, 'academy.get', { id: c.id }).course; };

test('lesson text keeps code\'s shape', () => {
  assert.equal(lessonText('a\n\tb'), 'a\n    b');
  assert.equal(lessonText('  \n '), '');
});

test('writing: a title and lessons with text are needed; guests can\'t write', () => {
  const { s, ann } = setup();
  assert.match(write(s, ann, { title: ' ' }).error, /title/);
  assert.match(write(s, ann, { lessons: [{ title: 'x', text: '' }] }).error, /needs some text/);
  assert.match(write(s, ann, { lessons: [{ title: '', text: 'x' }] }).error, /lesson 1 a title/);
  assert.match(write(s, ann, { lessons: Array.from({ length: kMostLessons + 1 }, () => LESSONS[1]) }).error, /up to 30 lessons/);
  assert.match(s.call(s.user('c'.repeat(64)), 'academy.save', { title: 'x' }).error, /Sign up/);
  const r = write(s, ann, { topic: 'knitting', level: 'wizard' });
  assert.ok(r.ok, r.error);
  assert.equal(r.course.topic, 'other');
  assert.equal(r.course.level, 'beginner');
  assert.equal(r.course.published, false, 'it starts as a draft');
  assert.equal(r.course.lessonList.length, 2);
});

test('drafts stay hidden until published; a course needs a lesson to go out', () => {
  const { s, ann, bob } = setup();
  const empty = write(s, ann, { lessons: [] }).course;
  assert.match(s.call(ann, 'academy.publish', { id: empty.id }).error, /at least one lesson/);
  const c = write(s, ann).course;
  assert.equal(s.call(bob, 'academy.list', {}).courses.length, 0);
  assert.match(s.call(bob, 'academy.get', { id: c.id }).error, /isn't here/);
  assert.match(s.call(bob, 'academy.publish', { id: c.id }).error, /isn't here/);
  assert.ok(s.call(ann, 'academy.publish', { id: c.id }).ok);
  const guest = s.user('d'.repeat(64));
  const r = s.call(guest, 'academy.list', {});
  assert.deepEqual(r.courses.map((x) => x.id), [c.id], 'guests can look');
  assert.equal(s.call(guest, 'academy.list', { topic: 'building' }).courses.length, 0);
  assert.equal(s.call(guest, 'academy.list', { q: 'lua' }).courses.length, 1);
  assert.equal(s.call(guest, 'academy.list', { by: 'ann' }).courses.length, 1);
});

test('lessons: next and back, and editing keeps lesson ids (so ticks stay)', () => {
  const { s, ann, bob } = setup();
  const c = published(s, ann);
  const [l1, l2] = c.lessonList.map((l) => l.id);
  const r = s.call(bob, 'academy.lesson', { id: c.id, lesson: l1 });
  assert.equal(r.lesson.next, l2);
  assert.equal(r.lesson.prev, '');
  assert.match(r.lesson.text, / {4}if x then end/);
  assert.equal(s.call(bob, 'academy.done', { id: c.id, lesson: l1 }).done, 1);
  const edited = s.call(ann, 'academy.save', { id: c.id, title: c.title, topic: 'scripting', lessons: [{ id: l1, title: 'First script', text: 'new words' }, { id: l2, ...LESSONS[1] }, { title: 'Loops', text: 'for i = 1, 3 do end' }] });
  assert.ok(edited.ok, edited.error);
  assert.deepEqual(edited.course.lessonList.slice(0, 2).map((l) => l.id), [l1, l2]);
  assert.equal(s.call(bob, 'academy.get', { id: c.id }).course.done, 1, 'bob\'s tick stayed');
  assert.match(s.call(bob, 'academy.save', { id: c.id, title: 'Mine now' }).error, /Only the writer/);
});

test('progress: finishing a course tells its writer, and counts learners once', () => {
  const { s, ann, bob } = setup();
  const c = published(s, ann);
  for (const l of c.lessonList) s.call(bob, 'academy.done', { id: c.id, lesson: l.id });
  assert.ok(ann.notes.some((n) => n.kind === 'academy' && /bob finished your course "Lua for beginners"/.test(n.text)));
  const after = s.call(bob, 'academy.get', { id: c.id }).course;
  assert.equal(after.done, 2);
  assert.equal(after.learners, 1);
  assert.equal(s.call(bob, 'academy.done', { id: c.id, lesson: c.lessonList[0].id, on: false }).done, 1, 'ticks can be taken off');
  const mine = s.call(bob, 'academy.mine', {});
  assert.equal(mine.learning.length, 1);
  assert.equal(mine.written.length, 0);
});

test('likes: once each, never your own', () => {
  const { s, ann, bob } = setup();
  const c = published(s, ann);
  assert.equal(s.call(bob, 'academy.like', { id: c.id }).likes, 1);
  assert.equal(s.call(bob, 'academy.like', { id: c.id, on: true }).likes, 1);
  assert.match(s.call(ann, 'academy.like', { id: c.id }).error, /own course/);
  const other = published(s, ann, { title: 'Building 101', topic: 'building' });
  assert.deepEqual(s.call(bob, 'academy.list', { sort: 'top' }).courses.map((x) => x.id), [c.id, other.id], 'most liked first');
});

test('a daily limit for new courses', () => {
  const { s, ann } = setup();
  for (let i = 0; i < kPerDay; i++) assert.ok(write(s, ann).ok);
  assert.match(write(s, ann).error, /Come back tomorrow/);
});

test('courses can be reported; the writer or staff delete them', () => {
  const { s, ann, bob } = setup();
  const c = published(s, ann);
  assert.match(s.call(ann, 'report.send', { kind: 'course', id: c.id, reason: 'spam' }).error, /yourself/);
  assert.ok(s.call(bob, 'report.send', { kind: 'course', id: c.id, reason: 'spam' }).ok);
  const list = s.call(s.staff, 'admin.reports', {});
  if (list.ok) assert.equal(list.reports.find((x) => x.kind === 'course').name, 'Lua for beginners');
  assert.match(s.call(bob, 'academy.delete', { id: c.id }).error, /writer or staff/);
  assert.ok(s.call(s.staff, 'academy.delete', { id: c.id }).ok);
  assert.equal(s.call(bob, 'academy.list', {}).courses.length, 0);
});

test('Academy is its own product on the site, in the Learning group', () => {
  const academy = PRODUCTS.find((p) => p.id === 'academy');
  assert.equal(academy.status, 'open');
  assert.equal(productOf('lesson').id, 'academy');
  assert.equal(activeLink('academy', '#/academy?mine=1'), 'academy-mine');
  assert.equal(activeLink('course', '#/course/x'), 'academy');
  const learning = GROUPS.find((g) => g.id === 'learning');
  assert.ok(learning.products.includes('academy'));
  const page = readFileSync(new URL('../../website/app/index.html', import.meta.url), 'utf8');
  assert.match(page, /href="academy\.css"/);
  assert.match(page, /href="#\/academy"/);
  const server = readFileSync(new URL('../server.js', import.meta.url), 'utf8');
  assert.match(server, /op === 'academy\.publish'/, 'Fyxa looks at published courses');
});
