// Search engines (Google, Bing...): pages they can read, and the sitemap that lists them.
//
// The web app (website/app/) is one page that changes with the part after "#" (#/game/12,
// #/watch/v-ab12...). Search engines ignore everything after "#", so to them the whole site
// was one page. These are real addresses with the content already in the HTML:
//   /games         /games/<id>       a game                (opens #/game/<id> in the app)
//   /items/<id>                      a catalog or Library item (#/item/<id> or #/library/<n>)
//   /videos        /videos/<id>      a video, playable right there (#/watch/<id>)
//   /art           /art/<id>         an art piece          (#/artpiece/<id>)
//   /audio         /audio/<id>       a sound or song, playable right there (#/track/<id>)
//   /paste         /paste/<id>       a paste               (#/pasteview/<id>)
//   /academy       /academy/<id>     a course              (#/course/<id>)
//   /groups        /groups/<id>      a group               (#/group/<id>)
//   /press         /press/<id>       a Press post: news, creator spotlights (#/pressview/<id>)
//   /calendar      /calendar/<id>    an event on the calendar: jams, contests, shows (#/calevent/<id>)
//   /users/<number>                  someone's profile     (#/user/<number>)
//   /embed/<id>                      just the video player, for other sites to put in a frame
//   /sitemap.xml                     every page above, plus the fixed pages in legal.js
// Each page has its own title and description, a canonical address, share cards (Open Graph and
// Twitter), structured data (schema.org JSON-LD), alt text on every picture, one <h1>, and a
// big button that opens the same thing in the web app.
//
// The pages only show what a visitor who isn't signed in could already see: they ask the server
// (seoAsk below, on the Durable Object) as a guest who never signed up, with the same read-only ops
// the app uses, and nothing else (SEO_OPS). "quiet: true" on video.get, art.get and paste.get
// means a search engine reading the page doesn't count as a view (both servers know it).
// Unlisted pastes get "noindex" and stay out of the sitemap.
//
// withSeoHead() also tidies the fixed pages (home, the app, products, legal): it adds the
// canonical address, share-card tags, the Google Search Console tag (GOOGLE_SITE_VERIFICATION),
// and the site's name and logo as structured data on the home page. See docs/google-search.md.

import { PUBLIC_PAGES, LEGAL_PAGES, LEGAL_UPDATED } from './legal.js';

export const SITE = 'https://gutsandbolts.net';
const NAME = 'Guts&Bolts';
const LOGO = SITE + '/img/logo.png';
const DEFAULT_PIC = SITE + '/img/engine-boom.jpg';
// The sections that have a list page, in the order the menu shows them.
export const SECTIONS = ['games', 'videos', 'audio', 'art', 'paste', 'academy', 'groups', 'press', 'calendar'];
const SECTION_NAMES = { games: 'Games', videos: 'Videos', audio: 'Audio', art: 'Art', paste: 'Paste', academy: 'Academy', groups: 'Groups', press: 'Press', calendar: 'Calendar' };
const SECTION_BLURBS = {
  games: 'Free games made by the Guts&Bolts community: obbies, shooters, horror, roleplay, tycoons and more. Play them on PC, Mac, Linux, Android, VR or in your browser.',
  videos: 'Videos from the Guts&Bolts community: gameplay, Studio tutorials, showcases and more.',
  art: 'Drawings, renders and pixel art from the Guts&Bolts community.',
  audio: 'Free music, loops and sound effects from the Guts&Bolts community. Listen here, or put them in your own games.',
  paste: 'Code and text shared by the Guts&Bolts community: Lua scripts, snippets and notes.',
  academy: 'Free courses from G&B Academy: learn Lua scripting, Studio, game design and more, one short lesson at a time.',
  groups: 'Groups on Guts&Bolts: communities of players and game makers.',
  press: 'News from the Guts&Bolts team, creator spotlights on the people making games and art, and stories from behind the scenes.',
  calendar: 'What\'s coming up on Guts&Bolts: game jams, art contests, live shows, meetups and game launches. Free to join.',
};
const PRESS_KIND_NAMES = { news: 'News', spotlight: 'Creator spotlight', devlog: 'Behind the scenes' };   // (press.js PRESS_KINDS)
// "Sat, 6 Dec 2026, 18:00 UTC": search engines and visitors without scripts see times in UTC.
const when = (sec) => new Date(sec * 1000).toUTCString().replace(/:\d\d GMT$/, ' UTC');
// Kinds that live in the Library (Create) rather than the catalog: the app opens them at #/library/<n>.
const LIBRARY_KINDS = new Set(['model', 'audio', 'decal', 'plugin', 'animation']);
const PLATFORMS = ['Windows', 'macOS', 'Linux', 'Android', 'Meta Quest', 'Web browser'];
const PAGE_TTL = 600, SITEMAP_TTL = 3600;   // seconds pages and the sitemap are kept in Cloudflare's cache
const kMostSitemapCalls = 20;   // pages of each list the sitemap reads at most

// ---------- small helpers ----------
export const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
// One line of plain text for a description: no line breaks, at most n letters, cut at a word.
export function blurb(s, n = 160) {
  const t = String(s ?? '').replace(/\s+/g, ' ').trim();
  if (t.length <= n) return t;
  const cut = t.slice(0, n - 1), space = cut.lastIndexOf(' ');
  return (space > n * 0.6 ? cut.slice(0, space) : cut).replace(/[\s.,;:!?-]+$/, '') + '…';
}
const iso = (sec) => (sec > 0 ? new Date(sec * 1000).toISOString() : undefined);
const day = (sec) => (sec > 0 ? new Date(sec * 1000).toISOString().slice(0, 10) : '');
// JSON-LD inside a <script>: "</" can't appear, or it would end the script early.
const ld = (obj) => '<script type="application/ld+json">' + JSON.stringify(obj).replace(/</g, '\\u003c') + '</script>';
const person = (p) => (p && p.userId ? { '@type': 'Person', name: p.name, url: SITE + '/users/' + p.userId } : { '@type': 'Person', name: (p && p.name) || NAME });
const abs = (path) => (!path ? '' : /^https?:/.test(path) ? path : SITE + path);
// ISO 8601 length for a video ("PT3M20S").
export const duration = (secs) => {
  const s = Math.max(0, Math.round(Number(secs) || 0));
  return 'PT' + (s >= 3600 ? Math.floor(s / 3600) + 'H' : '') + (s >= 60 ? Math.floor((s % 3600) / 60) + 'M' : '') + (s % 60) + 'S';
};

