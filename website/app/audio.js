// The Audio portal's pages (worker/audio.js does the work on the server):
//   #/audio?sort=&cat=&genre=&tag=&by=&q=&page=   the list (newest, best this week, most faved, most played)
//   #/track/<id>                                  one sound: waveform, player, "use it in your game", faves, comments
//   #/audioupload[?edit=<id>]                     upload a sound (or change its category, genre and tags)
// Plus the player bar at the bottom of the screen: it keeps playing while you look around
// the site, and plays the next sound in the list when one ends.
// app.js hands over its helpers with audioPages(ctx), and adds the pages it returns.
//
// Every sound here is a Library audio asset, so it has an ID games can use (Sound.SoundId).

export const AUDIO_CAT_NAMES = { music: 'Music', loop: 'Loops', sfx: 'Sound effects', ambience: 'Ambience', voice: 'Voice clips', other: 'Other' };
export const AUDIO_GENRE_NAMES = { electronic: 'Electronic', chiptune: 'Chiptune', rock: 'Rock', metal: 'Metal', hiphop: 'Hip-hop', pop: 'Pop',
  orchestral: 'Orchestral', ambient: 'Ambient', lofi: 'Lo-fi', jazz: 'Jazz', funk: 'Funk', other: 'Other' };
const SORTS = [['new', 'Newest'], ['week', 'Best this week'], ['top', 'Most faved'], ['plays', 'Most played']];
const kMostBytes = 6 * 1024 * 1024;   // (worker/server.js MAX_SIZE.audio)
const kFee = 20;                      // (worker/server.js FEE.audio)
const EXTS = ['mp3', 'wav', 'ogg', 'flac'];

const params = () => new URLSearchParams((location.hash.split('?')[1] || ''));
const soundUrl = (id) => '/sound/' + encodeURIComponent(id);
const idOf = (t) => String(t.num || t.id);   // what games use (Sound.SoundId)
const hasGenre = (cat) => cat === 'music' || cat === 'loop';

// 75 -> "1:15"
export function audioTime(secs) {
  const s = Math.max(0, Math.floor(Number(secs) || 0));
  return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
}

// The loudest bit of each of `bars` slices of the sound, from 0 to 1 (for the waveform).
export function peaksOf(channels, bars) {
  const n = channels[0] ? channels[0].length : 0, out = new Array(bars).fill(0);
  if (!n) return out;
  const step = n / bars;
  let top = 0;
  for (let b = 0; b < bars; b++) {
    const from = Math.floor(b * step), to = Math.max(from + 1, Math.floor((b + 1) * step));
    const hop = Math.max(1, Math.floor((to - from) / 400));   // (a few hundred samples per bar is plenty)
    let m = 0;
    for (const ch of channels) for (let i = from; i < to; i += hop) { const v = Math.abs(ch[i] || 0); if (v > m) m = v; }
    out[b] = m;
    if (m > top) top = m;
  }
  return top > 0 ? out.map((v) => v / top) : out;
}

