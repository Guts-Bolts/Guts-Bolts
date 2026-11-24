// "Press Play Anywhere": a shared video link plays right in Discord, Telegram, Slack and X.
// The Share button gives /videos/<id> (chat apps never see what's after "#"), that page has the
// chat-app video tags (og:video...), and /embed/<id> is just the player, which other sites may frame.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { makeServer } from './helpers.mjs';
import { build, videoTags, isSeoPath, appAddress, handleSeo } from '../seo.js';
import { withSecurityHeaders } from '../security.js';

const read = (p) => readFileSync(new URL('../../' + p, import.meta.url), 'utf8');
const meta = (html, key) => (new RegExp('<meta (?:property|name)="' + key.replace(/[:.]/g, '\\$&') + '" content="([^"]*)">').exec(html) || [])[1];

function setup(extra = {}) {
  const s = makeServer();
  const ann = s.signUp('ann');
  s.videoInit();
  const v = { id: 'vid-abc123', title: 'Stronger <live>', text: 'A music video', cat: 'music', tags: [], secs: 312, at: 1790000000,
    by: ann.id, likes: [], views: 3, comments: [], state: 'ready', thumb: 1, key: 'k', w: 854, h: 480, type: 'mp4', ...extra };
  s.videos.set(v.id, v);
  return { s, v, ask: async (req) => s.seoAsk(req) };
}

test('a video page has everything Discord needs to play it in the chat', async () => {
  const { s, v, ask } = setup();
  const r = await build(ask, '/videos/' + v.id);
  assert.equal(r.status, 200);
  const h = r.html;
  assert.equal(meta(h, 'og:type'), 'video.other');
  assert.equal(meta(h, 'og:video'), 'https://gutsandbolts.net/vid/vid-abc123');
  assert.equal(meta(h, 'og:video:secure_url'), 'https://gutsandbolts.net/vid/vid-abc123');
  assert.equal(meta(h, 'og:video:type'), 'video/mp4');
  assert.equal(meta(h, 'og:video:width'), '854');
  assert.equal(meta(h, 'og:video:height'), '480');
  assert.match(meta(h, 'og:image'), /^https:\/\/gutsandbolts\.net\/vidthumb\/vid-abc123/);
  assert.equal(meta(h, 'twitter:card'), 'player');
  assert.equal(meta(h, 'twitter:player'), 'https://gutsandbolts.net/embed/vid-abc123');
  assert.equal(meta(h, 'twitter:player:stream:content_type'), 'video/mp4');
  assert.equal((h.match(/name="twitter:card"/g) || []).length, 1, 'one card type');
  // People who click the link get G&B's player (and a normal video if scripts are off).
  assert.match(h, /<video class="media" data-gbp controls[^>]* src="\/vid\/vid-abc123"/);
  assert.match(h, /<script type="module" src="\/app\/player\.js"><\/script>/);
  assert.equal(s.videos.get(v.id).views, 3, 'a chat app reading the page isn\'t a view');
});

test('webm plays too; .mov (which chat apps can\'t play) just gets the big picture card', () => {
  assert.match(videoTags({ src: '/vid/a', type: 'webm', w: 0, h: 0, embed: '/embed/a' }), /og:video:type" content="video\/webm"[\s\S]*og:video:width" content="1280"/);
  const mov = videoTags({ src: '/vid/a', type: 'mov', w: 640, h: 360, embed: '/embed/a' });
  assert.ok(!mov.includes('og:video'), mov);
  assert.match(mov, /twitter:player" content="https:\/\/gutsandbolts\.net\/embed\/a"/);
  assert.match(videoTags({ src: '/vid/a', type: '', embed: '/embed/a' }), /video\/mp4/, 'older videos without a saved type are mp4');
});

test('/embed/<id> is just the player, and other sites may put it in a frame', async () => {
  const { v, ask } = setup();
  assert.ok(isSeoPath('/embed/vid-abc123'));
  assert.equal(appAddress('/embed/vid-abc123'), '/app/#/watch/vid-abc123');
  const r = await build(ask, '/embed/' + v.id);
  assert.equal(r.status, 200);
  assert.match(r.html, /<meta name="robots" content="noindex">/);
  assert.match(r.html, /<link rel="canonical" href="https:\/\/gutsandbolts\.net\/videos\/vid-abc123">/);
  assert.match(r.html, /<video class="full" data-gbp[^>]* src="\/vid\/vid-abc123"/);
  assert.ok(r.html.includes('Stronger &lt;live&gt;') && !r.html.includes('<live>'), 'titles are escaped');
  assert.ok(!r.html.includes('<header'), 'no site menu around it');
  const url = 'https://gutsandbolts.net/embed/' + v.id;
  const res = withSecurityHeaders(await handleSeo(new Request(url), {}, new URL(url), ask), new Request(url));
  assert.equal(res.headers.get('x-frame-options'), null);
  assert.match(res.headers.get('content-security-policy'), /frame-ancestors \*/);
  // Every other page still can't be framed.
  const other = withSecurityHeaders(new Response('<p>hi</p>', { headers: { 'content-type': 'text/html' } }), new Request('https://gutsandbolts.net/videos/' + v.id));
  assert.equal(other.headers.get('x-frame-options'), 'DENY');
  assert.match(other.headers.get('content-security-policy'), /frame-ancestors 'none'/);
});

test('hidden videos have no embed; a computer server sends /embed to the app', async () => {
  const { v, ask } = setup({ review: true });
  assert.equal((await build(ask, '/embed/' + v.id)).status, 404);
  assert.equal((await build(ask, '/embed/nope')).status, 404);
  const url = new URL('https://gutsandbolts.net/embed/' + v.id);
  const moved = await handleSeo(new Request(url), {}, url, null);
  assert.equal(moved.headers.get('location'), 'https://gutsandbolts.net/app/#/watch/' + v.id);
});

test('Share copies the /videos/ address, and /embed runs the worker first', () => {
  const app = read('website/app/videos.js');
  assert.match(app, /location\.origin \+ '\/videos\/' \+ encodeURIComponent\(id\)/);
  assert.ok(!/'\/app\/#\/watch\/' \+ encodeURIComponent\(id\)/.test(app), 'no more #/watch/ share links');
  assert.ok(read('wrangler.jsonc').includes('"/embed/*"'));
  assert.match(read('website/app/player.js'), /gbPlayer\(video/);
});
