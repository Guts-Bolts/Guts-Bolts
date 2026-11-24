// "Googleable" (worker/seo.js): real pages for search engines at /games/<id>, /videos/<id>... with
// titles, descriptions, canonical links, share cards, JSON-LD and alt text; the sitemap that lists
// them; "quiet" looks that don't count views; noindex for unlisted pastes and private pages; and
// the tidy-up of the fixed pages' <head>.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { makeServer } from './helpers.mjs';
import { build, sitemap, blurb, duration, esc, isSeoPath, appAddress, handleSeo, withSeoHead, SEO_OPS, HOME_DATA } from '../seo.js';

const read = (p) => readFileSync(new URL('../../' + p, import.meta.url), 'utf8');
const PNG = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==';
const LESSONS = [{ title: 'Your first script', text: 'print("hi")' }, { title: 'Variables', text: 'local x = 5' }];

function setup() {
  const s = makeServer();
  const ann = s.signUp('ann'), bob = s.signUp('bob');
  const ask = async (req) => s.seoAsk(req);
  return { s, ann, bob, ask };
}
// Every JSON-LD block on a page, read back.
const jsonLd = (html) => [...html.matchAll(/<script type="application\/ld\+json">(.*?)<\/script>/g)].map((m) => JSON.parse(m[1]));
const imgs = (html) => [...html.matchAll(/<img\b[^>]*>/g)].map((m) => m[0]);

test('small helpers: descriptions are one tidy line, video lengths in ISO 8601, text escaped', () => {
  assert.equal(blurb('  Hello\n\n world  '), 'Hello world');
  const long = blurb('word '.repeat(80));
  assert.ok(long.length <= 160 && long.endsWith('…'), long);
  assert.equal(duration(200), 'PT3M20S');
  assert.equal(duration(3725), 'PT1H2M5S');
  assert.equal(esc('<b>"x"</b>'), '&lt;b&gt;&quot;x&quot;&lt;/b&gt;');
  assert.ok(isSeoPath('/games/1001') && isSeoPath('/videos') && isSeoPath('/sitemap.xml'));
  assert.ok(!isSeoPath('/app/') && !isSeoPath('/games/1/2') && !isSeoPath('/api'));
  assert.equal(appAddress('/games/1001'), '/app/#/game/1001');
  assert.equal(appAddress('/videos/v-1'), '/app/#/watch/v-1');
  assert.equal(appAddress('/art'), '/app/#/art');
});

