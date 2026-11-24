// G&B's own video player (like YouTube's), wrapped around a plain <video>.
// Used by Videos' watch page (videos.js) and Live's watch page (streams.js).
//
//   const p = gbPlayer(videoElement, { id, title, by, poster, live, onTheater });
//
// What it adds: a red seek bar (what's loaded shows grey, hovering shows the time and a
// little picture), play/pause, volume (remembered), speed, captions (if the video has any),
// theater mode, fullscreen, picture-in-picture, picking up where you left off, phone
// lock-screen controls, and YouTube's keyboard keys (space/k, j/l, arrows, f, m, t, i, c, 0-9).
// On phones: tap shows the controls, double-tap the left or right side jumps 10 seconds.
// All its looks live in videoplayer.css under .gbp, so it never changes the page around it.

// 75 -> "1:15", 3725 -> "1:02:05"
export function playerTime(secs) {
  const s = Math.max(0, Math.floor(Number(secs) || 0)), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60;
  return (h ? h + ':' + String(m).padStart(2, '0') : String(m)) + ':' + String(r).padStart(2, '0');
}

export const SPEEDS = [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2];

// Which key does what (the same keys YouTube uses). Returns null for keys the player leaves alone.
export function keyAction(key, shift = false) {
  const k = key.length === 1 ? key.toLowerCase() : key;
  if (k === ' ' || k === 'k') return { do: 'toggle' };
  if (k === 'j') return { do: 'seek', by: -10 };
  if (k === 'l') return { do: 'seek', by: 10 };
  if (k === 'ArrowLeft') return { do: 'seek', by: -5 };
  if (k === 'ArrowRight') return { do: 'seek', by: 5 };
  if (k === 'ArrowUp') return { do: 'volume', by: 0.05 };
  if (k === 'ArrowDown') return { do: 'volume', by: -0.05 };
  if (k === 'f') return { do: 'fullscreen' };
  if (k === 'm') return { do: 'mute' };
  if (k === 't') return { do: 'theater' };
  if (k === 'i') return { do: 'pip' };
  if (k === 'c') return { do: 'captions' };
  if (k === 'Home') return { do: 'jump', to: 0 };
  if (k === 'End') return { do: 'jump', to: 1 };
  if (/^[0-9]$/.test(k)) return { do: 'jump', to: Number(k) / 10 };
  if (key === '>' || (shift && k === '.')) return { do: 'speed', by: 1 };
  if (key === '<' || (shift && k === ',')) return { do: 'speed', by: -1 };
  if (k === '.') return { do: 'frame', by: 1 / 30 };
  if (k === ',') return { do: 'frame', by: -1 / 30 };
  return null;
}

// The next speed up or down the list (stays at the ends).
export function nextSpeed(now, step) {
  let i = SPEEDS.indexOf(now);
  if (i < 0) i = SPEEDS.indexOf(1);
  return SPEEDS[Math.max(0, Math.min(SPEEDS.length - 1, i + step))];
}

// How far along the grey "loaded" part reaches, from 0 to 1 (the loaded piece around `at`).
export function loadedTo(ranges, at, length) {
  if (!length || !ranges) return 0;
  for (let i = 0; i < ranges.length; i++) {
    if (ranges.start(i) <= at + 0.5 && ranges.end(i) >= at) return Math.min(1, ranges.end(i) / length);
  }
  return 0;
}

// Where to pick up a video again: only if you'd watched a bit and weren't nearly done.
export function resumeAt(saved, length) {
  saved = Number(saved) || 0;
  if (!length || saved < 10 || saved > length - 15) return 0;
  return saved;
}

const store = {
  get(k) { try { return localStorage.getItem('gbp.' + k); } catch { return null; } },
  set(k, v) { try { if (v === null) localStorage.removeItem('gbp.' + k); else localStorage.setItem('gbp.' + k, String(v)); } catch { /* private window */ } },
};

