// Press's pages (worker/press.js does the work on the server):
//   #/press?kind=&page=      the latest posts (kind: news, spotlight or devlog)
//   #/pressview/<id>         one post: the text, the creator it's about and their games
//   #/presswrite?id=<id>     staff: write a new post, or change one (id=)
// app.js hands over its helpers with pressPages(ctx), and adds the pages it returns.

export const PRESS_KINDS = { news: 'News', spotlight: 'Creator spotlight', devlog: 'Behind the scenes' };   // (worker/press.js)

const params = () => new URLSearchParams((location.hash.split('?')[1] || ''));
const day = (sec) => new Date(sec * 1000).toLocaleDateString(undefined, { year: 'numeric', month: 'long', day: 'numeric' });

export function pressPages(ctx) {
  const { html, show, call, pageCall, signedIn, toast, verified, gameCard } = ctx;
  const me = () => ctx.me();
  const person = (p) => html`<a href="#/user/${p.userId || p.id}">${p.name}</a>${verified(p.verified)}`;
  const pic = (p) => (p.picture ? html`<img class="press-pic" src="/thumb/${encodeURIComponent(p.picture)}" alt="" loading="lazy">`
    : html`<span class="press-pic press-nopic press-${p.kind}" aria-hidden="true">${PRESS_KINDS[p.kind] || 'News'}</span>`);

  const hero = (canWrite) => html`<div class="portal-hero press-hero"><div><span class="portal-kicker">G&amp;B PRESS</span><h1>Press</h1>
      <p>News from the Guts&amp;Bolts team, creator spotlights on the people making games and art, and stories from behind the scenes.
        Want to see what's coming up? Look at the <a href="#/calendar">calendar</a>.</p>
      ${canWrite ? html`<p><a class="btn green" href="#/presswrite">Write a post</a> <a class="btn" href="#/press?drafts=1">Drafts</a></p>` : ''}</div></div>`;

  const card = (p, big) => html`<a class="press-card ${big ? 'big' : ''}" href="#/pressview/${p.id}">${pic(p)}
      <span class="press-card-text"><span class="press-kind">${p.kindName}</span><b>${p.title}</b>
        <span class="press-sum">${p.summary}</span>
        <span class="small muted">${day(p.at)} &middot; ${p.minutes} min read${p.published ? '' : ' · Draft'}</span></span></a>`;

  const pages = {};

  pages.press = async () => {
    const q = params();
    const args = { kind: q.get('kind') || '', page: Number(q.get('page') || 1), drafts: q.get('drafts') === '1' };
    const r = await pageCall('press.list', args);
    if (!r.ok) { show(html`${hero(false)}<div class="box"><p class="error">${r.error}</p></div>`); return; }
    const link = (page) => '#/press?' + new URLSearchParams(Object.assign(args.kind ? { kind: args.kind } : {}, { page: String(page) })).toString();
    const tabs = html`<div class="art-cats"><a class="${args.kind ? '' : 'on'}" href="#/press">Latest</a>${Object.entries(PRESS_KINDS).map(([k, n]) =>
      html`<a class="${args.kind === k ? 'on' : ''}" href="#/press?kind=${k}">${n}</a>`)}</div>`;
    const [first, ...rest] = r.posts;
    show(html`${hero(r.canWrite)}<div class="box"><h2 class="boxhead">${args.drafts ? 'Drafts' : args.kind ? PRESS_KINDS[args.kind] : 'Latest from Guts&Bolts'}</h2>
      ${tabs}
      ${first ? html`${args.page === 1 ? card(first, true) : ''}<div class="press-grid">${(args.page === 1 ? rest : r.posts).map((p) => card(p, false))}</div>`
        : html`<p class="muted">Nothing here yet. Check back soon!</p>`}
      <p class="art-pages">${r.page > 1 ? html`<a class="btn small" href="${link(r.page - 1)}">&laquo; Newer</a> ` : ''}
        ${r.more ? html`<a class="btn small" href="${link(r.page + 1)}">Older &raquo;</a>` : ''}</p></div>`);
  };

  pages.pressview = async (id) => {
    const r = await pageCall('press.get', { id });
    if (!r.ok) { show(html`<h1>Press</h1><p class="error">${r.error}</p><p><a href="#/press">&laquo; Back to Press</a></p>`); return; }
    const p = r.post;
    show(html`<p class="small"><a href="#/press">&laquo; Press</a></p>
      <article class="box press-post">
        <p class="press-kind">${p.kindName}${p.published ? '' : ' · Draft (only staff can see it)'}</p>
        <h1>${p.title}</h1>
        <p class="small muted">${day(p.at)} &middot; ${p.minutes} min read &middot; ${p.views} views${p.edited ? ' · updated ' + day(p.edited) : ''}</p>
        ${p.summary ? html`<p class="press-lead">${p.summary}</p>` : ''}
        ${p.blocks.map((b) => (b.h ? html`<h2>${b.h}</h2>` : html`<p class="press-para">${b.p}</p>`))}
        ${p.creator ? html`<div class="press-creator">This spotlight is about ${person(p.creator)}.
          <a class="btn small" href="#/user/${p.creator.userId}">See their profile</a></div>` : ''}
        ${p.games.length ? html`<h2>Games in this post</h2><div class="grid">${p.games.map(gameCard)}</div>` : ''}
        <p class="press-tools"><button class="btn small" data-press="link">Copy link</button>
          ${p.canEdit ? html`<a class="btn small" href="#/presswrite?id=${p.id}">Change it</a> <button class="btn small red" data-press="delete">Delete</button>` : ''}</p>
      </article>`);
    document.querySelectorAll('[data-press]').forEach((b) => b.addEventListener('click', async (e) => {
      e.preventDefault();
      if (b.dataset.press === 'link') {
        try { await navigator.clipboard.writeText(location.origin + '/press/' + p.id); toast('Copied the link.'); }
        catch { toast('Your browser didn\'t let us copy.'); }
      } else if (b.dataset.press === 'delete') {
        if (!confirm('Delete "' + p.title + '" for good?')) return;
        const done = await call('press.delete', { id: p.id });
        if (!done.ok) { toast(done.error); return; }
        toast('Deleted.');
        location.hash = '#/press';
      }
    }));
  };

  pages.presswrite = async () => {
    if (!signedIn() || !(me() && me().staff)) { show(html`<h1>Write a Press post</h1><p class="muted">Only Guts&amp;Bolts staff write Press posts.</p>`); return; }
    const id = params().get('id') || '';
    let old = null;
    if (id) { const r = await pageCall('press.get', { id }); if (r.ok) old = r.post; }
    const v = (k, d = '') => (old ? old[k] : d);
    show(html`<div class="box"><h2 class="boxhead">${old ? 'Change "' + old.title + '"' : 'Write a Press post'}</h2>
      <p class="small muted">Leave an empty line between paragraphs. Start a line with "## " for a small heading.
        Spotlights can name a creator (their number or @name): they get a notification when it's published.</p>
      <form class="form" data-press-write>
        <p><label>Title<br><input type="text" name="title" maxlength="120" value="${v('title')}" required style="width:100%"></label></p>
        <p><label>Summary <span class="small muted">(one line; the first paragraph if empty)</span><br><input type="text" name="summary" maxlength="300" value="${v('summary')}" style="width:100%"></label></p>
        <div class="row paste-options">
          <label>Kind<br><select name="kind">${Object.entries(PRESS_KINDS).map(([k, n]) => html`<option value="${k}" ${v('kind', 'news') === k ? 'selected' : ''}>${n}</option>`)}</select></label>
          <label>Creator <span class="small muted">(optional)</span><br><input type="text" name="creator" value="${old && old.creator ? old.creator.userId : ''}" placeholder="12 or @name"></label>
          <label>Game IDs <span class="small muted">(optional, up to 6)</span><br><input type="text" name="games" value="${old ? old.gameIds.join(' ') : ''}" placeholder="1012 1044"></label>
        </div>
        <p><textarea name="text" rows="18" style="width:100%" required>${v('text')}</textarea></p>
        <p><label><input type="checkbox" name="draft" ${old && !old.published ? 'checked' : ''}> Keep it as a draft (only staff see it)</label></p>
        <p><button class="btn green">${old ? 'Save' : 'Publish'}</button> <span id="pressMsg" class="small"></span></p>
      </form></div>`);
    const form = document.querySelector('[data-press-write]');
    const msg = document.getElementById('pressMsg');
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const btn = form.querySelector('button');
      btn.disabled = true; msg.className = 'small'; msg.textContent = 'Saving...';
      const args = { title: form.title.value, summary: form.summary.value, kind: form.kind.value, text: form.text.value,
        creator: form.creator.value.trim(), games: form.games.value, published: !form.draft.checked };
      const r = await call(old ? 'press.edit' : 'press.write', old ? Object.assign({ id: old.id }, args) : args);
      btn.disabled = false;
      if (!r.ok) { msg.textContent = r.error; msg.className = 'small error'; return; }
      toast(r.post.published ? 'Published!' : 'Saved as a draft.');
      location.hash = '#/pressview/' + r.post.id;
    });
  };

  return pages;
}