// ---------- videos in chat apps ----------
// Discord, Telegram, Slack, iMessage and the rest read these tags to show a video right in the
// chat. Discord plays the file itself (og:video, a plain mp4 or webm address); it only puts other
// sites' players (iframes) in chats for a short list of big sites, so the file is what counts.
// X (Twitter) and some others use the player card instead: twitter:player is /embed/<id>, a page
// with just our player that other sites are allowed to put in a frame (security.js).
// v: { src, type ('mp4' | 'webm' | 'mov' | ''), w, h, embed (path), secs }
const PLAYABLE = { mp4: 'video/mp4', webm: 'video/webm' };   // (what chat apps can play: not .mov)
export function videoTags(v) {
  const file = abs(v.src), mime = PLAYABLE[v.type] || (v.type ? '' : 'video/mp4');   // (older videos didn't save a type: they're mp4)
  const w = v.w > 0 ? Math.round(v.w) : 1280, h = v.h > 0 ? Math.round(v.h) : 720;
  const embed = SITE + v.embed;
  let t = '';
  if (mime) {
    t += '<meta property="og:video" content="' + esc(file) + '">\n' +
      '<meta property="og:video:url" content="' + esc(file) + '">\n' +
      '<meta property="og:video:secure_url" content="' + esc(file) + '">\n' +
      '<meta property="og:video:type" content="' + mime + '">\n' +
      '<meta property="og:video:width" content="' + w + '">\n' +
      '<meta property="og:video:height" content="' + h + '">\n';
    if (v.secs > 0) t += '<meta property="video:duration" content="' + Math.round(v.secs) + '">\n';
  }
  t += '<meta name="twitter:card" content="player">\n' +
    '<meta name="twitter:player" content="' + esc(embed) + '">\n' +
    '<meta name="twitter:player:width" content="' + w + '">\n' +
    '<meta name="twitter:player:height" content="' + h + '">\n' +
    (mime ? '<meta name="twitter:player:stream" content="' + esc(file) + '">\n<meta name="twitter:player:stream:content_type" content="' + mime + '">\n' : '');
  return t;
}
// ---------- the page around the content ----------
// opts: { title, description, path, image, imageAlt, type, body, data: [json-ld...], noindex, crumbs: [[name, path]],
//        video: { src, type, w, h, secs, embed } (chat-app video tags), head: extra <head> html }
export function page(opts, env = {}) {
  const url = SITE + opts.path;
  const title = opts.title ? opts.title + ' | ' + NAME : NAME;
  const image = abs(opts.image) || DEFAULT_PIC;
  const crumbs = [['Home', '/'], ...(opts.crumbs || [])];
  const data = [...(opts.data || []), {
    '@context': 'https://schema.org', '@type': 'BreadcrumbList',
    itemListElement: crumbs.map(([name, p], i) => ({ '@type': 'ListItem', position: i + 1, name, item: SITE + p })),
  }];
  const verify = String(env.GOOGLE_SITE_VERIFICATION || '').trim();
  return '<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n' +
    '<meta name="viewport" content="width=device-width, initial-scale=1">\n' +
    '<title>' + esc(title) + '</title>\n' +
    '<meta name="description" content="' + esc(opts.description) + '">\n' +
    (opts.noindex ? '<meta name="robots" content="noindex">\n' : '<meta name="robots" content="index, follow, max-image-preview:large">\n') +
    '<link rel="canonical" href="' + esc(url) + '">\n' +
    '<meta property="og:site_name" content="' + NAME + '">\n' +
    '<meta property="og:type" content="' + esc(opts.type || 'website') + '">\n' +
    '<meta property="og:title" content="' + esc(opts.title || NAME) + '">\n' +
    '<meta property="og:description" content="' + esc(opts.description) + '">\n' +
    '<meta property="og:url" content="' + esc(url) + '">\n' +
    '<meta property="og:image" content="' + esc(image) + '">\n' +
    '<meta property="og:image:alt" content="' + esc(opts.imageAlt || opts.title || NAME) + '">\n' +
    (opts.video ? videoTags(opts.video) : '<meta name="twitter:card" content="summary_large_image">\n') +
    '<meta name="twitter:title" content="' + esc(opts.title || NAME) + '">\n' +
    '<meta name="twitter:description" content="' + esc(opts.description) + '">\n' +
    '<meta name="twitter:image" content="' + esc(image) + '">\n' +
    (verify ? '<meta name="google-site-verification" content="' + esc(verify) + '">\n' : '') +
    '<link rel="icon" href="/img/icon.png">\n<link rel="apple-touch-icon" href="/img/apple-touch-icon.png">\n' +
    '<link rel="preload" href="/img/logo.png" as="image">\n' +
    '<link rel="stylesheet" href="/fonts/font.css">\n' +
    '<style>' + CSS + '</style>\n' + (opts.head || '') +
    data.map(ld).join('\n') + '\n</head>\n<body>\n' +
    '<header class="top"><div class="wrap"><a class="logo" href="/"><img src="/img/logo.png" alt="Guts &amp; Bolts" width="720" height="242"></a>' +
    '<nav aria-label="Sections">' + SECTIONS.map((s) => '<a href="/' + s + '">' + SECTION_NAMES[s] + '</a>').join('') +
    '<a class="go" href="/app/">Open the site</a></nav></div></header>\n' +
    '<nav class="crumbs wrap" aria-label="Breadcrumb">' + crumbs.map(([name, p], i) =>
      i === crumbs.length - 1 ? '<span aria-current="page">' + esc(name) + '</span>' : '<a href="' + esc(p) + '">' + esc(name) + '</a> › ').join('') + '</nav>\n' +
    '<main class="wrap">' + opts.body + '</main>\n' +
    '<footer class="wrap"><p>' + SECTIONS.map((s) => '<a href="/' + s + '">' + SECTION_NAMES[s] + '</a>').join(' · ') +
    ' · <a href="/products">Products</a> · <a href="/#download">Download</a> · <a href="/privacy">Privacy</a> · <a href="/contact">Contact</a></p>' +
    '<p>Guts&amp;Bolts is a free, independent game platform for adults (18+). Not affiliated with Roblox Corporation.</p></footer>\n' +
    '</body>\n</html>\n';
}

const CSS = `
:root{--red:#e11d2a;--red-dark:#a3141e;--text:#1c1f24;--muted:#5d6470;--bg:#f4f5f7;--panel:#fff;--line:#dfe2e7;--font:var(--gb-font,system-ui,sans-serif)}
@media (prefers-color-scheme:dark){:root{--text:#e9ecf1;--muted:#a1a9b6;--bg:#0f1319;--panel:#181e27;--line:#2c3542}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px/1.55 var(--font)}
a{color:var(--red)}img{max-width:100%;height:auto}
.wrap{width:100%;max-width:1440px;margin:0 auto;padding:0 clamp(16px,3vw,40px)}
.top{background:var(--red);color:#fff}.top .wrap{display:flex;align-items:center;gap:20px;flex-wrap:wrap;padding-top:10px;padding-bottom:10px}
.logo img{height:44px;width:auto;display:block}.top nav{display:flex;gap:6px;flex-wrap:wrap;margin-left:auto}
.top nav a{color:#fff;text-decoration:none;font-weight:600;padding:6px 12px;border-radius:999px}.top nav a:hover{background:rgba(255,255,255,.16)}
.top nav a.go{background:#fff;color:var(--red-dark)}
.crumbs{font-size:14px;color:var(--muted);padding-top:14px;padding-bottom:4px}
main{padding-bottom:32px}h1{font-size:clamp(26px,3.4vw,40px);line-height:1.15;margin:10px 0 8px}h2{font-size:22px;margin:28px 0 10px}
.by{color:var(--muted);margin:0 0 16px}.lead{max-width:80ch;white-space:pre-line}
.hero.solo{grid-template-columns:minmax(0,760px)}.hero.solo>div:first-child{display:none}
.hero{display:grid;grid-template-columns:minmax(0,1.6fr) minmax(0,1fr);gap:clamp(16px,3vw,36px);align-items:start}
@media (max-width:820px){.hero{grid-template-columns:1fr}}
.media{background:#000;border-radius:14px;overflow:hidden;display:block}.media img,.media video{display:block;width:100%;height:auto;max-height:78vh;object-fit:contain;margin:0 auto}
.hero .gbp{aspect-ratio:16/9;height:auto;max-height:78vh;border-radius:14px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:18px}
.btn{display:inline-block;background:var(--red);color:#fff;text-decoration:none;font-weight:700;padding:12px 22px;border-radius:999px;margin:8px 0}
.btn:hover{background:var(--red-dark)}dl{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;margin:12px 0}dt{color:var(--muted)}dd{margin:0}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:16px;padding:0;list-style:none}
.grid a{display:block;background:var(--panel);border:1px solid var(--line);border-radius:12px;overflow:hidden;color:var(--text);text-decoration:none;height:100%}
.grid a:hover{border-color:var(--red)}.grid img,.grid .nopic{display:block;width:100%;aspect-ratio:16/9;object-fit:cover;background:var(--line)}
.grid b{display:block;padding:10px 12px 2px}.grid small{display:block;padding:0 12px 12px;color:var(--muted)}
pre{background:#11161d;color:#e6edf7;padding:16px;border-radius:12px;overflow:auto;font-size:14px;max-height:70vh}
ol.lessons li{margin:4px 0}footer{border-top:1px solid var(--line);color:var(--muted);font-size:14px;padding-top:16px;padding-bottom:28px}
footer a{color:var(--muted)}`;

