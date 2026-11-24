// The Leaderboards page (#/leaderboards?period=week|all). worker/leaderboards.js adds the lists up:
// top creators, most played games and most faved art, for the last 7 days or all time.
// app.js hands over its helpers with leaderboardPages(ctx), and adds the pages it returns.

const params = () => new URLSearchParams((location.hash.split('?')[1] || ''));
const num = (n) => Number(n || 0).toLocaleString();
const artThumb = (id) => '/artpic/' + encodeURIComponent(id) + '/thumb';

export function leaderboardPages(ctx) {
  const { html, raw, show, pageCall, signedIn, verified, avatarSvg, gameIcon } = ctx;
  const me = () => ctx.me();
  const person = (p) => html`<a href="#/user/${p.userId || p.id}">${p.name}</a>${verified(p.verified)}`;
  // 1, 2, 3 get a gold, silver and bronze medal; the rest just their place.
  const place = (i) => html`<span class="lb-place lb-place-${i + 1}">${i < 3 ? raw(['&#129351;', '&#129352;', '&#129353;'][i]) : i + 1}</span>`;
  const empty = (what) => html`<p class="muted small lb-empty">${what}</p>`;

  const creatorsBox = (list, week) => html`<div class="box lb-box"><h2 class="boxhead">Top creators</h2>
      <p class="small muted">Points: one for every visit to their games, one for every fave on their art${week ? ' (this week)' : ''}.</p>
      ${list.length ? html`<ol class="lb-list">${list.map((c, i) => html`<li class="lb-row${signedIn() && me() && me().id === c.id ? ' lb-me' : ''}">
          ${place(i)}<span class="lb-pic">${avatarSvg(c.avatar, 40, [], true)}</span>
          <span class="lb-name">${person(c)}<span class="small muted">${num(c.visits)} visits &middot; ${num(c.faves)} faves</span></span>
          <span class="lb-score"><b>${num(c.points)}</b><span class="small muted">points</span></span></li>`)}</ol>`
        : empty(week ? 'Nobody has had visits or faves this week yet. Make a game or post some art!' : 'Nothing here yet.')}</div>`;

  const gamesBox = (list, week) => html`<div class="box lb-box"><h2 class="boxhead">Most played games</h2>
      <p class="small muted">Visits from other people${week ? ' in the last 7 days' : ', ever'}.</p>
      ${list.length ? html`<ol class="lb-list">${list.map((g, i) => html`<li class="lb-row">
          ${place(i)}<a class="lb-pic" href="#/game/${g.id}">${gameIcon(g, 40)}</a>
          <span class="lb-name"><a href="#/game/${g.id}"><b>${g.name}</b></a><span class="small muted">by ${person(g.by)}</span></span>
          <span class="lb-score"><b>${num(g.visits)}</b><span class="small muted">visits</span></span></li>`)}</ol>`
        : empty(week ? 'No games have been played this week yet.' : 'Nothing here yet.')}</div>`;

  const artBox = (list, week) => html`<div class="box lb-box"><h2 class="boxhead">Most faved art</h2>
      <p class="small muted">${week ? 'Art posted in the last 7 days, by faves.' : 'All the art ever posted, by faves.'}</p>
      ${list.length ? html`<ol class="lb-list">${list.map((p, i) => html`<li class="lb-row">
          ${place(i)}<a class="lb-pic lb-art" href="#/artpiece/${p.id}"><img loading="lazy" src="${artThumb(p.id)}" alt="${p.title}"></a>
          <span class="lb-name"><a href="#/artpiece/${p.id}"><b>${p.title}</b></a><span class="small muted">by ${person(p.by)}</span></span>
          <span class="lb-score"><b>${num(p.faves)}</b><span class="small muted">faves</span></span></li>`)}</ol>`
        : empty(week ? 'No art has been faved this week yet. Go fave something you like!' : 'Nothing here yet.')}</div>`;

  const pages = {};
  pages.leaderboards = async () => {
    const period = params().get('period') === 'all' ? 'all' : 'week';
    const week = period === 'week';
    const r = await pageCall('leaders.get', { period });
    const tab = (p, label) => html`<a class="btn${p === period ? ' green' : ''}" href="#/leaderboards?period=${p}">${label}</a>`;
    const hero = html`<div class="portal-hero lb-hero"><div><span class="portal-kicker">WHO'S ON TOP</span><h1>Leaderboards</h1>
        <p>The creators, games and art people loved most on Guts&amp;Bolts. The weekly lists cover the last 7 days, so there's
          always a fresh chance to get on the board.</p>
        <p class="lb-tabs">${tab('week', 'This week')} ${tab('all', 'All time')}</p></div></div>`;
    if (!r.ok) { show(html`${hero}<p class="error">${r.error}</p>`); return; }
    const mine = signedIn() && me() ? html`<a href="#/user/${me().userId}">your profile's achievements</a>` : html`<a href="#/signup">sign up</a> and earn achievements`;
    show(html`${hero}
      <div class="lb-grid">${creatorsBox(r.creators, week)}${gamesBox(r.games, week)}${artBox(r.art, week)}</div>
      <div class="box"><h2 class="boxhead">How to get on the board</h2>
        <ul class="lb-how">
          <li><b>Make a game</b> in Studio and publish it. Every visit from someone else counts (your own plays don't).</li>
          <li><b>Post art</b> in the <a href="#/art">Art portal</a>. Every fave counts, so post the things you're proud of.</li>
          <li>Only public things count: private, friends-only and tester games, and anything still waiting for a staff check, stay off the lists.</li>
          <li>The lists update every few minutes. While you're at it, check ${mine}.</li>
        </ul>
        <p class="small muted">Updated ${new Date(r.at * 1000).toLocaleTimeString()}.</p></div>`);
  };
  return pages;
}
