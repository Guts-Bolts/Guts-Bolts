// The Art portal's pages (worker/art.js does the work on the server):
//   #/art?sort=&cat=&tag=&by=&q=&page=   the gallery (newest, best this week, most faved)
//   #/artpiece/<id>                      one piece: the picture, faves, comments
//   #/artpost                            post a new piece
// app.js hands over its helpers with artPages(ctx), and adds the pages it returns.

export const ART_CAT_NAMES = { drawing: 'Drawing', painting: 'Painting', pixel: 'Pixel art', comic: 'Comic', fanart: 'Fan art',
  '3d': '3D render', screenshot: 'Screenshot', photo: 'Photo', other: 'Other' };
const SORTS = [['new', 'Newest'], ['week', 'Best this week'], ['top', 'Most faved']];
const kMostBytes = 2 * 1024 * 1024;   // (worker/art.js kMostBytes)

const params = () => new URLSearchParams((location.hash.split('?')[1] || ''));
const picUrl = (id, thumb) => '/artpic/' + encodeURIComponent(id) + (thumb ? '/thumb' : '');

// A file as base64 (without the "data:...;base64," bit).
const readB64 = (blob) => new Promise((ok, bad) => {
  const r = new FileReader();
  r.onload = () => ok(String(r.result).replace(/^data:[^,]*,/, ''));
  r.onerror = () => bad(new Error('Couldn\'t read that file.'));
  r.readAsDataURL(blob);
});
const loadImage = (file) => new Promise((ok, bad) => {
  const url = URL.createObjectURL(file), img = new Image();
  img.onload = () => ok(img);
  img.onerror = () => { URL.revokeObjectURL(url); bad(new Error('That isn\'t a picture this browser can open.')); };
  img.src = url;
});
// Draw the picture at most `side` pixels wide or tall, as a JPEG (white behind see-through bits).
function shrink(img, side, quality) {
  const k = Math.min(1, side / Math.max(img.naturalWidth, img.naturalHeight));
  const c = document.createElement('canvas');
  c.width = Math.max(1, Math.round(img.naturalWidth * k)); c.height = Math.max(1, Math.round(img.naturalHeight * k));
  const g = c.getContext('2d');
  g.fillStyle = '#fff'; g.fillRect(0, 0, c.width, c.height);
  g.drawImage(img, 0, 0, c.width, c.height);
  return new Promise((ok) => c.toBlob(ok, 'image/jpeg', quality));
}