// A card in a list page's grid.
const tile = (href, title, sub, pic, alt) => '<li><a href="' + esc(href) + '">' +
  (pic ? '<img src="' + esc(pic) + '" alt="' + esc(alt || title) + '" loading="lazy" width="320" height="180">' : '<span class="nopic" role="img" aria-label="' + esc(title) + '"></span>') +
  '<b>' + esc(title) + '</b><small>' + esc(sub) + '</small></a></li>';
const openBtn = (hash, label) => '<a class="btn" href="/app/' + esc(hash) + '">' + esc(label) + '</a>';
const byLine = (p, extra = '') => '<p class="by">by ' + (p && p.userId ? '<a href="/users/' + p.userId + '">' + esc(p.name) + '</a>' : esc((p && p.name) || NAME)) + extra + '</p>';
const count = (n, one) => Number(n || 0).toLocaleString('en-US') + ' ' + one + (n === 1 ? '' : 's');
const gamePic = (g) => (g.thumb ? '/thumb/' + encodeURIComponent(g.id) + '?v=' + g.thumb : g.icon ? '/icon/' + encodeURIComponent(g.id) + '?v=' + g.icon : '');
const itemHash = (a) => (LIBRARY_KINDS.has(a.kind) ? '#/library/' + encodeURIComponent(String(a.num || a.id)) : '#/item/' + encodeURIComponent(a.id));

// A video, ready for G&B's player (website/app/player.js wraps it; without scripts it's a normal video).
const videoTag = (v, cls) => '<video class="' + cls + '" data-gbp controls preload="metadata" playsinline src="' + esc(v.src) + '"' +
  (v.poster ? ' poster="' + esc(v.poster) + '"' : '') + (v.w && v.h ? ' width="' + v.w + '" height="' + v.h + '"' : '') +
  ' data-id="' + esc(v.id) + '" data-title="' + esc(v.title) + '" data-by="' + esc(v.by && v.by.name) + '" aria-label="' + esc(v.title) + '"></video>';
const PLAYER_HEAD = '<link rel="stylesheet" href="/app/videoplayer.css">\n<script type="module" src="/app/player.js"></script>\n';

// /embed/<id>: just the player, filling the whole frame, for other sites and apps to put in an
// iframe (X's player card, forums, blogs). security.js lets any site frame these pages (only these).
export async function embedPage(ask, id) {
  const r = await ask({ op: 'video.get', args: { id, quiet: true } });
  if (!r.ok || !r.video || r.video.state !== 'ready' || r.video.review || !r.video.src || r.video.src.includes('?k=')) return null;
  const v = r.video, path = '/videos/' + encodeURIComponent(v.id);
  return '<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1">\n' +
    '<title>' + esc(v.title) + ' | ' + NAME + '</title>\n<meta name="robots" content="noindex">\n' +
    '<link rel="canonical" href="' + SITE + path + '">\n<link rel="icon" href="/img/icon.png">\n' +
    '<style>html,body{margin:0;height:100%;background:#000;overflow:hidden;font-family:system-ui,sans-serif}' +
    '.gbp,video.full{position:absolute;inset:0;width:100%;height:100%}video{object-fit:contain;background:#000}' +
    '.gb-tag{position:absolute;top:10px;left:12px;z-index:5;color:#fff;text-decoration:none;font-weight:700;font-size:15px;' +
    'text-shadow:0 1px 3px rgba(0,0,0,.8);max-width:calc(100% - 24px);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;transition:opacity .3s}' +
    '.gb-tag small{display:block;font-weight:500;opacity:.85}.gbp:not(.gbp-paused):not(:hover) ~ .gb-tag{opacity:0}</style>\n' + PLAYER_HEAD +
    '</head>\n<body>\n' + videoTag(v, 'full') +
    '<a class="gb-tag" href="' + SITE + path + '" target="_blank" rel="noopener">' + esc(v.title) +
    '<small>' + esc(v.by && v.by.name) + ' · Guts&amp;Bolts</small></a>\n</body>\n</html>\n';
}

// ---------- one page for each kind of thing ----------
async function gamePageOrItem(ask, id, wantGame) {
  const r = await ask({ op: 'asset.info', args: { id } });
  if (!r.ok || !r.asset || r.asset.review) return null;
  const a = r.asset;
  if (wantGame !== (a.kind === 'game')) return { moved: a.kind === 'game' ? '/games/' + encodeURIComponent(a.id) : '/items/' + encodeURIComponent(a.id) };
  return a;
}

