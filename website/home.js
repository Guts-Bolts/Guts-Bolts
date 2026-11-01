// Languages (website/lang): translates this page and adds the picker to the footer.
  import { startTranslating, languagePicker } from './lang/i18n.js';
  startTranslating();
  const foot = document.querySelector('footer');
  foot.append(document.createElement('br'), document.createElement('br'), languagePicker());

// The banner slideshow: a new picture every 5 seconds (stops if you ask for less motion).
  (function () {
    const slides = Array.from(document.querySelectorAll('#hero .slide'));
    const dots = document.getElementById('dots'), caption = document.getElementById('caption');
    let at = 0, timer = null;
    const show = (i) => {
      slides[at].classList.remove('on'); dots.children[at].classList.remove('on');
      at = (i + slides.length) % slides.length;
      slides[at].classList.add('on'); dots.children[at].classList.add('on');
      caption.textContent = slides[at].dataset.caption;
    };
    slides.forEach((s, i) => {
      const b = document.createElement('button');
      b.type = 'button'; b.setAttribute('aria-label', s.dataset.caption);
      if (i === 0) b.className = 'on';
      b.onclick = () => { show(i); restart(); };
      dots.appendChild(b);
    });
    const still = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;
    const restart = () => { clearInterval(timer); if (!still) timer = setInterval(() => show(at + 1), 5000); };
    restart();
  })();

  // Point at the right download for this computer.
  (function () {
    const ua = navigator.userAgent;
    const os = /iPhone|iPad|iPod/i.test(ua) ? 'ios' : /OculusBrowser|Quest/i.test(ua) ? 'quest' : /Android/i.test(ua) ? 'android' : /Windows/i.test(ua) ? 'windows' : /Mac/i.test(ua) ? 'mac'
      : /Linux|X11/i.test(ua) ? 'linux' : '';
    if (!os) return;
    const names = { windows: 'Windows', mac: 'Mac', linux: 'Linux', android: 'Android', quest: 'Meta Quest', ios: 'iOS — in development' };
    if (os === 'ios') {
      document.getElementById('hero-os').textContent = 'iOS Player in development';
      return; // Don't mark an unavailable download as ready for this device.
    }
    const box = document.querySelector('.dl [data-os="' + os + '"]');
    if (!box) return;
    box.classList.add('mine');
    document.getElementById('hero-os').textContent = 'for ' + names[os];
  })();

  // The newest version, and what's new in it, from the release itself.
  fetch('/download/latest.json').then((r) => r.ok ? r.json() : null).then((m) => {
    if (!m || !m.version) return;
    const nick = m.name ? m.name.replace(/^Guts&Bolts\s+v?[\d.]+:?\s*/, '') : '';
    document.getElementById('dl-version').textContent = 'Version ' + m.version + (nick ? ' (' + nick + ')' : '') + '. It keeps itself up to date, so you only download it once.';
    document.getElementById('ribbon').textContent = 'NEW: Guts&Bolts ' + m.version;
    // The "## New" list in the release notes: "- **Thing**: what it does".
    const notes = String(m.notes || '');
    const part = notes.split(/^## /m).find((s) => /^New\b/.test(s));
    if (!part) return;
    const items = part.split('\n- ').slice(1).map((s) => s.replace(/\s+/g, ' ').trim()).slice(0, 10);
    if (!items.length) return;
    const list = document.getElementById('new-list');
    list.textContent = '';
    for (const it of items) {
      const li = document.createElement('li');
      const bold = it.match(/^\*\*(.+?)\*\*:?\s*(.*)$/);
      let rest = bold ? bold[2] : it;
      rest = rest.replace(/`([^`]+)`/g, '$1').replace(/\*\*/g, '');
      const firstSentence = rest.split(/(?<=[.!?])\s/)[0].replace(/[.:]$/, '');
      if (bold) { const b = document.createElement('b'); b.textContent = bold[1]; li.appendChild(b); if (firstSentence) li.append((/^[,;]/.test(firstSentence) ? '' : ': ') + firstSentence); }
      else li.textContent = firstSentence;
      list.appendChild(li);
    }
    document.getElementById('new-name').textContent = 'What\'s new in ' + m.version + (nick ? ': ' + nick : '');
  }).catch(() => {});