// The bar at the bottom of the screen. One for the whole site.
const bar = {
  el: null, audio: null, queue: [], at: -1, call: null, toast: null, srcs: new Map(), bytes: new Map(), heard: new Set(),
  get track() { return this.queue[this.at] || null; },
  ensure() {
    if (this.el) return;
    const el = document.createElement('div');
    el.className = 'audio-bar';
    el.hidden = true;
    el.innerHTML = '<button class="audio-bar-btn" data-ab="prev" title="Back" aria-label="Back">&#9198;</button>'
      + '<button class="audio-bar-btn audio-bar-play" data-ab="play" title="Play" aria-label="Play">&#9654;</button>'
      + '<button class="audio-bar-btn" data-ab="next" title="Next" aria-label="Next">&#9197;</button>'
      + '<div class="audio-bar-mid"><a class="audio-bar-title" href="#/audio"></a>'
      + '<div class="audio-bar-seek"><span class="audio-bar-time">0:00</span>'
      + '<input type="range" min="0" max="1000" value="0" aria-label="Where in the sound">'
      + '<span class="audio-bar-len">0:00</span></div></div>'
      + '<input class="audio-bar-vol" type="range" min="0" max="100" value="80" aria-label="Volume" title="Volume">'
      + '<button class="audio-bar-btn" data-ab="close" title="Close" aria-label="Close the player">&#10005;</button>';
    document.body.appendChild(el);
    this.el = el;
    const a = this.audio = new Audio();
    a.preload = 'metadata';
    try { a.volume = Math.max(0, Math.min(1, Number(localStorage.getItem('gbAudioVolume') || 0.8))); } catch { a.volume = 0.8; }
    el.querySelector('.audio-bar-vol').value = String(Math.round(a.volume * 100));
    const seek = el.querySelector('.audio-bar-seek input');
    let dragging = false;
    seek.addEventListener('input', () => { dragging = true; });
    seek.addEventListener('change', () => { dragging = false; if (a.duration) a.currentTime = a.duration * seek.value / 1000; });
    a.addEventListener('timeupdate', () => {
      if (!dragging && a.duration) seek.value = String(Math.round(1000 * a.currentTime / a.duration));
      el.querySelector('.audio-bar-time').textContent = audioTime(a.currentTime);
      this.changed();
    });
    a.addEventListener('loadedmetadata', () => { el.querySelector('.audio-bar-len').textContent = audioTime(a.duration); });
    a.addEventListener('play', () => { this.countPlay(); this.changed(); });
    a.addEventListener('pause', () => this.changed());
    a.addEventListener('ended', () => { if (this.at < this.queue.length - 1) this.play(this.at + 1); else this.changed(); });
    a.addEventListener('error', () => { if (this.track && this.toast) this.toast('That sound couldn\'t be played.'); });
    el.querySelector('.audio-bar-vol').addEventListener('input', (e) => {
      a.volume = Number(e.target.value) / 100;
      try { localStorage.setItem('gbAudioVolume', String(a.volume)); } catch { /* (private window) */ }
    });
    el.addEventListener('click', (e) => {
      const b = e.target.closest('[data-ab]');
      if (!b) return;
      const what = b.dataset.ab;
      if (what === 'play') this.toggle();
      else if (what === 'next' && this.at < this.queue.length - 1) this.play(this.at + 1);
      else if (what === 'prev') { if (a.currentTime > 3 || this.at === 0) a.currentTime = 0; else this.play(this.at - 1); }
      else if (what === 'close') { a.pause(); el.hidden = true; document.body.classList.remove('has-audio-bar'); this.changed(); }
    });
  },
  // Where the browser can get a sound: /sound/<id>, or (for one still waiting for a staff
  // check, which only its maker and staff can hear) the file through a signed request.
  async srcOf(t) {
    if (!t.review) return soundUrl(t.id);
    if (!this.srcs.has(t.id)) {
      const r = await this.call('get', { id: t.id });
      if (!r.ok) throw new Error(r.error);
      const type = { mp3: 'audio/mpeg', wav: 'audio/wav', ogg: 'audio/ogg', flac: 'audio/flac' }[t.ext] || '';
      const bin = atob(r.data), bytes = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
      this.bytes.set(t.id, bytes);
      this.srcs.set(t.id, URL.createObjectURL(new Blob([bytes], type ? { type } : {})));
    }
    return this.srcs.get(t.id);
  },
  async play(i, startAt = 0) {
    this.ensure();
    this.at = i;
    const t = this.track;
    if (!t) return;
    this.el.hidden = false;
    document.body.classList.add('has-audio-bar');
    const title = this.el.querySelector('.audio-bar-title');
    title.textContent = t.title + ' · ' + t.by.name;
    title.href = '#/track/' + encodeURIComponent(t.id);
    try { this.audio.src = await this.srcOf(t); } catch (e) { if (this.toast) this.toast(e.message); return; }
    if (startAt) this.audio.addEventListener('loadedmetadata', () => { this.audio.currentTime = startAt * (this.audio.duration || 0); }, { once: true });
    this.audio.play().catch(() => this.changed());
    this.changed();
  },
  // Play this sound (from this list), or pause/unpause it if it's the one playing.
  start(list, id, startAt = 0) {
    const i = list.findIndex((t) => t.id === id);
    if (i < 0) return;
    if (this.track && this.track.id === id && !startAt) { this.toggle(); return; }
    this.queue = list.slice();
    this.play(i, startAt);
  },
  toggle() {
    if (!this.audio || !this.track) return;
    if (this.audio.paused) this.audio.play().catch(() => {}); else this.audio.pause();
  },
  playing(id) { return !!this.audio && !!this.track && this.track.id === id && !this.audio.paused; },
  // A play counts once per sound while the page is open (the server counts one a day per person).
  countPlay() {
    const t = this.track;
    if (!t || this.heard.has(t.id) || !this.call) return;
    this.heard.add(t.id);
    this.call('audio.listen', { id: t.id }).catch(() => {});
  },
  // Let the page show what's playing (play buttons, the waveform's progress).
  changed() {
    if (!this.el) return;
    const on = this.track && !this.audio.paused;
    const btn = this.el.querySelector('.audio-bar-play');
    btn.innerHTML = on ? '&#10074;&#10074;' : '&#9654;';
    btn.title = on ? 'Pause' : 'Play';
    document.querySelectorAll('[data-audio-play]').forEach((b) => {
      const mine = this.playing(b.dataset.audioPlay);
      b.classList.toggle('on', mine);
      b.innerHTML = mine ? '&#10074;&#10074;' : '&#9654;';
      b.setAttribute('aria-label', mine ? 'Pause' : 'Play');
    });
    const wave = document.querySelector('canvas[data-wave]');
    if (wave && wave.draw) wave.draw(this.track && this.track.id === wave.dataset.wave && this.audio.duration ? this.audio.currentTime / this.audio.duration : 0);
  },
};

