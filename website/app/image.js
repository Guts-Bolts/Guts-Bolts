// G&B Image's pages (worker/image.js does the work on the server):
//   #/image?by=&page=        recent public pictures (by=me: all of yours; sort=pending: staff check)
//   #/imageview/<id>         one picture: its links (direct, Markdown, HTML, BBCode), download
//   #/imagenew?from=<id>     the picture editor: crop, turn, flip, resize, colours, then
//                            download it or upload it and get a link. The editor works for
//                            everyone, signed in or not: only uploading needs an account.
// app.js hands over its helpers with imagePages(ctx), and adds the pages it returns.
//
// Everything in the editor happens in the browser (a <canvas>): the picture only goes to the
// server if you press Upload.

const kMostBytes = 5 * 1024 * 1024;   // (worker/image.js kMostBytes)
const kMostSide = 8000;                // the editor's biggest picture (browsers struggle past this)
const params = () => new URLSearchParams((location.hash.split('?')[1] || ''));
export const imagePicUrl = (id, thumb) => '/imgpic/' + encodeURIComponent(id) + (thumb ? '/thumb' : '');
const sizeText = (n) => (n < 1024 * 1024 ? Math.max(1, Math.round(n / 1024)) + ' KB' : (n / 1048576).toFixed(1) + ' MB');
const MIME = { png: 'image/png', jpg: 'image/jpeg', webp: 'image/webp' };

const readB64 = (blob) => new Promise((ok, bad) => {
  const r = new FileReader();
  r.onload = () => ok(String(r.result).replace(/^data:[^,]*,/, ''));
  r.onerror = () => bad(new Error('Couldn\'t read that file.'));
  r.readAsDataURL(blob);
});
const loadImage = (src) => new Promise((ok, bad) => {
  const img = new Image();
  img.onload = () => ok(img);
  img.onerror = () => bad(new Error('That isn\'t a picture this browser can open.'));
  img.src = src;
});
const toBlob = (c, type, quality) => new Promise((ok) => c.toBlob(ok, type, quality));
const canvasOf = (w, h) => { const c = document.createElement('canvas'); c.width = Math.max(1, Math.round(w)); c.height = Math.max(1, Math.round(h)); return c; };

// The colour sliders, as a CSS filter (the same one the canvas draws with).
export function filterText(f) {
  const parts = [];
  if (f.brightness !== 100) parts.push('brightness(' + f.brightness + '%)');
  if (f.contrast !== 100) parts.push('contrast(' + f.contrast + '%)');
  if (f.saturate !== 100) parts.push('saturate(' + f.saturate + '%)');
  if (f.gray) parts.push('grayscale(100%)');
  if (f.sepia) parts.push('sepia(100%)');
  if (f.invert) parts.push('invert(100%)');
  if (f.blur) parts.push('blur(' + f.blur + 'px)');
  return parts.join(' ') || 'none';
}
// A crop box kept to a shape (w:h), inside the picture.
export function fitCrop(box, ratio, W, H) {
  let { x, y, w, h } = box;
  if (w < 0) { x += w; w = -w; }
  if (h < 0) { y += h; h = -h; }
  if (ratio) { if (w / h > ratio) w = h * ratio; else h = w / ratio; }
  x = Math.max(0, Math.min(W - 1, x)); y = Math.max(0, Math.min(H - 1, y));
  w = Math.max(1, Math.min(W - x, w)); h = Math.max(1, Math.min(H - y, h));
  if (ratio) { if (w / h > ratio) w = h * ratio; else h = w / ratio; }
  return { x: Math.round(x), y: Math.round(y), w: Math.round(w), h: Math.round(h) };
}

