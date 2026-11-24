// G&B Live's pages (worker/streams.js does the work on the server). Part of G&B Videos:
//   #/live          who's live now (and who was live a little while ago)
//   #/live/<id>     watch one stream, with its chat
//   #/golive        go live: pick your camera or screen, a title, then press Go live
// app.js hands over its helpers with streamPages(ctx), and adds the pages it returns (and gives
// liveRow, the "Live now" row, to videos.js).
//
// How the video gets from the streamer to watchers, without a paid streaming service:
//   - The streamer's browser records kSegSecs seconds at a time with MediaRecorder. Every piece
//     is a whole small WebM video (a new recorder starts just before the old one stops), and is
//     PUT to the server, which keeps the newest few in Cloudflare R2.
//   - Watchers' browsers fetch each new piece and feed them, one after another, into one
//     <video> through Media Source Extensions ('sequence' mode lines up their times).
import { VIDEO_CAT_NAMES, videoCount } from './videos.js';

const POLL_MS = 2500;   // how often the page asks what's new (newest piece, chat, viewers)
const SNAP_MS = 60000;  // how often the streamer's page sends a new picture for the list
const QUALITY = { 480: [854, 480, 900000], 720: [1280, 720, 1800000] };   // height -> [w, h, bits a second]
// What this browser records in, best first (worker/streams.js STREAM_MIMES).
const MIMES = ['video/webm;codecs=vp8,opus', 'video/webm;codecs=vp9,opus', 'video/webm;codecs=vp8', 'video/webm;codecs=vp9', 'video/webm'];

// 45 -> "0:45", 3725 -> "1:02:05": how long a stream has been on.
export function liveSince(at, t = Date.now() / 1000) {
  const s = Math.max(0, Math.floor(t - at)), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60;
  return (h ? h + ':' + String(m).padStart(2, '0') : String(m)) + ':' + String(r).padStart(2, '0');
}
const MSE = () => window.MediaSource || window.ManagedMediaSource;
export const canWatchLive = (mime) => { const M = MSE(); return !!M && M.isTypeSupported(mime); };
export const recordMime = () => (window.MediaRecorder ? MIMES.find((m) => MediaRecorder.isTypeSupported(m)) || '' : '');

// A watcher id for people without an account, so each browser counts once.
function guestViewer() {
  try {
    let v = localStorage.getItem('gbLiveViewer');
    if (!v) { v = Math.random().toString(36).slice(2, 14); localStorage.setItem('gbLiveViewer', v); }
    return v;
  } catch { return 'x' + Math.random().toString(36).slice(2, 10); }
}
// A JPEG of what's on screen, for the list of streams.
async function snapJpeg(video) {
  if (!video.videoWidth) return '';
  const k = Math.min(1, 480 / video.videoWidth);
  const c = document.createElement('canvas');
  c.width = Math.round(video.videoWidth * k); c.height = Math.round(video.videoHeight * k);
  c.getContext('2d').drawImage(video, 0, 0, c.width, c.height);
  const blob = await new Promise((ok) => c.toBlob(ok, 'image/jpeg', 0.72));
  if (!blob || blob.size > 200 * 1024) return '';
  return new Promise((ok) => { const r = new FileReader(); r.onload = () => ok(String(r.result).replace(/^data:[^,]*,/, '')); r.onerror = () => ok(''); r.readAsDataURL(blob); });
}

