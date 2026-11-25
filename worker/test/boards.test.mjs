// G&B Boards (worker/boards.js): making boards from templates, columns and cards, moving
// them, people on a board, who can look, reports, and Boards being its own product.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { makeServer } from './helpers.mjs';
import { boardDue, boardLabels, kColumns, kBoardsEach } from '../boards.js';
import { PRODUCTS, GROUPS, productOf, activeLink } from '../../website/app/products.js';
import { dueText } from '../../website/app/boards.js';

function setup() {
  const s = makeServer();
  return { s, ann: s.signUp('ann'), bob: s.signUp('bob'), cat: s.signUp('cat') };
}
const make = (s, who, extra = {}) => s.call(who, 'board.make', Object.assign({ title: 'My racing game', template: 'game' }, extra));

test('due dates and labels are tidied', () => {
  assert.equal(boardDue('2026-12-31'), '2026-12-31');
  assert.equal(boardDue('2026-02-30'), '', 'not a real day');
  assert.equal(boardDue('tomorrow'), '');
  assert.deepEqual(boardLabels(['red', 'pink', 'red', 'blue']), ['red', 'blue']);
  assert.equal(dueText('2026-11-24', '2026-11-24'), 'Due today');
  assert.equal(dueText('2026-11-27', '2026-11-24'), 'Due in 3 days');
  assert.equal(dueText('2026-11-22', '2026-11-24'), '2 days late');
});

test('making a board: a name is needed, templates give columns and starter cards, private to start', () => {
  const { s, ann } = setup();
  assert.match(make(s, ann, { title: '  ' }).error, /name/);
  assert.match(s.call(s.user('d'.repeat(64)), 'board.make', { title: 'x' }).error, /Sign up/);
  const r = make(s, ann);
  assert.ok(r.ok, r.error);
  assert.equal(r.board.visibility, 'private');
  assert.deepEqual(r.board.lists.map((c) => c.title), ['Ideas', 'To build', 'Testing', 'Done']);
  assert.ok(r.board.lists[0].cards.length >= 1);
  assert.equal(r.board.owner, true);
  const blank = make(s, ann, { template: 'nope' }).board;
  assert.deepEqual(blank.lists.map((c) => c.title), ['To do', 'Doing', 'Done']);
  assert.equal(s.call(ann, 'board.mine', {}).boards.length, 2);
});

