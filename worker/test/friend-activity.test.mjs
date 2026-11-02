// Friend activity: what friends do (playing, making, badges) goes to Supabase, and My Feed
// shows it to their friends and followers, checking privacy and blocks when it's read.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp } from './helpers.mjs';

const ENV = { SUPABASE_URL: 'https://pfpzmmsosyetkcoymoct.supabase.co', SUPABASE_SECRET_KEY: 'sb_secret_test_only' };

function withFetch(mock, run) {
  const original = globalThis.fetch;
  globalThis.fetch = mock;
  return Promise.resolve().then(run).finally(() => { globalThis.fetch = original; });
}
const activity = (s) => [...s.sql.exec("SELECT payload FROM supabase_outbox WHERE table_name = 'gb_friend_activity'")].map((r) => JSON.parse(r.payload));
const befriend = (s, a, b) => { a.friends.push(b.id); b.friends.push(a.id); s.saveUser(a, b); };

test('playing a public game is recorded once an hour; private games and guests are not', () => {
  const s = makeServer(ENV);
  const maker = signUp(s, 'FeedMaker'), fan = signUp(s, 'FeedFan');
  const game = s.addGame(maker, 'Tag');
  const hidden = s.addGame(maker, 'Secret Tag', { access: 'private' });
  for (const g of [game, hidden]) s.writeFile(g.id, new TextEncoder().encode('{"nodes":[]}'));
  s.call(fan, 'get', { id: game.id });
  s.call(fan, 'get', { id: game.id });
  s.call(maker, 'get', { id: hidden.id });
  const rows = activity(s);
  assert.equal(rows.length, 1);
  assert.equal(rows[0].kind, 'playing');
  assert.equal(rows[0].account_id, fan.id);
  assert.equal(rows[0].about_id, game.id);
});

test('My Feed mixes posts with activity, and leaves out people who hide it or are blocked', async () => {
  const s = makeServer(ENV);
  const me = signUp(s, 'FeedReader'), open = signUp(s, 'OpenFriend'), shy = signUp(s, 'ShyFriend');
  const game = s.addGame(open, 'Racing');
  befriend(s, me, open); befriend(s, me, shy);
  shy.privacy = { status: 'nobody' }; s.saveUser(shy);
  open.posts = [{ text: 'hello all', at: 100 }];
  const first = s.call(me, 'feed.list', {});
  assert.deepEqual(new Set(first.supabase.feed), new Set([open.id, shy.id]));
  Object.defineProperty(first, 'asker', { value: me, enumerable: false });
  const t = new Date().toISOString();
  await withFetch(async (input) => {
    const url = new URL(input);
    assert.equal(url.pathname, '/rest/v1/gb_friend_activity');
    assert.match(url.searchParams.get('account_id'), /^in\./);
    return Response.json([
      { account_id: open.id, kind: 'playing', about_id: game.id, detail: '', created_at: t },
      { account_id: shy.id, kind: 'playing', about_id: game.id, detail: '', created_at: t },
      { account_id: open.id, kind: 'created', about_id: 'deleted-thing', detail: '', created_at: t },
    ]);
  }, async () => {
    const r = await s.supabaseFollowUp(first);
    const lines = r.feed.map((p) => p.user.name + ':' + (p.activity ? p.activity.kind + ' ' + p.activity.name : p.text));
    assert.deepEqual(lines, ['OpenFriend:playing Racing', 'OpenFriend:hello all']);
  });
});

test('a new public upload and a badge are recorded for friends to see', () => {
  const s = makeServer(ENV);
  const maker = signUp(s, 'BadgeMaker');
  const game = s.addGame(maker, 'Badge Game');
  s.recordFriendActivity(maker, 'badge', game, 'Winner');
  s.recordFriendActivity(maker, 'created', game);
  assert.deepEqual(activity(s).map((r) => r.kind + ':' + r.detail), ['badge:Winner', 'created:']);
});