// The Audio portal's kinds (worker/audio.js AUDIO_CATS), and the file types it plays.
const AUDIO_KIND_NAMES = { music: 'Song', loop: 'Loop', sfx: 'Sound effect', ambience: 'Ambience', voice: 'Voice clip', other: 'Sound' };
const AUDIO_TYPES = { mp3: 'audio/mpeg', wav: 'audio/wav', ogg: 'audio/ogg', flac: 'audio/flac' };
const BUILDERS = {
  async games(ask, id) {
    if (!id) {
      const r = await ask({ op: 'list', args: { kind: 'game', sort: 'popular', limit: 100 } });
      const games = r.ok ? r.assets || [] : [];
      return listPage('games', games.map((g) => tile('/games/' + encodeURIComponent(g.id), g.name, count(g.plays, 'visit') + ' · by ' + g.creatorName,
        gamePic(g), g.name + ' thumbnail')), games.map((g) => '/games/' + encodeURIComponent(g.id)));
    }
    const g = await gamePageOrItem(ask, id, true);
    if (!g || g.moved) return g;
    const path = '/games/' + encodeURIComponent(g.id);
    const pic = gamePic(g);
    const description = blurb(g.description) || 'Play ' + g.name + ', a free game by ' + g.creatorName + ' on Guts&Bolts.';
    const shots = (g.shots || []).slice(0, 6);
    return {
      title: g.name, description, path, image: pic, imageAlt: g.name + ' thumbnail', type: 'website',
      crumbs: [['Games', '/games'], [g.name, path]],
      data: [{ '@context': 'https://schema.org', '@type': 'VideoGame', name: g.name, url: SITE + path, description: blurb(g.description, 500) || description,
        image: pic ? abs(pic) : undefined, author: person({ name: g.creatorName }),
        genre: (g.genres || []).length ? g.genres : undefined, gamePlatform: PLATFORMS, applicationCategory: 'Game',
        playMode: (g.maxPlayers || 0) > 1 ? 'MultiPlayer' : 'SinglePlayer', isAccessibleForFree: true,
        dateCreated: iso(g.created), dateModified: iso(g.updated), publisher: { '@type': 'Organization', name: NAME, url: SITE },
        interactionStatistic: { '@type': 'InteractionCounter', interactionType: 'https://schema.org/PlayAction', userInteractionCount: g.plays || 0 } }],
      body: '<article><h1>' + esc(g.name) + '</h1>' + byLine({ name: g.creatorName }) +
        '<div class="hero' + (pic || shots.length ? '' : ' solo') + '"><div>' + (pic ? '<span class="media"><img src="' + esc(pic) + '" alt="' + esc(g.name + ' thumbnail') + '" width="1280" height="720"></span>' : '') +
        (shots.length ? '<h2>Screenshots</h2><ul class="grid">' + shots.map((s, i) => '<li><a href="/shot/' + encodeURIComponent(g.id) + '/' + encodeURIComponent(s) + '">' +
          '<img src="/shot/' + encodeURIComponent(g.id) + '/' + encodeURIComponent(s) + '" alt="' + esc(g.name + ' screenshot ' + (i + 1)) + '" loading="lazy" width="320" height="180"></a></li>').join('') + '</ul>' : '') +
        '</div><div class="card">' + openBtn('#/game/' + encodeURIComponent(g.id), 'Play ' + g.name) +
        '<dl><dt>Visits</dt><dd>' + Number(g.plays || 0).toLocaleString('en-US') + '</dd>' +
        (g.likes !== undefined ? '<dt>Likes</dt><dd>' + Number(g.likes || 0).toLocaleString('en-US') + ' 👍 · ' + Number(g.dislikes || 0).toLocaleString('en-US') + ' 👎</dd>' : '') +
        ((g.genres || []).length ? '<dt>Genre</dt><dd>' + esc(g.genres.join(', ')) + '</dd>' : '') +
        '<dt>Max players</dt><dd>' + esc(g.maxPlayers || '') + '</dd><dt>Updated</dt><dd>' + day(g.updated) + '</dd></dl>' +
        (g.description ? '<h2>About this game</h2><p class="lead">' + esc(g.description) + '</p>' : '') + '</div></div></article>',
    };
  },

  async items(ask, id) {
    if (!id) return null;
    const a = await gamePageOrItem(ask, id, false);
    if (!a || a.moved) return a;
    const path = '/items/' + encodeURIComponent(a.id);
    const pic = a.thumb ? '/thumb/' + encodeURIComponent(a.id) + '?v=' + a.thumb : '';
    const description = blurb(a.description) || a.name + ', made by ' + a.creatorName + ' on Guts&Bolts.';
    return {
      title: a.name, description, path, image: pic, imageAlt: a.name, crumbs: [[LIBRARY_KINDS.has(a.kind) ? 'Library' : 'Catalog', '/'], [a.name, path]],
      data: [{ '@context': 'https://schema.org', '@type': 'CreativeWork', name: a.name, url: SITE + path, description, image: pic ? abs(pic) : undefined,
        genre: a.kind, author: person({ name: a.creatorName }), dateCreated: iso(a.created), dateModified: iso(a.updated) }],
      body: '<article><h1>' + esc(a.name) + '</h1>' + byLine({ name: a.creatorName }) + '<div class="hero"><div>' +
        (pic ? '<span class="media"><img src="' + esc(pic) + '" alt="' + esc(a.name) + '" width="420" height="420"></span>' : '') + '</div><div class="card">' +
        openBtn(itemHash(a), 'See it on Guts&Bolts') + '<dl><dt>Type</dt><dd>' + esc(a.kind) + '</dd>' +
        (a.price > 0 ? '<dt>Price</dt><dd>' + Number(a.price).toLocaleString('en-US') + ' Bolts</dd>' : '<dt>Price</dt><dd>Free</dd>') + '</dl>' +
        (a.description ? '<p class="lead">' + esc(a.description) + '</p>' : '') + '</div></div></article>',
    };
  },

  async videos(ask, id) {
    if (!id) {
      const r = await ask({ op: 'video.list', args: { sort: 'top' } });
      const vids = r.ok ? r.videos || [] : [];
      return listPage('videos', vids.map((v) => tile('/videos/' + encodeURIComponent(v.id), v.title, count(v.views, 'view') + ' · ' + v.by.name, v.poster, v.title)),
        vids.map((v) => '/videos/' + encodeURIComponent(v.id)));
    }
    const r = await ask({ op: 'video.get', args: { id, quiet: true } });
    if (!r.ok || !r.video || r.video.state !== 'ready' || r.video.review || !r.video.src || r.video.src.includes('?k=')) return null;
    const v = r.video;
    const path = '/videos/' + encodeURIComponent(v.id);
    const description = blurb(v.text) || v.title + ', a video by ' + v.by.name + ' on Guts&Bolts.';
    return {
      title: v.title, description, path, image: v.poster, imageAlt: v.title, type: 'video.other',
      video: { src: v.src, type: v.type, w: v.w, h: v.h, secs: v.secs, embed: '/embed/' + encodeURIComponent(v.id) },
      crumbs: [['Videos', '/videos'], [v.title, path]],
      data: [{ '@context': 'https://schema.org', '@type': 'VideoObject', name: v.title, description: blurb(v.text, 2000) || description,
        thumbnailUrl: [abs(v.poster) || DEFAULT_PIC], uploadDate: iso(v.at), duration: v.secs ? duration(v.secs) : undefined,
        contentUrl: abs(v.src), embedUrl: SITE + path, url: SITE + path, author: person(v.by), keywords: (v.tags || []).join(', ') || undefined,
        width: v.w || undefined, height: v.h || undefined,
        interactionStatistic: { '@type': 'InteractionCounter', interactionType: 'https://schema.org/WatchAction', userInteractionCount: v.views || 0 } }],
      // (people who click a shared link land here: player.js gives the video G&B's own player)
      head: PLAYER_HEAD,
      body: '<article><div class="hero"><div>' + videoTag(v, 'media') +
        '<h1>' + esc(v.title) + '</h1>' + byLine(v.by, ' · ' + count(v.views, 'view') + ' · ' + day(v.at)) +
        (v.text ? '<p class="lead">' + esc(v.text) + '</p>' : '') + '</div><div class="card">' +
        openBtn('#/watch/' + encodeURIComponent(v.id), 'Watch on Guts&Bolts') +
        '<p>Like it, comment and follow ' + esc(v.by.name) + ' on Guts&amp;Bolts.</p>' +
        ((v.tags || []).length ? '<dl><dt>Tags</dt><dd>' + esc(v.tags.join(', ')) + '</dd></dl>' : '') + '</div></div></article>',
    };
  },

  async audio(ask, id) {
    const sub = (t) => count(t.plays, 'play') + ' · ' + t.by.name;
    if (!id) {
      const r = await ask({ op: 'audio.list', args: { sort: 'top' } });
      const tracks = r.ok ? r.tracks || [] : [];
      return listPage('audio', tracks.map((t) => tile('/audio/' + encodeURIComponent(t.id), t.title, sub(t), '', t.title)),
        tracks.map((t) => '/audio/' + encodeURIComponent(t.id)));
    }
    const r = await ask({ op: 'audio.get', args: { id } });
    if (!r.ok || !r.track || r.track.review || r.track.access === 'private') return null;
    const t = r.track;
    const path = '/audio/' + encodeURIComponent(t.id), src = '/sound/' + encodeURIComponent(t.id);
    const kind = AUDIO_KIND_NAMES[t.cat] || 'Sound';
    const description = blurb(t.text) || t.title + ', a free ' + kind.toLowerCase() + ' by ' + t.by.name + ' on Guts&Bolts.';
    const music = t.cat === 'music' || t.cat === 'loop';
    const num = String(t.num || t.id);
    return {
      title: t.title, description, path, type: music ? 'music.song' : 'website', crumbs: [['Audio', '/audio'], [t.title, path]],
      data: [{ '@context': 'https://schema.org', '@type': music ? 'MusicRecording' : 'AudioObject', name: t.title, description,
        contentUrl: abs(src), url: SITE + path, encodingFormat: AUDIO_TYPES[t.ext] || undefined, isAccessibleForFree: true,
        [music ? 'byArtist' : 'creator']: person(t.by), datePublished: iso(t.at), genre: t.genre || undefined, keywords: (t.tags || []).join(', ') || undefined,
        interactionStatistic: { '@type': 'InteractionCounter', interactionType: 'https://schema.org/ListenAction', userInteractionCount: t.plays || 0 } }],
      body: '<article><h1>' + esc(t.title) + '</h1>' + byLine(t.by, ' · ' + esc(kind) + (t.genre ? ' · ' + esc(t.genre) : '') + ' · ' + day(t.at)) +
        '<div class="hero"><div><audio class="media" controls preload="none" src="' + esc(src) + '" aria-label="' + esc(t.title) + '"></audio>' +
        (t.text ? '<p class="lead">' + esc(t.text) + '</p>' : '') + '</div><div class="card">' +
        openBtn('#/track/' + encodeURIComponent(t.id), 'Listen on Guts&Bolts') +
        '<p>Free to use in your own Guts&amp;Bolts games: in Studio, paste the ID into a Sound\'s SoundId.</p>' +
        '<dl><dt>Sound ID</dt><dd>' + esc(num) + '</dd><dt>Plays</dt><dd>' + Number(t.plays || 0).toLocaleString('en-US') + '</dd>' +
        '<dt>Faves</dt><dd>' + Number(t.faves || 0).toLocaleString('en-US') + '</dd>' +
        ((t.tags || []).length ? '<dt>Tags</dt><dd>' + esc(t.tags.join(', ')) + '</dd>' : '') + '</dl></div></div></article>',
    };
  },

  async art(ask, id) {
    if (!id) {
      const r = await ask({ op: 'art.list', args: { sort: 'top' } });
      const pieces = r.ok ? r.pieces || [] : [];
      return listPage('art', pieces.map((p) => tile('/art/' + encodeURIComponent(p.id), p.title, 'by ' + p.by.name, '/artpic/' + encodeURIComponent(p.id) + '/thumb', p.title)),
        pieces.map((p) => '/art/' + encodeURIComponent(p.id)));
    }
    const r = await ask({ op: 'art.get', args: { id, quiet: true } });
    if (!r.ok || !r.piece || r.piece.review) return null;
    const p = r.piece;
    const path = '/art/' + encodeURIComponent(p.id), pic = '/artpic/' + encodeURIComponent(p.id);
    const description = blurb(p.text) || p.title + ', art by ' + p.by.name + ' on Guts&Bolts.';
    return {
      title: p.title, description, path, image: pic, imageAlt: p.title, type: 'article', crumbs: [['Art', '/art'], [p.title, path]],
      data: [{ '@context': 'https://schema.org', '@type': 'ImageObject', name: p.title, description, contentUrl: abs(pic), url: SITE + path,
        creator: person(p.by), creditText: p.by.name, datePublished: iso(p.at), keywords: (p.tags || []).join(', ') || undefined }],
      body: '<article><h1>' + esc(p.title) + '</h1>' + byLine(p.by, ' · ' + day(p.at)) + '<div class="hero"><div><a class="media" href="' + esc(pic) + '">' +
        '<img src="' + esc(pic) + '" alt="' + esc(p.title + (p.text ? ': ' + blurb(p.text, 100) : '')) + '"></a></div><div class="card">' +
        openBtn('#/artpiece/' + encodeURIComponent(p.id), 'See it on Guts&Bolts') + (p.text ? '<p class="lead">' + esc(p.text) + '</p>' : '') +
        '<dl><dt>Faves</dt><dd>' + Number(p.faves || 0).toLocaleString('en-US') + '</dd>' + ((p.tags || []).length ? '<dt>Tags</dt><dd>' + esc(p.tags.join(', ')) + '</dd>' : '') +
        '</dl></div></div></article>',
    };
  },

  async paste(ask, id) {
    if (!id) {
      const r = await ask({ op: 'paste.list', args: {} });
      const list = r.ok ? r.pastes || [] : [];
      return listPage('paste', list.map((p) => tile('/paste/' + encodeURIComponent(p.id), p.title, p.lang + ' · ' + count(p.lines, 'line') + ' · by ' + p.by.name)),
        list.map((p) => '/paste/' + encodeURIComponent(p.id)));
    }
    const r = await ask({ op: 'paste.get', args: { id, quiet: true } });
    if (!r.ok || !r.paste) return null;
    const p = r.paste;
    const path = '/paste/' + encodeURIComponent(p.id);
    const description = blurb(p.title + ': ' + String(p.text || '').slice(0, 400));
    return {
      title: p.title, description, path, noindex: p.visibility !== 'public', crumbs: [['Paste', '/paste'], [p.title, path]],
      data: [{ '@context': 'https://schema.org', '@type': 'SoftwareSourceCode', name: p.title, url: SITE + path, programmingLanguage: p.lang,
        author: person(p.by), dateCreated: iso(p.at), codeSampleType: 'full' }],
      body: '<article><h1>' + esc(p.title) + '</h1>' + byLine(p.by, ' · ' + esc(p.lang) + ' · ' + count(p.lines, 'line') + ' · ' + day(p.at)) +
        openBtn('#/pasteview/' + encodeURIComponent(p.id), 'Open on Guts&Bolts') + ' <a href="/pasteraw/' + encodeURIComponent(p.id) + '">Raw text</a>' +
        '<pre><code>' + esc(String(p.text || '').slice(0, 20000)) + '</code></pre></article>',
    };
  },

  async academy(ask, id) {
    if (!id) {
      const r = await ask({ op: 'academy.list', args: { sort: 'top' } });
      const list = r.ok ? r.courses || [] : [];
      return listPage('academy', list.map((c) => tile('/academy/' + encodeURIComponent(c.id), c.title, count(c.lessons, 'lesson') + ' · ' + c.level + ' · by ' + c.by.name)),
        list.map((c) => '/academy/' + encodeURIComponent(c.id)));
    }
    const r = await ask({ op: 'academy.get', args: { id } });
    if (!r.ok || !r.course || !r.course.published) return null;
    const c = r.course;
    const path = '/academy/' + encodeURIComponent(c.id);
    const description = blurb(c.summary) || 'A free ' + c.topic + ' course by ' + c.by.name + ' on G&B Academy.';
    return {
      title: c.title, description, path, type: 'article', crumbs: [['Academy', '/academy'], [c.title, path]],
      data: [{ '@context': 'https://schema.org', '@type': 'Course', name: c.title, description, url: SITE + path, about: c.topic, educationalLevel: c.level,
        isAccessibleForFree: true, author: person(c.by), datePublished: iso(c.at), dateModified: iso(c.edited || c.at),
        provider: { '@type': 'Organization', name: 'G&B Academy', url: SITE + '/academy' },
        hasCourseInstance: { '@type': 'CourseInstance', courseMode: 'online', courseWorkload: 'PT' + Math.max(1, c.lessons * 10) + 'M' },
        offers: { '@type': 'Offer', category: 'Free', price: 0, priceCurrency: 'USD' } }],
      body: '<article><h1>' + esc(c.title) + '</h1>' + byLine(c.by, ' · ' + esc(c.topic) + ' · ' + esc(c.level)) + '<div class="hero"><div>' +
        (c.summary ? '<p class="lead">' + esc(c.summary) + '</p>' : '') + '<h2>Lessons</h2><ol class="lessons">' +
        (c.lessonList || []).map((l) => '<li><a href="/app/#/lesson/' + encodeURIComponent(c.id) + '/' + encodeURIComponent(l.id) + '">' + esc(l.title) + '</a></li>').join('') +
        '</ol></div><div class="card">' + openBtn('#/course/' + encodeURIComponent(c.id), 'Start the course') +
        '<dl><dt>Lessons</dt><dd>' + c.lessons + '</dd><dt>Learners</dt><dd>' + Number(c.learners || 0).toLocaleString('en-US') + '</dd></dl></div></div></article>',
    };
  },

  async groups(ask, id) {
    if (!id) {
      const r = await ask({ op: 'groups.list', args: {} });
      const list = r.ok ? r.groups || [] : [];
      return listPage('groups', list.map((g) => tile('/groups/' + encodeURIComponent(g.id), g.name, count(g.members, 'member'))),
        list.map((g) => '/groups/' + encodeURIComponent(g.id)));
    }
    const r = await ask({ op: 'groups.get', args: { id } });
    if (!r.ok || !r.group) return null;
    const g = r.group;
    const path = '/groups/' + encodeURIComponent(g.id);
    const description = blurb(g.description) || g.name + ', a group with ' + count(g.members, 'member') + ' on Guts&Bolts.';
    const games = (r.games || []).slice(0, 24);
    return {
      title: g.name, description, path, crumbs: [['Groups', '/groups'], [g.name, path]],
      data: [{ '@context': 'https://schema.org', '@type': 'Organization', name: g.name, description, url: SITE + path,
        foundingDate: day(g.created) || undefined, numberOfEmployees: undefined, memberOf: { '@type': 'Organization', name: NAME, url: SITE } }],
      body: '<article><h1>' + esc(g.name) + '</h1><p class="by">' + count(g.members, 'member') + ' · owned by ' + esc(g.ownerName) + '</p>' +
        '<div class="hero"><div>' + (g.description ? '<p class="lead">' + esc(g.description) + '</p>' : '') +
        (games.length ? '<h2>Games</h2><ul class="grid">' + games.map((x) => tile('/games/' + encodeURIComponent(x.id), x.name, count(x.plays, 'visit'), gamePic(x), x.name + ' thumbnail')).join('') + '</ul>' : '') +
        '</div><div class="card">' + openBtn('#/group/' + encodeURIComponent(g.id), 'See the group') + '</div></div></article>',
    };
  },

  async press(ask, id) {
    if (!id) {
      const r = await ask({ op: 'press.list', args: {} });
      const list = r.ok ? r.posts || [] : [];
      const tiles = list.map((p) => '<li><a href="/press/' + encodeURIComponent(p.id) + '">' +
        (p.picture ? '<img src="/thumb/' + encodeURIComponent(p.picture) + '" alt="' + esc(p.title) + '" loading="lazy" width="320" height="180">' : '<span class="nopic" role="img" aria-label="' + esc(p.title) + '"></span>') +
        '<b>' + esc(p.title) + '</b><small>' + esc(p.kindName + ' · ' + day(p.at)) + '</small><small>' + esc(blurb(p.summary, 140)) + '</small></a></li>');
      return listPage('press', tiles, list.map((p) => '/press/' + encodeURIComponent(p.id)));
    }
    const r = await ask({ op: 'press.get', args: { id, quiet: true } });
    if (!r.ok || !r.post || !r.post.published) return null;
    const p = r.post;
    const path = '/press/' + encodeURIComponent(p.id);
    const games = (p.games || []).slice(0, 6);
    const pic = games[0] ? gamePic(games[0]) : '';
    return {
      title: p.title, description: blurb(p.summary || p.text), path, type: 'article', image: pic, imageAlt: games[0] ? games[0].name + ' thumbnail' : p.title,
      crumbs: [['Press', '/press'], [p.title, path]],
      data: [{ '@context': 'https://schema.org', '@type': 'NewsArticle', headline: p.title.slice(0, 110), description: p.summary, url: SITE + path,
        image: [abs(pic) || DEFAULT_PIC], datePublished: iso(p.at), dateModified: iso(p.edited || p.at), articleSection: PRESS_KIND_NAMES[p.kind] || 'News',
        author: { '@type': 'Organization', name: NAME, url: SITE }, publisher: { '@type': 'Organization', name: NAME, url: SITE, logo: { '@type': 'ImageObject', url: LOGO } },
        about: p.creator ? person(p.creator) : undefined, wordCount: String(p.text || '').split(/\s+/).filter(Boolean).length }],
      body: '<article><p class="by">' + esc(PRESS_KIND_NAMES[p.kind] || 'News') + ' · ' + day(p.at) + ' · ' + p.minutes + ' min read</p><h1>' + esc(p.title) + '</h1>' +
        (p.summary ? '<p class="lead"><b>' + esc(p.summary) + '</b></p>' : '') +
        (p.blocks || []).map((b) => (b.h ? '<h2>' + esc(b.h) + '</h2>' : '<p class="lead">' + esc(b.p) + '</p>')).join('') +
        (p.creator ? '<p>More from <a href="/users/' + p.creator.userId + '">' + esc(p.creator.name) + '</a>.</p>' : '') +
        (games.length ? '<h2>Games in this post</h2><ul class="grid">' + games.map((x) => tile('/games/' + encodeURIComponent(x.id), x.name, count(x.plays, 'visit'), gamePic(x), x.name + ' thumbnail')).join('') + '</ul>' : '') +
        openBtn('#/pressview/' + encodeURIComponent(p.id), 'Read it on Guts&Bolts') + '</article>',
    };
  },

  async calendar(ask, id) {
    if (!id) {
      const r = await ask({ op: 'calendar.list', args: {} });
      const list = r.ok ? r.events || [] : [];
      return listPage('calendar', list.map((e) => tile('/calendar/' + encodeURIComponent(e.id), e.title, e.kindName + ' · ' + when(e.start), '', e.title)),
        list.map((e) => '/calendar/' + encodeURIComponent(e.id)));
    }
    const r = await ask({ op: 'calendar.get', args: { id } });
    if (!r.ok || !r.event) return null;
    const e = r.event;
    const path = '/calendar/' + encodeURIComponent(e.id);
    const host = e.group ? { '@type': 'Organization', name: e.group.name, url: SITE + '/groups/' + encodeURIComponent(e.group.id) } : person(e.by);
    const description = blurb(e.text) || e.kindName + ' on Guts&Bolts, ' + when(e.start) + '.';
    return {
      title: e.title, description, path, crumbs: [['Calendar', '/calendar'], [e.title, path]],
      data: [{ '@context': 'https://schema.org', '@type': 'Event', name: e.title, description, url: SITE + path, startDate: iso(e.start), endDate: iso(e.end),
        eventAttendanceMode: 'https://schema.org/OnlineEventAttendanceMode', eventStatus: 'https://schema.org/EventScheduled',
        location: { '@type': 'VirtualLocation', url: SITE + '/app/' + (e.link || '#/calevent/' + encodeURIComponent(e.id)) },
        organizer: host, isAccessibleForFree: true, offers: { '@type': 'Offer', price: 0, priceCurrency: 'USD', availability: 'https://schema.org/InStock', url: SITE + path } }],
      body: '<article><p class="by">' + esc(e.kindName) + (e.official ? ' · Official' : '') + '</p><h1>' + esc(e.title) + '</h1>' +
        '<div class="hero"><div>' + (e.text ? '<p class="lead">' + esc(e.text) + '</p>' : '') + '</div><div class="card">' +
        openBtn('#/calevent/' + encodeURIComponent(e.id), 'See it on the calendar') +
        '<dl><dt>Starts</dt><dd>' + esc(when(e.start)) + '</dd><dt>Ends</dt><dd>' + esc(when(e.end)) + '</dd>' +
        '<dt>Hosted by</dt><dd>' + (e.group ? '<a href="/groups/' + encodeURIComponent(e.group.id) + '">' + esc(e.group.name) + '</a>'
          : e.by.userId ? '<a href="/users/' + e.by.userId + '">' + esc(e.by.name) + '</a>' : esc(e.by.name)) + '</dd>' +
        '<dt>Going</dt><dd>' + Number(e.going || 0).toLocaleString('en-US') + '</dd></dl></div></div></article>',
    };
  },

  async users(ask, id) {
    if (!id || !/^\d{1,11}$/.test(id)) return null;
    const r = await ask({ op: 'profile', args: { id } });
    if (!r.ok || !r.user || r.user.banned) return null;
    const u = r.user;
    const path = '/users/' + u.userId;
    const games = (r.creations || []).filter((a) => a.kind === 'game' && !a.review).slice(0, 24);
    const description = blurb(r.blurb) || u.name + ' on Guts&Bolts' + (games.length ? ': ' + count(games.length, 'game') + ' made, ' + count(r.placeVisits, 'visit') + '.' : '.');
    return {
      title: u.name + ' (@' + (u.username || u.name) + ')', description, path, type: 'profile', image: '/avatar/' + encodeURIComponent(u.id) + '.png', imageAlt: u.name + '\'s avatar',
      crumbs: [['People', '/'], [u.name, path]],
      data: [{ '@context': 'https://schema.org', '@type': 'ProfilePage', url: SITE + path, dateCreated: iso(u.created),
        mainEntity: { '@type': 'Person', name: u.name, alternateName: u.username ? '@' + u.username : undefined, identifier: String(u.userId), description: r.blurb || undefined,
          interactionStatistic: { '@type': 'InteractionCounter', interactionType: 'https://schema.org/FollowAction', userInteractionCount: r.followerCount || 0 } } }],
      body: '<article><h1>' + esc(u.name) + '</h1><p class="by">@' + esc(u.username || u.name) + ' · joined ' + day(u.created) + '</p><div class="hero"><div>' +
        (r.blurb ? '<p class="lead">' + esc(r.blurb) + '</p>' : '') +
        (games.length ? '<h2>Games by ' + esc(u.name) + '</h2><ul class="grid">' + games.map((x) => tile('/games/' + encodeURIComponent(x.id), x.name, count(x.plays, 'visit'), gamePic(x), x.name + ' thumbnail')).join('') + '</ul>' : '') +
        '</div><div class="card">' + openBtn('#/user/' + u.userId, 'See the profile') + '<dl><dt>Friends</dt><dd>' + (r.friendCount || 0) + '</dd>' +
        '<dt>Followers</dt><dd>' + (r.followerCount || 0) + '</dd><dt>Game visits</dt><dd>' + Number(r.placeVisits || 0).toLocaleString('en-US') + '</dd></dl></div></div></article>',
    };
  },
};

