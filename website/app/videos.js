// G&B Videos' pages (worker/videos.js does the work on the server):
//   #/videos?sort=&cat=&tag=&by=&q=&page=   the home feed (newest, trending this week, most liked)
//   #/watch/<id>                            one video: the player, likes, comments, up next
//   #/videoupload[?edit=<id>]               upload a video (or change its words)
// app.js hands over its helpers with videoPages(ctx), and adds the pages it returns.

export const VIDEO_CAT_NAMES = { gaming: 'Gaming', music: 'Music', funny: 'Funny', howto: 'How-to', animation: 'Animation',
  vlog: 'Vlogs', sports: 'Sports', science: 'Science & tech', news: 'News', other: 'Other' };
const SORTS = [['new', 'Newest'], ['week', 'Trending this week'], ['top', 'Most liked'], ['views', 'Most watched']];
const kMostBytes = 95 * 1024 * 1024, kThumbBytes = 200 * 1024;   // (worker/videos.js)

const params = () => new URLSearchParams((location.hash.split('?')[1] || ''));

// 75 -> "1:15", 3725 -> "1:02:05"
export function videoTime(secs) {
  const s = Math.max(0, Math.round(Number(secs) || 0)), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60;
  return (h ? h + ':' + String(m).padStart(2, '0') : String(m)) + ':' + String(r).padStart(2, '0');
}
// 1234 -> "1.2K"
export function videoCount(n) {
  n = Number(n) || 0;
  if (n < 1000) return String(n);
  if (n < 1e6) return (n / 1000).toFixed(n < 10000 ? 1 : 0).replace(/\.0$/, '') + 'K';
  return (n / 1e6).toFixed(1).replace(/\.0$/, '') + 'M';
}

const blobB64 = (blob) => new Promise((ok, bad) => {
  const r = new FileReader();
  r.onload = () => ok(String(r.result).replace(/^data:[^,]*,/, ''));
  r.onerror = () => bad(new Error('Couldn\'t read that file.'));
  r.readAsDataURL(blob);
});
// Draw something (a video frame or a picture) at most `side` pixels wide, as a JPEG small enough to send.
async function snapshot(source, w, h) {
  for (const [side, q] of [[640, 0.82], [480, 0.75], [320, 0.7]]) {
    const k = Math.min(1, side / Math.max(w, h));
    const c = document.createElement('canvas');
    c.width = Math.max(1, Math.round(w * k)); c.height = Math.max(1, Math.round(h * k));
    const g = c.getContext('2d');
    g.fillStyle = '#000'; g.fillRect(0, 0, c.width, c.height);
    g.drawImage(source, 0, 0, c.width, c.height);
    const blob = await new Promise((ok) => c.toBlob(ok, 'image/jpeg', q));
    if (blob && blob.size <= kThumbBytes) return blob;
  }
  return null;
}
// What the browser can tell about a video file: how long, how big, and a picture from it.
function lookAtVideo(file) {
  return new Promise((ok) => {
    const url = URL.createObjectURL(file), v = document.createElement('video');
    let done = false;
    const finish = (info) => { if (done) return; done = true; ok(Object.assign({ url }, info)); };
    v.muted = true; v.playsInline = true; v.preload = 'auto';
    v.onloadedmetadata = () => { v.currentTime = Math.min(1.5, (v.duration || 0) / 3); };
    v.onseeked = async () => {
      const thumb = v.videoWidth ? await snapshot(v, v.videoWidth, v.videoHeight).catch(() => null) : null;
      finish({ secs: v.duration || 0, w: v.videoWidth, h: v.videoHeight, thumb });
    };
    v.onerror = () => finish({ secs: 0, w: 0, h: 0, thumb: null, unplayable: true });
    setTimeout(() => finish({ secs: v.duration || 0, w: v.videoWidth || 0, h: v.videoHeight || 0, thumb: null }), 8000);
    v.src = url;
  });
}
// Send the file to the upload address with a progress bar (fetch can't show upload progress).
function sendFile(address, file, onProgress) {
  return new Promise((ok) => {
    const x = new XMLHttpRequest();
    x.open('PUT', address);
    x.upload.onprogress = (e) => { if (e.lengthComputable) onProgress(e.loaded / e.total); };
    x.onload = () => { try { ok(JSON.parse(x.responseText)); } catch { ok({ ok: false, error: 'The server said something odd (' + x.status + '). Try again.' }); } };
    x.onerror = () => ok({ ok: false, error: 'The upload stopped. Check your connection and try again.' });
    x.send(file);
  });
}