// Little icons (drawn, no picture files).
const I = {
  play: '<path d="M8 5v14l11-7z"/>',
  pause: '<path d="M6 5h4v14H6zM14 5h4v14h-4z"/>',
  replay: '<path d="M12 5V1L7 6l5 5V7a6 6 0 1 1-6 6H4a8 8 0 1 0 8-8z"/>',
  vol: '<path d="M3 9v6h4l5 5V4L7 9zm13.5 3A4.5 4.5 0 0 0 14 8v8a4.5 4.5 0 0 0 2.5-4zM14 3.2v2.1a7 7 0 0 1 0 13.4v2.1a9 9 0 0 0 0-17.6z"/>',
  volLow: '<path d="M3 9v6h4l5 5V4L7 9zm13.5 3A4.5 4.5 0 0 0 14 8v8a4.5 4.5 0 0 0 2.5-4z"/>',
  mute: '<path d="M3 9v6h4l5 5V4L7 9zm18.4 6.6L18.8 13l2.6-2.6-1.4-1.4-2.6 2.6-2.6-2.6-1.4 1.4 2.6 2.6-2.6 2.6 1.4 1.4 2.6-2.6 2.6 2.6z"/>',
  gear: '<path d="M19.4 13a7.5 7.5 0 0 0 0-2l2.1-1.6-2-3.5-2.5 1a7.4 7.4 0 0 0-1.7-1L15 3.3h-4l-.4 2.6a7.4 7.4 0 0 0-1.7 1l-2.5-1-2 3.5L6.5 11a7.5 7.5 0 0 0 0 2l-2.1 1.6 2 3.5 2.5-1c.5.4 1.1.7 1.7 1l.4 2.6h4l.4-2.6c.6-.3 1.2-.6 1.7-1l2.5 1 2-3.5zM13 15.5a3.5 3.5 0 1 1 0-7 3.5 3.5 0 0 1 0 7z" transform="translate(-1 0)"/>',
  cc: '<path d="M19 4H5a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2V6a2 2 0 0 0-2-2zm-8 7H9.5v-.5h-2v3h2V13H11v1a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1v-4a1 1 0 0 1 1-1h3a1 1 0 0 1 1 1zm7 0h-1.5v-.5h-2v3h2V13H18v1a1 1 0 0 1-1 1h-3a1 1 0 0 1-1-1v-4a1 1 0 0 1 1-1h3a1 1 0 0 1 1 1z"/>',
  pip: '<path d="M19 11h-8v6h8zm4 8V5a2 2 0 0 0-2-2H3a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h18a2 2 0 0 0 2-2zm-2 0H3V5h18z"/>',
  theater: '<path d="M19 7H5a2 2 0 0 0-2 2v6a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2V9a2 2 0 0 0-2-2zm0 8H5V9h14z"/>',
  full: '<path d="M7 14H5v5h5v-2H7zm-2-4h2V7h3V5H5zm12 7h-3v2h5v-5h-2zM14 5v2h3v3h2V5z"/>',
  unfull: '<path d="M5 16h3v3h2v-5H5zm3-8H5v2h5V5H8zm6 11h2v-3h3v-2h-5zm2-11V5h-2v5h5V8z"/>',
  back: '<path d="M11 18V6l-8.5 6zm.5-6 8.5 6V6z"/>',
  fwd: '<path d="M4 18l8.5-6L4 6zm9-12v12l8.5-6z"/>',
};
const svg = (name) => '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">' + I[name] + '</svg>';