export function imagePages(ctx) {
  const { html, show, call, pageCall, signedIn, toast, ago, needSignIn, reportLink, verified } = ctx;
  const me = () => ctx.me();
  const person = (p) => html`<a href="#/user/${p.userId || p.id}">${p.name}</a>${verified(p.verified)}`;

  const hero = html`<div class="portal-hero image-hero"><div><span class="portal-kicker">G&amp;B CREATION</span><h1>Image</h1>
      <p>Put a picture online and get a link for forums, chats and game pages. Crop it, turn it, resize it or fix its colours
        first with the free editor: it all happens in your browser.</p>
      <p><a class="btn green" href="#/imagenew">Upload or edit a picture</a></p></div></div>`;

  // Waiting pictures aren't public, so /imgpic won't show them: the uploader and staff get them
  // through their own (signed) request instead.
  const fillWaiting = () => document.querySelectorAll('img[data-img-waiting]').forEach(async (img) => {
    const r = await call('image.picture', { id: img.dataset.imgWaiting, thumb: img.dataset.thumb === '1' });
    if (r.ok && r.data) img.src = 'data:image/' + (r.type === 'jpg' ? 'jpeg' : r.type) + ';base64,' + r.data;
  });
  const pic = (m, thumb, lazy) => m.review
    ? html`<img data-img-waiting="${m.id}" data-thumb="${thumb ? '1' : ''}" alt="${m.title}">`
    : html`<img ${lazy ? 'loading=lazy' : ''} src="${imagePicUrl(m.id, thumb && m.thumb)}" alt="${m.title}">`;
  const flag = (m) => m.review ? html`<span class="art-flag">${m.review === 'pending' ? 'Waiting for a check' : 'Didn\'t pass'}</span>`
    : m.visibility === 'unlisted' ? html`<span class="paste-flag">Unlisted</span>` : '';
  const card = (m) => html`<a class="art-card" href="#/imageview/${m.id}">
      <span class="art-thumb">${pic(m, true, true)}</span>
      <span class="art-card-text"><b>${m.title}</b><span class="small muted">by ${m.by.name} &middot; ${ago(m.at)}</span>
        <span class="small">${m.w && m.h ? m.w + ' × ' + m.h + ' · ' : ''}${sizeText(m.size)} ${flag(m)}</span></span></a>`;
  const grid = (list, empty) => list.length ? html`<div class="art-grid">${list.map(card)}</div>` : html`<p class="muted">${empty}</p>`;

  const pages = {};

  pages.image = async () => {
    const q = params();
    if (q.get('sort') === 'pending' && signedIn() && me().staff) {
      const r = await pageCall('image.pending', {});
      show(html`${hero}<div class="box"><h2 class="boxhead">Waiting for a staff check</h2>
        <p class="small muted">Pictures from people who aren't Verified wait here. Open one to let it through or turn it down.</p>
        ${r.ok ? grid(r.images, 'Nothing waiting. Nice!') : html`<p class="error">${r.error}</p>`}</div>`);
      fillWaiting();
      return;
    }
    if (q.get('by') === 'me') {
      if (!signedIn()) { show(html`${hero}${needSignIn('see your pictures')}`); return; }
      const r = await pageCall('image.mine', {});
      show(html`${hero}<div class="box"><h2 class="boxhead">My pictures</h2>
        <p class="small muted">All of yours, unlisted ones too. Only people with the link can open an unlisted picture.</p>
        ${r.ok ? grid(r.images, 'You haven\'t uploaded any yet.') : html`<p class="error">${r.error}</p>`}</div>`);
      fillWaiting();
      return;
    }
    const page = Number(q.get('page') || 1), by = q.get('by') || '';
    const r = await pageCall('image.list', { page, by });
    if (!r.ok) { show(html`${hero}<div class="box"><p class="error">${r.error}</p></div>`); return; }
    const link = (n) => '#/image?' + (by ? 'by=' + encodeURIComponent(by) + '&' : '') + 'page=' + n;
    show(html`${hero}
      <div class="box"><h2 class="boxhead">${by && r.images[0] ? 'Pictures by ' + r.images[0].by.name : 'Recent pictures'}</h2>
        ${signedIn() && me().staff ? html`<p><a class="btn small" href="#/image?sort=pending">Waiting for a check</a></p>` : ''}
        ${by ? html`<p class="small"><a href="#/image">&laquo; All pictures</a></p>` : ''}
        ${grid(r.images, 'No pictures here yet.')}
        <p class="art-pages">${r.page > 1 ? html`<a class="btn small" href="${link(r.page - 1)}">&laquo; Newer</a> ` : ''}
          ${r.more ? html`<a class="btn small" href="${link(r.page + 1)}">Older &raquo;</a>` : ''}</p></div>
      <div class="box image-about"><h2 class="boxhead">What you can do here</h2>
        <ul>
          <li><b>Share a picture with one link.</b> Paste it in a forum post, a chat or your game's page. There are ready-made codes for Markdown, HTML and forums (BBCode).</li>
          <li><b>Fix it up first.</b> Crop, turn, flip and resize it, change its brightness, contrast and colours, or make it black and white.</li>
          <li><b>Make it smaller.</b> Save it as JPG or WebP with the quality you pick, so it loads fast.</li>
          <li><b>Keep it to yourself.</b> Unlisted pictures only open with their link. You can delete yours any time.</li>
        </ul>
        <p class="small muted">Free, with the same account as the rest of Guts&amp;Bolts. Follow the <a href="#/terms">rules</a>: no pictures you don't have the right to share.</p></div>`);
  };

  pages.imageview = async (id) => {
    const r = await pageCall('image.get', { id });
    if (!r.ok) { show(html`<h1>Image</h1><p class="error">${r.error}</p><p><a href="#/image">&laquo; Back to Image</a></p>`); return; }
    const m = r.image;
    const staff = signedIn() && me().staff;
    const direct = location.origin + imagePicUrl(m.id), pageUrl = location.origin + '/#/imageview/' + m.id;
    const codes = [['Link to this page', pageUrl], ['Direct link (the picture itself)', direct], ['Markdown', '![' + m.title + '](' + direct + ')'],
      ['HTML', '<img src="' + direct + '" alt="' + m.title.replace(/"/g, '&quot;') + '">'], ['Forums (BBCode)', '[img]' + direct + '[/img]']];
    const reviewBox = m.review ? html`<div class="box info"><b>${m.review === 'pending' ? 'Waiting for a staff check.' : 'This didn\'t pass the staff check.'}</b>
        ${m.review === 'pending' ? ' Only you and staff can see it until then, so the links won\'t work for other people yet.' : m.reviewNote ? html` Staff said: "${m.reviewNote}"` : ''}
        ${staff && m.review === 'pending' ? html`<p><button class="btn green small" data-img="ok">Let it through</button>
          <button class="btn red small" data-img="no">Turn it down</button></p>` : ''}</div>` : '';
    show(html`<p class="small"><a href="#/image">&laquo; Image</a></p>${reviewBox}
      <div class="image-view">
        <div class="image-stage">${m.review ? pic(m, false) : html`<a href="${imagePicUrl(m.id)}" target="_blank" rel="noopener">${pic(m, false)}</a>`}</div>
        <div class="box image-side">
          <h1>${m.title}</h1>
          <p class="small">by ${person(m.by)} <span class="muted">&middot; ${ago(m.at)} &middot; ${m.views} views</span></p>
          <p class="small muted">${String(m.type).toUpperCase()}${m.w && m.h ? ' · ' + m.w + ' × ' + m.h + ' pixels' : ''} · ${sizeText(m.size)} ${flag(m)}</p>
          ${m.review ? '' : html`<div class="image-codes">${codes.map(([label, text], i) => html`<label class="small">${label}
            <span class="row"><input type="text" readonly value="${text}" data-code="${i}"><button class="btn small" data-copy="${i}">Copy</button></span></label>`)}</div>`}
          <p class="paste-tools">
            <button class="btn small green" data-img="download">Download</button>
            <a class="btn small" href="#/imagenew?from=${m.id}">Edit a copy</a>
            ${m.mine ? html`<button class="btn small" data-img="edit">Rename / who can see it</button>` : ''}
            ${m.canDelete ? html`<button class="btn small red" data-img="delete">Delete</button>` : ''}
            ${signedIn() && !m.mine ? html`<span class="small">${reportLink('image', m.id, 'the picture "' + m.title + '"')}</span>` : ''}</p>
          <p class="small"><a href="#/image?by=${m.by.userId}">More from ${m.by.name}</a></p>
          <form class="form" data-img-edit hidden>
            <p><label>Title<br><input type="text" name="title" maxlength="80" value="${m.title}" style="width:100%"></label></p>
            <p><label>Who can see it<br><select name="visibility"><option value="public">Everyone (in the Recent list)</option>
              <option value="unlisted" ${m.visibility === 'unlisted' ? 'selected' : ''}>Only people with the link</option></select></label></p>
            <p><button class="btn green small">Save</button></p></form>
        </div>
      </div>`);
    fillWaiting();
    const again = () => pages.imageview(id);
    document.querySelectorAll('[data-copy]').forEach((b) => b.addEventListener('click', async (e) => {
      e.preventDefault();
      const box = document.querySelector('[data-code="' + b.dataset.copy + '"]');
      try { await navigator.clipboard.writeText(box.value); toast('Copied.'); } catch { box.select(); toast('Press Ctrl+C to copy it.'); }
    }));
    document.querySelectorAll('[data-code]').forEach((box) => box.addEventListener('focus', () => box.select()));
    const editForm = document.querySelector('[data-img-edit]');
    editForm.addEventListener('submit', async (e) => {
      e.preventDefault();
      const done = await call('image.edit', { id, title: editForm.title.value, visibility: editForm.visibility.value });
      if (!done.ok) { toast(done.error); return; }
      toast('Saved.');
      again();
    });
    document.querySelectorAll('[data-img]').forEach((b) => b.addEventListener('click', async (e) => {
      e.preventDefault();
      const what = b.dataset.img;
      if (what === 'edit') editForm.hidden = !editForm.hidden;
      else if (what === 'download') {
        const blob = await pictureBlob(m);
        if (!blob) { toast('Couldn\'t get the picture.'); return; }
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = (m.title.replace(/[^a-z0-9 _.-]+/gi, '').trim().replace(/\s+/g, '-') || 'picture') + '.' + m.type;
        a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 1000);
      } else if (what === 'delete') {
        if (!confirm('Delete "' + m.title + '" for good? Links to it will stop working.')) return;
        const done = await call('image.delete', { id });
        if (!done.ok) { toast(done.error); return; }
        toast('Deleted.');
        location.hash = '#/image?by=me';
      } else if (what === 'ok' || what === 'no') {
        const note = what === 'no' ? prompt('Why? (the uploader sees this)') : '';
        if (note === null) return;
        const done = await call('image.review', { id, ok: what === 'ok', note });
        if (!done.ok) { toast(done.error); return; }
        again();
      }
    }));
  };

  // The picture's bytes: from /imgpic for checked ones, or through a signed request for waiting ones.
  async function pictureBlob(m) {
    if (!m.review) {
      try { const res = await fetch(imagePicUrl(m.id)); if (res.ok) return await res.blob(); } catch { /* falls through */ }
      return null;
    }
    const r = await call('image.picture', { id: m.id });
    if (!r.ok || !r.data) return null;
    const bin = atob(r.data), bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return new Blob([bytes], { type: 'image/' + (r.type === 'jpg' ? 'jpeg' : r.type) });
  }

  pages.imagenew = async () => {
    const from = params().get('from') || '';
    let start = null;
    if (from) {
      const r = await pageCall('image.get', { id: from, quiet: true });
      if (r.ok) start = { image: r.image, blob: await pictureBlob(r.image) };
    }
    show(html`<div class="box image-editor"><h2 class="boxhead">Upload or edit a picture</h2>
      <div class="image-drop" data-drop>
        <p><b>Pick a picture</b>, drop one here, or paste one (Ctrl+V).</p>
        <p><input type="file" accept="image/*" data-file></p>
        <p class="small muted">PNG, JPG, GIF or WebP. Editing happens on your computer; nothing is sent until you press Upload.</p>
      </div>
      <div data-work hidden>
        <div class="image-toolbar">
          <span class="image-group"><b class="small">Turn</b>
            <button class="btn small" data-tool="left" title="Turn left">&#8634; Left</button>
            <button class="btn small" data-tool="right" title="Turn right">&#8635; Right</button>
            <button class="btn small" data-tool="flipx">Flip &#8596;</button>
            <button class="btn small" data-tool="flipy">Flip &#8597;</button></span>
          <span class="image-group"><b class="small">Crop</b>
            <select data-ratio title="Shape"><option value="0">Any shape</option><option value="1">Square</option><option value="1.7778">16:9 (wide)</option>
              <option value="1.3333">4:3</option><option value="0.5625">9:16 (tall)</option></select>
            <button class="btn small" data-tool="crop">Crop to the box</button></span>
          <span class="image-group"><button class="btn small" data-tool="undo">Undo</button>
            <button class="btn small" data-tool="reset">Start over</button></span>
        </div>
        <p class="small muted" data-hint>Drag on the picture to draw a crop box.</p>
        <div class="image-canvas-wrap"><canvas data-view></canvas></div>
        <div class="image-panels">
          <fieldset class="image-panel"><legend>Size</legend>
            <label class="small">Width <input type="number" min="1" max="${kMostSide}" data-w></label>
            <label class="small">Height <input type="number" min="1" max="${kMostSide}" data-h></label>
            <label class="small"><input type="checkbox" data-keep checked> Keep the shape</label>
            <p><button class="btn small" data-tool="resize">Resize</button> <button class="btn small" data-tool="half">Half size</button></p>
          </fieldset>
          <fieldset class="image-panel"><legend>Colours</legend>
            <label class="small">Brightness <input type="range" min="0" max="200" value="100" data-f="brightness"></label>
            <label class="small">Contrast <input type="range" min="0" max="200" value="100" data-f="contrast"></label>
            <label class="small">Colour <input type="range" min="0" max="200" value="100" data-f="saturate"></label>
            <label class="small">Blur <input type="range" min="0" max="10" value="0" data-f="blur"></label>
            <p class="small"><label><input type="checkbox" data-f="gray"> Black and white</label>
              <label><input type="checkbox" data-f="sepia"> Old photo</label>
              <label><input type="checkbox" data-f="invert"> Negative</label></p>
            <p><button class="btn small" data-tool="nocolor">Back to normal</button></p>
          </fieldset>
          <fieldset class="image-panel"><legend>Save as</legend>
            <label class="small">Kind <select data-format><option value="png">PNG (sharp, bigger)</option><option value="jpg">JPG (photos, small)</option>
              <option value="webp">WebP (small, modern)</option></select></label>
            <label class="small" data-qbox>Quality <input type="range" min="40" max="100" value="90" data-quality> <span data-qtext>90%</span></label>
            <p class="small muted" data-info></p>
            <p><button class="btn small" data-tool="download">Download</button></p>
          </fieldset>
        </div>
        <div class="image-upload">
          <h3>Upload and get a link</h3>
          ${signedIn() ? html`<form class="form" data-up>
            <p><label>Title<br><input type="text" name="title" maxlength="80" style="width:100%" placeholder="Untitled"></label></p>
            <p><label>Who can see it<br><select name="visibility"><option value="public">Everyone (in the Recent list)</option>
              <option value="unlisted">Only people with the link</option></select></label></p>
            ${!me().verified && !me().staff ? html`<p class="small muted">Staff check pictures from people who aren't Verified before the link works for everyone.</p>` : ''}
            <p><button class="btn green">Upload</button> <span class="small" data-msg></span></p></form>`
            : html`<p class="small">You can edit and download without an account. ${needSignIn('upload it and get a link')}</p>`}
        </div>
      </div></div>`);

    const root = document.querySelector('.image-editor');
    const $ = (sel) => root.querySelector(sel);
    const view = $('[data-view]'), g = view.getContext('2d');
    const S = { work: null, original: null, file: null, edited: false, undo: [], crop: null, ratio: 0,
      f: { brightness: 100, contrast: 100, saturate: 100, blur: 0, gray: false, sepia: false, invert: false }, scale: 1 };

    const colorsChanged = () => filterText(S.f) !== 'none';
    function info() {
      $('[data-w]').value = S.work.width; $('[data-h]').value = S.work.height;
      const fmt = $('[data-format]').value;
      $('[data-qbox]').hidden = fmt === 'png';
      $('[data-qtext]').textContent = $('[data-quality]').value + '%';
      $('[data-info]').textContent = S.work.width + ' × ' + S.work.height + ' pixels' + (S.file && !S.edited && !colorsChanged() ? ' · ' + sizeText(S.file.size) + ' (as it is)' : '');
    }
    function draw() {
      const W = S.work.width, H = S.work.height;
      const most = Math.min(root.clientWidth - 8 || 800, 1000);
      S.scale = Math.min(1, most / W, 640 / H);
      view.width = Math.max(1, Math.round(W * S.scale)); view.height = Math.max(1, Math.round(H * S.scale));
      g.save();
      g.filter = filterText(S.f);
      g.clearRect(0, 0, view.width, view.height);
      g.drawImage(S.work, 0, 0, view.width, view.height);
      g.restore();
      if (S.crop) {
        const c = S.crop, k = S.scale;
        g.fillStyle = 'rgba(0,0,0,.5)';
        g.fillRect(0, 0, view.width, c.y * k); g.fillRect(0, (c.y + c.h) * k, view.width, view.height - (c.y + c.h) * k);
        g.fillRect(0, c.y * k, c.x * k, c.h * k); g.fillRect((c.x + c.w) * k, c.y * k, view.width - (c.x + c.w) * k, c.h * k);
        g.strokeStyle = '#fff'; g.lineWidth = 2; g.setLineDash([6, 4]);
        g.strokeRect(c.x * k + 1, c.y * k + 1, c.w * k - 2, c.h * k - 2);
        g.setLineDash([]);
        $('[data-hint]').textContent = 'Crop box: ' + c.w + ' × ' + c.h + '. Press "Crop to the box" to cut, or click outside it to start again.';
      } else $('[data-hint]').textContent = 'Drag on the picture to draw a crop box.';
      info();
    }
    // Change the picture: remember the old one for Undo.
    function change(next) {
      S.undo.push(S.work);
      if (S.undo.length > 15) S.undo.shift();
      S.work = next; S.edited = true; S.crop = null;
      draw();
    }
    async function open(blob) {
      try {
        const url = URL.createObjectURL(blob);
        const img = await loadImage(url);
        let w = img.naturalWidth, h = img.naturalHeight;
        const k = Math.min(1, kMostSide / Math.max(w, h));
        const c = canvasOf(w * k, h * k);
        c.getContext('2d').drawImage(img, 0, 0, c.width, c.height);
        URL.revokeObjectURL(url);
        Object.assign(S, { work: c, original: c, file: blob, edited: k < 1, shrunk: k < 1, undo: [], crop: null });
        $('[data-work]').hidden = false;
        const t = root.querySelector('[data-up] [name=title]');
        if (t && !t.value && blob.name) t.value = blob.name.replace(/\.[a-z0-9]+$/i, '').replace(/[_-]+/g, ' ').slice(0, 80);
        $('[data-format]').value = /jpe?g/.test(blob.type) ? 'jpg' : /webp/.test(blob.type) ? 'webp' : 'png';
        draw();
      } catch (err) { toast(err.message); }
    }
    // The finished picture, as a file: the one you picked if nothing changed (GIFs stay animated).
    async function output() {
      const fmt = $('[data-format]').value;
      const same = S.file && !S.edited && !colorsChanged() && MIME[fmt] === S.file.type;
      if (same || (S.file && !S.edited && !colorsChanged() && S.file.type === 'image/gif')) return { blob: S.file, type: S.file.type };
      const c = canvasOf(S.work.width, S.work.height), cg = c.getContext('2d');
      if (fmt === 'jpg') { cg.fillStyle = '#fff'; cg.fillRect(0, 0, c.width, c.height); }   // (JPG has no see-through bits)
      cg.filter = filterText(S.f);
      cg.drawImage(S.work, 0, 0);
      const blob = await toBlob(c, MIME[fmt], Number($('[data-quality]').value) / 100);
      return { blob, type: MIME[fmt] };
    }

    const file = $('[data-file]'), drop = $('[data-drop]');
    file.addEventListener('change', () => { if (file.files[0]) open(file.files[0]); });
    drop.addEventListener('dragover', (e) => { e.preventDefault(); drop.classList.add('on'); });
    drop.addEventListener('dragleave', () => drop.classList.remove('on'));
    drop.addEventListener('drop', (e) => {
      e.preventDefault(); drop.classList.remove('on');
      const f = [...(e.dataTransfer.files || [])].find((x) => /^image\//.test(x.type));
      if (f) open(f);
    });
    const onPaste = (e) => {
      if (!document.body.contains(root)) { window.removeEventListener('paste', onPaste); return; }   // (left the page)
      const f = [...((e.clipboardData && e.clipboardData.files) || [])].find((x) => /^image\//.test(x.type));
      if (f) { e.preventDefault(); open(f); }
    };
    window.addEventListener('paste', onPaste);
    if (start && start.blob) {
      open(start.blob);
      const t = root.querySelector('[data-up] [name=title]');
      if (t) t.value = start.image.title;
    }

    // Drawing the crop box.
    let dragFrom = null;
    const at = (e) => { const r = view.getBoundingClientRect(); return { x: (e.clientX - r.left) / S.scale, y: (e.clientY - r.top) / S.scale }; };
    view.addEventListener('pointerdown', (e) => { if (!S.work) return; dragFrom = at(e); view.setPointerCapture(e.pointerId); S.crop = null; draw(); });
    view.addEventListener('pointermove', (e) => {
      if (!dragFrom) return;
      const p = at(e);
      if (Math.abs(p.x - dragFrom.x) < 3 && Math.abs(p.y - dragFrom.y) < 3) return;
      S.crop = fitCrop({ x: dragFrom.x, y: dragFrom.y, w: p.x - dragFrom.x, h: p.y - dragFrom.y }, S.ratio, S.work.width, S.work.height);
      draw();
    });
    view.addEventListener('pointerup', () => { dragFrom = null; });
    $('[data-ratio]').addEventListener('change', (e) => {
      S.ratio = Number(e.target.value) || 0;
      if (S.ratio) {   // a ready-made box in the middle, as big as fits
        const W = S.work.width, H = S.work.height;
        const w = Math.min(W, H * S.ratio), h = w / S.ratio;
        S.crop = fitCrop({ x: (W - w) / 2, y: (H - h) / 2, w, h }, S.ratio, W, H);
      }
      draw();
    });

    root.querySelectorAll('[data-f]').forEach((el) => el.addEventListener('input', () => {
      S.f[el.dataset.f] = el.type === 'checkbox' ? el.checked : Number(el.value);
      draw();
    }));
    $('[data-format]').addEventListener('change', info);
    $('[data-quality]').addEventListener('input', info);
    $('[data-w]').addEventListener('input', () => { if ($('[data-keep]').checked) $('[data-h]').value = Math.max(1, Math.round($('[data-w]').value * S.work.height / S.work.width)); });
    $('[data-h]').addEventListener('input', () => { if ($('[data-keep]').checked) $('[data-w]').value = Math.max(1, Math.round($('[data-h]').value * S.work.width / S.work.height)); });

    const turn = (dir) => {
      const c = canvasOf(S.work.height, S.work.width), cg = c.getContext('2d');
      cg.translate(c.width / 2, c.height / 2); cg.rotate(dir * Math.PI / 2);
      cg.drawImage(S.work, -S.work.width / 2, -S.work.height / 2);
      change(c);
    };
    const flip = (x) => {
      const c = canvasOf(S.work.width, S.work.height), cg = c.getContext('2d');
      cg.translate(x ? c.width : 0, x ? 0 : c.height); cg.scale(x ? -1 : 1, x ? 1 : -1);
      cg.drawImage(S.work, 0, 0);
      change(c);
    };
    const resize = (w, h) => {
      w = Math.round(w); h = Math.round(h);
      if (!(w >= 1 && h >= 1 && w <= kMostSide && h <= kMostSide)) { toast('Pick a size from 1 to ' + kMostSide + ' pixels.'); return; }
      const c = canvasOf(w, h), cg = c.getContext('2d');
      cg.imageSmoothingQuality = 'high';
      cg.drawImage(S.work, 0, 0, w, h);
      change(c);
    };
    root.querySelectorAll('[data-tool]').forEach((b) => b.addEventListener('click', async (e) => {
      e.preventDefault();
      if (!S.work) return;
      const t = b.dataset.tool;
      if (t === 'left' || t === 'right') turn(t === 'left' ? -1 : 1);
      else if (t === 'flipx' || t === 'flipy') flip(t === 'flipx');
      else if (t === 'crop') {
        if (!S.crop) { toast('Drag on the picture to draw a crop box first.'); return; }
        const c = canvasOf(S.crop.w, S.crop.h);
        c.getContext('2d').drawImage(S.work, S.crop.x, S.crop.y, S.crop.w, S.crop.h, 0, 0, S.crop.w, S.crop.h);
        change(c);
      } else if (t === 'resize') resize(Number($('[data-w]').value), Number($('[data-h]').value));
      else if (t === 'half') resize(S.work.width / 2, S.work.height / 2);
      else if (t === 'undo') {
        if (!S.undo.length) { toast('Nothing to undo.'); return; }
        S.work = S.undo.pop(); S.crop = null; S.edited = S.work !== S.original || S.shrunk; draw();
      } else if (t === 'reset') {
        S.work = S.original; S.undo = []; S.crop = null; S.edited = S.shrunk;
        root.querySelector('[data-tool=nocolor]').click();
      } else if (t === 'nocolor') {
        Object.assign(S.f, { brightness: 100, contrast: 100, saturate: 100, blur: 0, gray: false, sepia: false, invert: false });
        root.querySelectorAll('[data-f]').forEach((el) => { if (el.type === 'checkbox') el.checked = false; else el.value = el.dataset.f === 'blur' ? 0 : 100; });
        draw();
      } else if (t === 'download') {
        const out = await output();
        if (!out.blob) { toast('Your browser couldn\'t save it that way. Try PNG.'); return; }
        const a = document.createElement('a');
        a.href = URL.createObjectURL(out.blob);
        a.download = 'picture.' + (out.type === 'image/jpeg' ? 'jpg' : out.type.split('/')[1]);
        a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 1000);
      }
    }));

    const up = root.querySelector('[data-up]');
    if (up) up.addEventListener('submit', async (e) => {
      e.preventDefault();
      const msg = up.querySelector('[data-msg]'), btn = up.querySelector('button');
      if (!S.work) { msg.textContent = 'Pick a picture first.'; msg.className = 'small error'; return; }
      btn.disabled = true; msg.className = 'small'; msg.textContent = 'Getting it ready...';
      try {
        const out = await output();
        if (!out.blob) throw new Error('Your browser couldn\'t save it that way. Try PNG.');
        if (out.blob.size > kMostBytes) throw new Error('That\'s ' + sizeText(out.blob.size) + ', and the most is ' + (kMostBytes >> 20) + ' MB. Save it as JPG or WebP, or make it smaller.');
        const tc = canvasOf(...(() => { const k = Math.min(1, 360 / Math.max(S.work.width, S.work.height)); return [S.work.width * k, S.work.height * k]; })());
        const tg = tc.getContext('2d');
        tg.fillStyle = '#fff'; tg.fillRect(0, 0, tc.width, tc.height);
        tg.filter = filterText(S.f);
        tg.drawImage(S.work, 0, 0, tc.width, tc.height);
        const thumb = await toBlob(tc, 'image/jpeg', 0.8);
        msg.textContent = 'Uploading...';
        const r = await call('image.upload', { title: up.title.value, visibility: up.visibility.value, w: S.work.width, h: S.work.height,
          data: await readB64(out.blob), thumb: thumb ? await readB64(thumb) : '' });
        if (!r.ok) throw new Error(r.error);
        toast(r.image.review ? 'Uploaded! Staff will check it soon, then the link works for everyone.' : 'Uploaded! Copy the link to share it.');
        location.hash = '#/imageview/' + r.image.id;
      } catch (err) {
        msg.textContent = err.message; msg.className = 'small error';
      } finally { btn.disabled = false; }
    });
  };

  return pages;
}