test('cards: add, edit (only the bits sent), move between columns, delete', () => {
  const { s, ann } = setup();
  const b = make(s, ann, { template: 'blank' }).board, id = b.id;
  const [todo, doing] = b.lists;
  assert.match(s.call(ann, 'board.card', { id, do: 'add', column: todo.id, title: ' ' }).error, /name/);
  let r = s.call(ann, 'board.card', { id, do: 'add', column: todo.id, title: 'Make the map' });
  assert.ok(r.ok, r.error);
  r = s.call(ann, 'board.card', { id, do: 'add', column: todo.id, title: 'Add cars', to: 0 });
  assert.deepEqual(r.board.lists[0].cards.map((k) => k.title), ['Add cars', 'Make the map'], 'put where it was dropped');
  const card = r.board.lists[0].cards[1].id;
  r = s.call(ann, 'board.card', { id, do: 'edit', card, text: 'Hills and a lake', labels: ['green', 'nope'], due: '2026-12-01',
    checks: [{ text: 'Hills' }, { text: 'Lake', done: true }, { text: '   ' }] });
  const k = r.board.lists[0].cards[1];
  assert.equal(k.title, 'Make the map', 'left-out bits stay');
  assert.equal(k.text, 'Hills and a lake');
  assert.deepEqual(k.labels, ['green']);
  assert.equal(k.due, '2026-12-01');
  assert.deepEqual(k.checks.map((x) => [x.text, x.done]), [['Hills', false], ['Lake', true]], 'empty checklist lines are dropped');
  assert.match(s.call(ann, 'board.card', { id, do: 'edit', card, title: '' }).error, /name/);
  r = s.call(ann, 'board.card', { id, do: 'move', card, column: doing.id });
  assert.equal(r.board.lists[1].cards[0].id, card);
  assert.equal(r.board.lists[0].cards.length, 1);
  r = s.call(ann, 'board.card', { id, do: 'edit', card, done: true });
  assert.equal(r.board.done, 1);
  r = s.call(ann, 'board.card', { id, do: 'delete', card });
  assert.equal(r.board.cards, 1);
  assert.match(s.call(ann, 'board.card', { id, do: 'move', card, column: doing.id }).error, /isn't there/);
});

test('columns: add, rename, move, delete, and a most', () => {
  const { s, ann } = setup();
  const id = make(s, ann, { template: 'blank' }).board.id;
  let r = s.call(ann, 'board.column', { id, do: 'add', title: 'Waiting' });
  assert.deepEqual(r.board.lists.map((c) => c.title), ['To do', 'Doing', 'Done', 'Waiting']);
  const w = r.board.lists[3].id;
  r = s.call(ann, 'board.column', { id, do: 'move', column: w, to: 1 });
  assert.deepEqual(r.board.lists.map((c) => c.title), ['To do', 'Waiting', 'Doing', 'Done']);
  r = s.call(ann, 'board.column', { id, do: 'rename', column: w, title: 'Blocked' });
  assert.equal(r.board.lists[1].title, 'Blocked');
  r = s.call(ann, 'board.column', { id, do: 'delete', column: w });
  assert.equal(r.board.lists.length, 3);
  for (let i = 3; i < kColumns; i++) assert.ok(s.call(ann, 'board.column', { id, do: 'add', title: 'C' + i }).ok);
  assert.match(s.call(ann, 'board.column', { id, do: 'add', title: 'one too many' }).error, /at most/);
});

test('people: the owner adds and removes them; they can change cards and leave; others can\'t', () => {
  const { s, ann, bob, cat } = setup();
  const b = make(s, ann).board, id = b.id, col = b.lists[0].id;
  assert.match(s.call(bob, 'board.get', { id }).error, /private/, 'private boards are only for people on them');
  assert.match(s.call(bob, 'board.member', { id, who: 'cat' }).error, /isn't here/);
  let r = s.call(ann, 'board.member', { id, who: 'bob' });
  assert.ok(r.ok, r.error);
  assert.ok(bob.notes.some((n) => n.kind === 'board' && n.about === id), 'bob is told');
  assert.match(s.call(ann, 'board.member', { id, who: 'bob' }).error, /already/);
  assert.match(s.call(ann, 'board.member', { id, who: 'nobody-here' }).error, /nobody/);
  r = s.call(bob, 'board.card', { id, do: 'add', column: col, title: 'Bob\'s idea' });
  assert.ok(r.ok, 'people on it can add cards');
  const card = r.board.lists[0].cards.at(-1).id;
  r = s.call(bob, 'board.card', { id, do: 'edit', card, people: [ann.id, cat.id, bob.id] });
  assert.deepEqual(r.board.lists[0].cards.at(-1).people.map((p) => p.name), ['ann', 'bob'], 'only people on the board can be put on a card');
  assert.ok(ann.notes.some((n) => /put you on the card/.test(n.text)), 'ann is told');
  assert.match(s.call(bob, 'board.set', { id, title: 'Bob\'s now' }).error, /owner/);
  assert.match(s.call(bob, 'board.member', { id, who: 'cat' }).error, /owner/);
  assert.match(s.call(bob, 'board.delete', { id }).error, /owner/);
  assert.ok(s.call(bob, 'board.member', { id, who: bob.id, on: false }).ok, 'bob leaves');
  r = s.call(ann, 'board.get', { id });
  assert.equal(r.board.members.length, 0);
  assert.deepEqual(r.board.lists[0].cards.at(-1).people.map((p) => p.name), ['ann'], 'and comes off his cards');
  assert.match(s.call(ann, 'board.member', { id, who: ann.id, on: false }).error, /You own this board/);
});

test('who can look: link and public boards are read-only for others; public ones are listed and can be reported', () => {
  const { s, ann, bob } = setup();
  const id = make(s, ann).board.id;
  const guest = s.user('e'.repeat(64));
  assert.ok(s.call(ann, 'board.set', { id, visibility: 'link', color: 'blue', text: 'Our plan' }).ok);
  let r = s.call(guest, 'board.get', { id });
  assert.ok(r.ok, 'anyone with the link can look');
  assert.equal(r.board.canEdit, false);
  assert.equal(r.board.color, 'blue');
  assert.equal(s.call(guest, 'board.list', {}).boards.length, 0, 'link boards aren\'t listed');
  assert.match(s.call(bob, 'board.card', { id, do: 'add', column: r.board.lists[0].id, title: 'x' }).error, /Only people on this board/);
  s.call(ann, 'board.set', { id, visibility: 'public' });
  assert.equal(s.call(guest, 'board.list', {}).boards.length, 1);
  assert.ok(s.call(bob, 'report.send', { kind: 'board', id, reason: 'spam' }).ok);
  assert.match(s.call(ann, 'report.send', { kind: 'board', id, reason: 'spam' }).error, /yourself/);
  assert.ok(s.call(s.staff, 'board.delete', { id }).ok, 'staff can delete one');
  assert.match(s.call(guest, 'board.get', { id }).error, /isn't here/);
});

test('a most boards each', () => {
  const { s, ann } = setup();
  for (let i = 0; i < kBoardsEach; i++) assert.ok(make(s, ann, { template: 'blank' }).ok);
  assert.match(make(s, ann).error, /Delete an old one/);
});

test('Boards is its own product on the site, in the Productivity group', () => {
  const p = PRODUCTS.find((x) => x.id === 'boards');
  assert.equal(p.status, 'open');
  assert.equal(productOf('board').id, 'boards');
  assert.equal(activeLink('boards', '#/boards?public=1'), 'boards-public');
  assert.equal(activeLink('board', '#/board/bd-1'), 'boards');
  const g = GROUPS.find((x) => x.id === 'productivity');
  assert.deepEqual(g.products, ['boards']);
  assert.ok(!g.later.includes('Boards'));
  const page = readFileSync(new URL('../../website/app/index.html', import.meta.url), 'utf8');
  assert.match(page, /href="boards\.css"/);
});
