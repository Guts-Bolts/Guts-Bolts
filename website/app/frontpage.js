// The busy front page (Home, #/): what people are doing across the whole site right now,
// Newgrounds-style. Who's live, the pick of the day, new videos, the best art this week,
// hot forum threads, new games and new courses, plus a strip of site-wide numbers.
//
// Everything here comes from list calls the products already have (video.list, art.list,
// stream.list, forum.list, list, academy.list), so nothing new is needed on either server.
// A shelf with nothing in it is left out, so a quiet site never shows empty boxes.

const VIDEO_TIME = (s) => { s = Math.max(0, Math.round(s || 0)); return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0'); };
const count = (n) => { n = n || 0; return n >= 1e6 ? (n / 1e6).toFixed(1).replace(/\.0$/, '') + 'M' : n >= 1e3 ? (n / 1e3).toFixed(1).replace(/\.0$/, '') + 'K' : String(n); };
const plural = (n, word) => count(n) + ' ' + word + (n === 1 ? '' : 's');

// The same pick all day for everyone, a different one tomorrow (days since 1970, UTC).
export function dayNumber(t = Date.now()) { return Math.floor(t / 86400000); }
export function pickOfTheDay(candidates, day = dayNumber()) {
  const list = candidates.filter(Boolean);
  if (!list.length) return null;
  return list[((day % list.length) + list.length) % list.length];
}

// What the front page asks the servers for. Each call can fail on its own (a server on a
// computer without Videos, say) and that shelf just doesn't show.
export const FRONT_CALLS = [
  ['live', 'stream.list', {}],
  ['videos', 'video.list', { sort: 'new', page: 1 }],
  ['videosWeek', 'video.list', { sort: 'week', page: 1 }],
  ['art', 'art.list', { sort: 'week', page: 1 }],
  ['artAll', 'art.list', { sort: 'new', page: 1 }],
  ['threads', 'forum.list', { board: 'all', sort: 'hot', page: 0 }],
  ['games', 'list', { kind: 'game', sort: 'new', limit: 6 }],
  ['gamesTop', 'list', { kind: 'game', sort: 'popular', limit: 6 }],
  ['courses', 'academy.list', { sort: 'new', page: 1 }],
];

export async function loadFront(pageCall) {
  const out = {};
  // (No .catch here: calls never throw, except pageCall's "you left this page" signal,
  // which has to reach app.js so it stops drawing.)
  const results = await Promise.all(FRONT_CALLS.map(([, op, args]) => pageCall(op, args)));
  FRONT_CALLS.forEach(([key], i) => { out[key] = results[i] && results[i].ok ? results[i] : null; });
  return out;
}

export function frontPage(ctx, data) {
  const { html, ago, gameCard } = ctx;
  const list = (r, key) => (r && Array.isArray(r[key]) ? r[key] : []);
  const live = list(data.live, 'live');
  const videos = list(data.videos, 'videos').slice(0, 6);
  const videosWeek = list(data.videosWeek, 'videos');
  let art = list(data.art, 'pieces');
  if (!art.length) art = list(data.artAll, 'pieces');   // nothing faved this week yet: show the newest
  art = art.slice(0, 8);
  const threads = list(data.threads, 'threads').slice(0, 8);
  const games = list(data.games, 'assets').slice(0, 6);
  const gamesTop = list(data.gamesTop, 'assets');
  const courses = list(data.courses, 'courses').slice(0, 4);

  const box = (title, more, body, cls = '') => html`<div class="box front-box ${cls}"><h2 class="boxhead">${title}${more ? html`<a href="${more}" class="front-more">See all &raquo;</a>` : ''}</h2>${body}</div>`;

  const videoCard = (v) => html`<a class="front-video" href="#/watch/${v.id}">
      <span class="front-thumb">${v.poster ? html`<img loading="lazy" src="${v.poster}" alt="">` : html`<span class="front-nopic">&#9654;</span>`}
        ${v.secs ? html`<span class="front-time">${VIDEO_TIME(v.secs)}</span>` : ''}</span>
      <b>${v.title}</b><span class="small muted">${v.by ? v.by.name : ''} &middot; ${plural(v.views || 0, 'view')} &middot; ${ago(v.at)}</span></a>`;
  const liveCard = (s) => html`<a class="front-video front-live" href="#/live/${s.id}">
      <span class="front-thumb">${s.poster ? html`<img loading="lazy" src="${s.poster}" alt="">` : html`<span class="front-nopic">&#9679;</span>`}
        <span class="front-livebadge">LIVE</span><span class="front-time">${count(s.viewers || 0)} watching</span></span>
      <b>${s.title}</b><span class="small muted">${s.by ? s.by.name : ''}</span></a>`;
  const artCard = (p) => html`<a class="front-art" href="#/artpiece/${p.id}" title="${p.title}">
      <img loading="lazy" src="/artpic/${encodeURIComponent(p.id)}/thumb" alt="${p.title}">
      <span class="front-art-text"><b>${p.title}</b><span class="small">&#9829; ${p.faves || 0} &middot; ${p.by ? p.by.name : ''}</span></span></a>`;
  const threadRow = (t) => html`<a class="front-thread" href="#/forum/t/${t.id}">
      <b>${t.title}</b><span class="small muted">${t.by ? t.by.name : ''} &middot; ${count(t.replies || 0)} repl${t.replies === 1 ? 'y' : 'ies'} &middot; ${ago(t.last || t.at)}</span></a>`;
  const courseRow = (c) => html`<a class="front-thread" href="#/course/${c.id}">
      <b>${c.title}</b><span class="small muted">by ${c.by ? c.by.name : ''} &middot; ${plural(c.lessons || 0, 'lesson')} &middot; ${plural(c.learners || 0, 'learner')}</span></a>`;

  // Pick of the day: one thing from the best of this week (art, videos or games), the same
  // all day so people can talk about it.
  const candidates = [
    ...list(data.art, 'pieces').slice(0, 3).map((p) => ({ kind: 'art', p })),
    ...videosWeek.slice(0, 3).map((v) => ({ kind: 'video', v })),
    ...gamesTop.slice(0, 3).map((g) => ({ kind: 'game', g })),
  ];
  const pick = pickOfTheDay(candidates);
  const pickBox = pick ? html`<div class="box front-pick"><span class="front-kicker">&#9733; Pick of the day</span>
      ${pick.kind === 'art' ? html`<a class="front-pick-body" href="#/artpiece/${pick.p.id}">
          <span class="front-pick-pic"><img src="/artpic/${encodeURIComponent(pick.p.id)}/thumb" alt="${pick.p.title}"></span>
          <span><b>${pick.p.title}</b><span class="small">Art by ${pick.p.by ? pick.p.by.name : ''} &middot; &#9829; ${pick.p.faves || 0}</span>
            <span class="btn green front-pick-go">Look at it &raquo;</span></span></a>`
        : pick.kind === 'video' ? html`<a class="front-pick-body" href="#/watch/${pick.v.id}">
          <span class="front-pick-pic">${pick.v.poster ? html`<img src="${pick.v.poster}" alt="">` : html`<span class="front-nopic">&#9654;</span>`}</span>
          <span><b>${pick.v.title}</b><span class="small">A video by ${pick.v.by ? pick.v.by.name : ''} &middot; ${plural(pick.v.views || 0, 'view')}</span>
            <span class="btn green front-pick-go">Watch it &raquo;</span></span></a>`
        : html`<a class="front-pick-body" href="#/game/${pick.g.id}">
          <span class="front-pick-pic">${pick.g.thumb ? html`<img src="/thumb/${encodeURIComponent(pick.g.id)}?v=${pick.g.thumb}" alt="">` : html`<span class="front-nopic">${pick.g.name}</span>`}</span>
          <span><b>${pick.g.name}</b><span class="small">A game by ${pick.g.creatorName || ''} &middot; ${plural(pick.g.plays || 0, 'visit')}</span>
            <span class="btn green front-pick-go">Play it &raquo;</span></span></a>`}
    </div>` : '';

  // Site-wide numbers: how much is on Guts&Bolts.
  const totals = [
    [data.games && data.games.total, 'game', '#/games?sort=new'],
    [data.videos && data.videos.total, 'video', '#/videos'],
    [data.artAll && data.artAll.total, 'art piece', '#/art'],
    [data.courses && data.courses.total, 'course', '#/academy'],
    [live.length, 'live now', '#/live'],
  ].filter(([n]) => typeof n === 'number');
  const stats = totals.length ? html`<div class="front-stats">${totals.map(([n, word, href]) =>
    html`<a href="${href}"><b>${count(n)}</b><span>${word === 'live now' ? word : word + (n === 1 ? '' : 's')}</span></a>`)}</div>` : '';

  const liveBox = live.length ? box(html`<span class="front-dot"></span> Live now`, '#/live', html`<div class="front-row">${live.slice(0, 4).map(liveCard)}</div>`, 'front-livebox') : '';
  const videoBox = videos.length ? box('New videos', '#/videos', html`<div class="front-row">${videos.map(videoCard)}</div>`) : '';
  const artBox = art.length ? box(data.art && list(data.art, 'pieces').length ? 'Best art this week' : 'New art', '#/art?sort=week', html`<div class="front-artgrid">${art.map(artCard)}</div>`) : '';
  const threadBox = threads.length ? box('Hot on the forums', '#/forum', html`<div class="front-list">${threads.map(threadRow)}</div>`) : '';
  const gameBox = games.length ? box('New games', '#/games?sort=new', html`<div class="grid">${games.map(gameCard)}</div>`, 'front-games') : '';
  const courseBox = courses.length ? box('New in the Academy', '#/academy', html`<div class="front-list">${courses.map(courseRow)}</div>`) : '';

  return html`${stats}${pickBox}${liveBox}${videoBox}<div class="front-cols"><div>${artBox}</div><div>${threadBox}${courseBox}</div></div>${gameBox}`;
}