export function audioPages(ctx) {
  const { html, show, call, pageCall, signedIn, toast, ago, needSignIn, reportLink, verified, fileBase64 } = ctx;
  const me = () => ctx.me();
  bar.call = call; bar.toast = toast;
  const person = (p) => html`<a href="#/user/${p.userId || p.id}">${p.name}</a>${verified(p.verified)}`;
  const kindLine = (t) => (AUDIO_CAT_NAMES[t.cat] || 'Other') + (t.genre ? ' · ' + (AUDIO_GENRE_NAMES[t.genre] || t.genre) : '');

  const hero = html`<div class="portal-hero audio-hero"><div><span class="portal-kicker">THE AUDIO PORTAL</span><h1>Audio</h1>
      <p>Music, loops and sound effects from the Guts&amp;Bolts crowd. Listen right here, fave the best ones, and put any of them
        in your own games for free.</p>
      <p><a class="btn green" href="#/audioupload">Upload a sound</a></p></div></div>`;

  // Build a list address from what's picked now, with some things changed.
  const listLink = (cur, change) => {
    const q = new URLSearchParams(cur);
    for (const [k, v] of Object.entries(change)) { if (v === '' || v === null || v === undefined) q.delete(k); else q.set(k, String(v)); }
    if (!('page' in change)) q.delete('page');
    const s = q.toString();
    return '#/audio' + (s ? '?' + s : '');
  };

  const row = (t, n) => html`<tr class="audio-row">
      <td class="audio-n">${n}</td>
      <td><button class="audio-play" data-audio-play="${t.id}" aria-label="Play">&#9654;</button></td>
      <td class="audio-name"><a href="#/track/${t.id}"><b>${t.title}</b></a><span class="small muted">by ${t.by.name}</span>
        ${t.review ? html`<span class="small audio-flag">${t.review === 'pending' ? 'Waiting for a check' : 'Didn\'t pass'}</span>`
          : t.access === 'private' ? html`<span class="small audio-flag">Private</span>` : ''}</td>
      <td class="audio-kind small">${kindLine(t)}</td>
      <td class="audio-num small" title="Plays">&#9654; ${t.plays}</td>
      <td class="audio-num small" title="Faves">&#9829; ${t.faves}</td>
      <td class="audio-when small muted">${ago(t.at)}</td></tr>`;

  const hookPlays = (list) => {
    document.querySelectorAll('[data-audio-play]').forEach((b) => b.addEventListener('click', (e) => {
      e.preventDefault();
      bar.start(list, b.dataset.audioPlay);
    }));
    bar.changed();
  };

  const pages = {};

  pages.audio = async () => {
    const q = params();
    const by = q.get('by') === 'me' ? (signedIn() ? me().userId : '') : q.get('by') || '';
    const args = { sort: q.get('sort') || 'new', cat: q.get('cat') || '', genre: q.get('genre') || '', tag: q.get('tag') || '',
      q: q.get('q') || '', by, page: Number(q.get('page') || 1) };
    const r = await pageCall('audio.list', args);
    if (!r.ok) { show(html`${hero}<div class="box"><p class="error">${r.error}</p></div>`); return; }
    const sortBar = html`<div class="art-tabs">${SORTS.map(([k, label]) =>
      html`<a class="btn small ${args.sort === k ? 'blue' : ''}" href="${listLink(q, { sort: k === 'new' ? '' : k })}">${label}</a> `)}</div>`;
    const catBar = html`<div class="art-cats"><a class="${args.cat ? '' : 'on'}" href="${listLink(q, { cat: '', genre: '' })}">All</a>${Object.entries(AUDIO_CAT_NAMES).map(([k, n]) =>
      html`<a class="${args.cat === k ? 'on' : ''}" href="${listLink(q, { cat: k, genre: hasGenre(k) ? args.genre : '' })}">${n}</a>`)}</div>`;
    const genreBar = !args.cat || hasGenre(args.cat) ? html`<div class="art-cats audio-genres"><span class="muted">Genre:</span>
      <a class="${args.genre ? '' : 'on'}" href="${listLink(q, { genre: '' })}">Any</a>${Object.entries(AUDIO_GENRE_NAMES).map(([k, n]) =>
      html`<a class="${args.genre === k ? 'on' : ''}" href="${listLink(q, { genre: k })}">${n}</a>`)}</div>` : '';
    const title = q.get('by') === 'me' ? 'My audio' : by && r.tracks[0] ? 'Audio by ' + r.tracks[0].by.name
      : args.tag ? 'Tagged #' + args.tag : args.cat ? AUDIO_CAT_NAMES[args.cat] : 'All audio';
    const first = (r.page - 1) * 30;
    show(html`${hero}<div class="box"><h2 class="boxhead">${title} <span class="small muted">(${r.total})</span></h2>
      <form class="row art-search" data-audio-search><input type="search" name="q" value="${args.q}" placeholder="Search names and tags" style="flex:1">
        <button class="btn small">Search</button></form>
      ${sortBar}${catBar}${genreBar}
      ${(args.tag || by || args.q) ? html`<p class="small"><a href="#/audio">&laquo; Back to all audio</a></p>` : ''}
      ${r.tracks.length ? html`<div class="audio-table-wrap"><table class="audio-table"><tbody>${r.tracks.map((t, i) => row(t, first + i + 1))}</tbody></table></div>`
        : html`<p class="muted">No sounds here yet. <a href="#/audioupload">Be the first to upload one!</a></p>`}
      <p class="art-pages">${r.page > 1 ? html`<a class="btn small" href="${listLink(q, { page: r.page - 1 })}">&laquo; Back</a> ` : ''}
        ${r.more ? html`<a class="btn small" href="${listLink(q, { page: r.page + 1 })}">More &raquo;</a>` : ''}</p>
      <p class="small muted">Every sound here is free to use in your own games. Open one to get its ID.</p></div>`);
    hookPlays(r.tracks);
    document.querySelector('[data-audio-search]').addEventListener('submit', (e) => {
      e.preventDefault();
      location.hash = listLink(q, { q: e.currentTarget.q.value.trim() });
    });
  };

  // The waveform: the sound's loudness drawn as bars, played part in red. Click it to jump there.
  async function drawWave(canvas, t, list) {
    let peaks = new Array(160).fill(0.15);
    const paint = (done) => {
      const w = canvas.clientWidth || 600, h = canvas.clientHeight || 110, dpr = window.devicePixelRatio || 1;
      if (canvas.width !== Math.round(w * dpr)) { canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr); }
      const g = canvas.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, w, h);
      const style = getComputedStyle(canvas);
      const played = style.getPropertyValue('--wave-played').trim() || '#d4202a', rest = style.getPropertyValue('--wave-rest').trim() || '#8a8f98';
      const bw = w / peaks.length;
      peaks.forEach((p, i) => {
        const bh = Math.max(2, p * (h - 6));
        g.fillStyle = (i + 0.5) / peaks.length <= done ? played : rest;
        g.fillRect(i * bw + 0.5, (h - bh) / 2, Math.max(1, bw - 1.5), bh);
      });
    };
    canvas.draw = paint;
    paint(0);
    canvas.addEventListener('click', (e) => {
      const box = canvas.getBoundingClientRect(), x = Math.max(0, Math.min(1, (e.clientX - box.left) / box.width));
      if (bar.track && bar.track.id === t.id && bar.audio.duration) { bar.audio.currentTime = x * bar.audio.duration; if (bar.audio.paused) bar.toggle(); }
      else bar.start(list, t.id, x);
    });
    try {
      // (a waiting sound's bytes came through a signed request: the page can't fetch() a blob: address)
      const bytes = t.review ? (await bar.srcOf(t), bar.bytes.get(t.id).slice().buffer) : await (await fetch(soundUrl(t.id))).arrayBuffer();
      const Ctx = window.AudioContext || window.webkitAudioContext;
      if (!Ctx) return;
      const ac = new Ctx();
      const buf = await ac.decodeAudioData(bytes);
      const chans = [];
      for (let c = 0; c < Math.min(2, buf.numberOfChannels); c++) chans.push(buf.getChannelData(c));
      peaks = peaksOf(chans, 160);
      const len = document.getElementById('trackLen');
      if (len) len.textContent = audioTime(buf.duration);
      if (ac.close) ac.close();
      bar.changed();
      if (!bar.track || bar.track.id !== t.id) paint(0);
    } catch { /* (the flat bars stay: it still plays) */ }
  }

  pages.track = async (id) => {
    const r = await pageCall('audio.get', { id });
    if (!r.ok) { show(html`<h1>Audio</h1><p class="error">${r.error}</p><p><a href="#/audio">&laquo; Back to the Audio portal</a></p>`); return; }
    const t = r.track;
    const num = idOf(t);
    const comments = t.commentList.map((c) => html`<div class="art-comment"><p>${person(c.by)} <span class="small muted">${ago(c.at)}</span>
        ${c.canDelete ? html` <a href="#" class="small" data-audio-uncomment="${c.id}">Delete</a>` : ''}
        ${signedIn() && c.by.id !== me().id ? reportLink('audio', t.id + ':' + c.id, 'this comment by ' + c.by.name) : ''}</p>
      <p class="art-comment-text">${c.text}</p></div>`);
    const notice = t.review ? html`<div class="box info"><b>${t.review === 'pending' ? 'Waiting for a staff check.' : 'This didn\'t pass the staff check.'}</b>
        Only you and staff can hear it until then (Verified creators skip this).</div>`
      : t.access === 'private' ? html`<div class="box info"><b>This sound is private.</b> It isn't in the Audio portal, but your games can still use it.
        To share it, open it in the <a href="#/library/${num}">Library</a> and make it public.</div>` : '';
    show(html`<p class="small"><a href="#/audio">&laquo; Audio portal</a></p>${notice}
      <div class="audio-track">
        <div class="audio-stage box">
          <div class="audio-stage-top">
            <button class="audio-play audio-play-big" data-audio-play="${t.id}" aria-label="Play">&#9654;</button>
            <div><h1>${t.title}</h1>
              <p>by ${person(t.by)} <span class="small muted">&middot; ${kindLine(t)} &middot; ${ago(t.at)}</span></p></div>
          </div>
          <canvas class="audio-wave" data-wave="${t.id}" role="img" aria-label="The sound's waveform. Click to jump to a spot."></canvas>
          <p class="small muted audio-stage-foot"><span id="trackLen"></span> &middot; ${(t.ext || '').toUpperCase()} &middot; ${Math.max(1, Math.round(t.size / 1024))} KB
            &middot; &#9654; ${t.plays} plays</p>
          <p><button class="btn ${t.faved ? 'red' : ''}" data-audio="fave" ${t.review ? 'disabled' : ''}>&#9829; ${t.faved ? 'Faved' : 'Fave'} (${t.faves})</button></p>
          ${t.text ? html`<p class="art-text">${t.text}</p>` : ''}
          <p class="small">${t.tags.map((g) => html`<a class="art-tag" href="#/audio?tag=${g}">#${g}</a> `)}</p>
          <p class="small"><a href="#/audio?by=${t.by.userId}">More by ${t.by.name}</a>
            ${t.canTag ? html` &middot; <a href="#/audioupload?edit=${encodeURIComponent(t.id)}">Change category and tags</a>` : ''}
            ${t.mine ? html` &middot; <a href="#/library/${num}">Name, words, privacy</a>` : ''}
            ${signedIn() && !t.mine ? html` &middot; ${reportLink('audio', t.id, 'the sound "' + t.title + '"')}` : ''}</p>
        </div>
        <div class="audio-use box">
          <h2 class="boxhead">Use it in your game</h2>
          <p class="small">It's free. In Studio, add a <b>Sound</b>, then paste this ID into its <b>SoundId</b>.</p>
          <p class="row"><input type="text" class="asset-id" value="${num}" readonly aria-label="Sound ID" style="flex:1">
            <button class="btn blue" data-audio="copy">Copy ID</button></p>
          <p class="small">Or from a script:</p>
          <pre class="audio-code">local sound = Instance.new("Sound")
sound.SoundId = "${num}"
sound.Parent = workspace
sound:Play()</pre>
        </div>
      </div>
      <div class="box"><h2 class="boxhead">Comments (${t.commentList.length})</h2>
        ${t.review ? html`<p class="muted small">Comments open once it passes the staff check.</p>`
          : signedIn() ? html`<form class="form" data-audio-comment><textarea name="text" maxlength="500" rows="3" placeholder="What do you think of it?" required></textarea>
            <p><button class="btn green">Post comment</button></p></form>` : needSignIn('comment')}
        ${comments.length ? comments : html`<p class="muted small">No comments yet.</p>`}</div>`);

    // Play this one; when it ends, the bar carries on with nothing after it (it's on its own page).
    const list = bar.track && bar.queue.some((x) => x.id === t.id) ? bar.queue.map((x) => (x.id === t.id ? t : x)) : [t];
    hookPlays(list);
    drawWave(document.querySelector('canvas[data-wave]'), t, list);
    const again = () => pages.track(id);
    document.querySelectorAll('[data-audio]').forEach((b) => b.addEventListener('click', async (e) => {
      e.preventDefault();
      const what = b.dataset.audio;
      if (what === 'copy') {
        try { await navigator.clipboard.writeText(num); toast('Copied ' + num + '. Paste it into a Sound\'s SoundId.'); }
        catch { document.querySelector('.audio-use .asset-id').select(); }
      } else if (what === 'fave') {
        if (!signedIn()) { toast('Sign up to fave sounds.'); return; }
        const done = await call('audio.fave', { id: t.id, on: !t.faved });
        if (!done.ok) { toast(done.error); return; }
        again();
      }
    }));
    const form = document.querySelector('[data-audio-comment]');
    if (form) form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const done = await call('audio.comment', { id: t.id, text: form.text.value });
      if (!done.ok) { toast(done.error); return; }
      again();
    });
    document.querySelectorAll('[data-audio-uncomment]').forEach((a) => a.addEventListener('click', async (e) => {
      e.preventDefault();
      if (!confirm('Delete this comment?')) return;
      const done = await call('audio.uncomment', { id: t.id, comment: a.dataset.audioUncomment });
      if (!done.ok) { toast(done.error); return; }
      again();
    }));
  };

  pages.audioupload = async () => {
    if (!signedIn()) { show(html`${hero}${needSignIn('upload sounds')}`); return; }
    const editId = params().get('edit') || '';
    let old = null;
    if (editId) {
      const r = await pageCall('audio.get', { id: editId });
      if (!r.ok || !r.track.canTag) { show(html`<h1>Change a sound</h1><p class="error">${r.ok ? 'Only the person who uploaded it can change this.' : r.error}</p>`); return; }
      old = r.track;
    }
    const waits = !me().verified && !me().staff;
    const pick = (cur) => html`<option value="">(none)</option>${Object.entries(AUDIO_GENRE_NAMES).map(([k, n]) => html`<option value="${k}" ${cur === k ? 'selected' : ''}>${n}</option>`)}`;
    show(html`<div class="box art-post"><h2 class="boxhead">${old ? 'Change "' + old.title + '"' : 'Upload a sound'}</h2>
      ${old ? '' : html`<p class="small muted">An .mp3, .wav, .ogg or .flac file, up to ${kMostBytes >> 20} MB. Uploading costs ${kFee} Bolts.
        Only upload sounds you made yourself (or have the right to share), and follow the <a href="#/terms">rules</a>.
        ${waits ? 'Staff check new sounds from people who aren\'t Verified before anyone else can hear them.' : ''}</p>`}
      <form class="form" data-audio-post>
        ${old ? '' : html`<p><input type="file" name="file" accept=".mp3,.wav,.ogg,.flac,audio/mpeg,audio/wav,audio/ogg,audio/flac" required></p>
          <p><audio id="audioPreview" controls hidden></audio></p>
          <p><label>Name<br><input type="text" name="title" maxlength="50" required style="width:100%"></label></p>
          <p><label>About it <span class="small muted">(optional)</span><br><textarea name="text" maxlength="1000" rows="3" style="width:100%"></textarea></label></p>`}
        <p class="row" style="gap:12px;flex-wrap:wrap"><label>Category<br><select name="cat">${Object.entries(AUDIO_CAT_NAMES).map(([k, n]) =>
          html`<option value="${k}" ${(old ? old.cat === k : k === 'music') ? 'selected' : ''}>${n}</option>`)}</select></label>
          <label>Genre <span class="small muted">(music and loops)</span><br><select name="genre">${pick(old ? old.genre : '')}</select></label></p>
        <p><label>Tags <span class="small muted">(up to 8, split by spaces or commas)</span><br>
          <input type="text" name="tags" maxlength="200" value="${old ? old.tags.join(' ') : ''}" placeholder="boss battle 8bit" style="width:100%"></label></p>
        ${old ? '' : html`<p><label style="font-weight:normal"><input type="checkbox" name="public"> <b>Public</b>: show it in the Audio portal and the Library,
          so anyone can hear it and use it</label><br><span class="small muted">Leave it off to keep it to yourself (your games can still use it).</span></p>`}
        <p><button class="btn green">${old ? 'Save' : 'Upload it'}</button> <span id="audioMsg" class="small"></span></p>
      </form></div>`);
    const form = document.querySelector('[data-audio-post]');
    const msg = document.getElementById('audioMsg');
    const say = (text, bad) => { msg.textContent = text; msg.className = 'small' + (bad ? ' error' : ''); };
    const syncGenre = () => { form.genre.disabled = !hasGenre(form.cat.value); };
    form.cat.addEventListener('change', syncGenre);
    syncGenre();
    if (form.file) form.file.addEventListener('change', () => {
      const file = form.file.files[0];
      if (!file) return;
      const pv = document.getElementById('audioPreview');
      pv.src = URL.createObjectURL(file); pv.hidden = false;
      if (!form.title.value) form.title.value = file.name.replace(/\.[a-z0-9]+$/i, '').replace(/[_-]+/g, ' ').slice(0, 50);
      say(file.size > kMostBytes ? 'That file is too big (the most is ' + (kMostBytes >> 20) + ' MB).' : '', file.size > kMostBytes);
    });
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const tagArgs = { cat: form.cat.value, genre: hasGenre(form.cat.value) ? form.genre.value : '', tags: form.tags.value };
      const btn = form.querySelector('button');
      if (old) {
        btn.disabled = true; say('Saving...');
        const r = await call('audio.tag', Object.assign({ id: old.id }, tagArgs));
        btn.disabled = false;
        if (!r.ok) { say(r.error, true); return; }
        toast('Saved.');
        location.hash = '#/track/' + encodeURIComponent(old.id);
        return;
      }
      const file = form.file.files[0];
      if (!file) { say('Pick a sound first.', true); return; }
      const ext = (file.name.split('.').pop() || '').toLowerCase();
      if (!EXTS.includes(ext)) { say('Sounds must be .mp3, .wav, .ogg or .flac.', true); return; }
      if (file.size > kMostBytes) { say('That file is too big (the most is ' + (kMostBytes >> 20) + ' MB).', true); return; }
      btn.disabled = true; say('Uploading...');
      const up = await call('upload', { kind: 'audio', name: form.title.value, description: form.text.value, data: await fileBase64(file),
        meta: { ext }, access: form.public.checked ? 'public' : 'private' });
      if (!up.ok) { btn.disabled = false; say(up.error, true); return; }
      const tagged = await call('audio.tag', Object.assign({ id: up.asset.id }, tagArgs));
      btn.disabled = false;
      if (!tagged.ok) toast('Uploaded, but its category didn\'t save: ' + tagged.error);
      toast('Uploaded "' + up.asset.name + '"!' + (up.fee ? ' (' + up.fee + ' Bolts)' : '')
        + (up.asset.review === 'pending' ? ' Staff check it before anyone else can hear it.' : ''));
      location.hash = '#/track/' + encodeURIComponent(up.asset.id);
    });
  };

  return pages;
}