test('a game has its own page: title, description, canonical, share card, VideoGame data, alt text, one h1', async () => {
  const { s, ann, ask } = setup();
  const g = s.addGame(ann, 'Glass Lagoon <Obby>', { description: 'Swim through see-through water.\nMind the sharks!', thumb: 5, genres: ['Obby'], plays: 42 });
  const r = await build(ask, '/games/' + g.id);
  assert.equal(r.status, 200);
  const h = r.html;
  assert.match(h, /<title>Glass Lagoon &lt;Obby&gt; \| Guts&amp;Bolts<\/title>/);
  assert.match(h, /<meta name="description" content="Swim through see-through water\. Mind the sharks!">/);
  assert.match(h, new RegExp('<link rel="canonical" href="https://gutsandbolts\\.net/games/' + g.id + '">'));
  assert.match(h, /<meta property="og:image" content="https:\/\/gutsandbolts\.net\/thumb\//);
  assert.match(h, /<meta name="twitter:card" content="summary_large_image">/);
  assert.equal((h.match(/<h1>/g) || []).length, 1, 'one main heading');
  for (const i of imgs(h)) assert.match(i, /alt="[^"]+"/, 'every picture says what it is: ' + i);
  assert.match(h, new RegExp('href="/app/#/game/' + g.id + '"'), 'a button opens it in the app');
  const data = jsonLd(h);
  const game = data.find((d) => d['@type'] === 'VideoGame');
  assert.equal(game.name, 'Glass Lagoon <Obby>');
  assert.equal(game.author.name, 'ann');
  assert.deepEqual(game.genre, ['Obby']);
  assert.equal(game.interactionStatistic.userInteractionCount, 42);
  assert.ok(data.find((d) => d['@type'] === 'BreadcrumbList'));
  assert.ok(!h.includes('<script>') && !/<\/script>[^\n]*<script>/.test(h), 'no runnable scripts');
});

test('private games, games under review and things that aren\'t there are 404s; an item at /games moves', async () => {
  const { s, ann, ask } = setup();
  const hidden = s.addGame(ann, 'Secret', { access: 'private' });
  const checking = s.addGame(ann, 'Pending', { review: true });
  assert.equal((await build(ask, '/games/' + hidden.id)).status, 404);
  assert.equal((await build(ask, '/games/' + checking.id)).status, 404);
  assert.equal((await build(ask, '/games/nope')).status, 404);
  assert.match((await build(ask, '/games/nope')).html, /<meta name="robots" content="noindex">/);
  const hat = s.addGame(ann, 'Red Cap', { kind: 'hat', thumb: 1 });
  assert.deepEqual(await build(ask, '/games/' + hat.id), { moved: '/items/' + hat.id });
  const item = await build(ask, '/items/' + hat.id);
  assert.equal(item.status, 200);
  assert.match(item.html, /href="\/app\/#\/item\//);
});

test('videos: a page you can watch right there, with VideoObject data, and reading it isn\'t a view', async () => {
  const { s, ann, ask } = setup();
  s.videoInit();
  const v = { id: 'v-abc123', title: 'Speedrun', text: 'World record attempt', cat: 'gaming', tags: ['speedrun'], secs: 125, at: 1790000000,
    by: ann.id, likes: [], views: 7, comments: [], state: 'ready', thumb: 1, key: 'k', w: 1280, h: 720 };
  s.videos.set(v.id, v);
  const r = await build(ask, '/videos/' + v.id);
  assert.equal(r.status, 200);
  assert.match(r.html, /<video class="media"[^>]* controls[^>]* src="\/vid\/v-abc123"/);
  const vo = jsonLd(r.html).find((d) => d['@type'] === 'VideoObject');
  assert.equal(vo.duration, 'PT2M5S');
  assert.equal(vo.uploadDate, new Date(1790000000 * 1000).toISOString());
  assert.equal(vo.contentUrl, 'https://gutsandbolts.net/vid/v-abc123');
  assert.match(vo.thumbnailUrl[0], /^https:\/\/gutsandbolts\.net\/vidthumb\//);
  assert.equal(s.videos.get(v.id).views, 7, 'a search engine reading the page doesn\'t count as a view');
  // (the app still counts real views)
  s.call(s.user('d'.repeat(64)), 'video.get', { id: v.id });
  assert.equal(s.videos.get(v.id).views, 8);
  v.review = true;
  assert.equal((await build(ask, '/videos/' + v.id)).status, 404, 'videos being checked stay hidden');
  const list = await build(ask, '/videos');
  assert.equal(list.status, 200);
  assert.ok(jsonLd(list.html).find((d) => d['@type'] === 'CollectionPage'));
});

test('art, courses, groups and profiles get pages; unlisted pastes are noindex', async () => {
  const { s, ann, bob, ask } = setup();
  const art = s.call(ann, 'art.post', { title: 'Robot at sunset', cat: 'drawing', data: PNG });
  assert.ok(art.ok, art.error);
  if (art.piece.review) s.call(s.staff, 'art.review', { id: art.piece.id, ok: true });
  const a = await build(ask, '/art/' + art.piece.id);
  assert.equal(a.status, 200);
  assert.match(a.html, /<img src="\/artpic\/[^"]+" alt="Robot at sunset">/);
  assert.equal(s.art.get(art.piece.id).views || 0, 0);

  const c = s.call(ann, 'academy.save', { title: 'Lua for beginners', summary: 'Learn to script', topic: 'scripting', level: 'beginner', lessons: LESSONS }).course;
  assert.equal((await build(ask, '/academy/' + c.id)).status, 404, 'drafts stay hidden');
  s.call(ann, 'academy.publish', { id: c.id, on: true });
  const course = await build(ask, '/academy/' + c.id);
  assert.equal(course.status, 200);
  const data = jsonLd(course.html).find((d) => d['@type'] === 'Course');
  assert.equal(data.provider.name, 'G&B Academy');
  assert.match(course.html, /Your first script/);

  const pub = s.call(bob, 'paste.make', { title: 'Door script', text: 'local door = workspace.Door', lang: 'lua' }).paste;
  const unl = s.call(bob, 'paste.make', { title: 'Secret', text: 'shh', lang: 'text', visibility: 'unlisted' }).paste;
  const pp = await build(ask, '/paste/' + pub.id);
  assert.match(pp.html, /<meta name="robots" content="index/);
  assert.match(pp.html, /local door = workspace\.Door/);
  assert.match((await build(ask, '/paste/' + unl.id)).html, /<meta name="robots" content="noindex">/);
  assert.equal(s.pastes.get(pub.id).views || 0, 0);

  const prof = await build(ask, '/users/' + ann.userId);
  assert.equal(prof.status, 200);
  assert.equal(jsonLd(prof.html).find((d) => d['@type'] === 'ProfilePage').mainEntity.name, 'ann');
  assert.equal((await build(ask, '/users/999999')).status, 404);
  assert.equal((await build(ask, '/users/abc')).status, 404);
});

test('the guest asker only runs read-only ops', () => {
  const { s, ann } = setup();
  assert.match(s.seoAsk({ op: 'paste.make', args: { text: 'x' } }).error, /Not a page op/);
  assert.match(s.seoAsk({ op: 'account.login', args: {} }).error, /Not a page op/);
  for (const op of SEO_OPS) assert.ok(/^(list|profile)$|\.(get|list|info)$/.test(op), op);
  const before = s.users.size;
  s.addGame(ann, 'A');
  s.seoAsk({ op: 'list', args: { kind: 'game' } });
  assert.equal(s.users.size, before, 'no guest account is made');
});

test('the sitemap lists the fixed pages, the sections and every public thing', async () => {
  const { s, ann, bob, ask } = setup();
  const g = s.addGame(ann, 'Glass Lagoon');
  s.addGame(ann, 'Secret', { access: 'private' });
  const pub = s.call(bob, 'paste.make', { title: 'p', text: 'x', lang: 'lua' }).paste;
  const unl = s.call(bob, 'paste.make', { title: 'u', text: 'y', lang: 'lua', visibility: 'unlisted' }).paste;
  const map = await sitemap(ask);
  assert.match(map, /^<\?xml version="1\.0" encoding="UTF-8"\?>/);
  for (const p of ['/', '/app/', '/privacy', '/games', '/videos', '/art', '/paste', '/academy', '/groups', '/products/Sysfix', '/games/' + g.id, '/paste/' + pub.id])
    assert.ok(map.includes('<loc>https://gutsandbolts.net' + p + '</loc>'), p);
  assert.ok(!map.includes(unl.id), 'unlisted pastes stay out');
  assert.equal((map.match(/Secret/g) || []).length, 0);
  assert.equal(map.split('<url>').length - 1, new Set([...map.matchAll(/<loc>(.*?)<\/loc>/g)].map((m) => m[1])).size, 'no address twice');
});

test('the page handler: GET only, a computer server just opens the app, and pages are cached', async () => {
  const { s, ann, ask } = setup();
  const g = s.addGame(ann, 'Cached');
  const env = {};
  const url = (p) => new URL('https://gutsandbolts.net' + p);
  assert.equal(await handleSeo(new Request(url('/app/')), env, url('/app/'), ask), null);
  assert.equal((await handleSeo(new Request(url('/games'), { method: 'POST' }), env, url('/games'), ask)).status, 405);
  const moved = await handleSeo(new Request(url('/games/' + g.id)), env, url('/games/' + g.id), null);
  assert.equal(moved.status, 302);
  assert.equal(moved.headers.get('location'), 'https://gutsandbolts.net/app/#/game/' + g.id);
  const res = await handleSeo(new Request(url('/games/' + g.id)), env, url('/games/' + g.id), ask);
  assert.equal(res.status, 200);
  assert.match(res.headers.get('content-type'), /text\/html/);
  assert.match(res.headers.get('cache-control'), /max-age=600/);
  const map = await handleSeo(new Request(url('/sitemap.xml')), env, url('/sitemap.xml'), ask);
  assert.match(map.headers.get('content-type'), /application\/xml/);
  assert.match(await map.text(), /\/games\//);
  // The Search Console tag goes on every page once it's set.
  const tagged = await build(ask, '/games/' + g.id, { GOOGLE_SITE_VERIFICATION: 'abc_123' });
  assert.match(tagged.html, /<meta name="google-site-verification" content="abc_123">/);
});

test('private pages say noindex; the home page names the site; the wiring is in place', () => {
  const res = withSeoHead(new Response('<html><head></head></html>', { headers: { 'content-type': 'text/html' } }), new URL('https://gutsandbolts.net/checkout'));
  assert.equal(res.headers.get('X-Robots-Tag'), 'noindex, nofollow');
  const plain = new Response('{}', { headers: { 'content-type': 'application/json' } });
  assert.equal(withSeoHead(plain, new URL('https://gutsandbolts.net/')), plain);
  assert.deepEqual(HOME_DATA.map((d) => d['@type']), ['WebSite', 'Organization', 'SoftwareApplication']);
  const wrangler = read('wrangler.jsonc');
  for (const p of ['"/games"', '"/games/*"', '"/videos/*"', '"/art/*"', '"/paste/*"', '"/academy/*"', '"/groups/*"', '"/users/*"', '"/items/*"', '"/sitemap.xml"'])
    assert.ok(wrangler.includes(p), p + ' runs the worker first');
  const index = read('worker/index.js');
  assert.match(index, /handleSeo\(request, env, url/);
  assert.match(index, /withSeoHead\(withAdsMeta/);
  // Both servers know "quiet" (no view counted).
  for (const f of ['src/server/ServerVideo.cpp', 'src/server/ServerArt.cpp', 'src/server/ServerPaste.cpp']) assert.match(read(f), /"quiet"/, f);
  for (const f of ['worker/videos.js', 'worker/art.js', 'worker/paste.js']) assert.match(read(f), /args\.quiet !== true/, f);
  assert.match(read('docs/google-search.md'), /Search Console/);
});

test('pictures in the web app say what they show', () => {
  const app = read('website/app/app.js');
  assert.match(app, /class="game-icon" src="[^"]*" alt="\$\{\(g\.name \|\| 'Game'\) \+ ' icon'\}"/);
  assert.match(read('website/app/videos.js'), /src="\$\{v\.poster\}" alt="\$\{v\.title\}"/);
  assert.match(read('website/app/streams.js'), /src="\$\{s\.poster\}" alt="\$\{s\.title\}"/);
  for (const f of ['website/index.html', 'website/products.html']) for (const i of read(f).match(/<img\b[^>]*>/g) || []) assert.match(i, /\balt=/, f + ': ' + i);
});
