// The Groups and People pages (worker/group-pages.js and server.js groupOp do the work):
//   #/groups?q=                    group cards with icons and banners, your groups, make a group
//   #/group/<id>/<tab>/<rank>/<page>   a group: banner, icon, shout, and tabs
//        tabs: about, news (announcements), members (pick a rank, see who has it), games, chat (Lyreto),
//              bolts (the group's Bolts) and settings
//   #/people?q=&show=              people as cards: avatar, badges, level, online or playing
// app.js hands over its helpers with groupPages(ctx).

// What each group permission lets a rank do (the server checks the same).
export const GROUP_PERMS = [['shout', 'Shout and post announcements'], ['manage', 'Let people in, remove people, delete posts, change settings and pictures'],
  ['ranks', 'Change people\'s ranks'], ['games', 'Add their games to the group'], ['funds', 'See the group\'s Bolts and pay people']];
const GROUP_TABS = [['about', 'About'], ['news', 'Announcements'], ['members', 'Members'], ['games', 'Games'], ['chat', 'Group chat']];
const PEOPLE_SHOW = [['', 'Everyone'], ['online', 'Online now'], ['verified', 'Verified'], ['club', 'Boltworks Club']];
const MEMBERS_PER_PAGE = 30;

const params = () => new URLSearchParams(location.hash.split('?')[1] || '');
const hexColor = (n) => '#' + Number(n || 0).toString(16).padStart(6, '0');
// Two letters for a group without an icon ("Guts & Bolts Official" -> "GB").
export function initials(name) {
  const words = String(name || '').split(/[^A-Za-z0-9]+/).filter(Boolean);
  const s = words.length > 1 ? words[0][0] + words[1][0] : (words[0] || '?').slice(0, 2);
  return s.toUpperCase();
}
// A darker shade of the group's colour, for the banner's gradient.
function shade(n, f) {
  const c = Number(n || 0);
  const ch = (v) => Math.max(0, Math.min(255, Math.round(v * f)));
  return 'rgb(' + ch((c >> 16) & 255) + ',' + ch((c >> 8) & 255) + ',' + ch(c & 255) + ')';
}
const decalUrl = (p) => '/decal/' + encodeURIComponent(p.id);

