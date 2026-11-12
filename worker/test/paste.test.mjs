// G&B Paste (worker/paste.js): making pastes, the public list and unlisted links, running
// out, deleting, reports, the /pasteraw address, and Paste being its own product on the site.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { makeServer } from './helpers.mjs';
import { pasteText, kMostBytes, kPerDay } from '../paste.js';
import { PRODUCTS, GROUPS, productOf, activeLink } from '../../website/app/products.js';

function setup() {
  const s = makeServer();
  return { s, ann: s.signUp('ann'), bob: s.signUp('bob') };
}
const make = (s, who, extra = {}) => s.call(who, 'paste.make', Object.assign({ title: 'My script', text: 'local x = 1\nprint(x)', lang: 'lua' }, extra));

test('the text is kept as typed, only tidied', () => {
  assert.equal(pasteText('a\r\nb\rc\t\u0007d  \n\n\n'), 'a\nb\nc\td');
  assert.equal(pasteText('  if x then\n    y()\n  end'), '  if x then\n    y()\n  end', 'spaces at the start stay');
  assert.equal(pasteText('<script>alert(1)</script> damn'), '<script>alert(1)</script> damn', 'no word filter: code and logs stay whole');
  assert.equal(pasteText('x'.repeat(kMostBytes)).length, kMostBytes);
  assert.equal(pasteText('x'.repeat(kMostBytes + 1)), null, 'too big');
});

test('making a paste: some text needed, guests can\'t, odd choices are tidied', () => {
  const { s, ann } = setup();
  assert.match(make(s, ann, { text: '  \n ' }).error, /Paste something/);
  assert.match(make(s, ann, { text: 'x'.repeat(kMostBytes + 1) }).error, /too long/);
  assert.match(make(s, ann, { expires: 5 }).error, /runs out/);
  assert.match(s.call(s.user('c'.repeat(64)), 'paste.make', { text: 'hi' }).error, /Sign up/);
  const r = make(s, ann, { title: '', lang: 'klingon', visibility: 'secret' });
  assert.ok(r.ok, r.error);
  assert.equal(r.paste.title, 'Untitled');
  assert.equal(r.paste.lang, 'text');
  assert.equal(r.paste.visibility, 'public');
  assert.match(r.paste.id, /^[0-9a-f]{12}$/, 'long random ids, so unlisted links can\'t be guessed');
  assert.equal(r.paste.lines, 2);
});

test('the list: public ones only, newest first, by language and person, without the text', () => {
  const { s, ann, bob } = setup();
  make(s, ann, { title: 'Hidden', visibility: 'unlisted' });
  const a = make(s, ann, { lang: 'python' }).paste.id;
  const guest = s.user('d'.repeat(64));
  const r = s.call(guest, 'paste.list', {});
  assert.ok(r.ok, r.error);
  assert.deepEqual(r.pastes.map((p) => p.id), [a], 'guests can look; unlisted ones aren\'t there');
  assert.equal(r.pastes[0].text, undefined);
  assert.equal(s.call(bob, 'paste.list', { lang: 'lua' }).pastes.length, 0);
  assert.equal(s.call(bob, 'paste.list', { by: 'ann' }).pastes.length, 1);
  assert.equal(s.call(ann, 'paste.mine', {}).pastes.length, 2, 'your own list has the unlisted one too');
});

test('opening a paste counts a view (not your own); only the maker or staff delete', () => {
  const { s, ann, bob } = setup();
  const id = make(s, ann, { visibility: 'unlisted' }).paste.id;
  assert.equal(s.call(ann, 'paste.get', { id }).paste.views, 0);
  const r = s.call(bob, 'paste.get', { id });
  assert.equal(r.paste.text, 'local x = 1\nprint(x)');
  assert.equal(r.paste.views, 1);
  assert.equal(r.paste.canDelete, false);
  assert.match(s.call(bob, 'paste.delete', { id }).error, /Only the person who made it/);
  assert.ok(s.call(s.staff, 'paste.delete', { id }).ok, 'staff can');
  assert.match(s.call(bob, 'paste.get', { id }).error, /isn't here/);
});

test('pastes run out', () => {
  const { s, ann } = setup();
  const r = make(s, ann, { expires: 1 });
  assert.ok(r.paste.until > Date.now() / 1000);
  s.pastes.get(r.paste.id).until = Math.floor(Date.now() / 1000) - 5;   // (an hour later)
  assert.match(s.call(ann, 'paste.get', { id: r.paste.id }).error, /run out/);
  assert.equal(s.pastes.has(r.paste.id), false, 'and it\'s deleted for good');
});

test('a daily limit', () => {
  const { s, ann } = setup();
  for (let i = 0; i < kPerDay; i++) assert.ok(make(s, ann).ok);
  assert.match(make(s, ann).error, /Come back tomorrow/);
  assert.ok(make(s, s.staff).ok, 'staff have a bigger limit');
});

test('pastes can be reported, but not your own', () => {
  const { s, ann, bob } = setup();
  const id = make(s, ann, { title: 'Free bolts generator' }).paste.id;
  assert.match(s.call(ann, 'report.send', { kind: 'paste', id, reason: 'scam' }).error, /yourself/);
  const r = s.call(bob, 'report.send', { kind: 'paste', id, reason: 'scam' });
  assert.ok(r.ok, r.error);
  const list = s.call(s.staff, 'admin.reports', {});
  if (list.ok) {
    const mine = list.reports.filter((x) => x.kind === 'paste');
    assert.equal(mine[0].name, 'Free bolts generator');
    assert.match(mine[0].copy.body, /local x = 1/);
  }
});

test('/pasteraw serves the text alone, as plain text that never runs', async () => {
  const { s, ann } = setup();
  const id = make(s, ann, { text: '<b>hi</b>' }).paste.id;
  const r = s.pasteRawResponse(id);
  assert.equal(r.status, 200);
  assert.match(r.headers.get('content-type'), /^text\/plain/);
  assert.equal(r.headers.get('x-content-type-options'), 'nosniff');
  assert.match(r.headers.get('content-security-policy'), /sandbox/);
  assert.equal(await r.text(), '<b>hi</b>');
  assert.equal(s.pasteRawResponse('nope').status, 404);
});

test('Paste is its own product on the site, in the Development group', () => {
  const paste = PRODUCTS.find((p) => p.id === 'paste');
  assert.equal(paste.status, 'open');
  assert.equal(productOf('pasteview').id, 'paste');
  assert.equal(activeLink('paste', '#/paste?by=me'), 'paste-mine');
  assert.equal(activeLink('pasteview', '#/pasteview/x'), 'paste');
  const dev = GROUPS.find((g) => g.id === 'development');
  assert.ok(dev.products.includes('paste'));
  assert.ok(!dev.later.includes('Paste'));
  const page = readFileSync(new URL('../../website/app/index.html', import.meta.url), 'utf8');
  assert.match(page, /href="paste\.css"/);
  assert.match(page, /href="#\/paste"/);
  const server = readFileSync(new URL('../server.js', import.meta.url), 'utf8');
  assert.match(server, /op === 'paste\.make'/, 'Fyxa looks at new pastes');
  const site = readFileSync(new URL('../index.js', import.meta.url), 'utf8');
  assert.match(site, /pasteraw/);
});