function listPage(section, tiles, paths) {
  const name = SECTION_NAMES[section];
  return {
    title: name, description: SECTION_BLURBS[section], path: '/' + section, crumbs: [[name, '/' + section]],
    data: [{ '@context': 'https://schema.org', '@type': 'CollectionPage', name: name + ' on ' + NAME, url: SITE + '/' + section, description: SECTION_BLURBS[section],
      mainEntity: { '@type': 'ItemList', numberOfItems: paths.length, itemListElement: paths.slice(0, 100).map((p, i) => ({ '@type': 'ListItem', position: i + 1, url: SITE + p })) } }],
    body: '<h1>' + esc(name) + ' on Guts&amp;Bolts</h1><p class="lead">' + esc(SECTION_BLURBS[section]) + '</p>' +
      openBtn('#/' + (section === 'groups' ? 'groups' : section === 'paste' ? 'paste' : section), 'Open ' + name + ' in the app') +
      (tiles.length ? '<ul class="grid">' + tiles.join('') + '</ul>' : '<p>Nothing here yet.</p>'),
  };
}

// ---------- the sitemap ----------
async function pages(ask, op, args, key, most = kMostSitemapCalls) {
  const out = [];
  for (let page = 1; page <= most; page++) {
    const r = await ask({ op, args: Object.assign({}, args, { page }) });
    if (!r.ok) break;
    out.push(...(r[key] || []));
    if (!r.more) break;
  }
  return out;
}

