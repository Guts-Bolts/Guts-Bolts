// "What's On": the events calendar (calendar.js), Press posts by staff (press.js) and polls on
// forum threads and group walls (polls.js), plus their pages for search engines (seo.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { makeServer } from './helpers.mjs';
import { calendarLink, calendarTimesProblem, kPerDay } from '../calendar.js';
import { pressSlug, pressBlocks } from '../press.js';
import { makePoll, pollJson, pollVote } from '../polls.js';
import { build, sitemap, appAddress, SECTIONS } from '../seo.js';
import { PRODUCTS, GROUPS, productOf, activeLink } from '../../website/app/products.js';

const now = () => Math.floor(Date.now() / 1000);
const H = 3600, D = 86400;
function setup() {
  const s = makeServer();
  return { s, ann: s.signUp('ann'), bob: s.signUp('bob') };
}
const say = (x, n) => String(x).trim().slice(0, n);

// ---------- polls ----------
test('a poll needs a question and 2 to 6 different answers', () => {
  const t = now();
  assert.equal(makePoll(undefined, say, t).poll, null, 'no poll asked for');
  assert.match(makePoll({ question: '', options: ['a', 'b'] }, say, t).error, /question/);
  assert.match(makePoll({ question: 'Best map?', options: ['a', ' A ', ''] }, say, t).error, /2 different/);
  assert.match(makePoll({ question: 'Best map?', options: ['1', '2', '3', '4', '5', '6', '7'] }, say, t).error, /at most 6/);
  assert.match(makePoll({ question: 'Best map?', options: ['a', 'b'], hours: 5 }, say, t).error, /how long/);
  const { poll } = makePoll({ question: 'Best map?', options: ['Docks', 'Desert'] }, say, t);
  assert.equal(poll.until, t + 168 * H, 'a week by default');
});

test('one vote each, change it or take it back, none after it closes, nobody sees who picked what', () => {
  const t = now();
  const { poll } = makePoll({ question: 'Q', options: ['a', 'b', 'c'], hours: 24 }, say, t);
  assert.equal(pollVote(poll, 'x', 1, t), '');
  assert.equal(pollVote(poll, 'y', 1, t), '');
  assert.equal(pollVote(poll, 'x', 2, t), '', 'changing your vote');
  assert.match(pollVote(poll, 'x', 9, t), /Pick one/);
  let j = pollJson(poll, 'x', t);
  assert.deepEqual(j.options.map((o) => o.votes), [0, 1, 1]);
  assert.equal(j.total, 2);
  assert.equal(j.mine, 2);
  assert.equal(JSON.stringify(j).includes('"y"'), false, 'who voted stays hidden');
  assert.equal(pollVote(poll, 'y', -1, t), '', 'take it back');
  assert.equal(pollJson(poll, 'y', t).mine, -1);
  assert.match(pollVote(poll, 'x', 0, t + 25 * H), /closed/);
  assert.equal(pollJson(poll, 'x', t + 25 * H).closed, true);
});

test('forum threads can carry a poll', () => {
  const { s, ann, bob } = setup();
  let r = s.call(ann, 'forum.post', { board: 'offtopic', title: 'Pick the next map', text: '', poll: { question: 'Which one?', options: ['Docks', 'Desert'] } });
  assert.ok(r.ok, r.error);
  const id = r.thread.id;
  assert.equal(r.thread.hasPoll, true);
  assert.equal(r.thread.poll.question, 'Which one?');
  assert.match(s.call(ann, 'forum.post', { board: 'offtopic', title: 'Bad', text: 'x', poll: { question: 'Q', options: ['one'] } }).error, /2 different/);
  r = s.call(bob, 'forum.pollVote', { thread: id, option: 1 });
  assert.ok(r.ok, r.error);
  assert.equal(r.poll.options[1].votes, 1);
  assert.equal(r.poll.mine, 1);
  assert.match(s.call(bob, 'forum.pollVote', { thread: id, option: 'x' }).error, /Pick one/);
  r = s.call(ann, 'forum.thread', { thread: id });
  assert.equal(r.thread.poll.total, 1);
  assert.equal(r.thread.poll.mine, -1);
  assert.equal(s.call(s.user('c'.repeat(64)), 'forum.thread', { thread: id }).thread.poll.total, 1, 'guests see the results');
  assert.match(s.call(s.user('c'.repeat(64)), 'forum.pollVote', { thread: id, option: 0 }).error, /Sign up/);
});