export function groupPages(ctx) {
  const { html, show, call, pageCall, signedIn, toast, verified, club, partner, avatarSvg, avatarPicture, lyretoCard,
    LYRETO_FIELD_NOTE, gameCard, pollBox, pollFields, reportLink, ago, bolts, addShareBox, render, view } = ctx;
  const me = () => ctx.me();

  // The group's square icon: its decal, or its initials on its colour.
  const groupIcon = (g, size = 'md') => g.icon
    ? html`<span class="gp-icon gp-${size}"><img src="${decalUrl(g.icon)}" alt="${g.name} icon" loading="lazy"></span>`
    : html`<span class="gp-icon gp-${size} gp-initials" style="background:linear-gradient(135deg, ${hexColor(g.color)}, ${shade(g.color, 0.55)})">${initials(g.name)}</span>`;
  const bannerStyle = (g) => g.banner
    ? "background:#222 center / cover no-repeat url('" + decalUrl(g.banner) + "')"
    : 'background:linear-gradient(120deg, ' + hexColor(g.color) + ', ' + shade(g.color, 0.45) + ')';

  // A group card for the lists: banner strip, icon, name, owner, members.
  const groupCard = (g) => html`<a class="gp-card" href="#/group/${g.id}">
    <div class="gp-card-banner" style="${bannerStyle(g)}"></div>
    <div class="gp-card-body">${groupIcon(g, 'md')}
      <div class="gp-card-text"><b class="gp-card-name">${g.name}</b>
        <span class="small muted">by ${g.ownerName}${verified(g.ownerVerified)}</span>
        <span class="gp-card-meta"><span class="gp-chip">&#128101; ${Number(g.members).toLocaleString()} ${g.members === 1 ? 'member' : 'members'}</span>
          ${g.open ? '' : html`<span class="gp-chip">&#128274; Ask to join</span>`}
          ${g.role ? html`<span class="gp-chip gp-chip-red">${g.role}</span>` : ''}</span></div></div>
    ${g.shout ? html`<div class="gp-card-shout small">&#128227; ${g.shout}</div>` : ''}</a>`;

  // A person card (People page, group members): avatar, badges, level, online.
  const personCard = (p, extra = '') => html`<div class="pp-card">
    <a class="pp-link" href="#/user/${p.userId}">
      <div class="pp-pic" data-pp-avatar="${p.userId}">${avatarSvg(p.avatar, 70)}
        <span class="pp-dot ${p.online ? 'on' : ''}" title="${p.online ? (p.playing ? 'Playing ' + p.playing : 'Online') : 'Offline'}"></span></div>
      <div class="pp-name"><b>${p.username || p.name}</b>${verified(p.verified)}${club(p)}</div>
      <div class="pp-sub small muted">#${p.userId}${p.role ? html` · <span class="pp-role">${p.role}</span>` : ''}</div>
      <div class="pp-chips"><span class="lv-pill">Lv ${p.level || 1}</span>
        ${p.official ? html`<span class="gp-chip gp-chip-red">Staff</span>` : ''}${p.partner ? partner(p) : ''}</div>
      <div class="pp-status small">${p.playing ? html`<span class="pp-playing">&#127918; ${p.playing}</span>` : p.online ? html`<span class="pp-online">Online</span>` : html`<span class="muted">Offline</span>`}</div>
    </a>${extra}</div>`;

  // Swap the flat avatar drawings for 3D pictures as cards scroll into sight.
  function upgradeAvatars(people) {
    const byId = new Map(people.map((p) => [String(p.userId), p]));
    const look = (box) => {
      const p = byId.get(box.dataset.ppAvatar);
      if (!p || box.dataset.done) return;
      box.dataset.done = '1';
      avatarPicture(p.avatar, p.wearing || [], 90).then((url) => {
        const svg = box.querySelector('svg');
        if (url && svg && box.isConnected) svg.outerHTML = html`<img src="${url}" alt="${(p.username || p.name) + '\'s avatar'}">`.s;
      }).catch(() => {});
    };
    const boxes = view.querySelectorAll('[data-pp-avatar]');
    if (typeof IntersectionObserver === 'undefined') { boxes.forEach(look); return; }
    const io = new IntersectionObserver((seen) => seen.forEach((e) => { if (e.isIntersecting) { io.unobserve(e.target); look(e.target); } }), { rootMargin: '200px' });
    boxes.forEach((b) => io.observe(b));
  }

  // ---------- #/groups ----------
  async function groupsPage() {
    const query = params().get('q') || '';
    const [r, mine] = await Promise.all([pageCall('groups.list', { query }), signedIn() ? pageCall('groups.mine', {}) : { ok: false }]);
    const m = me();
    show(html`<div class="portal-hero gp-hero"><div><span class="portal-kicker">G&amp;B COMMUNITIES</span><h1>Groups</h1>
        <p>Find your people. Groups have their own icon and banner, a shout, announcements, ranks, games and a group chat on Lyreto.</p></div>
        <a class="btn green big" href="#/groups" data-gp-make>Make a group</a></div>
      <form class="gp-search" data-form="groupSearch"><input type="search" name="q" placeholder="Search groups" value="${query}">
        <button class="btn blue">Search</button></form>
      ${mine.ok && mine.groups.length && !query ? html`<h2 class="gp-h2">My groups</h2><div class="gp-grid">${mine.groups.map(groupCard)}</div>` : ''}
      <h2 class="gp-h2">${query ? 'Groups matching "' + query + '"' : 'Popular groups'}</h2>
      ${r.ok ? (r.groups.length ? html`<div class="gp-grid">${r.groups.map(groupCard)}</div>` : html`<p class="muted">No groups found. Make one!</p>`)
        : html`<p class="error">${r.error}</p>`}
      <h2 class="gp-h2" id="make">Make a group</h2>
      <div class="box gp-make">${signedIn() ? html`<form class="form" data-form="groupCreate">
        <div class="gp-make-preview">${groupIcon({ name: 'My group', color: 0x3a7bd5 }, 'lg')}<div><b data-gp-preview-name>My group</b>
          <p class="small muted">You can add an icon and a banner from the group's Settings once it's made.</p></div></div>
        <label>Name</label><input type="text" name="name" maxlength="40" required data-gp-name>
        <label>Description</label><textarea name="description" maxlength="1000" placeholder="What is your group about?"></textarea>
        <label>Colour</label><input type="color" name="color" value="#3a7bd5" data-gp-color>
        <label><input type="checkbox" name="open" checked> Anyone can join (untick: people ask to join)</label>
        <p><button class="btn green">Make it${m.verified ? ' (free)' : ' for 50 Bolts'}</button></p></form>`
      : html`<form class="form" data-form="groupCreate"><label>Name</label><input type="text" name="name" maxlength="40">
        <p><button class="btn green">Make a group</button></p></form>`}</div>`);
    // The little preview follows the name and colour you type.
    const nameIn = view.querySelector('[data-gp-name]'), colIn = view.querySelector('[data-gp-color]');
    const prev = view.querySelector('.gp-make-preview .gp-icon');
    const redraw = () => {
      if (!prev) return;
      const name = (nameIn && nameIn.value.trim()) || 'My group', c = parseInt((colIn ? colIn.value : '#3a7bd5').slice(1), 16);
      prev.textContent = initials(name);
      prev.style.background = 'linear-gradient(135deg, ' + hexColor(c) + ', ' + shade(c, 0.55) + ')';
      view.querySelector('[data-gp-preview-name]').textContent = name;
    };
    if (nameIn) nameIn.addEventListener('input', redraw);
    if (colIn) colIn.addEventListener('input', redraw);
    const make = view.querySelector('[data-gp-make]');
    if (make) make.addEventListener('click', (e) => { e.preventDefault(); document.getElementById('make').scrollIntoView({ behavior: 'smooth' }); if (nameIn) nameIn.focus(); });
  }

  // ---------- #/group/<id>/<tab>/<rank>/<page> ----------
  async function groupPage(id, tabArg, rankArg, pageArg) {
    const r = await pageCall('groups.get', { id });
    if (!r.ok) { show(html`<h1>Group not found</h1><p class="muted">${r.error}</p>`); return; }
    const m = me();
    const g = r.group, role = r.myRole, mine = r.myRank, owner = !!mine && mine.id === 'Owner';
    const can = (perm) => !!mine && (owner || mine.perms.includes(perm));
    const myLevel = mine ? mine.level : 0;
    const tabs = GROUP_TABS.slice();
    if (r.funds !== undefined) tabs.push(['bolts', 'Group Bolts']);
    if (can('manage') || owner) tabs.push(['settings', 'Settings']);
    const tab = tabs.some(([k]) => k === tabArg) ? tabArg : 'about';
    const base = '#/group/' + g.id;
    const counts = r.rankCounts || {};
    const news = r.news || [];

    const join = !signedIn() ? html`<button class="btn green" data-act="group" data-op="groups.join" data-id="${g.id}">${g.open ? 'Join group' : 'Ask to join'}</button>`
      : role ? (owner ? html`<span class="gp-chip gp-chip-red">You own this group</span>` : html`<button class="btn small" data-act="group" data-op="groups.leave" data-id="${g.id}">Leave group</button>`)
        : r.requested ? html`<span class="gp-chip">You asked to join</span>`
          : html`<button class="btn green" data-act="group" data-op="groups.join" data-id="${g.id}">${g.open ? 'Join group' : 'Ask to join'}</button>`;

    const shout = r.shoutInfo && r.shoutInfo.text ? html`<div class="gp-shout"><div class="gp-shout-head"><span class="gp-shout-label">&#128227; Group shout</span>
        <span class="small muted">${ago(r.shoutInfo.time)}</span></div>
        <p class="gp-shout-text">${r.shoutInfo.text}</p>
        <span class="small">— <a href="#/user/${r.shoutInfo.id}">${r.shoutInfo.name}</a>${verified(r.shoutInfo.verified)}</span></div>` : '';
    const shoutForm = can('shout') ? html`<form class="gp-shout-form" data-form="groupShout"><input type="hidden" name="id" value="${g.id}">
      <input type="text" name="text" maxlength="200" placeholder="Shout something to the whole group">
      <button class="btn blue">Shout</button></form>` : '';

    const head = html`<div class="gp-head"><div class="gp-banner" style="${bannerStyle(g)}"></div>
      <div class="gp-head-row">${groupIcon(g, 'xl')}
        <div class="gp-head-text"><h1>${g.name}</h1>
          <p class="small">By <a href="#/user/${g.owner}">${g.ownerName}</a>${verified(g.ownerVerified)}
            · <b>${Number(g.members).toLocaleString()}</b> ${g.members === 1 ? 'member' : 'members'}
            ${g.open ? '' : ' · ask to join'}${role ? html` · your rank: <b>${role}</b>` : ''}</p></div>
        <div class="gp-head-act">${join}${owner ? '' : html` ${reportLink('group', g.id, g.name)}`}</div></div>
      <nav class="gp-tabs">${tabs.map(([k, label]) => html`<a class="${k === tab ? 'on' : ''}" href="${base}/${k}">${label}${k === 'news' && news.length ? html` <span class="gp-count">${news.length}</span>` : ''}${k === 'members' ? html` <span class="gp-count">${g.members}</span>` : ''}</a>`)}</nav></div>`;

    let body = '';
    if (tab === 'about') body = aboutTab();
    else if (tab === 'news') body = newsTab();
    else if (tab === 'members') body = await membersTab();
    else if (tab === 'games') body = gamesTab(true);
    else if (tab === 'chat') body = chatTab();
    else if (tab === 'bolts') body = boltsTab();
    else if (tab === 'settings') body = await settingsTab();
    show(html`<p class="gp-back"><a href="#/groups">&lt; Groups</a></p>${head}<div class="gp-body">${body}</div>`);
    wire();
    if (owner && tab === 'settings') addShareBox(view, 'group', g.id, call, toast);   // share links (sharelinks.js)

    function newsItem(n, full) {
      const canDelete = signedIn() && (n.by === m.id || can('manage'));
      return html`<article class="gp-news"><div class="gp-news-head"><b class="gp-news-title">${n.title}</b>
          <span class="small muted">${ago(n.time)}</span></div>
        <p class="gp-news-text${full ? '' : ' gp-clip'}">${n.text}</p>
        <div class="small muted">Posted by <a href="#/user/${n.by}">${n.name}</a>${verified(n.verified)}
          ${canDelete ? html` · <button type="button" class="linkish" data-gp-unnews="${n.id}">Delete</button>` : ''}</div></article>`;
    }
    function aboutTab() {
      const games = r.games.slice(0, 4);
      return html`<div class="gp-cols"><div class="gp-main">
          ${shout}${shoutForm}
          <section class="box"><h2 class="gp-h3">About</h2>
            ${g.description ? html`<p class="gp-desc">${g.description}</p>` : html`<p class="muted">No description yet.</p>`}</section>
          ${news.length ? html`<section><h2 class="gp-h3">Latest announcement <a class="small" href="${base}/news">See all (${news.length})</a></h2>${newsItem(news[0], false)}</section>` : ''}
          ${games.length ? html`<section><h2 class="gp-h3">Games <a class="small" href="${base}/games">See all (${r.games.length})</a></h2>
            <div class="grid">${games.map((x) => gameCard(x))}</div></section>` : ''}
          <section><h2 class="gp-h3">Wall</h2>
          ${role ? html`<form class="gp-post" data-form="groupPost"><input type="hidden" name="id" value="${g.id}">
            <input type="text" name="text" maxlength="300" placeholder="Say something to the group">
            <button class="btn blue">Post</button>${pollFields()}</form>` : signedIn() ? html`<p class="small muted">Join the group to post on its wall.</p>` : ''}
          <div class="wall gp-wall">${r.wall.length ? r.wall.slice().reverse().map((p) => html`<div><a href="#/user/${p.id}"><b>${p.name}</b></a>${verified(p.verified)}
            <span class="small muted">${ago(p.time)}</span>
            ${signedIn() && (p.id === m.id || can('manage')) ? html`<button class="btn small" data-act="group" data-op="groups.deletePost" data-id="${g.id}"
              data-by="${p.id}" data-time="${p.time}">Delete</button>` : ''}<br>${p.text}
            ${p.poll ? pollBox(p.poll, { group: g.id, by: p.id, time: p.time }, !!role) : ''}</div>`) : html`<p class="muted">Nothing on the wall yet.</p>`}</div></section>
        </div>
        <aside class="gp-side">
          <div class="box"><h3 class="gp-h3">Ranks</h3><ul class="gp-ranklist">${r.ranks.map((x) => html`<li><a href="${base}/members/${x.id}">${x.name}</a>
            <span class="gp-chip">${counts[x.id] || 0}</span></li>`)}</ul></div>
          ${g.lyreto ? lyretoCard(g.lyreto, g.name) : html`<div class="box"><h3 class="gp-h3">Group chat</h3>
            <p class="small muted">${can('manage') ? 'Give your group a chat room on Lyreto.' : 'This group has no group chat yet.'}</p>
            ${can('manage') ? html`<a class="btn small" href="${base}/chat">Set it up</a>` : ''}</div>`}
          <div class="box small muted">Made ${new Date(g.created * 1000).toLocaleDateString(undefined, { year: 'numeric', month: 'long', day: 'numeric' })}</div>
        </aside></div>`;
    }
    function newsTab() {
      return html`${can('shout') ? html`<form class="box form gp-news-form" data-gp-news><b>New announcement</b>
          <input type="text" name="title" maxlength="80" placeholder="Title" required>
          <textarea name="text" maxlength="2000" rows="4" placeholder="What do you want everyone in the group to know?" required></textarea>
          <p class="small muted">Everyone in the group gets a notification.</p>
          <p><button class="btn blue">Post announcement</button></p></form>` : ''}
        ${news.length ? news.map((n) => newsItem(n, true)) : html`<p class="muted">No announcements yet.</p>`}`;
    }
    async function membersTab() {
      const rank = r.ranks.some((x) => x.id === rankArg) ? rankArg : '';
      const page = Math.max(1, Number(pageArg) || 1);
      const list = await pageCall('groups.members', { id: g.id, rank, offset: (page - 1) * MEMBERS_PER_PAGE, limit: MEMBERS_PER_PAGE });
      const giveable = r.ranks.filter((x) => x.id !== 'Owner' && (owner || x.level < myLevel));
      const tools = (u) => {
        const below = u.rankLevel < myLevel && u.id !== m.id && u.rank !== 'Owner';
        const bits = [];
        if (can('ranks') && below) bits.push(html`<form class="gp-rankpick" data-form="groupRank"><input type="hidden" name="id" value="${g.id}">
          <input type="hidden" name="user" value="${u.id}"><select name="rank" aria-label="Rank">${giveable.map((x) => html`<option value="${x.id}" ${x.id === u.rank ? 'selected' : ''}>${x.name}</option>`)}</select>
          <button class="btn small">Set</button></form>`);
        if (can('manage') && below) bits.push(html`<button class="btn small red" data-act="group" data-op="groups.member" data-id="${g.id}" data-user="${u.id}" data-action="kick"
          data-confirm="Remove ${u.name} from ${g.name}?">Remove</button>`);
        if (owner && u.rank !== 'Owner') bits.push(html`<button class="btn small" data-act="group" data-op="groups.member" data-id="${g.id}" data-user="${u.id}" data-action="owner"
          data-confirm="Make ${u.name} the owner of ${g.name}? You can't undo this yourself.">Make owner</button>`);
        return bits.length ? html`<div class="pp-tools">${bits}</div>` : '';
      };
      const pages = list.ok ? Math.max(1, Math.ceil(list.total / MEMBERS_PER_PAGE)) : 1;
      const at = (p) => base + '/members/' + (rank || 'all') + '/' + p;
      ctx.lastPeople = list.ok ? list.people : [];
      return html`${can('manage') && r.requests && r.requests.length ? html`<div class="box gp-requests"><h3 class="gp-h3">Waiting to join (${r.requests.length})</h3>
          <div class="list">${r.requests.map((u) => html`<div><a class="grow" href="#/user/${u.id}">${u.name}</a>${verified(u.verified)}
            <button class="btn green small" data-act="group" data-op="groups.request" data-id="${g.id}" data-user="${u.id}" data-accept="1">Let in</button>
            <button class="btn small" data-act="group" data-op="groups.request" data-id="${g.id}" data-user="${u.id}">Decline</button></div>`)}</div></div>` : ''}
        <div class="gp-members-bar"><h2 class="gp-h3">Members</h2>
          <label class="gp-rank-select"><span class="small muted">Rank</span>
            <select data-go aria-label="Show the members with this rank">
              <option value="${base}/members/all" ${rank ? '' : 'selected'}>Everyone (${g.members})</option>
              ${r.ranks.map((x) => html`<option value="${base}/members/${x.id}" ${x.id === rank ? 'selected' : ''}>${x.name} (${counts[x.id] || 0})</option>`)}
            </select></label></div>
        ${!list.ok ? html`<p class="error">${list.error}</p>` : list.people.length
          ? html`<div class="pp-grid">${list.people.map((u) => personCard(u, tools(u)))}</div>`
          : html`<p class="muted">Nobody has this rank yet.</p>`}
        ${pages > 1 ? html`<p class="gp-pager">${page > 1 ? html`<a class="btn small" href="${at(page - 1)}">&lt; Back</a>` : ''}
          <span class="small muted">Page ${page} of ${pages}</span>${page < pages ? html`<a class="btn small" href="${at(page + 1)}">Next &gt;</a>` : ''}</p>` : ''}
        ${owner ? html`<h2 class="gp-h3">Edit ranks</h2><p class="small muted">Higher levels are more in charge. People can only change the rank of people below them.
          A group can have up to 10 ranks.</p>
          <div class="ranks gp-ranks">${r.ranks.map(rankForm)}${r.ranks.length < 10 ? html`<h3>New rank</h3>${rankForm(null)}` : ''}</div>` : ''}`;
    }
    function rankForm(x) {
      return html`<form class="form rank-edit" data-form="groupRankSave"><input type="hidden" name="id" value="${g.id}">
        <input type="hidden" name="rank" value="${x ? x.id : ''}">
        <div class="row"><input type="text" name="name" maxlength="24" placeholder="Rank name" value="${x ? x.name : ''}" required style="max-width:200px">
          ${x && (x.id === 'Owner' || x.id === 'Member') ? html`<span class="muted small">level ${x.level}</span><input type="hidden" name="level" value="${x.level}">`
            : html`<label class="small">Level <input type="number" name="level" min="2" max="254" value="${x ? x.level : ''}" required style="width:70px"></label>`}
          ${x ? html`<span class="gp-chip">${counts[x.id] || 0} ${counts[x.id] === 1 ? 'person' : 'people'}</span>` : ''}</div>
        ${x && x.id === 'Owner' ? html`<p class="small muted">The owner can do everything.</p>`
          : html`<div class="perm-picks">${GROUP_PERMS.map(([k, label]) => html`<label class="choice small"><input type="checkbox" name="perm" value="${k}" ${x && x.perms.includes(k) ? 'checked' : ''}> ${label}</label>`)}</div>`}
        <p><button class="btn small blue">${x ? 'Save' : 'Add rank'}</button>
          ${x && x.id !== 'Owner' && x.id !== 'Member' ? html` <button type="button" class="btn small red" data-act="group" data-op="groups.rankDelete" data-id="${g.id}" data-rank="${x.id}"
            data-confirm="Delete the ${x.name} rank? Everyone in it becomes a ${r.ranks.find((y) => y.id === 'Member').name}.">Delete</button>` : ''}</p></form>`;
    }
    function gamesTab() {
      return html`${r.games.length ? html`<div class="grid">${r.games.map((x) => html`<div class="group-game">${gameCard(x)}
          ${owner || (can('games') && x.creator === m.id) ? html`<button class="btn small" data-act="group" data-op="groups.removeGame" data-id="${g.id}" data-game="${x.id}"
            data-confirm="Take ${x.name} out of ${g.name}? Its sales go back to its creator.">Take out</button>` : ''}</div>`)}</div>`
        : html`<p class="muted">No games yet.</p>`}
        ${can('games') ? html`<p class="small muted">To add one of your games, open its Configure page and pick this group under "Group". Its game pass and product sales then go to the group's Bolts.</p>` : ''}`;
    }
    function chatTab() {
      return html`<div class="gp-chat">
        ${g.lyreto ? lyretoCard(g.lyreto, g.name) : html`<div class="box gp-chat-empty"><div class="lyreto-pic lyreto-mark">L</div>
          <div><b>${g.name} has no group chat yet</b><p class="small muted">Group chats live on Lyreto, the chat app Guts&amp;Bolts partners with: text channels, voice and more, free.</p></div></div>`}
        ${can('manage') ? html`<div class="box"><h3 class="gp-h3">${g.lyreto ? 'Change the group chat' : 'Make a group chat'}</h3>
          <ol class="small gp-steps"><li>Make a server on <a href="https://lyreto.com/" target="_blank" rel="noopener noreferrer">Lyreto</a> for your group (it's free).</li>
            <li>In Lyreto, copy your server's invite link.</li><li>Paste it here and save. Everyone in the group sees a "Join on Lyreto" button.</li></ol>
          <form class="form" data-gp-lyreto><input type="url" name="lyreto" maxlength="200" placeholder="https://lyreto.com/..." value="${g.lyreto ? g.lyreto.link : ''}">
            <p class="small muted">${LYRETO_FIELD_NOTE}</p><p><button class="btn blue">Save</button></p></form></div>` : ''}</div>`;
    }
    function boltsTab() {
      return html`<div class="box">
        <p class="big-bolts">${bolts(r.funds)}</p>
        <p class="small muted">Sales from the group's games go here. You pay members from it.</p>
        ${can('funds') ? html`<form class="row" data-form="groupPayout"><input type="hidden" name="id" value="${g.id}">
          <select name="user">${r.memberList.map((u) => html`<option value="${u.id}">${u.name}</option>`)}</select>
          <input type="number" name="amount" min="1" max="${Math.max(1, r.funds)}" placeholder="Bolts" required style="width:100px">
          <button class="btn green" ${r.funds > 0 ? '' : 'disabled'}>Pay</button></form>` : ''}
        ${r.ledger.length ? html`<table class="ledger">${r.ledger.map((e) => html`<tr><td class="${e.amount < 0 ? 'neg' : 'pos'}">${e.amount > 0 ? '+' : ''}${e.amount}</td>
          <td>${e.reason}</td><td class="muted small">${ago(e.at)}</td></tr>`)}</table>` : html`<p class="muted small">Nothing yet.</p>`}</div>`;
    }
    async function settingsTab() {
      // Your own decals, to pick from with one click.
      const inv = signedIn() ? await pageCall('profile.inventory', { id: String(m.userId), cat: 'decals', limit: 24 }) : { ok: false };
      const decals = inv.ok ? inv.items.filter((a) => a.kind === 'decal') : [];
      const pickRow = (slot) => decals.length ? html`<div class="gp-decal-picks">${decals.map((a) => html`<button type="button" class="gp-decal" data-gp-pick="${slot}" data-decal-id="${a.id}" title="${a.name}">
          <img src="/decal/${encodeURIComponent(a.id)}" alt="${a.name}" loading="lazy"></button>`)}</div>` : '';
      return html`<div class="box"><h3 class="gp-h3">Icon and banner</h3>
          <p class="small muted">Use any decal from the <a href="#/create/library?cat=decal">Library</a>: click one of yours below, or paste a decal's number or ID.
            Want a new picture? <a href="#/create/decal">Upload a decal</a> first (a square one for the icon, a wide one like 1200 × 300 for the banner).
            New decals from people who aren't Verified show once staff have checked them.</p>
          <form class="form gp-pics" data-gp-pics>
            <div class="gp-pic-slot"><div class="gp-pic-preview">${groupIcon(g, 'lg')}</div><div class="grow"><label>Icon</label>
              <div class="row"><input type="text" name="icon" maxlength="80" placeholder="Decal number or ID" value="${g.icon ? g.icon.num || g.icon.id : ''}">
              ${g.icon ? html`<button type="button" class="btn small" data-gp-clear="icon">Take it off</button>` : ''}</div>${pickRow('icon')}</div></div>
            <div class="gp-pic-slot"><div class="gp-pic-preview"><div class="gp-banner-mini" style="${bannerStyle(g)}"></div></div><div class="grow"><label>Banner</label>
              <div class="row"><input type="text" name="banner" maxlength="80" placeholder="Decal number or ID" value="${g.banner ? g.banner.num || g.banner.id : ''}">
              ${g.banner ? html`<button type="button" class="btn small" data-gp-clear="banner">Take it off</button>` : ''}</div>${pickRow('banner')}</div></div>
            <p><button class="btn blue">Save pictures</button></p></form></div>
        <div class="box"><h3 class="gp-h3">About the group</h3><form class="form" data-form="groupEdit"><input type="hidden" name="id" value="${g.id}">
          <label>Description</label><textarea name="description" maxlength="1000">${g.description}</textarea>
          <label>Colour</label><input type="color" name="color" value="${hexColor(g.color)}">
          <input type="hidden" name="lyreto" value="${g.lyreto ? g.lyreto.link : ''}">
          <label><input type="checkbox" name="open" ${g.open ? 'checked' : ''}> Anyone can join</label>
          <p><button class="btn blue">Save</button>
          ${owner ? html` <button type="button" class="btn red" data-act="group" data-op="groups.delete" data-id="${g.id}">Delete group</button>` : ''}</p></form></div>`;
    }

    function wire() {
      if (tab === 'members') upgradeAvatars(ctx.lastPeople || []);
      const newsForm = view.querySelector('[data-gp-news]');
      if (newsForm) newsForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const f = e.currentTarget;
        const res = await call('groups.announce', { id: g.id, title: f.title.value, text: f.text.value });
        if (!res.ok) { toast(res.error); return; }
        toast('Announcement posted.');
        render();
      });
      view.querySelectorAll('[data-gp-unnews]').forEach((b) => b.addEventListener('click', async () => {
        if (!confirm('Delete this announcement?')) return;
        const res = await call('groups.announceDelete', { id: g.id, post: b.dataset.gpUnnews });
        if (!res.ok) { toast(res.error); return; }
        render();
      }));
      const ly = view.querySelector('[data-gp-lyreto]');
      if (ly) ly.addEventListener('submit', async (e) => {
        e.preventDefault();
        const res = await call('groups.edit', { id: g.id, lyreto: e.currentTarget.lyreto.value.trim() });
        toast(res.ok ? 'Saved. Lyreto\'s card shows in a moment.' : res.error);
        if (res.ok) render();
      });
      const pics = view.querySelector('[data-gp-pics]');
      if (pics) {
        const save = async (args) => {
          const res = await call('groups.pictures', Object.assign({ id: g.id }, args));
          if (!res.ok) { toast(res.error); return; }
          toast(res.waiting ? 'Saved. It shows once staff have checked the decal.' : 'Pictures saved.');
          render();
        };
        pics.addEventListener('submit', (e) => { e.preventDefault(); save({ icon: pics.icon.value.trim(), banner: pics.banner.value.trim() }); });
        pics.querySelectorAll('[data-gp-clear]').forEach((b) => b.addEventListener('click', () => save({ [b.dataset.gpClear]: '' })));
        pics.querySelectorAll('.gp-decal img').forEach((img) => img.addEventListener('error', () => img.closest('button').classList.add('waiting')));
        pics.querySelectorAll('[data-gp-pick]').forEach((b) => b.addEventListener('click', () => {
          pics[b.dataset.gpPick].value = b.dataset.decalId;
          pics.querySelectorAll('[data-gp-pick="' + b.dataset.gpPick + '"]').forEach((x) => x.classList.toggle('on', x === b));
        }));
      }
    }
  }

  // ---------- #/people ----------
  async function peoplePage() {
    const q = params().get('q') || '', showWho = PEOPLE_SHOW.some(([k]) => k === params().get('show')) ? params().get('show') : '';
    const r = await pageCall('users.search', { query: q, show: showWho });
    const link = (k) => '#/people?' + new URLSearchParams(Object.assign(q ? { q } : {}, k ? { show: k } : {}));
    const people = r.ok ? r.users : [];
    const online = people.filter((p) => p.online).length;
    show(html`<div class="portal-hero pp-hero"><div><span class="portal-kicker">G&amp;B COMMUNITIES</span><h1>People</h1>
        <p>Find friends, creators and the Guts&amp;Bolts team. Click someone to see their profile, add them as a friend or join their game.</p></div></div>
      <form class="gp-search" data-form="peopleSearch"><input type="search" name="q" placeholder="Search by username or #number" value="${q}">
        <button class="btn blue">Search</button></form>
      <nav class="gp-pills">${PEOPLE_SHOW.map(([k, label]) => html`<a class="${k === showWho ? 'on' : ''}" href="${link(k)}">${label}</a>`)}</nav>
      ${r.ok ? (people.length ? html`<p class="small muted">${people.length} ${people.length === 1 ? 'person' : 'people'}${online && showWho !== 'online' ? ' · ' + online + ' online now' : ''}${people.length >= 50 ? ' (the first 50: search to narrow it down)' : ''}</p>
          <div class="pp-grid">${people.map((p) => personCard(p))}</div>`
        : html`<p class="muted">${showWho === 'online' ? 'Nobody you can see is online right now.' : 'Nobody found.'}</p>`)
        : html`<p class="error">${r.error}</p>`}`);
    upgradeAvatars(people);
  }

  return { groups: groupsPage, group: groupPage, people: peoplePage };
}