export function gbPlayer(video, opts = {}) {
  const live = !!opts.live;
  const touchy = typeof matchMedia === 'function' && matchMedia('(hover: none)').matches;
  video.controls = false;
  video.playsInline = true;
  video.setAttribute('playsinline', '');

  // Wrap the <video> in the player box, right where it was.
  const box = document.createElement('div');
  box.className = 'gbp' + (live ? ' gbp-live' : '') + (touchy ? ' gbp-touch' : '') + ' gbp-paused gbp-fresh';
  box.tabIndex = -1;
  box.setAttribute('role', 'region');
  box.setAttribute('aria-label', 'Video player');
  video.parentNode.insertBefore(box, video);
  box.appendChild(video);
  box.insertAdjacentHTML('beforeend', `
    <div class="gbp-shade" aria-hidden="true"></div>
    <div class="gbp-spin" aria-hidden="true"></div>
    <div class="gbp-flash" aria-hidden="true"></div>
    <div class="gbp-tap gbp-tap-l" aria-hidden="true">${svg('back')}<span>10 seconds</span></div>
    <div class="gbp-tap gbp-tap-r" aria-hidden="true">${svg('fwd')}<span>10 seconds</span></div>
    <button type="button" class="gbp-big" aria-label="Play">${svg('play')}</button>
    <div class="gbp-note" aria-live="polite"></div>
    <div class="gbp-menu" role="menu" hidden></div>
    <div class="gbp-bottom">
      ${live ? '' : `<div class="gbp-seek" role="slider" tabindex="0" aria-label="Seek" aria-valuemin="0" aria-valuemax="0" aria-valuenow="0">
        <div class="gbp-rail"><div class="gbp-loaded"></div><div class="gbp-hoverbar"></div><div class="gbp-done"></div></div>
        <div class="gbp-knob"></div>
        <div class="gbp-peek"><canvas width="160" height="90"></canvas><span>0:00</span></div>
      </div>`}
      <div class="gbp-row">
        <button type="button" class="gbp-btn gbp-play" aria-label="Play (k)">${svg('play')}</button>
        <span class="gbp-vol">
          <button type="button" class="gbp-btn gbp-mute" aria-label="Mute (m)">${svg('vol')}</button>
          <input class="gbp-volbar" type="range" min="0" max="1" step="0.01" aria-label="Volume">
        </span>
        ${live ? '<button type="button" class="gbp-livebtn" aria-label="Jump to live">LIVE</button>'
          : '<span class="gbp-time"><span class="gbp-now">0:00</span> / <span class="gbp-len">0:00</span></span>'}
        <span class="gbp-gap"></span>
        <button type="button" class="gbp-btn gbp-cc" aria-label="Captions (c)" aria-pressed="false" hidden>${svg('cc')}</button>
        ${live ? '' : `<button type="button" class="gbp-btn gbp-gear" aria-label="Settings" aria-haspopup="menu">${svg('gear')}</button>`}
        <button type="button" class="gbp-btn gbp-pip" aria-label="Picture in picture (i)" hidden>${svg('pip')}</button>
        ${opts.onTheater ? `<button type="button" class="gbp-btn gbp-theater" aria-label="Theater mode (t)">${svg('theater')}</button>` : ''}
        <button type="button" class="gbp-btn gbp-full" aria-label="Full screen (f)">${svg('full')}</button>
      </div>
    </div>`);
  const $ = (s) => box.querySelector(s);
  const big = $('.gbp-big'), playBtn = $('.gbp-play'), muteBtn = $('.gbp-mute'), volBar = $('.gbp-volbar');
  const seek = $('.gbp-seek'), menu = $('.gbp-menu'), note = $('.gbp-note'), flash = $('.gbp-flash');
  const ccBtn = $('.gbp-cc'), pipBtn = $('.gbp-pip'), fullBtn = $('.gbp-full'), theaterBtn = $('.gbp-theater');

  // ---- Remembered things ----
  const savedVol = Number(store.get('volume'));
  video.volume = store.get('volume') !== null && savedVol >= 0 && savedVol <= 1 ? savedVol : 1;
  if (!live && store.get('muted') === '1') video.muted = true;
  const posKey = opts.id && !live ? 'at.' + opts.id : '';

  // ---- Little helpers ----
  const length = () => (isFinite(video.duration) ? video.duration : 0);
  let noteTimer = 0;
  const say = (text) => { note.textContent = text; box.classList.add('gbp-noting'); clearTimeout(noteTimer); noteTimer = setTimeout(() => box.classList.remove('gbp-noting'), 1400); };
  const pop = (icon) => { flash.innerHTML = svg(icon); flash.classList.remove('go'); void flash.offsetWidth; flash.classList.add('go'); };
  const play = () => { const p = video.play(); if (p && p.catch) p.catch(() => {}); };
  const toggle = () => { if (video.paused || video.ended) { play(); pop('play'); } else { video.pause(); pop('pause'); } };
  const seekTo = (t) => { if (!live && length()) video.currentTime = Math.max(0, Math.min(length() - 0.05, t)); };
  const setVolume = (v) => {
    v = Math.max(0, Math.min(1, Math.round(v * 100) / 100));
    video.volume = v;
    video.muted = v === 0;
    say('Volume ' + Math.round(v * 100) + '%');
  };
  const toggleMute = () => {
    if (video.muted || video.volume === 0) { video.muted = false; if (video.volume === 0) video.volume = 0.5; } else video.muted = true;
  };

  // ---- Showing and hiding the controls ----
  let idle = 0;
  const wake = () => {
    box.classList.add('gbp-awake');
    clearTimeout(idle);
    idle = setTimeout(() => { if (!video.paused && !menu.matches(':hover') && menu.hidden && !dragging) box.classList.remove('gbp-awake'); }, touchy ? 3000 : 2500);
  };
  box.addEventListener('pointermove', (e) => { if (e.pointerType === 'mouse') wake(); });
  box.addEventListener('pointerleave', (e) => { if (e.pointerType === 'mouse' && !video.paused && menu.hidden) box.classList.remove('gbp-awake'); });
  box.addEventListener('focusin', wake);

  // ---- Play state ----
  const drawPlay = () => {
    const paused = video.paused || video.ended;
    box.classList.toggle('gbp-paused', paused);
    box.classList.toggle('gbp-ended', video.ended);
    const icon = video.ended ? 'replay' : paused ? 'play' : 'pause';
    playBtn.innerHTML = svg(icon);
    playBtn.setAttribute('aria-label', (video.ended ? 'Replay' : paused ? 'Play' : 'Pause') + ' (k)');
    big.innerHTML = svg(video.ended ? 'replay' : 'play');
    big.setAttribute('aria-label', video.ended ? 'Replay' : 'Play');
    if (paused) box.classList.add('gbp-awake'); else wake();
  };
  video.addEventListener('play', () => { box.classList.remove('gbp-fresh'); drawPlay(); });
  video.addEventListener('pause', drawPlay);
  video.addEventListener('ended', drawPlay);
  video.addEventListener('waiting', () => box.classList.add('gbp-loading'));
  ['playing', 'canplay', 'seeked', 'pause'].forEach((ev) => video.addEventListener(ev, () => box.classList.remove('gbp-loading')));
  video.addEventListener('error', () => { if (video.error && !live) say('This video couldn\'t be played.'); });
  big.addEventListener('click', (e) => { e.stopPropagation(); toggle(); });
  playBtn.addEventListener('click', toggle);

  // Clicking the picture: play/pause on a computer (double-click is full screen).
  // On a phone: tap shows/hides the controls, double-tap a side jumps 10 seconds.
  let lastTap = 0, tapTimer = 0;
  video.addEventListener('click', (e) => {
    if (!menu.hidden) { closeMenu(); return; }
    if (!touchy) { toggle(); return; }
    const now = Date.now(), r = box.getBoundingClientRect(), x = (e.clientX - r.left) / r.width;
    if (now - lastTap < 320 && !live && (x < 0.35 || x > 0.65)) {
      clearTimeout(tapTimer);
      const fwd = x > 0.5;
      seekTo(video.currentTime + (fwd ? 10 : -10));
      const t = $(fwd ? '.gbp-tap-r' : '.gbp-tap-l');
      t.classList.remove('go'); void t.offsetWidth; t.classList.add('go');
      lastTap = now;   // (keep tapping to keep jumping)
      return;
    }
    lastTap = now;
    clearTimeout(tapTimer);
    tapTimer = setTimeout(() => {
      if (box.classList.contains('gbp-awake') && !video.paused) { clearTimeout(idle); box.classList.remove('gbp-awake'); } else wake();
    }, 260);
  });
  video.addEventListener('dblclick', (e) => { if (!touchy) { e.preventDefault(); fullscreen(); } });

  // ---- Time and seek bar ----
  const drawTime = () => {
    if (live) return;
    const len = length(), at = video.currentTime;
    $('.gbp-now').textContent = playerTime(at);
    $('.gbp-len').textContent = playerTime(len);
    const f = len ? at / len : 0;
    if (!dragging) { $('.gbp-done').style.transform = `scaleX(${f})`; $('.gbp-knob').style.left = (f * 100) + '%'; }
    $('.gbp-loaded').style.transform = `scaleX(${loadedTo(video.buffered, at, len)})`;
    seek.setAttribute('aria-valuemax', String(Math.floor(len)));
    seek.setAttribute('aria-valuenow', String(Math.floor(at)));
    seek.setAttribute('aria-valuetext', playerTime(at) + ' of ' + playerTime(len));
  };
  video.addEventListener('timeupdate', drawTime);
  video.addEventListener('progress', drawTime);
  video.addEventListener('loadedmetadata', drawTime);
  video.addEventListener('durationchange', drawTime);

  let dragging = false;
  if (seek) {
    const peek = $('.gbp-peek'), peekText = peek.querySelector('span'), canvas = peek.querySelector('canvas');
    const frac = (e) => { const r = seek.getBoundingClientRect(); return Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)); };
    // The little picture above the bar: a second, quiet copy of the video that jumps to where you point.
    let shadow = null, wanted = -1, busy = false;
    const grab = () => {
      if (busy || wanted < 0 || !shadow) return;
      busy = true;
      const t = wanted; wanted = -1;
      shadow.currentTime = t;
    };
    const wantPeek = (t) => {
      if (touchy || !video.currentSrc || video.currentSrc.startsWith('blob:')) return;
      if (!shadow) {
        shadow = document.createElement('video');
        shadow.muted = true; shadow.preload = 'auto'; shadow.playsInline = true;
        shadow.crossOrigin = video.crossOrigin;
        shadow.src = video.currentSrc;
        shadow.addEventListener('seeked', () => {
          try { canvas.getContext('2d').drawImage(shadow, 0, 0, canvas.width, canvas.height); peek.classList.add('pic'); } catch { /* not allowed */ }
          busy = false; grab();
        });
        shadow.addEventListener('error', () => { busy = false; shadow = null; });
      }
      wanted = t; grab();
    };
    const showPeek = (f) => {
      const len = length();
      if (!len) return;
      peekText.textContent = playerTime(f * len);
      const r = seek.getBoundingClientRect(), half = (peek.offsetWidth || 160) / 2;
      peek.style.left = Math.max(half, Math.min(r.width - half, f * r.width)) + 'px';
      $('.gbp-hoverbar').style.transform = `scaleX(${f})`;
      seek.classList.add('gbp-hovering');
      wantPeek(f * len);
    };
    const dragTo = (f) => {
      $('.gbp-done').style.transform = `scaleX(${f})`;
      $('.gbp-knob').style.left = (f * 100) + '%';
      $('.gbp-now').textContent = playerTime(f * length());
    };
    let wasPlaying = false;
    seek.addEventListener('pointerdown', (e) => {
      if (!length()) return;
      e.preventDefault();
      dragging = true;
      seek.setPointerCapture(e.pointerId);
      seek.classList.add('gbp-dragging');
      wasPlaying = !video.paused;
      video.pause();
      const f = frac(e); dragTo(f); showPeek(f);
    });
    seek.addEventListener('pointermove', (e) => {
      const f = frac(e);
      if (dragging) { dragTo(f); showPeek(f); } else if (e.pointerType === 'mouse') showPeek(f);
    });
    const letGo = (e) => {
      if (!dragging) return;
      dragging = false;
      seek.classList.remove('gbp-dragging', 'gbp-hovering');
      seekTo(frac(e) * length());
      if (wasPlaying) play();
      wake();
    };
    seek.addEventListener('pointerup', letGo);
    seek.addEventListener('pointercancel', letGo);
    seek.addEventListener('pointerleave', () => { if (!dragging) seek.classList.remove('gbp-hovering'); });
  }

  // ---- Volume ----
  const drawVolume = () => {
    const v = video.muted ? 0 : video.volume;
    muteBtn.innerHTML = svg(v === 0 ? 'mute' : v < 0.5 ? 'volLow' : 'vol');
    muteBtn.setAttribute('aria-label', (v === 0 ? 'Unmute' : 'Mute') + ' (m)');
    volBar.value = String(v);
    volBar.style.setProperty('--gbp-vol', (v * 100) + '%');
    if (!live) { store.set('volume', video.volume); store.set('muted', video.muted ? '1' : '0'); }
    else if (!video.muted) store.set('volume', video.volume);
  };
  video.addEventListener('volumechange', drawVolume);
  muteBtn.addEventListener('click', toggleMute);
  volBar.addEventListener('input', () => { video.volume = Number(volBar.value); video.muted = Number(volBar.value) === 0; });
  drawVolume();

  // ---- Live: the LIVE button jumps to the newest part ----
  if (live) {
    const liveBtn = $('.gbp-livebtn');
    const edge = () => { const b = video.buffered; return b.length ? b.end(b.length - 1) : 0; };
    const drawLive = () => liveBtn.classList.toggle('behind', edge() - video.currentTime > 12);
    video.addEventListener('timeupdate', drawLive);
    liveBtn.addEventListener('click', () => { const e = edge(); if (e) video.currentTime = Math.max(0, e - 2); play(); });
  }

  // ---- Captions (only shown if the video has caption tracks) ----
  const tracks = () => Array.from(video.textTracks || []).filter((t) => t.kind === 'subtitles' || t.kind === 'captions');
  const drawCaptions = () => {
    const list = tracks();
    ccBtn.hidden = !list.length;
    const on = list.some((t) => t.mode === 'showing');
    ccBtn.classList.toggle('on', on);
    ccBtn.setAttribute('aria-pressed', String(on));
  };
  const toggleCaptions = () => {
    const list = tracks();
    if (!list.length) return;
    const on = list.some((t) => t.mode === 'showing');
    list.forEach((t, i) => { t.mode = !on && i === 0 ? 'showing' : 'disabled'; });
    store.set('captions', on ? '0' : '1');
    say(on ? 'Captions off' : 'Captions on');
    drawCaptions();
  };
  ccBtn.addEventListener('click', toggleCaptions);
  if (video.textTracks) {
    video.textTracks.addEventListener?.('addtrack', () => {
      if (store.get('captions') === '1') { const t = tracks()[0]; if (t) t.mode = 'showing'; }
      drawCaptions();
    });
  }
  drawCaptions();

  // ---- Settings menu (speed, loop) ----
  const closeMenu = () => { menu.hidden = true; $('.gbp-gear')?.classList.remove('on'); };
  const setSpeed = (s) => { video.playbackRate = s; say(s === 1 ? 'Normal speed' : s + 'x'); };
  const drawMenu = (page) => {
    if (page === 'speed') {
      menu.innerHTML = '<button type="button" class="gbp-mback" data-m="main">&lsaquo; Playback speed</button>' + SPEEDS.map((s) =>
        `<button type="button" role="menuitemradio" aria-checked="${video.playbackRate === s}" data-speed="${s}">${s === 1 ? 'Normal' : s}</button>`).join('');
    } else {
      menu.innerHTML = `<button type="button" role="menuitem" data-m="speed"><span>Playback speed</span><b>${video.playbackRate === 1 ? 'Normal' : video.playbackRate + 'x'} &rsaquo;</b></button>
        <button type="button" role="menuitemcheckbox" aria-checked="${video.loop}" data-m="loop"><span>Loop</span><b>${video.loop ? 'On' : 'Off'}</b></button>
        ${tracks().length ? `<button type="button" role="menuitemcheckbox" data-m="cc"><span>Captions</span><b>${tracks().some((t) => t.mode === 'showing') ? 'On' : 'Off'}</b></button>` : ''}
        <button type="button" role="menuitem" data-m="keys"><span>Keyboard keys</span><b>&rsaquo;</b></button>`;
    }
    if (page === 'keys') {
      menu.innerHTML = '<button type="button" class="gbp-mback" data-m="main">&lsaquo; Keyboard keys</button><div class="gbp-keys">' + [
        ['Space / K', 'Play or pause'], ['J / L', 'Back / forward 10 s'], ['&larr; / &rarr;', 'Back / forward 5 s'], ['&uarr; / &darr;', 'Volume'],
        ['M', 'Mute'], ['F', 'Full screen'], ['T', 'Theater mode'], ['I', 'Picture in picture'], ['C', 'Captions'],
        ['0 - 9', 'Jump to 0% - 90%'], ['&lt; / &gt;', 'Slower / faster'], [', / .', 'One frame (paused)'],
      ].map(([k, w]) => `<span><kbd>${k}</kbd> ${w}</span>`).join('') + '</div>';
    }
  };
  menu.addEventListener('click', (e) => {
    e.stopPropagation();
    const b = e.target.closest('button');
    if (!b) return;
    if (b.dataset.speed) { setSpeed(Number(b.dataset.speed)); closeMenu(); return; }
    const m = b.dataset.m;
    if (m === 'loop') { video.loop = !video.loop; drawMenu('main'); return; }
    if (m === 'cc') { toggleCaptions(); drawMenu('main'); return; }
    drawMenu(m);
  });
  $('.gbp-gear')?.addEventListener('click', (e) => {
    e.stopPropagation();
    if (!menu.hidden) { closeMenu(); return; }
    drawMenu('main'); menu.hidden = false; e.currentTarget.classList.add('on'); wake();
  });
  const outside = (e) => { if (!menu.hidden && !menu.contains(e.target) && !e.target.closest('.gbp-gear')) closeMenu(); };
  document.addEventListener('click', outside);

  // ---- Theater, full screen, picture in picture ----
  const fsEl = () => document.fullscreenElement || document.webkitFullscreenElement;
  const fullscreen = () => {
    if (fsEl()) { (document.exitFullscreen || document.webkitExitFullscreen).call(document); return; }
    const ask = box.requestFullscreen || box.webkitRequestFullscreen;
    if (ask) {
      const p = ask.call(box);
      if (p && p.then) p.then(() => { try { screen.orientation?.lock?.('landscape').catch(() => {}); } catch { /* ok */ } }).catch(() => video.webkitEnterFullscreen?.());
    } else if (video.webkitEnterFullscreen) video.webkitEnterFullscreen();   // (iPhone only lets the video itself go full screen)
  };
  const drawFull = () => {
    const on = fsEl() === box;
    box.classList.toggle('gbp-fs', on);
    fullBtn.innerHTML = svg(on ? 'unfull' : 'full');
    fullBtn.setAttribute('aria-label', (on ? 'Exit full screen' : 'Full screen') + ' (f)');
  };
  document.addEventListener('fullscreenchange', drawFull);
  document.addEventListener('webkitfullscreenchange', drawFull);
  fullBtn.addEventListener('click', fullscreen);

  const pipOk = typeof document !== 'undefined' && (document.pictureInPictureEnabled || video.webkitSupportsPresentationMode?.('picture-in-picture'));
  pipBtn.hidden = !pipOk || video.disablePictureInPicture;
  const pip = () => {
    if (!pipOk) return;
    if (document.pictureInPictureElement) { document.exitPictureInPicture().catch(() => {}); return; }
    if (video.requestPictureInPicture) video.requestPictureInPicture().catch(() => say('Picture in picture isn\'t ready yet.'));
    else video.webkitSetPresentationMode?.('picture-in-picture');
  };
  pipBtn.addEventListener('click', pip);

  let theater = store.get('theater') === '1';
  const drawTheater = () => { if (opts.onTheater) { opts.onTheater(theater); theaterBtn.classList.toggle('on', theater); theaterBtn.setAttribute('aria-label', (theater ? 'Default view' : 'Theater mode') + ' (t)'); } };
  const toggleTheater = () => { if (!opts.onTheater) return; theater = !theater; store.set('theater', theater ? '1' : '0'); drawTheater(); };
  theaterBtn?.addEventListener('click', toggleTheater);
  drawTheater();

  // ---- Keyboard ----
  const onKey = (e) => {
    if (!box.isConnected) { stop(); return; }
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const t = e.target;
    if (t && t !== box && !box.contains(t) && (t.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName))) return;
    if (t && box.contains(t) && t.tagName === 'INPUT' && /^Arrow/.test(e.key)) return;   // (the volume slider has its own arrows)
    if (t && t.tagName === 'BUTTON' && (e.key === ' ' || e.key === 'Enter') && t !== box) return;   // (let buttons be pressed)
    if (e.key === 'Escape' && !menu.hidden) { closeMenu(); return; }
    const a = keyAction(e.key, e.shiftKey);
    if (!a) return;
    if (live && (a.do === 'seek' || a.do === 'jump' || a.do === 'speed' || a.do === 'frame')) return;
    e.preventDefault();
    if (a.do === 'toggle') toggle();
    else if (a.do === 'seek') { seekTo(video.currentTime + a.by); say((a.by > 0 ? '+' : '') + a.by + ' seconds'); }
    else if (a.do === 'jump') seekTo(a.to * length());
    else if (a.do === 'volume') setVolume((video.muted ? 0 : video.volume) + a.by);
    else if (a.do === 'mute') { toggleMute(); say(video.muted ? 'Muted' : 'Sound on'); }
    else if (a.do === 'fullscreen') fullscreen();
    else if (a.do === 'theater') toggleTheater();
    else if (a.do === 'pip') pip();
    else if (a.do === 'captions') { if (tracks().length) toggleCaptions(); else say('No captions for this video'); }
    else if (a.do === 'speed') setSpeed(nextSpeed(video.playbackRate, a.by));
    else if (a.do === 'frame') { if (video.paused) seekTo(video.currentTime + a.by); }
    wake();
  };
  document.addEventListener('keydown', onKey);

  // ---- Picking up where you left off ----
  if (posKey) {
    video.addEventListener('loadedmetadata', () => {
      const at = resumeAt(store.get(posKey), length());
      if (at) { video.currentTime = at; say('Picking up at ' + playerTime(at)); drawTime(); }
    }, { once: true });
    let lastSave = 0;
    video.addEventListener('timeupdate', () => {
      const now = Date.now();
      if (now - lastSave < 4000) return;
      lastSave = now;
      store.set(posKey, video.currentTime > length() - 15 ? null : Math.floor(video.currentTime));
    });
    video.addEventListener('ended', () => store.set(posKey, null));
  }

  // ---- Phone lock screen / headphone buttons ----
  if (typeof navigator !== 'undefined' && navigator.mediaSession && typeof MediaMetadata !== 'undefined') {
    video.addEventListener('play', () => {
      try {
        navigator.mediaSession.metadata = new MediaMetadata({ title: opts.title || 'G&B Videos', artist: opts.by || '',
          artwork: opts.poster ? [{ src: opts.poster, sizes: '320x180', type: 'image/jpeg' }] : [] });
        navigator.mediaSession.setActionHandler('play', play);
        navigator.mediaSession.setActionHandler('pause', () => video.pause());
        if (!live) {
          navigator.mediaSession.setActionHandler('seekbackward', () => seekTo(video.currentTime - 10));
          navigator.mediaSession.setActionHandler('seekforward', () => seekTo(video.currentTime + 10));
        }
      } catch { /* older browser */ }
    });
  }

  function stop() {
    document.removeEventListener('keydown', onKey);
    document.removeEventListener('click', outside);
    document.removeEventListener('fullscreenchange', drawFull);
    document.removeEventListener('webkitfullscreenchange', drawFull);
    clearTimeout(idle); clearTimeout(noteTimer); clearTimeout(tapTimer);
  }

  drawPlay(); drawTime();
  return { box, stop, toggle, seekTo, setSpeed, fullscreen };
}