test('group walls can carry a poll, for members only', () => {
  const { s, ann, bob } = setup();
  const g = s.call(ann, 'groups.create', { name: 'Builders' }).group;
  let r = s.call(ann, 'groups.post', { id: g.id, text: 'Meeting day?', poll: { question: 'When?', options: ['Friday', 'Saturday'], hours: 72 } });
  assert.ok(r.ok, r.error);
  const post = s.call(ann, 'groups.get', { id: g.id }).wall[0];
  assert.equal(post.poll.question, 'When?');
  assert.match(s.call(bob, 'groups.pollVote', { id: g.id, by: post.id, time: post.time, option: 0 }).error, /Join/);
  s.call(bob, 'groups.join', { id: g.id });
  r = s.call(bob, 'groups.pollVote', { id: g.id, by: post.id, time: post.time, option: 0 });
  assert.ok(r.ok, r.error);
  assert.equal(r.poll.options[0].votes, 1);
  assert.equal(s.call(ann, 'groups.get', { id: g.id }).wall[0].poll.total, 1);
});

// ---------- the calendar ----------
test('calendar links stay on the site, and times are checked', () => {
  assert.equal(calendarLink(''), '');
  assert.equal(calendarLink('#/game/12'), '#/game/12');
  assert.equal(calendarLink('https://gutsandbolts.net/app/#/live'), '#/live');
  assert.equal(calendarLink('https://evil.example/x'), null);
  assert.equal(calendarLink('javascript:alert(1)'), null);
  assert.equal(calendarLink('#/x" onmouseover="y'), null);
  const t = now();
  assert.equal(calendarTimesProblem(t + H, t + 2 * H, t), '');
  assert.match(calendarTimesProblem(t - 2 * H, t + H, t), /gone by/);
  assert.match(calendarTimesProblem(t + H, t + H + 60, t), /15 minutes/);
  assert.match(calendarTimesProblem(t + H, t + 40 * D, t), /31 days/);
  assert.match(calendarTimesProblem(t + 400 * D, t + 401 * D, t), /a year/);
});

test('making events: anyone signed up, staff ones are official, limits a day', () => {
  const { s, ann } = setup();
  const t = now();
  const ev = (extra = {}) => Object.assign({ title: 'Winter Jam', text: 'Make a game about snow.', kind: 'jam', start: t + D, end: t + 8 * D }, extra);
  let r = s.call(ann, 'calendar.make', ev());
  assert.ok(r.ok, r.error);
  assert.equal(r.event.official, false);
  assert.equal(r.event.kindName, 'Game jam');
  assert.equal(r.event.going, 1, 'the maker is going');
  assert.equal(r.event.state, 'soon');
  assert.match(s.call(ann, 'calendar.make', ev({ title: '' })).error, /name/);
  assert.match(s.call(ann, 'calendar.make', ev({ link: 'https://x.com' })).error, /Guts&Bolts/);
  assert.match(s.call(s.user('c'.repeat(64)), 'calendar.make', ev()).error, /Sign up/);
  for (let i = 1; i < kPerDay; i++) assert.ok(s.call(ann, 'calendar.make', ev({ title: 'Jam ' + i })).ok);
  assert.match(s.call(ann, 'calendar.make', ev()).error, /today/);
  r = s.call(s.staff, 'calendar.make', ev({ title: 'Official Jam', kind: 'live' }));
  assert.equal(r.event.official, true);
});