export async function sitemap(ask) {
  const urls = new Map();   // path -> lastmod (YYYY-MM-DD or '')
  const add = (p, sec) => { if (!urls.has(p)) urls.set(p, day(sec)); };
  for (const p of PUBLIC_PAGES) urls.set(canonicalOf(p).slice(SITE.length), LEGAL_PAGES.includes(p) ? LEGAL_UPDATED : '');
  for (const s of SECTIONS) add('/' + s, 0);
  const people = new Set();
  const games = [];
  for (let offset = 0; offset < 2000; offset += 100) {
    const r = await ask({ op: 'list', args: { kind: 'game', sort: 'popular', limit: 100, offset } });
    if (!r.ok) break;
    games.push(...(r.assets || []));
    if ((r.assets || []).length < 100) break;
  }
  for (const g of games) { if (!g.review) add('/games/' + encodeURIComponent(g.id), g.updated); }
  for (const v of await pages(ask, 'video.list', { sort: 'new' }, 'videos')) { add('/videos/' + encodeURIComponent(v.id), v.at); if (v.by && v.by.userId) people.add(v.by.userId); }
  for (const t of await pages(ask, 'audio.list', { sort: 'new' }, 'tracks')) { add('/audio/' + encodeURIComponent(t.id), t.at); if (t.by && t.by.userId) people.add(t.by.userId); }
  for (const p of await pages(ask, 'art.list', { sort: 'new' }, 'pieces')) { add('/art/' + encodeURIComponent(p.id), p.at); if (p.by && p.by.userId) people.add(p.by.userId); }
  for (const p of await pages(ask, 'paste.list', {}, 'pastes')) { if (p.visibility === 'public') add('/paste/' + encodeURIComponent(p.id), p.at); }
  for (const c of await pages(ask, 'academy.list', { sort: 'new' }, 'courses')) { add('/academy/' + encodeURIComponent(c.id), c.edited || c.at); if (c.by && c.by.userId) people.add(c.by.userId); }
  for (const p of await pages(ask, 'press.list', {}, 'posts')) add('/press/' + encodeURIComponent(p.id), p.edited || p.at);
  const cal = await ask({ op: 'calendar.list', args: {} });
  for (const e of (cal.ok ? cal.events || [] : [])) add('/calendar/' + encodeURIComponent(e.id), e.at);
  const g = await ask({ op: 'groups.list', args: {} });
  for (const x of (g.ok ? g.groups || [] : [])) add('/groups/' + encodeURIComponent(x.id), x.created);
  // People who made something people can find (creator ids on games are account ids, not numbers,
  // so those come from their game's page instead).
  for (const n of people) add('/users/' + n, 0);
  return '<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n' +
    [...urls].map(([p, mod]) => '  <url><loc>' + esc(SITE + p) + '</loc>' + (mod ? '<lastmod>' + mod + '</lastmod>' : '') + '</url>').join('\n') + '\n</urlset>\n';
}