export function videoPages(ctx) {
  const { html, show, call, pageCall, signedIn, toast, ago, needSignIn, reportLink, verified } = ctx;
  const me = () => ctx.me();
  const person = (p) => html`<a href="#/videos?by=${p.userId}">${p.name}</a>${verified(p.verified)}`;

  const hero = html`<div class="portal-hero video-hero"><div><span class="portal-kicker">G&amp;B VIDEOS</span><h1>Videos</h1>
      <p>Watch videos about anything: games, music, how-tos, funny stuff. Upload your own for free, and follow people to hear about their new ones.</p>
      <p><a class="btn green" href="#/videoupload">Upload a video</a> <a class="btn red" href="#/golive">&#9679; Go live</a> <a class="btn" href="#/live">Watch live</a></p></div></div>`;

  const flag = (v) => v.state === 'uploading' ? 'Upload didn\'t finish' : v.review === 'pending' ? 'Waiting for a check' : v.review ? 'Didn\'t pass' : '';
  const card = (v) => html`<a class="video-card" href="#/watch/${v.id}">
      <span class="video-thumb">${v.poster ? html`<img loading="lazy" src="${v.poster}" alt="">` : html`<span class="video-nopic">&#9654;</span>`}
        ${v.secs ? html`<span class="video-time">${videoTime(v.secs)}</span>` : ''}</span>
      <span class="video-card-text"><b>${v.title}</b><span class="small muted">${v.by.name}</span>
        <span class="small muted">${videoCount(v.views)} views &middot; ${ago(v.at)}${flag(v) ? html` &middot; <span class="video-flag">${flag(v)}</span>` : ''}</span></span></a>`;

  const feedLink = (cur, change) => {
    const q = new URLSearchParams(cur);
    for (const [k, v] of Object.entries(change)) { if (v === '' || v === null || v === undefined) q.delete(k); else q.set(k, String(v)); }
    if (!('page' in change)) q.delete('page');
    const s = q.toString();
    return '#/videos' + (s ? '?' + s : '');
  };

  const pages = {};

  pages.videos = async () => {
    const q = params();
    const staff = signedIn() && me().staff;
    if (q.get('sort') === 'pending' && staff) {
      const r = await pageCall('video.pending', {});
      show(html`${hero}<div class="box"><h2 class="boxhead">Waiting for a staff check</h2>
        <p class="small muted">Videos from people who aren't Verified wait here. Open one, watch it, then let it through or turn it down.</p>
        ${r.ok ? (r.videos.length ? html`<div class="video-grid">${r.videos.map(card)}</div>` : html`<p class="muted">Nothing waiting. Nice!</p>`)
          : html`<p class="error">${r.error}</p>`}</div>`);
      return;
    }
    if (q.get('by') === 'me' && !signedIn()) { show(html`${hero}${needSignIn('see your videos')}`); return; }
    const by = q.get('by') === 'me' ? me().userId : q.get('by') || '';
    const args = { sort: q.get('sort') || 'new', cat: q.get('cat') || '', tag: q.get('tag') || '', q: q.get('q') || '', by, page: Number(q.get('page') || 1) };
    const r = await pageCall('video.list', args);
    if (!r.ok) { show(html`${hero}<div class="box"><p class="error">${r.error}</p></div>`); return; }
    const sortBar = html`<div class="video-tabs">${SORTS.map(([k, label]) =>
      html`<a class="btn small ${args.sort === k ? 'blue' : ''}" href="${feedLink(q, { sort: k === 'new' ? '' : k })}">${label}</a> `)}
      ${staff ? html`<a class="btn small" href="#/videos?sort=pending">Waiting for a check</a>` : ''}</div>`;
    const catBar = html`<div class="video-cats"><a class="${args.cat ? '' : 'on'}" href="${feedLink(q, { cat: '' })}">All</a>${Object.entries(VIDEO_CAT_NAMES).map(([k, n]) =>
      html`<a class="${args.cat === k ? 'on' : ''}" href="${feedLink(q, { cat: k })}">${n}</a>`)}</div>`;
    const channel = by && r.videos[0] ? r.videos[0].by : null;
    const title = q.get('by') === 'me' ? 'My videos' : channel ? channel.name + '\'s videos' : args.tag ? 'Tagged #' + args.tag : args.q ? 'Search: ' + args.q : 'Home';
    show(html`${hero}<div id="videoLiveRow"></div><div class="box"><h2 class="boxhead">${title} <span class="small muted">(${r.total})</span></h2>
      ${channel && q.get('by') !== 'me' ? html`<p class="small">${videoCount(channel.followers)} follower${channel.followers === 1 ? '' : 's'} &middot; <a href="#/user/${channel.userId}">Profile</a></p>` : ''}
      <form class="row video-search" data-video-search><input type="search" name="q" value="${args.q}" placeholder="Search videos" style="flex:1">
        <button class="btn small">Search</button></form>
      ${sortBar}${catBar}
      ${(args.tag || by || args.q) ? html`<p class="small"><a href="#/videos">&laquo; Back to all videos</a></p>` : ''}
      ${r.videos.length ? html`<div class="video-grid">${r.videos.map(card)}</div>`
        : html`<p class="muted">No videos here yet. <a href="#/videoupload">Be the first to upload one!</a></p>`}
      <p class="video-pages">${r.page > 1 ? html`<a class="btn small" href="${feedLink(q, { page: r.page - 1 })}">&laquo; Back</a> ` : ''}
        ${r.more ? html`<a class="btn small" href="${feedLink(q, { page: r.page + 1 })}">More &raquo;</a>` : ''}</p></div>`);
    if (ctx.liveRow && !by && !args.q && !args.tag && args.page === 1) ctx.liveRow(document.getElementById('videoLiveRow'));   // (streams.js)
    document.querySelector('[data-video-search]').addEventListener('submit', (e) => {
      e.preventDefault();
      location.hash = feedLink(q, { q: e.currentTarget.q.value.trim() });
    });
  };

  pages.watch = async (id) => {
    const r = await pageCall('video.get', { id });
    if (!r.ok) { show(html`<h1>Videos</h1><p class="error">${r.error}</p><p><a href="#/videos">&laquo; Back to videos</a></p>`); return; }
    const v = r.video;
    const staff = signedIn() && me().staff;
    const reviewBox = v.state === 'uploading' ? html`<div class="box info"><b>This upload didn't finish.</b> Delete it and upload the video again.</div>`
      : v.review ? html`<div class="box info"><b>${v.review === 'pending' ? 'Waiting for a staff check.' : 'This didn\'t pass the staff check.'}</b>
        ${v.review === 'pending' ? ' Only you and staff can watch it until then.' : v.reviewNote ? html` Staff said: "${v.reviewNote}"` : ''}
        ${staff && v.review === 'pending' ? html`<p><button class="btn green small" data-video="ok">Let it through</button>
          <button class="btn red small" data-video="no">Turn it down</button></p>` : ''}</div>` : '';
    const open = v.state === 'ready' && !v.review;
    const comments = v.commentList.map((c) => html`<div class="video-comment"><p><a href="#/user/${c.by.userId}">${c.by.name}</a>${verified(c.by.verified)}
        <span class="small muted">${ago(c.at)}</span>
        ${c.canDelete ? html` <a href="#" class="small" data-video-uncomment="${c.id}">Delete</a>` : ''}
        ${signedIn() && c.by.id !== me().id ? reportLink('video', v.id + ':' + c.id, 'this comment by ' + c.by.name) : ''}</p>
      <p class="video-comment-text">${c.text}</p></div>`);
    const followBtn = signedIn() && !v.mine
      ? html`<button class="btn small ${v.following ? '' : 'blue'}" data-act="follow" data-op="${v.following ? 'follow.remove' : 'follow.add'}" data-user="${v.by.id}">${v.following ? 'Following' : 'Follow'}</button>` : '';
    show(html`<p class="small"><a href="#/videos">&laquo; Videos</a></p>${reviewBox}
      <div class="video-watch">
        <div class="video-main">
          <div class="video-stage">${v.src ? html`<video controls playsinline preload="metadata" src="${v.src}" ${v.poster ? html`poster="${v.poster}"` : ''}></video>`
            : html`<span class="video-nopic">&#9654;</span>`}</div>
          <h1 class="video-title">${v.title}</h1>
          <div class="video-bar">
            <span class="video-channel">${person(v.by)} <span class="small muted">${videoCount(v.by.followers)} follower${v.by.followers === 1 ? '' : 's'}</span> ${followBtn}</span>
            <span class="video-actions"><button class="btn small ${v.liked ? 'red' : ''}" data-video="like" ${open ? '' : 'disabled'}>&#128077; ${v.liked ? 'Liked' : 'Like'} (${videoCount(v.likes)})</button>
              <button class="btn small" data-video="share" ${open ? '' : 'disabled'}>Share</button></span>
          </div>
          <div class="video-about box">
            <p class="small"><b>${videoCount(v.views)} views</b> &middot; ${ago(v.at)}${v.edited ? ' (edited)' : ''} &middot;
              <a href="#/videos?cat=${v.cat}">${VIDEO_CAT_NAMES[v.cat] || 'Other'}</a>
              ${v.tags.map((t) => html` <a class="video-tag" href="#/videos?tag=${t}">#${t}</a>`)}</p>
            ${v.text ? html`<p class="video-text">${v.text}</p>` : ''}
            <p class="small"><a href="#/videos?by=${v.by.userId}">More from ${v.by.name}</a>
              ${v.mine ? html` &middot; <a href="#" data-video="edit">Edit</a>` : ''}
              ${v.canDelete ? html` &middot; <a href="#" data-video="delete">Delete</a>` : ''}
              ${signedIn() && !v.mine ? html` &middot; ${reportLink('video', v.id, 'the video "' + v.title + '"')}` : ''}</p>
          </div>
          <div class="box"><h2 class="boxhead">Comments (${v.commentList.length})</h2>
            ${!open ? html`<p class="muted small">Comments open once everyone can watch it.</p>`
              : signedIn() ? html`<form class="form" data-video-comment><textarea name="text" maxlength="1000" rows="3" placeholder="Say something nice (or useful)" required></textarea>
                <p><button class="btn green">Post comment</button></p></form>` : needSignIn('comment')}
            ${comments.length ? comments : html`<p class="muted small">No comments yet.</p>`}</div>
        </div>
        <aside class="video-next box"><h2 class="boxhead">Up next</h2><div id="videoNext" class="small muted">Loading...</div></aside>
      </div>`);

    // Up next: more from the same person first, then what's popular.
    Promise.all([call('video.list', { by: v.by.userId }), call('video.list', { sort: 'week' }), call('video.list', { sort: 'top' })]).then((lists) => {
      const box = document.getElementById('videoNext');
      if (!box) return;
      const seen = new Set([v.id]), next = [];
      for (const l of lists) for (const x of (l.ok ? l.videos : [])) if (!seen.has(x.id) && !x.review && x.state === 'ready' && next.length < 10) { seen.add(x.id); next.push(x); }
      box.className = 'video-next-list';
      box.innerHTML = (next.length ? html`${next.map(card)}` : html`<p class="small muted">Nothing else yet.</p>`).s;
    });

    const again = () => pages.watch(id);
    document.querySelectorAll('[data-video]').forEach((b) => b.addEventListener('click', async (e) => {
      e.preventDefault();
      const what = b.dataset.video;
      if (what === 'like') {
        if (!signedIn()) { toast('Sign up to like videos.'); return; }
        const done = await call('video.like', { id, on: !v.liked });
        if (!done.ok) { toast(done.error); return; }
        again();
      } else if (what === 'share') {
        const link = location.origin + '/app/#/watch/' + encodeURIComponent(id);
        try { await navigator.clipboard.writeText(link); toast('Link copied!'); } catch { prompt('Copy this link:', link); }
      } else if (what === 'delete') {
        if (!confirm('Delete "' + v.title + '" for good?')) return;
        const done = await call('video.delete', { id });
        if (!done.ok) { toast(done.error); return; }
        toast('Deleted.');
        location.hash = '#/videos?by=me';
      } else if (what === 'edit') {
        location.hash = '#/videoupload?edit=' + encodeURIComponent(id);
      } else if (what === 'ok' || what === 'no') {
        const note = what === 'no' ? prompt('Why? (the uploader sees this)') : '';
        if (note === null) return;
        const done = await call('video.review', { id, ok: what === 'ok', note });
        if (!done.ok) { toast(done.error); return; }
        again();
      }
    }));
    const form = document.querySelector('[data-video-comment]');
    if (form) form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const done = await call('video.comment', { id, text: form.text.value });
      if (!done.ok) { toast(done.error); return; }
      again();
    });
    document.querySelectorAll('[data-video-uncomment]').forEach((a) => a.addEventListener('click', async (e) => {
      e.preventDefault();
      if (!confirm('Delete this comment?')) return;
      const done = await call('video.uncomment', { id, comment: a.dataset.videoUncomment });
      if (!done.ok) { toast(done.error); return; }
      again();
    }));
  };

  pages.videoupload = async () => {
    if (!signedIn()) { show(html`${hero}${needSignIn('upload videos')}`); return; }
    const editId = params().get('edit') || '';
    let old = null;
    if (editId) {
      const r = await pageCall('video.get', { id: editId });
      if (!r.ok || !r.video.mine) { show(html`<h1>Edit video</h1><p class="error">${r.ok ? 'Only the uploader can change this.' : r.error}</p>`); return; }
      old = r.video;
    }
    const waits = !me().verified && !me().staff;
    show(html`<div class="box video-upload"><h2 class="boxhead">${old ? 'Edit your video' : 'Upload a video'}</h2>
      ${old ? '' : html`<p class="small muted">MP4, MOV or WebM, up to ${kMostBytes >> 20} MB. It's free.
        Only upload videos you made yourself (or have the right to share), and follow the <a href="#/terms">rules</a>.
        ${waits ? 'Staff check new videos from people who aren\'t Verified before everyone can watch them.' : ''}</p>`}
      <form class="form" data-video-upload>
        ${old ? (old.poster ? html`<p><img class="video-preview" src="${old.poster}" alt=""></p>` : '')
          : html`<p><input type="file" name="file" accept="video/mp4,video/webm,video/quicktime,.mp4,.mov,.webm,.m4v" required></p>
            <p><video class="video-preview" id="videoPreview" controls muted playsinline hidden></video></p>`}
        <p><label>Title<br><input type="text" name="title" maxlength="100" required value="${old ? old.title : ''}" style="width:100%"></label></p>
        <p><label>Category<br><select name="cat">${Object.entries(VIDEO_CAT_NAMES).map(([k, n]) =>
          html`<option value="${k}" ${old && old.cat === k ? 'selected' : ''}>${n}</option>`)}</select></label></p>
        <p><label>Tags <span class="small muted">(up to 10, split by spaces or commas)</span><br>
          <input type="text" name="tags" maxlength="250" value="${old ? old.tags.join(' ') : ''}" placeholder="speedrun funny obby" style="width:100%"></label></p>
        <p><label>Description <span class="small muted">(optional)</span><br><textarea name="text" maxlength="3000" rows="5" style="width:100%">${old ? old.text : ''}</textarea></label></p>
        <p><label>Picture <span class="small muted">(optional: we take one from the video if you don't pick one)</span><br>
          <input type="file" name="pic" accept="image/png,image/jpeg,image/webp"></label></p>
        <div class="video-progress" id="videoProgress" hidden><span></span></div>
        <p><button class="btn green">${old ? 'Save' : 'Upload'}</button> <span id="videoMsg" class="small"></span></p>
      </form></div>`);
    const form = document.querySelector('[data-video-upload]');
    const msg = document.getElementById('videoMsg');
    const say = (text, bad) => { msg.textContent = text; msg.className = 'small' + (bad ? ' error' : ''); };
    let picked = null;   // what lookAtVideo found out
    if (form.file) form.file.addEventListener('change', async () => {
      picked = null;
      const file = form.file.files[0];
      if (!file) return;
      if (file.size > kMostBytes) { say('That video is too big (the most is ' + (kMostBytes >> 20) + ' MB). Try a shorter one, or save it smaller.', true); return; }
      say('Looking at it...');
      picked = await lookAtVideo(file);
      if (!form.title.value) form.title.value = file.name.replace(/\.[a-z0-9]+$/i, '').replace(/[_-]+/g, ' ').slice(0, 100);
      const pv = document.getElementById('videoPreview');
      pv.src = picked.url; pv.hidden = false;
      say(picked.unplayable ? 'This browser can\'t play that video, so others may not be able to either. MP4 works best.' : 'Ready: ' + videoTime(picked.secs) + ' long.', picked.unplayable);
    });
    // A picture they picked themselves, made small like the one from the video.
    const ownPicture = async () => {
      const f = form.pic.files[0];
      if (!f) return null;
      const url = URL.createObjectURL(f);
      try {
        const img = await new Promise((ok, bad) => { const i = new Image(); i.onload = () => ok(i); i.onerror = () => bad(new Error('That picture won\'t open.')); i.src = url; });
        return await snapshot(img, img.naturalWidth, img.naturalHeight);
      } finally { URL.revokeObjectURL(url); }
    };
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const btn = form.querySelector('button');
      const words = { title: form.title.value, cat: form.cat.value, tags: form.tags.value, text: form.text.value };
      let pic = null;
      try { pic = await ownPicture(); } catch (err) { say(err.message, true); return; }
      if (old) {
        btn.disabled = true; say('Saving...');
        const r = await call('video.edit', Object.assign({ id: old.id, thumb: pic ? await blobB64(pic) : '' }, words));
        btn.disabled = false;
        if (!r.ok) { say(r.error, true); return; }
        toast('Saved.');
        location.hash = '#/watch/' + old.id;
        return;
      }
      const file = form.file.files[0];
      if (!file || !picked) { say('Pick a video first.', true); return; }
      btn.disabled = true; say('Getting ready...');
      const thumb = pic || picked.thumb;
      const start = await call('video.start', Object.assign(words, { size: file.size, secs: picked.secs || 0, w: picked.w || 0, h: picked.h || 0,
        thumb: thumb ? await blobB64(thumb) : '' }));
      if (!start.ok) { btn.disabled = false; say(start.error, true); return; }
      const bar = document.getElementById('videoProgress');
      bar.hidden = false;
      const done = await sendFile(start.upload, file, (k) => {
        bar.firstElementChild.style.width = Math.round(k * 100) + '%';
        say('Uploading... ' + Math.round(k * 100) + '%');
      });
      btn.disabled = false;
      if (!done.ok) { say(done.error, true); return; }
      toast(done.video.review ? 'Uploaded! Staff will check it soon, then everyone can watch it.' : 'Uploaded!');
      location.hash = '#/watch/' + done.video.id;
    });
  };

  return pages;
}