export function artPages(ctx) {
  const { html, show, call, pageCall, signedIn, toast, ago, needSignIn, reportLink, verified } = ctx;
  const me = () => ctx.me();
  const person = (p) => html`<a href="#/user/${p.userId || p.id}">${p.name}</a>${verified(p.verified)}`;

  const hero = html`<div class="portal-hero art-hero"><div><span class="portal-kicker">THE ART PORTAL</span><h1>Art</h1>
      <p>Drawings, pixel art, comics, renders and screenshots from the Guts&amp;Bolts crowd. Post yours, fave the best ones.</p>
      <p><a class="btn green" href="#/artpost">Post your art</a></p></div></div>`;

  // Pictures waiting for a staff check aren't public, so /artpic won't show them: the artist
  // and staff get them through their own (signed) request instead.
  const fillWaiting = () => document.querySelectorAll('img[data-art-waiting]').forEach(async (img) => {
    const r = await call('art.picture', { id: img.dataset.artWaiting, thumb: img.dataset.thumb === '1' });
    if (r.ok && r.data) img.src = 'data:image/' + (r.type === 'jpg' ? 'jpeg' : r.type) + ';base64,' + r.data;
  });
  const pic = (p, thumb, lazy) => p.review
    ? html`<img data-art-waiting="${p.id}" data-thumb="${thumb ? '1' : ''}" alt="${p.title}">`
    : html`<img ${lazy ? 'loading=lazy' : ''} src="${picUrl(p.id, thumb)}" alt="${p.title}">`;

  const card = (p) => html`<a class="art-card" href="#/artpiece/${p.id}">
      <span class="art-thumb">${pic(p, true, true)}</span>
      <span class="art-card-text"><b>${p.title}</b><span class="small muted">by ${p.by.name}</span>
        <span class="small">&#9829; ${p.faves} &middot; ${ART_CAT_NAMES[p.cat] || 'Other'}${p.review ? html` &middot; <span class="art-flag">${p.review === 'pending' ? 'Waiting for a check' : 'Didn\'t pass'}</span>` : ''}</span></span></a>`;

  // Build a gallery address from what's picked now, with some things changed.
  const galleryLink = (cur, change) => {
    const q = new URLSearchParams(cur);
    for (const [k, v] of Object.entries(change)) { if (v === '' || v === null || v === undefined) q.delete(k); else q.set(k, String(v)); }
    if (!('page' in change)) q.delete('page');
    const s = q.toString();
    return '#/art' + (s ? '?' + s : '');
  };

  const pages = {};

  pages.art = async () => {
    const q = params();
    const staff = signedIn() && me().staff;
    if (q.get('sort') === 'pending' && staff) {
      const r = await pageCall('art.pending', {});
      show(html`${hero}<div class="box"><h2 class="boxhead">Waiting for a staff check</h2>
        <p class="small muted">Art from people who aren't Verified waits here. Open one to let it through or turn it down.</p>
        ${r.ok ? (r.pieces.length ? html`<div class="art-grid">${r.pieces.map(card)}</div>` : html`<p class="muted">Nothing waiting. Nice!</p>`)
          : html`<p class="error">${r.error}</p>`}</div>`);
      fillWaiting();
      return;
    }
    const by = q.get('by') === 'me' ? (signedIn() ? me().userId : '') : q.get('by') || '';
    const args = { sort: q.get('sort') || 'new', cat: q.get('cat') || '', tag: q.get('tag') || '', q: q.get('q') || '', by, page: Number(q.get('page') || 1) };
    const r = await pageCall('art.list', args);
    if (!r.ok) { show(html`${hero}<div class="box"><p class="error">${r.error}</p></div>`); return; }
    const sortBar = html`<div class="art-tabs">${SORTS.map(([k, label]) =>
      html`<a class="btn small ${args.sort === k ? 'blue' : ''}" href="${galleryLink(q, { sort: k === 'new' ? '' : k })}">${label}</a> `)}
      ${staff ? html`<a class="btn small" href="#/art?sort=pending">Waiting for a check</a>` : ''}</div>`;
    const catBar = html`<div class="art-cats"><a class="${args.cat ? '' : 'on'}" href="${galleryLink(q, { cat: '' })}">All</a>${Object.entries(ART_CAT_NAMES).map(([k, n]) =>
      html`<a class="${args.cat === k ? 'on' : ''}" href="${galleryLink(q, { cat: k })}">${n}</a>`)}</div>`;
    const title = q.get('by') === 'me' ? 'My art' : by && r.pieces[0] ? 'Art by ' + r.pieces[0].by.name : args.tag ? 'Tagged #' + args.tag : 'Gallery';
    show(html`${hero}<div class="box"><h2 class="boxhead">${title} <span class="small muted">(${r.total})</span></h2>
      <form class="row art-search" data-art-search><input type="search" name="q" value="${args.q}" placeholder="Search titles and tags" style="flex:1">
        <button class="btn small">Search</button></form>
      ${sortBar}${catBar}
      ${(args.tag || by || args.q) ? html`<p class="small"><a href="#/art">&laquo; Back to all art</a></p>` : ''}
      ${r.pieces.length ? html`<div class="art-grid">${r.pieces.map(card)}</div>`
        : html`<p class="muted">No art here yet. <a href="#/artpost">Be the first to post some!</a></p>`}
      <p class="art-pages">${r.page > 1 ? html`<a class="btn small" href="${galleryLink(q, { page: r.page - 1 })}">&laquo; Newer</a> ` : ''}
        ${r.more ? html`<a class="btn small" href="${galleryLink(q, { page: r.page + 1 })}">More &raquo;</a>` : ''}</p></div>`);
    fillWaiting();
    document.querySelector('[data-art-search]').addEventListener('submit', (e) => {
      e.preventDefault();
      location.hash = galleryLink(q, { q: e.currentTarget.q.value.trim() });
    });
  };

  pages.artpiece = async (id) => {
    const r = await pageCall('art.get', { id });
    if (!r.ok) { show(html`<h1>Art</h1><p class="error">${r.error}</p><p><a href="#/art">&laquo; Back to the gallery</a></p>`); return; }
    const p = r.piece;
    const staff = signedIn() && me().staff;
    const reviewBox = p.review ? html`<div class="box info"><b>${p.review === 'pending' ? 'Waiting for a staff check.' : 'This didn\'t pass the staff check.'}</b>
        ${p.review === 'pending' ? ' Only you and staff can see it until then.' : p.reviewNote ? html` Staff said: "${p.reviewNote}"` : ''}
        ${staff && p.review === 'pending' ? html`<p><button class="btn green small" data-art="ok">Let it through</button>
          <button class="btn red small" data-art="no">Turn it down</button></p>` : ''}</div>` : '';
    const comments = p.commentList.map((c) => html`<div class="art-comment"><p>${person(c.by)} <span class="small muted">${ago(c.at)}</span>
        ${c.canDelete ? html` <a href="#" class="small" data-art-uncomment="${c.id}">Delete</a>` : ''}
        ${signedIn() && c.by.id !== me().id ? reportLink('art', p.id + ':' + c.id, 'this comment by ' + c.by.name) : ''}</p>
      <p class="art-comment-text">${c.text}</p></div>`);
    show(html`<p class="small"><a href="#/art">&laquo; Art gallery</a></p>${reviewBox}
      <div class="art-piece">
        <div class="art-stage">${p.review ? pic(p, false) : html`<a href="${picUrl(p.id)}" target="_blank" rel="noopener">${pic(p, false)}</a>`}</div>
        <div class="art-side box">
          <h1>${p.title}</h1>
          <p>by ${person(p.by)} <span class="small muted">&middot; ${ago(p.at)}${p.edited ? ' (edited)' : ''}</span></p>
          <p><button class="btn ${p.faved ? 'red' : ''}" data-art="fave" ${p.review ? 'disabled' : ''}>&#9829; ${p.faved ? 'Faved' : 'Fave'} (${p.faves})</button>
            <span class="small muted">${p.views} views</span></p>
          <p class="small"><a href="#/art?cat=${p.cat}">${ART_CAT_NAMES[p.cat] || 'Other'}</a>${p.tags.length ? ' · ' : ''}${p.tags.map((t) => html`<a class="art-tag" href="#/art?tag=${t}">#${t}</a> `)}</p>
          ${p.text ? html`<p class="art-text">${p.text}</p>` : ''}
          <p class="small"><a href="#/art?by=${p.by.userId}">More by ${p.by.name}</a>
            ${p.mine ? html` &middot; <a href="#" data-art="edit">Edit</a>` : ''}
            ${p.canDelete ? html` &middot; <a href="#" data-art="delete">Delete</a>` : ''}
            ${signedIn() && !p.mine ? html` &middot; ${reportLink('art', p.id, 'the art "' + p.title + '"')}` : ''}</p>
        </div>
      </div>
      <div class="box"><h2 class="boxhead">Comments (${p.commentList.length})</h2>
        ${p.review ? html`<p class="muted small">Comments open once it passes the staff check.</p>`
          : signedIn() ? html`<form class="form" data-art-comment><textarea name="text" maxlength="500" rows="3" placeholder="Say something nice (or useful)" required></textarea>
            <p><button class="btn green">Post comment</button></p></form>` : needSignIn('comment')}
        ${comments.length ? comments : html`<p class="muted small">No comments yet.</p>`}</div>`);

    fillWaiting();
    const again = () => pages.artpiece(id);
    document.querySelectorAll('[data-art]').forEach((b) => b.addEventListener('click', async (e) => {
      e.preventDefault();
      const what = b.dataset.art;
      if (what === 'fave') {
        if (!signedIn()) { toast('Sign up to fave art.'); return; }
        const done = await call('art.fave', { id, on: !p.faved });
        if (!done.ok) { toast(done.error); return; }
        again();
      } else if (what === 'delete') {
        if (!confirm('Delete "' + p.title + '" for good?')) return;
        const done = await call('art.delete', { id });
        if (!done.ok) { toast(done.error); return; }
        toast('Deleted.');
        location.hash = '#/art?by=me';
      } else if (what === 'edit') {
        location.hash = '#/artpost?edit=' + encodeURIComponent(id);
      } else if (what === 'ok' || what === 'no') {
        const note = what === 'no' ? prompt('Why? (the artist sees this)') : '';
        if (note === null) return;
        const done = await call('art.review', { id, ok: what === 'ok', note });
        if (!done.ok) { toast(done.error); return; }
        again();
      }
    }));
    const form = document.querySelector('[data-art-comment]');
    if (form) form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const done = await call('art.comment', { id, text: form.text.value });
      if (!done.ok) { toast(done.error); return; }
      again();
    });
    document.querySelectorAll('[data-art-uncomment]').forEach((a) => a.addEventListener('click', async (e) => {
      e.preventDefault();
      if (!confirm('Delete this comment?')) return;
      const done = await call('art.uncomment', { id, comment: a.dataset.artUncomment });
      if (!done.ok) { toast(done.error); return; }
      again();
    }));
  };

  pages.artpost = async () => {
    if (!signedIn()) { show(html`${hero}${needSignIn('post art')}`); return; }
    const editId = params().get('edit') || '';
    let old = null;
    if (editId) {
      const r = await pageCall('art.get', { id: editId });
      if (!r.ok || !r.piece.mine) { show(html`<h1>Edit art</h1><p class="error">${r.ok ? 'Only the artist can change this.' : r.error}</p>`); return; }
      old = r.piece;
    }
    const waits = !me().verified && !me().staff;
    show(html`<div class="box art-post"><h2 class="boxhead">${old ? 'Edit your art' : 'Post your art'}</h2>
      ${old ? '' : html`<p class="small muted">PNG, JPG, GIF or WebP. Big pictures are shrunk to fit ${kMostBytes >> 20} MB.
        Only post art you made yourself (or have the right to share), and follow the <a href="#/terms">rules</a>.
        ${waits ? 'Staff check new art from people who aren\'t Verified before everyone can see it.' : ''}</p>`}
      <form class="form" data-art-post>
        ${old ? html`<p class="art-preview-box">${pic(old, false)}</p>`
          : html`<p><input type="file" name="file" accept="image/png,image/jpeg,image/gif,image/webp" required></p>
            <p><img class="art-preview" id="artPreview" alt="" hidden></p>`}
        <p><label>Title<br><input type="text" name="title" maxlength="60" required value="${old ? old.title : ''}" style="width:100%"></label></p>
        <p><label>Category<br><select name="cat">${Object.entries(ART_CAT_NAMES).map(([k, n]) =>
          html`<option value="${k}" ${old && old.cat === k ? 'selected' : ''}>${n}</option>`)}</select></label></p>
        <p><label>Tags <span class="small muted">(up to 8, split by spaces or commas)</span><br>
          <input type="text" name="tags" maxlength="200" value="${old ? old.tags.join(' ') : ''}" placeholder="robot fanart sunset" style="width:100%"></label></p>
        <p><label>About it <span class="small muted">(optional)</span><br><textarea name="text" maxlength="1000" rows="4" style="width:100%">${old ? old.text : ''}</textarea></label></p>
        <p><button class="btn green">${old ? 'Save' : 'Post it'}</button> <span id="artMsg" class="small"></span></p>
      </form></div>`);
    fillWaiting();
    const form = document.querySelector('[data-art-post]');
    const msg = document.getElementById('artMsg');
    let picked = null;   // { data, thumb } as base64
    if (form.file) form.file.addEventListener('change', async () => {
      picked = null;
      const file = form.file.files[0];
      if (!file) return;
      msg.textContent = 'Getting it ready...';
      try {
        const img = await loadImage(file);
        // Keep the file as it is if it fits (GIFs stay animated); otherwise shrink it.
        let full = file;
        if (file.size > kMostBytes || !/^image\/(png|jpeg|gif|webp)$/.test(file.type)) {
          full = await shrink(img, 2560, 0.9);
          if (full && full.size > kMostBytes) full = await shrink(img, 1600, 0.85);
        }
        if (!full || full.size > kMostBytes) throw new Error('That picture is too big, even shrunk.');
        const thumb = await shrink(img, 360, 0.8);
        picked = { data: await readB64(full), thumb: thumb ? await readB64(thumb) : '' };
        if (!form.title.value) form.title.value = file.name.replace(/\.[a-z0-9]+$/i, '').replace(/[_-]+/g, ' ').slice(0, 60);
        const pv = document.getElementById('artPreview');
        pv.src = img.src; pv.hidden = false;
        msg.textContent = '';
      } catch (err) { msg.textContent = err.message; msg.className = 'small error'; }
    });
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const words = { title: form.title.value, cat: form.cat.value, tags: form.tags.value, text: form.text.value };
      if (!old && !picked) { msg.textContent = 'Pick a picture first.'; msg.className = 'small error'; return; }
      const btn = form.querySelector('button');
      btn.disabled = true; msg.className = 'small'; msg.textContent = old ? 'Saving...' : 'Posting...';
      const r = old ? await call('art.edit', Object.assign({ id: old.id }, words)) : await call('art.post', Object.assign(words, picked));
      btn.disabled = false;
      if (!r.ok) { msg.textContent = r.error; msg.className = 'small error'; return; }
      toast(r.piece.review ? 'Posted! Staff will check it soon, then everyone can see it.' : old ? 'Saved.' : 'Posted!');
      location.hash = '#/artpiece/' + r.piece.id;
    });
  };

  return pages;
}