test('the calendar list: upcoming, a range of time, past ones, by kind', () => {
  const { s, ann } = setup();
  const t = now();
  s.call(ann, 'calendar.make', { title: 'Later', kind: 'art', start: t + 5 * D, end: t + 6 * D });
  s.call(ann, 'calendar.make', { title: 'Sooner', kind: 'live', start: t + D, end: t + D + 2 * H });
  const old = s.call(ann, 'calendar.make', { title: 'Old', kind: 'meetup', start: t + H, end: t + 2 * H }).event;
  s.calendar.get(old.id).start = t - 3 * D; s.calendar.get(old.id).end = t - 2 * D;
  let r = s.call(ann, 'calendar.list', {});
  assert.deepEqual(r.events.map((e) => e.title), ['Sooner', 'Later']);
  assert.equal(r.events[0].text, undefined, 'the list leaves the description out');
  assert.deepEqual(s.call(ann, 'calendar.list', { when: 'past' }).events.map((e) => e.title), ['Old']);
  assert.deepEqual(s.call(ann, 'calendar.list', { kind: 'art' }).events.map((e) => e.title), ['Later']);
  assert.deepEqual(s.call(ann, 'calendar.list', { from: t - 4 * D, to: t + 2 * D }).events.map((e) => e.title), ['Old', 'Sooner']);
  assert.match(s.call(ann, 'calendar.list', { from: t, to: t + 90 * D }).error, /two months/);
  assert.equal(s.call(s.user('c'.repeat(64)), 'calendar.list', {}).events.length, 2, 'guests can look');
});

