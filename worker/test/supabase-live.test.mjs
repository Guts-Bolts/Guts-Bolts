// Live updates: the server pings open pages through Supabase Realtime Broadcast.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp } from './helpers.mjs';
import { broadcastSupabase, supabasePublicConfig } from '../supabase.js';
import { _heard, listen, onLive } from '../../website/app/live.js';

const ENV = {
  SUPABASE_URL: 'https://pfpzmmsosyetkcoymoct.supabase.co',
  SUPABASE_SECRET_KEY: 'sb_secret_test_only',
  SUPABASE_PUBLISHABLE_KEY: 'sb_publishable_test_only_key',
};

function withFetch(mock, run) {
  const original = globalThis.fetch;
  globalThis.fetch = mock;
  return Promise.resolve().then(run).finally(() => { globalThis.fetch = original; });
}

test('broadcasts go to Realtime with the server key, empty payloads and only known topics', async () => {
  let sent = null;
  await withFetch(async (input, init) => {
    assert.equal(String(input), 'https://pfpzmmsosyetkcoymoct.supabase.co/realtime/v1/api/broadcast');
    assert.equal(init.headers.apikey, ENV.SUPABASE_SECRET_KEY);
    assert.equal(init.redirect, 'manual');
    sent = JSON.parse(init.body);
    return new Response(null, { status: 202 });
  }, () => broadcastSupabase(ENV, [
    { topic: 'gb-activity', event: 'auctions' },
    { topic: 'gb-me-' + 'ab'.repeat(16), event: 'me' },
    { topic: 'someone-elses-channel', event: 'me' },
    { topic: 'gb-activity', event: 'Not Allowed!' },
  ]));
  assert.deepEqual(sent.messages.map((m) => m.topic + '|' + m.event), ['gb-activity|auctions', 'gb-me-' + 'ab'.repeat(16) + '|me']);
  assert(sent.messages.every((m) => Object.keys(m.payload).length === 0 && m.private === false));
});

test('the public live config only hands out a publishable key, never the secret one', () => {
  assert.deepEqual(supabasePublicConfig(ENV), { url: ENV.SUPABASE_URL, key: ENV.SUPABASE_PUBLISHABLE_KEY });
  assert.equal(supabasePublicConfig({ ...ENV, SUPABASE_PUBLISHABLE_KEY: 'sb_secret_oops_wrong_key' }), null);
  assert.equal(supabasePublicConfig({ SUPABASE_URL: ENV.SUPABASE_URL }), null);
});

test('a notification or a message pings only that person\'s own secret topic', async () => {
  const s = makeServer(ENV);
  const a = signUp(s, 'LiveAlice'), b = signUp(s, 'LiveBob');
  const topicA = s.meJson(a).live, topicB = s.meJson(b).live;
  assert.match(topicA, /^gb-me-[a-f0-9]{32}$/);
  assert.notEqual(topicA, topicB);
  assert.equal(s.meJson(a).live, topicA, 'the topic stays the same');
  s.livePings.length = 0;
  s.notify(a, 'friend', 'LiveBob sent you a friend request.', b.id);
  assert.deepEqual(s.livePings, [{ topic: topicA, event: 'me' }]);
  s.livePings.length = 0;
  const r = s.call(a, 'message.send', { to: String(b.userId), subject: 'hi', body: 'hello there' });
  assert.equal(r.ok, true, r.error);
  assert(s.livePings.some((p) => p.topic === topicB));
});

test('without Supabase there are no topics and no pings', () => {
  const s = makeServer();
  const a = signUp(s, 'NoLiveAlice');
  assert.equal(s.meJson(a).live, '');
  s.notify(a, 'friend', 'hi');
  assert.equal(s.livePings.length, 0);
});

test('the website folds a burst of pings into one refresh and ignores unknown topics', () => {
  const heard = [];
  onLive((topic, event) => heard.push(topic + '|' + event));
  listen('gb-me-test');
  const ping = (topic, event) => JSON.stringify({ topic: 'realtime:' + topic, event: 'broadcast', payload: { type: 'broadcast', event, payload: {} } });
  _heard(ping('gb-me-test', 'me'));
  _heard(ping('gb-me-test', 'me'));
  _heard(ping('gb-me-other', 'me'));
  _heard('not json');
  assert.deepEqual(heard, ['gb-me-test|me']);
});