// ---------- asking the server ----------
// The read-only ops the pages may use (all of them are open to guests in the app too).
export const SEO_OPS = new Set(['list', 'asset.info', 'profile', 'groups.list', 'groups.get', 'video.list', 'video.get',
  'audio.list', 'audio.get', 'art.list', 'art.get', 'paste.list', 'paste.get', 'academy.list', 'academy.get', 'press.list', 'press.get', 'calendar.list', 'calendar.get']);
// A visitor who isn't signed in, like a first-time guest on the website, but never saved.
function visitor() {
  return { id: '0'.repeat(64), name: 'Player', created: 0, lastSeen: 0, ledger: [], grants: {}, owned: [], banned: false,
    friends: [], friendIn: [], friendOut: [], following: [], followers: [], blocked: [], favorites: [], itemFavorites: [], usedModels: [],
    username: '', userId: 0, chats: [], partyInvites: [] };
}
// Adds seoAsk(req) to the server (worker/server.js): the Worker calls it straight (Durable Object RPC).
export function installSeo(Cls) {
  Cls.prototype.seoAsk = function (req) {
    const op = req && typeof req.op === 'string' ? req.op : '';
    if (!SEO_OPS.has(op)) return { ok: false, error: 'Not a page op.' };
    const args = req.args && typeof req.args === 'object' && !Array.isArray(req.args) ? Object.assign({}, req.args) : {};
    if (op.endsWith('.get')) args.quiet = true;   // reading a page isn't a view
    try {
      // (a fresh copy each time, so nothing an op does to "me" sticks)
      return JSON.parse(JSON.stringify(this.op(op, visitor(), args)));
    } catch (e) {
      return { ok: false, error: 'The server hit a problem: ' + (e && e.message || e) };
    } finally {
      this.flush();
    }
  };
}

// ---------- routing ----------
const ROUTE = /^\/(games|items|videos|audio|art|paste|academy|groups|users|press|calendar)(?:\/([^/]{1,80}))?\/?$/;
const EMBED = /^\/embed\/([^/]{1,80})\/?$/;
export const isSeoPath = (path) => path === '/sitemap.xml' || ROUTE.test(path) || EMBED.test(path);