test('going, changing the time tells people going, only the maker or staff can change it', () => {
  const { s, ann, bob } = setup();
  const t = now();
  const e = s.call(ann, 'calendar.make', { title: 'Talk show', kind: 'live', start: t + D, end: t + D + H, link: '#/live' }).event;
  let r = s.call(bob, 'calendar.going', { id: e.id });
  assert.equal(r.event.going, 2);
  assert.equal(r.event.amGoing, true);
  assert.match(s.call(bob, 'calendar.edit', { id: e.id, title: 'Mine now', start: e.start, end: e.end }).error, /Only the person/);
  r = s.call(ann, 'calendar.edit', { id: e.id, title: 'Talk show', kind: 'live', start: t + 2 * D, end: t + 2 * D + H });
  assert.ok(r.ok, r.error);
  assert.match(bob.notes[0].text, /new time/);
  assert.equal(bob.notes[0].kind, 'calendar');
  r = s.call(bob, 'calendar.going', { id: e.id, on: false });
  assert.equal(r.event.going, 1);
  s.call(bob, 'calendar.going', { id: e.id });
  assert.ok(s.call(ann, 'calendar.delete', { id: e.id }).ok);
  assert.match(bob.notes[0].text, /called off/);
  assert.match(s.call(ann, 'calendar.get', { id: e.id }).error, /isn't on the calendar/);
});

test('events can be hosted by a group (manage needed) and reported', () => {
  const { s, ann, bob } = setup();
  const t = now();
  const g = s.call(ann, 'groups.create', { name: 'Jammers' }).group;
  const base = { title: 'Group jam', kind: 'jam', start: t + D, end: t + 2 * D, group: g.id };
  assert.match(s.call(bob, 'calendar.make', base).error, /manage/);
  const e = s.call(ann, 'calendar.make', base).event;
  assert.equal(e.group.name, 'Jammers');
  assert.ok(s.call(bob, 'report.send', { kind: 'calendar', id: e.id, reason: 'spam' }).ok);
  assert.equal(s.call(s.staff, 'admin.reports', {}).reports.find((x) => x.kind === 'calendar').name, 'Group jam');
});

// ---------- Press ----------
test('Press ids read well, and the text becomes headings and paragraphs', () => {
  assert.equal(pressSlug('Big Winter Jam! Café'), 'big-winter-jam-cafe');
  assert.equal(pressSlug('!!!'), 'post');
  assert.deepEqual(pressBlocks('Hello there.\n\n## What\'s new\nCars.\n\n\nBye'), [{ p: 'Hello there.' }, { h: 'What\'s new' }, { p: 'Cars.' }, { p: 'Bye' }]);
});

test('only staff write Press posts; everyone reads them; drafts stay hidden', () => {
  const { s, ann } = setup();
  const game = s.addGame(ann, 'Snow Fort');
  assert.match(s.call(ann, 'press.write', { title: 'Hi', text: 'x' }).error, /staff/);
  let r = s.call(s.staff, 'press.write', { title: 'Creator spotlight: ann', kind: 'spotlight', creator: ann.userId, games: [game.id],
    text: 'ann makes cosy games.\n\n## Snow Fort\nA game about snow forts.' });
  assert.ok(r.ok, r.error);
  const post = r.post;
  assert.match(post.id, /^creator-spotlight-ann-[0-9a-f]{6}$/);
  assert.equal(post.summary, 'ann makes cosy games.', 'the first paragraph when there is no summary');
  assert.equal(post.games[0].name, 'Snow Fort');
  assert.equal(post.creator.name, 'ann');
  assert.match(ann.notes[0].text, /spotlight/);
  assert.equal(ann.notes[0].kind, 'press');
  s.call(s.staff, 'press.edit', { id: post.id, title: post.title, text: post.blocks.map((b) => b.p || '## ' + b.h).join('\n\n'), kind: 'spotlight', creator: ann.userId, games: [game.id] });
  assert.equal(ann.notes.filter((n) => n.kind === 'press').length, 1, 'told only once');
  assert.match(s.call(s.staff, 'press.write', { title: 'x', text: 'y', games: ['nope'] }).error, /isn't a game/);

  const draft = s.call(s.staff, 'press.write', { title: 'Secret plans', text: 'Soon.', published: false }).post;
  const guest = s.user('c'.repeat(64));
  r = s.call(guest, 'press.list', {});
  assert.deepEqual(r.posts.map((p) => p.title), ['Creator spotlight: ann']);
  assert.match(s.call(guest, 'press.get', { id: draft.id }).error, /isn't here/);
  assert.deepEqual(s.call(s.staff, 'press.list', { drafts: true }).posts.map((p) => p.title), ['Secret plans']);
  r = s.call(s.staff, 'press.edit', { id: draft.id, title: 'Secret plans', text: 'Now.', published: true });
  assert.ok(r.post.published);
  assert.equal(s.call(guest, 'press.list', { kind: 'news' }).posts.length, 1);
  r = s.call(guest, 'press.get', { id: post.id });
  assert.equal(r.post.views, 1);
  assert.equal(r.post.canEdit, false);
  assert.ok(s.call(s.staff, 'press.delete', { id: draft.id }).ok);
});

// ---------- pages for search engines ----------
test('Press posts and events get real pages and are in the sitemap', async () => {
  const { s, ann } = setup();
  const t = now();
  const post = s.call(s.staff, 'press.write', { title: 'The Winter Update', summary: 'Snow, sleds and a jam.', text: 'Snow is here.\n\n## Sleds\nRide them.' }).post;
  const ev = s.call(ann, 'calendar.make', { title: 'Sled Race', text: 'Race sleds.', kind: 'meetup', start: t + D, end: t + D + 2 * H, link: '#/games' }).event;
  const ask = async (req) => s.seoAsk(req);
  assert.ok(SECTIONS.includes('press') && SECTIONS.includes('calendar'));
  let p = await build(ask, '/press/' + post.id);
  assert.equal(p.status, 200);
  assert.match(p.html, /<h1>The Winter Update<\/h1>/);
  assert.match(p.html, /<h2>Sleds<\/h2>/);
  assert.match(p.html, /"@type":"NewsArticle"/);
  p = await build(ask, '/press');
  assert.match(p.html, /The Winter Update/);
  p = await build(ask, '/calendar/' + ev.id);
  assert.equal(p.status, 200);
  assert.match(p.html, /"@type":"Event"/);
  assert.match(p.html, /Sled Race/);
  p = await build(ask, '/calendar');
  assert.match(p.html, /Sled Race/);
  assert.equal(s.press.get(post.id).views || 0, 0, 'search engines don\'t count as views');
  const map = await sitemap(ask);
  assert.match(map, new RegExp('/press/' + post.id));
  assert.match(map, new RegExp('/calendar/' + ev.id));
  assert.equal(appAddress('/press/' + post.id), '/app/#/pressview/' + post.id);
  assert.equal(appAddress('/calendar/' + ev.id), '/app/#/calevent/' + ev.id);
});

// ---------- the website ----------
test('Press is its own product in Community, and the calendar sits in Communities', () => {
  const press = PRODUCTS.find((p) => p.id === 'press');
  assert.equal(press.status, 'open');
  assert.ok(GROUPS.find((g) => g.id === 'community').products.includes('press'));
  assert.equal(productOf('pressview').id, 'press');
  assert.equal(productOf('calendar').id, 'communities');
  assert.equal(activeLink('calevent', '#/calevent/x'), 'calendar');
  const app = readFileSync(new URL('../../website/app/app.js', import.meta.url), 'utf8');
  assert.match(app, /pressPages\(/);
  assert.match(app, /calendarPages\(/);
  assert.match(app, /forum\.pollVote/);
  assert.match(app, /groups\.pollVote/);
});