// Plays a stream's pieces in one <video>. feed(stream) is called after each stream.get.
class LivePlayer {
  constructor(video, stream, onNote) {
    this.video = video; this.mime = stream.mime; this.seg = stream.segSecs || 4; this.onNote = onNote;
    this.src = stream.src; this.next = -1; this.busy = false; this.dead = false;
    this.open();
  }
  open() {
    const M = MSE();
    this.ms = new M();
    this.sb = null;
    this.video.disableRemotePlayback = true;   // (iPhone's ManagedMediaSource wants this)
    this.video.src = URL.createObjectURL(this.ms);
    this.ms.addEventListener('sourceopen', () => {
      try { this.sb = this.ms.addSourceBuffer(this.mime); this.sb.mode = 'sequence'; }
      catch { this.onNote('This browser can\'t play this stream. Try Chrome, Edge or Firefox.'); this.dead = true; }
    }, { once: true });
  }
  append(bytes) {
    return new Promise((ok) => {
      const done = () => { this.sb.removeEventListener('updateend', done); this.sb.removeEventListener('error', done); ok(); };
      this.sb.addEventListener('updateend', done); this.sb.addEventListener('error', done);
      try { this.sb.appendBuffer(bytes); } catch { done(); }
    });
  }
  async feed(stream) {
    if (this.dead || this.busy || !this.sb || stream.last < 0) return;
    this.busy = true;
    try {
      // Start two pieces behind the newest, so there's always one on its way.
      if (this.next < 0 || this.next < stream.first) this.next = Math.max(stream.first, stream.last - 1);
      while (this.next <= stream.last && !this.dead) {
        const n = this.next++;
        const r = await fetch(this.src + n).catch(() => null);
        if (!r || !r.ok) continue;   // (a piece that didn't make it: skip it)
        const bytes = await r.arrayBuffer();
        if (this.ms.readyState !== 'open' || this.video.error) { this.restart(stream); return; }
        await this.append(bytes);
        if (this.video.error) { this.restart(stream); return; }
      }
      const b = this.video.buffered;
      if (b.length) {
        const end = b.end(b.length - 1);
        // Fell far behind (a slow connection, or the tab was hidden): jump to near the newest.
        if (end - this.video.currentTime > this.seg * 3 + 2) this.video.currentTime = Math.max(b.start(b.length - 1), end - this.seg);
        // Let go of what's long been watched, so memory doesn't fill up.
        if (this.video.currentTime - b.start(0) > 60 && !this.sb.updating) { try { this.sb.remove(b.start(0), this.video.currentTime - 30); } catch { /* next time */ } }
        if (this.video.paused && !this.video.ended) this.video.play().catch(() => {});
      }
    } finally { this.busy = false; }
  }
  // Something went wrong with the pieces: start again from the newest one.
  restart(stream) {
    this.busy = false;
    this.next = stream.last;
    this.open();
  }
  stop() { this.dead = true; try { if (this.ms.readyState === 'open') this.ms.endOfStream(); } catch { /* already done */ } }
}