// The page for this address as { status, html } (or { moved }), without the cache. For tests too.
export async function build(ask, path, env = {}) {
  const e = EMBED.exec(path);
  if (e) {
    let html = null;
    try { html = await embedPage(ask, decodeURIComponent(e[1])); } catch { return { status: 503, html: '<!doctype html><title>Try again soon</title><p>Guts&amp;Bolts is busy. Try again in a moment.</p>' }; }
    return html ? { status: 200, html } : { status: 404, html: '<!doctype html><title>Not found</title><style>body{background:#000;color:#fff;font-family:system-ui;display:grid;place-items:center;height:100vh;margin:0}</style><p>This video isn\'t on Guts&amp;Bolts (any more).</p>' };
  }
  const m = ROUTE.exec(path);
  if (!m) return null;
  let id = '';
  try { id = m[2] ? decodeURIComponent(m[2]) : ''; } catch { return { status: 404, html: notFound(env) }; }
  let p = null;
  try { p = await BUILDERS[m[1]](ask, id); } catch { return { status: 503, html: page({ title: 'Try again soon', description: 'Guts&Bolts is busy right now.', path, noindex: true, body: '<h1>Try again in a moment</h1><p>The server is busy. <a href="/app/">Open the site</a>.</p>' }, env) }; }
  if (!p) return { status: 404, html: notFound(env, path) };
  if (p.moved) return { moved: p.moved };
  return { status: 200, html: page(p, env) };
}
const notFound = (env, path = '/') => page({ title: 'Not found', description: 'That page isn\'t on Guts&Bolts (any more).', path, noindex: true,
  body: '<h1>That isn\'t here</h1><p>It may be private, still being checked by staff, or deleted.</p>' + openBtn('#/', 'Go to Guts&Bolts') }, env);

// Where the app shows the same thing, for a server on a computer (GB_SERVER), which has no seoAsk.
export function appAddress(path) {
  const e = EMBED.exec(path);
  if (e) return '/app/#/watch/' + e[1];
  const m = ROUTE.exec(path);
  if (!m) return '/app/';
  const id = m[2] || '';
  const hash = { games: '#/game/', items: '#/item/', videos: '#/watch/', audio: '#/track/', art: '#/artpiece/', paste: '#/pasteview/', academy: '#/course/',
    groups: '#/group/', users: '#/user/', press: '#/pressview/', calendar: '#/calevent/' }[m[1]];
  return '/app/' + (id ? hash + id : '#/' + m[1]);
}

// GET /games/12 and the rest: the page, kept in Cloudflare's cache for a few minutes. null for other addresses.
// ask(req) answers an op as a guest (the Durable Object's seoAsk), or is null when the server runs
// on a computer: then the addresses just open the app, and the sitemap has the fixed pages only.
export async function handleSeo(request, env, url, ask, ctx) {
  if (!isSeoPath(url.pathname)) return null;
  if (request.method !== 'GET' && request.method !== 'HEAD') return new Response('Use GET.', { status: 405, headers: { allow: 'GET, HEAD' } });
  if (!ask) {
    if (url.pathname !== '/sitemap.xml') return Response.redirect(url.origin + appAddress(url.pathname), 302);
    ask = async () => ({ ok: false });
  }
  const cache = typeof caches !== 'undefined' ? caches.default : null;
  const key = new Request(url.origin + url.pathname, { method: 'GET' });
  if (cache) {
    const hit = await cache.match(key);
    if (hit) return request.method === 'HEAD' ? new Response(null, hit) : hit;
  }
  let res;
  if (url.pathname === '/sitemap.xml') {
    res = new Response(await sitemap(ask), { headers: { 'content-type': 'application/xml; charset=utf-8', 'cache-control': 'public, max-age=' + SITEMAP_TTL } });
  } else {
    const r = await build(ask, url.pathname, env);
    if (r.moved) return Response.redirect(url.origin + r.moved, 301);
    res = new Response(r.html, { status: r.status, headers: { 'content-type': 'text/html; charset=utf-8',
      'cache-control': r.status === 200 ? 'public, max-age=' + PAGE_TTL : 'no-store' } });
  }
  if (cache && res.status === 200 && ctx && ctx.waitUntil) ctx.waitUntil(cache.put(key, res.clone()));
  return request.method === 'HEAD' ? new Response(null, res) : res;
}

// ---------- the fixed pages (home, the app, products, legal) ----------
const NOINDEX = /^\/(checkout|server)(\/|\.html|$)/;
const TIDY = /^\/(|index\.html|app\/(index\.html)?|products(\/[a-z]+)?|privacy|cookies|copyright|contact)$/i;
const canonicalOf = (path) => {
  if (path === '/' || path === '/index.html') return SITE + '/';
  if (/^\/app\//.test(path)) return SITE + '/app/';
  const product = /^\/products\/(sysfix|contentforge)$/i.exec(path);
  if (product) return SITE + '/products/' + (product[1].toLowerCase() === 'sysfix' ? 'Sysfix' : 'ContentForge');
  return SITE + path.replace(/\.html$/, '');
};
export const HOME_DATA = [
  { '@context': 'https://schema.org', '@type': 'WebSite', name: NAME, alternateName: ['Guts and Bolts', 'G&B'], url: SITE + '/' },
  { '@context': 'https://schema.org', '@type': 'Organization', name: NAME, url: SITE + '/', logo: LOGO,
    sameAs: ['https://github.com/Guts-Bolts/Guts-Bolts'] },
  { '@context': 'https://schema.org', '@type': 'SoftwareApplication', name: NAME, applicationCategory: 'GameApplication',
    operatingSystem: 'Windows, macOS, Linux, Android, Meta Quest', url: SITE + '/', image: DEFAULT_PIC,
    offers: { '@type': 'Offer', price: 0, priceCurrency: 'USD' } },
];

export function withSeoHead(response, url, env = {}) {
  const type = (response.headers.get('content-type') || '').toLowerCase();
  if (!type.startsWith('text/html')) return response;
  const path = url.pathname;
  if (NOINDEX.test(path)) {
    const headers = new Headers(response.headers);
    headers.set('X-Robots-Tag', 'noindex, nofollow');
    return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
  }
  if (!TIDY.test(path) || response.status !== 200 || typeof HTMLRewriter === 'undefined') return response;
  const seen = { canonical: false, ogType: false, ogUrl: false, ogSite: false, twitter: false, ogImage: '' };
  const canonical = canonicalOf(path);
  const verify = String(env.GOOGLE_SITE_VERIFICATION || '').trim();
  return new HTMLRewriter()
    .on('link[rel="canonical"]', { element() { seen.canonical = true; } })
    .on('meta[property="og:type"]', { element() { seen.ogType = true; } })
    .on('meta[property="og:url"]', { element() { seen.ogUrl = true; } })
    .on('meta[property="og:site_name"]', { element() { seen.ogSite = true; } })
    .on('meta[name="twitter:card"]', { element() { seen.twitter = true; } })
    .on('meta[property="og:image"]', { element(el) {
      // Share cards need the whole address ("img/x.jpg" doesn't work in a chat app).
      const c = el.getAttribute('content') || '';
      const full = c ? new URL(c, SITE + (path.startsWith('/app/') ? '/app/' : '/')).toString() : '';
      if (full) { el.setAttribute('content', full); seen.ogImage = full; }
    } })
    .on('head', { element(head) {
      head.onEndTag((end) => {
        let add = '';
        if (!seen.canonical) add += '<link rel="canonical" href="' + esc(canonical) + '">\n';
        if (!seen.ogUrl) add += '<meta property="og:url" content="' + esc(canonical) + '">\n';
        if (!seen.ogType) add += '<meta property="og:type" content="website">\n';
        if (!seen.ogSite) add += '<meta property="og:site_name" content="' + NAME + '">\n';
        if (!seen.ogImage) add += '<meta property="og:image" content="' + DEFAULT_PIC + '">\n';
        if (!seen.twitter) add += '<meta name="twitter:card" content="summary_large_image">\n';
        if (verify) add += '<meta name="google-site-verification" content="' + esc(verify) + '">\n';
        if (path === '/' || path === '/index.html') add += HOME_DATA.map(ld).join('\n') + '\n';
        end.before(add, { html: true });
      });
    } })
    .transform(response);
}