export function streamPages(ctx) {
  const { html, show, call, pageCall, signedIn, toast, ago, needSignIn, reportLink, verified } = ctx;
  const me = () => ctx.me();
  const person = (p) => html`<a href="#/videos?by=${p.userId}">${p.name}</a>${verified(p.verified)}`;
  const hero = html`<div class="portal-hero video-hero live-hero"><div><span class="portal-kicker">G&amp;B LIVE</span><h1>Live</h1>
      <p>Watch people stream games, art, music and talk shows, and chat with them while it happens. Going live is free, right from your browser.</p>
      <p><a class="btn red" href="#/golive">&#9679; Go live</a> <a class="btn" href="#/videos">Videos</a></p></div></div>`;
  const card = (s) => html`<a class="video-card live-card" href="#/live/${s.id}">
      <span class="video-thumb">${s.poster ? html`<img loading="lazy" src="${s.poster}" alt="${s.title}">` : html`<span class="video-nopic">&#9679;</span>`}
        ${s.state === 'live' ? html`<span class="live-badge">LIVE</span><span class="video-time">${videoCount(s.viewers)} watching</span>`
          : html`<span class="video-time">Ended</span>`}</span>
      <span class="video-card-text"><b>${s.title}</b><span class="small muted">${s.by.name}</span>
        <span class="small muted">${VIDEO_CAT_NAMES[s.cat] || 'Other'} &middot; ${s.state === 'live' ? 'started ' + ago(s.at) : 'was live ' + ago(s.ended)}</span></span></a>`;

  // Chat lines, with the tools you're allowed to use on each.
  const chatLine = (stream, c) => html`<div class="live-line" data-line="${c.id}"><a class="live-who" href="#/user/${c.by.userId}">${c.by.name}</a>${verified(c.by.verified)}
      <span class="live-text">${c.text}</span>
      <span class="live-tools">${c.canDelete ? html`<a href="#" data-chat-del="${c.id}" title="Delete">&times;</a>` : ''}
        ${stream.canMod && c.by.id !== stream.by.id && c.by.id !== me().id ? html`<a href="#" data-chat-timeout="${c.by.id}" title="Time out for 10 minutes">&#9208;</a>` : ''}
        ${signedIn() && c.by.id !== me().id ? reportLink('stream', stream.id + ':' + c.id, 'this chat line by ' + c.by.name) : ''}</span></div>`;

  // The chat panel: used by watchers and by the streamer's own page.
  function chatPanel(stream) {
    return html`<aside class="live-chat box"><h2 class="boxhead">Live chat</h2>
      <div class="live-lines" id="liveLines"><p class="small muted" id="liveQuiet">Nothing yet. Say hi!</p></div>
      ${stream.state !== 'live' ? html`<p class="small muted">This stream has ended, so its chat is closed.</p>`
        : signedIn() ? html`<form class="live-say" data-live-say><input type="text" name="text" maxlength="300" placeholder="Say something" autocomplete="off" required>
          <button class="btn small green">Send</button></form><p class="small muted" id="liveSlow">${stream.slow ? 'Slow mode: one line every ' + stream.slow + ' seconds.' : ''}</p>`
          : needSignIn('chat')}
      ${stream.canMod && stream.state === 'live' ? html`<p class="small">Slow mode: <select data-live-slow>${[0, 5, 15, 60].map((n) =>
        html`<option value="${n}" ${stream.slow === n ? 'selected' : ''}>${n ? n + ' seconds' : 'off'}</option>`)}</select></p>` : ''}</aside>`;
  }
  // Keeps the chat panel up to date; returns poll(), which asks the server what's new.
  function chatKeeper(id, onStream) {
    let after = 0, stream = null, stopped = false;
    const lines = () => document.getElementById('liveLines');
    const viewer = signedIn() ? '' : guestViewer();
    const poll = async () => {
      if (stopped) return null;
      const r = await call('stream.get', { id, after, viewer });
      if (!r.ok || !lines()) return r;
      stream = r.stream;
      if (r.chat.length) {
        document.getElementById('liveQuiet')?.remove();
        const box = lines(), stick = box.scrollHeight - box.scrollTop - box.clientHeight < 40;
        box.insertAdjacentHTML('beforeend', r.chat.map((c) => chatLine(stream, c).s).join(''));
        after = r.chat[r.chat.length - 1].n;
        while (box.children.length > 200) box.firstElementChild.remove();
        if (stick) box.scrollTop = box.scrollHeight;
      }
      onStream(r);
      return r;
    };
    document.addEventListener('click', async function tools(e) {
      if (!lines()) { document.removeEventListener('click', tools); return; }
      const del = e.target.closest('[data-chat-del]'), out = e.target.closest('[data-chat-timeout]');
      if (!del && !out) return;
      e.preventDefault();
      if (del) {
        const r = await call('stream.unchat', { id, chat: del.dataset.chatDel });
        if (!r.ok) { toast(r.error); return; }
        del.closest('.live-line').remove();
      } else {
        if (!confirm('Time this person out of the chat for 10 minutes? Their lines go too.')) return;
        const r = await call('stream.timeout', { id, user: out.dataset.chatTimeout });
        if (!r.ok) { toast(r.error); return; }
        document.querySelectorAll('[data-chat-timeout="' + CSS.escape(out.dataset.chatTimeout) + '"]').forEach((a) => a.closest('.live-line').remove());
        toast('Timed out for 10 minutes.');
      }
    });
    const form = document.querySelector('[data-live-say]');
    if (form) form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const text = form.text.value;
      if (!text.trim()) return;
      const r = await call('stream.chat', { id, text });
      if (!r.ok) { toast(r.error); return; }
      form.text.value = '';
      poll();
    });
    const slow = document.querySelector('[data-live-slow]');
    if (slow) slow.addEventListener('change', async () => {
      const r = await call('stream.slow', { id, secs: Number(slow.value) });
      if (!r.ok) toast(r.error); else toast(r.stream.slow ? 'Slow mode is on.' : 'Slow mode is off.');
    });
    return { poll, stop: () => { stopped = true; } };
  }

  const pages = {};

  // #/live: who's live; #/live/<id>: watch one.
  pages.live = async (id) => {
    if (!id) {
      const r = await pageCall('stream.list', {});
      if (!r.ok) { show(html`${hero}<div class="box"><p class="error">${r.error}</p></div>`); return; }
      show(html`${hero}<div class="box"><h2 class="boxhead">Live now <span class="small muted">(${r.live.length})</span></h2>
        ${r.live.length ? html`<div class="video-grid" id="liveGrid">${r.live.map(card)}</div>`
          : html`<p class="muted">Nobody's live right now. <a href="#/golive">Be the first!</a></p>`}</div>
        ${r.recent.length ? html`<div class="box"><h2 class="boxhead">Live a little while ago</h2><div class="video-grid">${r.recent.map(card)}</div></div>` : ''}`);
      return;
    }
    const r = await pageCall('stream.get', { id, viewer: signedIn() ? '' : guestViewer() });
    if (!r.ok) { show(html`<h1>Live</h1><p class="error">${r.error}</p><p><a href="#/live">&laquo; Back to Live</a></p>`); return; }
    const s = r.stream;
    const playable = canWatchLive(s.mime);
    const followBtn = signedIn() && !s.mine
      ? html`<button class="btn small ${s.following ? '' : 'blue'}" data-act="follow" data-op="${s.following ? 'follow.remove' : 'follow.add'}" data-user="${s.by.id}">${s.following ? 'Following' : 'Follow'}</button>` : '';
    show(html`<p class="small"><a href="#/live">&laquo; Live</a></p>
      <div class="video-watch live-watch">
        <div class="video-main">
          <div class="video-stage live-stage">${s.state !== 'live' ? html`<div class="live-over"><b>This stream has ended.</b>
              <span class="small">It was live ${ago(s.ended)}${s.peak ? ', with up to ' + videoCount(s.peak) + ' watching' : ''}.</span>
              <a class="btn small" href="#/videos?by=${s.by.userId}">See ${s.by.name}'s videos</a></div>`
            : !playable ? html`<div class="live-over"><b>This browser can't play live streams yet.</b><span class="small">Try Chrome, Edge or Firefox.</span></div>`
            : html`<video id="livePlayer" playsinline muted autoplay controls></video>
              <button class="btn small live-sound" id="liveSound">&#128264; Turn sound on</button>
              <div class="live-wait" id="liveWait">Waiting for the stream...</div>`}</div>
          <h1 class="video-title">${s.state === 'live' ? html`<span class="live-badge inline">LIVE</span> ` : ''}${s.title}</h1>
          <div class="video-bar">
            <span class="video-channel">${person(s.by)} <span class="small muted">${videoCount(s.by.followers)} follower${s.by.followers === 1 ? '' : 's'}</span> ${followBtn}</span>
            <span class="video-actions"><span class="small" id="liveInfo">${s.state === 'live' ? html`<b>${videoCount(s.viewers)}</b> watching &middot; ${liveSince(s.at)}` : ''}</span>
              ${s.state === 'live' ? html`<button class="btn small" data-live="share">Share</button>` : ''}
              ${s.canEnd && !s.mine ? html`<button class="btn small red" data-live="end">End stream (staff)</button>` : ''}</span>
          </div>
          <div class="video-about box"><p class="small"><a href="#/videos?cat=${s.cat}">${VIDEO_CAT_NAMES[s.cat] || 'Other'}</a> &middot;
            <a href="#/videos?by=${s.by.userId}">More from ${s.by.name}</a>
            ${s.mine && s.state === 'live' ? html` &middot; <a href="#/golive">Back to your stream controls</a>` : ''}
            ${signedIn() && !s.mine ? html` &middot; ${reportLink('stream', s.id, 'the stream "' + s.title + '"')}` : ''}</p></div>
        </div>
        ${chatPanel(s)}
      </div>`);
    const video = document.getElementById('livePlayer');
    const note = (text) => { const w = document.getElementById('liveWait'); if (w) { w.textContent = text; w.hidden = false; } };
    const player = video ? new LivePlayer(video, s, note) : null;
    if (video) {
      video.addEventListener('playing', () => { const w = document.getElementById('liveWait'); if (w) w.hidden = true; });
      document.getElementById('liveSound').addEventListener('click', (e) => { video.muted = false; video.play().catch(() => {}); e.currentTarget.remove(); });
      video.addEventListener('volumechange', () => { if (!video.muted) document.getElementById('liveSound')?.remove(); });
    }
    let timer = null;
    const keeper = chatKeeper(id, (got) => {
      const st = got.stream, info = document.getElementById('liveInfo');
      if (info && st.state === 'live') info.innerHTML = html`<b>${videoCount(st.viewers)}</b> watching &middot; ${liveSince(st.at)}`.s;
      if (st.state !== 'live' && s.state === 'live') { clearInterval(timer); keeper.stop(); if (player) player.stop(); pages.live(id); return; }
      if (got.timeoutUntil) { const f = document.querySelector('[data-live-say] input'); if (f) f.placeholder = 'You\'re in a time out for a few minutes'; }
      if (player) player.feed(st);
    });
    keeper.poll();
    if (s.state === 'live') timer = setInterval(() => {
      if (!document.getElementById('liveLines')) { clearInterval(timer); keeper.stop(); if (player) player.stop(); return; }   // (left the page)
      if (document.visibilityState === 'visible' || player) keeper.poll();
    }, POLL_MS);
    document.querySelectorAll('[data-live]').forEach((b) => b.addEventListener('click', async () => {
      if (b.dataset.live === 'share') {
        const link = location.origin + '/app/#/live/' + encodeURIComponent(id);
        try { await navigator.clipboard.writeText(link); toast('Link copied!'); } catch { prompt('Copy this link:', link); }
      } else if (b.dataset.live === 'end') {
        if (!confirm('End this stream for everyone? The streamer is told.')) return;
        const done = await call('stream.end', { id });
        if (!done.ok) { toast(done.error); return; }
        pages.live(id);
      }
    }));
  };

  // #/golive: the streamer's own page. It records and sends while it stays open.
  let live = null;   // the stream that's on right now: {id, push, n, recorder, media, timers...}
  pages.golive = async () => {
    if (!signedIn()) { show(html`${hero}${needSignIn('go live')}`); return; }
    if (live) { studio(); return; }
    if (!me().verified && !me().staff) {
      show(html`${hero}<div class="box info"><b>Only Verified people can go live for now.</b>
        Nobody can check a live stream before it's shown, so for now it's for people staff already trust.
        You can still <a href="#/videoupload">upload videos</a>, and <a href="#/live">watch and chat</a>.</div>`);
      return;
    }
    const mime = recordMime();
    const canScreen = !!(navigator.mediaDevices && navigator.mediaDevices.getDisplayMedia);
    show(html`<div class="box live-setup"><h2 class="boxhead">Go live</h2>
      ${!mime || !navigator.mediaDevices ? html`<p class="error">This browser can't stream. Use Chrome, Edge or Firefox on a computer.</p>` : ''}
      <p class="small muted">Your stream is public: anyone can watch and chat. Follow the <a href="#/terms">rules</a>; staff can end streams that break them.
        Your followers are told when you go live.</p>
      <div class="live-setup-grid">
        <div><div class="video-stage"><video id="livePreview" muted playsinline autoplay></video><div class="live-wait" id="previewNote">Pick what to show, then press Show preview.</div></div></div>
        <form class="form" data-golive>
          <p><label>What to show<br><select name="source"><option value="camera">My camera</option>${canScreen ? html`<option value="screen">My screen (a game, a window, a tab)</option>` : ''}</select></label></p>
          <p><label><input type="checkbox" name="mic" checked> Use my microphone</label></p>
          <p><label>Quality<br><select name="quality"><option value="480">480p (uses less internet)</option><option value="720" selected>720p</option></select></label></p>
          <p><button type="button" class="btn small" data-preview>Show preview</button></p>
          <p><label>Title<br><input type="text" name="title" maxlength="100" required style="width:100%" placeholder="Building an obby, come hang out"></label></p>
          <p><label>Category<br><select name="cat">${Object.entries(VIDEO_CAT_NAMES).map(([k, n]) => html`<option value="${k}">${n}</option>`)}</select></label></p>
          <p><button class="btn red" ${mime ? '' : 'disabled'}>&#9679; Go live</button> <span class="small" id="liveMsg"></span></p>
        </form>
      </div></div>`);
    const form = document.querySelector('[data-golive]');
    const say = (t, bad) => { const m = document.getElementById('liveMsg'); m.textContent = t; m.className = 'small' + (bad ? ' error' : ''); };
    let media = null;
    const stopMedia = () => { if (media) media.getTracks().forEach((x) => x.stop()); media = null; };
    // Ask for the camera or screen (and microphone), and show it.
    const getMedia = async () => {
      stopMedia();
      const [w, h] = QUALITY[form.quality.value];
      const size = { width: { ideal: w }, height: { ideal: h }, frameRate: { ideal: 30, max: 30 } };
      if (form.source.value === 'screen') {
        media = await navigator.mediaDevices.getDisplayMedia({ video: size, audio: true });
        if (form.mic.checked && !media.getAudioTracks().length) {
          try { (await navigator.mediaDevices.getUserMedia({ audio: true })).getAudioTracks().forEach((x) => media.addTrack(x)); } catch { /* no microphone: no sound */ }
        }
      } else {
        media = await navigator.mediaDevices.getUserMedia({ video: size, audio: form.mic.checked });
      }
      const pv = document.getElementById('livePreview');
      pv.srcObject = media;
      document.getElementById('previewNote').hidden = true;
      return media;
    };
    const preview = async () => {
      try { await getMedia(); say(''); } catch (e) { say(e && e.name === 'NotAllowedError' ? 'The browser wasn\'t allowed to use it. Allow it in the address bar, then try again.' : 'Couldn\'t start that: ' + (e && e.message || e), true); }
    };
    form.querySelector('[data-preview]').addEventListener('click', preview);
    ['source', 'mic', 'quality'].forEach((k) => form[k].addEventListener('change', () => { if (media) preview(); }));
    // Leaving the page before going live lets the camera go.
    const leave = () => { if (!document.querySelector('[data-golive]')) { stopMedia(); window.removeEventListener('hashchange', leave); } };
    window.addEventListener('hashchange', leave);
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const btn = form.querySelector('button.red');
      btn.disabled = true;
      try { if (!media) await getMedia(); } catch (err) { btn.disabled = false; say('Couldn\'t start your camera or screen: ' + (err && err.message || err), true); return; }
      say('Going live...');
      const pv = document.getElementById('livePreview');
      await new Promise((ok) => (pv.readyState >= 2 ? ok() : pv.addEventListener('loadeddata', ok, { once: true })));
      const r = await call('stream.start', { title: form.title.value, cat: form.cat.value, mime, thumb: await snapJpeg(pv) });
      if (!r.ok) { btn.disabled = false; say(r.error, true); return; }
      window.removeEventListener('hashchange', leave);
      live = { id: r.stream.id, push: r.push, seg: r.segSecs, mime, media, bits: QUALITY[form.quality.value][2], n: 0, sending: 0, dropped: 0, at: r.stream.at, stream: r.stream };
      startRecording();
      media.getVideoTracks()[0].addEventListener('ended', () => endStream('You stopped sharing, so the stream ended.'));
      studio();
    });
  };

  // Record kSegSecs-second pieces, each a whole little video, and send them in order.
  function startRecording() {
    const L = live;
    const queue = [];
    const sendNext = async () => {
      if (L.sending || !queue.length) return;
      L.sending = 1;
      const { n, blob } = queue.shift();
      try {
        const r = await fetch(L.push + '&n=' + n, { method: 'PUT', body: blob });
        const j = await r.json().catch(() => ({}));
        if (r.status === 404 || r.status === 410) { endStream(j.error || 'The stream ended.'); return; }
        if (j.ok) { L.sent = n; L.viewers = j.viewers; L.fails = 0; } else L.fails = (L.fails || 0) + 1;
      } catch { L.fails = (L.fails || 0) + 1; }
      L.sending = 0;
      updateStatus();
      sendNext();
    };
    const recordOne = () => {
      const rec = new MediaRecorder(L.media, { mimeType: L.mime, videoBitsPerSecond: L.bits, audioBitsPerSecond: 96000 });
      const parts = [];
      rec.ondataavailable = (e) => { if (e.data && e.data.size) parts.push(e.data); };
      rec.onstop = () => {
        const blob = new Blob(parts, { type: 'video/webm' });
        if (blob.size < 1000 || !live) return;   // (an empty piece: nothing to send)
        const n = L.n++;
        queue.push({ n, blob });
        // A slow connection: don't fall further and further behind; skip the oldest waiting piece.
        while (queue.length > 3) { queue.shift(); L.dropped++; }
        sendNext();
      };
      rec.start();
      return rec;
    };
    L.recorder = recordOne();
    L.timer = setInterval(() => {
      if (!live) return;
      const next = recordOne();   // (the new one starts before the old one stops, so nothing is missed)
      L.recorder.stop();
      L.recorder = next;
    }, L.seg * 1000);
    L.snapTimer = setInterval(async () => {
      const pv = document.getElementById('liveMine');
      const shot = pv ? await snapJpeg(pv) : '';
      if (shot && live) call('stream.snap', { id: L.id, thumb: shot });
    }, SNAP_MS);
    L.warn = (e) => { e.preventDefault(); e.returnValue = ''; };
    window.addEventListener('beforeunload', L.warn);
  }
  async function endStream(why) {
    const L = live;
    if (!L) return;
    live = null;
    clearInterval(L.timer); clearInterval(L.snapTimer); clearInterval(L.pollTimer);
    window.removeEventListener('beforeunload', L.warn);
    try { L.recorder.stop(); } catch { /* already stopped */ }
    L.media.getTracks().forEach((x) => x.stop());
    if (L.keeper) L.keeper.stop();
    await call('stream.end', { id: L.id });
    toast(why || 'Your stream has ended. Thanks for streaming!');
    if (location.hash.startsWith('#/golive')) pages.golive();
  }
  function updateStatus() {
    const el = document.getElementById('liveStatus');
    if (!el || !live) return;
    const L = live;
    el.innerHTML = html`<span class="live-badge inline">LIVE</span> <b>${liveSince(L.at)}</b> &middot; <b>${videoCount(L.viewers || 0)}</b> watching
      ${L.fails > 1 ? html` &middot; <span class="error">Pieces aren't getting through: check your internet.</span>` : ''}
      ${L.dropped ? html` &middot; <span class="small muted">${L.dropped} piece(s) skipped (slow internet)</span>` : ''}`.s;
  }
  // The streamer's controls while live: what's going out, End, and the chat with mod tools.
  function studio() {
    const L = live;
    show(html`<div class="video-watch live-watch">
        <div class="video-main">
          <div class="video-stage"><video id="liveMine" muted playsinline autoplay></video></div>
          <h1 class="video-title">${L.stream.title}</h1>
          <div class="video-bar"><span id="liveStatus" class="small"></span>
            <span class="video-actions"><a class="btn small" href="#/live/${L.id}" target="_blank" rel="noopener">See what watchers see</a>
              <button class="btn small red" data-live-end>End stream</button></span></div>
          <div class="box info small">Keep this tab open while you're live: it's what sends your stream. If you close it, the stream ends by itself.
            Use the &times; and &#9208; buttons in chat to delete a line or give someone a 10-minute time out.</div>
        </div>
        ${chatPanel(Object.assign({}, L.stream, { state: 'live' }))}
      </div>`);
    document.getElementById('liveMine').srcObject = L.media;
    document.querySelector('[data-live-end]').addEventListener('click', () => { if (confirm('End your stream?')) endStream(); });
    updateStatus();
    if (L.keeper) L.keeper.stop();
    clearInterval(L.pollTimer);
    L.keeper = chatKeeper(L.id, (got) => {
      L.viewers = got.stream.viewers;
      if (got.stream.state !== 'live' && live === L) endStream(got.stream.canEnd ? '' : 'Your stream was ended.');
      updateStatus();
    });
    L.keeper.poll();
    // (keeps asking while you're on other pages too, so the stream isn't missed; the recorder keeps going)
    L.pollTimer = setInterval(() => { if (document.getElementById('liveLines')) L.keeper.poll(); else updateStatus(); }, POLL_MS);
  }

  // A small "Live now" row for the top of the Videos home (videos.js calls it).
  const liveRow = async (el) => {
    const r = await call('stream.list', {});
    if (!r.ok || !r.live.length || !el.isConnected) return;
    el.innerHTML = html`<div class="box live-row"><h2 class="boxhead"><span class="live-badge inline">LIVE</span> Live now
      <a class="small" href="#/live">See all</a></h2><div class="video-grid">${r.live.slice(0, 4).map(card)}</div></div>`.s;
  };

  return { pages, liveRow };
}
