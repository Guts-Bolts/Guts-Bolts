// Languages (website/lang): translates this page and adds the picker to the footer.
  import { startTranslating, languagePicker } from './lang/i18n.js';
  import { call as apiCall } from './app/gb.js';
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


// Live auction promotion for the public landing page. Auction state and bids always come
// from the server; the browser only formats the returned end timestamp for the countdown.
(function () {
  const root = document.getElementById('auction-feature');
  if (!root) return;
  let loading = false;
  let lastItems = [];

  const pad = (n) => String(n).padStart(2, '0');
  function countdown(end) {
    let left = Math.max(0, Math.floor(Number(end) - Date.now() / 1000));
    const days = Math.floor(left / 86400);
    left %= 86400;
    const hours = Math.floor(left / 3600);
    const minutes = Math.floor((left % 3600) / 60);
    const seconds = left % 60;
    return (days ? days + 'd ' : '') + pad(hours) + ':' + pad(minutes) + ':' + pad(seconds);
  }
  function isActive(item) {
    const auction = item && item.auction;
    return !!auction && !auction.done && !auction.cancelled && Number(auction.endsAt) > Date.now() / 1000;
  }
  function element(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  }
  function makePicture(item) {
    const art = element('div', 'auction-promo-art');
    const fallback = element('span', 'auction-promo-art-fallback', String(item.kind || 'ITEM').toUpperCase());
    art.appendChild(fallback);
    const img = document.createElement('img');
    img.alt = item.name + ' thumbnail';
    img.decoding = 'async';
    img.onerror = () => { img.remove(); };
    if (item.thumb) {
      img.src = '/thumb/' + encodeURIComponent(item.id) + '?v=' + encodeURIComponent(item.thumb);
      art.appendChild(img);
    } else {
      // (the 3D drawer is big, so it only loads when an item has no picture yet)
      import('./app/avatar3d.js').then((m) => m.itemPicture(item, 240)).then((url) => {
        if (!url || !art.isConnected) return;
        img.src = url;
        art.replaceChildren(img);
      }).catch(() => {});
    }
    return art;
  }
  function makeBanner(item) {
    const auction = item.auction;
    const secondsLeft = Number(auction.endsAt) - Date.now() / 1000;
    const urgent = secondsLeft <= 3600;
    const today = secondsLeft <= 86400;
    const banner = element('a', 'auction-promo-main' + (urgent ? ' urgent' : ''));
    banner.href = '/app/#/item/' + encodeURIComponent(item.id);
    banner.setAttribute('aria-label', 'Auction: ' + item.name);
    banner.appendChild(makePicture(item));

    const copy = element('div', 'auction-promo-copy');
    copy.appendChild(element('span', 'auction-promo-kicker', urgent ? 'AUCTION  ·  ENDING SOON' : today ? 'AUCTION  ·  ENDING TODAY' : 'LIVE AUCTION'));
    copy.appendChild(element('h3', '', item.name));
    copy.appendChild(element('p', '', item.description || 'A limited-time chance to claim this exclusive item.'));
    const bids = Array.isArray(auction.bids) ? auction.bids : [];
    const highest = bids.reduce((top, bid) => Math.max(top, Number(bid.amount) || 0), 0);
    const bidBox = element('div', 'auction-promo-bid');
    bidBox.appendChild(element('span', '', bids.length ? 'CURRENT HIGH BID' : 'STARTING BID'));
    bidBox.appendChild(element('strong', '', (highest || Number(auction.minBid) || 0).toLocaleString() + ' Bolts'));
    copy.appendChild(bidBox);
    banner.appendChild(copy);

    const clock = element('div', 'auction-promo-clock');
    clock.appendChild(element('span', '', 'TIME REMAINING'));
    const time = element('strong', '', countdown(auction.endsAt));
    time.dataset.auctionEnd = String(auction.endsAt);
    clock.appendChild(time);
    clock.appendChild(element('b', 'auction-promo-cta', 'BID NOW  →'));
    banner.appendChild(clock);
    return banner;
  }
  function render(items) {
    const active = (items || []).filter(isActive).sort((a, b) => Number(a.auction.endsAt) - Number(b.auction.endsAt));
    lastItems = active;
    if (!active.length) {
      root.hidden = true;
      root.replaceChildren();
      return;
    }
    root.hidden = false;
    root.classList.toggle('urgent', Number(active[0].auction.endsAt) - Date.now() / 1000 <= 3600);
    root.classList.toggle('ending-today', Number(active[0].auction.endsAt) - Date.now() / 1000 <= 86400);
    const head = element('div', 'auction-promo-head');
    head.appendChild(element('h2', '', 'LIVE AUCTIONS'));
    const all = element('a', '', 'VIEW ALL AUCTIONS  »');
    all.href = '/app/#/catalog?cat=auctions';
    head.appendChild(all);
    root.replaceChildren(head, makeBanner(active[0]));

    if (active.length > 1) {
      const more = element('div', 'auction-promo-more');
      active.slice(1, 4).forEach((item) => {
        const link = element('a', '');
        link.href = '/app/#/item/' + encodeURIComponent(item.id);
        link.appendChild(element('span', 'auction-promo-mini-tag', 'LIVE AUCTION'));
        link.appendChild(element('b', 'auction-promo-mini-name', item.name));
        const time = element('span', 'auction-promo-mini-time', countdown(item.auction.endsAt));
        time.dataset.auctionEnd = String(item.auction.endsAt);
        link.appendChild(time);
        more.appendChild(link);
      });
      root.appendChild(more);
    }
  }
  async function refresh() {
    if (loading || document.visibilityState === 'hidden') return;
    loading = true;
    try {
      // again=false: a busy server's "are you a person?" check never pops up on the front page.
      const response = await apiCall('list', { kind: 'clothing', auctions: true, limit: 100 }, false);
      if (response && response.ok && Array.isArray(response.assets)) render(response.assets);
      else if (!lastItems.length) root.hidden = true;
    } catch {
      if (!lastItems.length) root.hidden = true;
    } finally {
      loading = false;
    }
  }
  function tick() {
    let expired = false;
    root.querySelectorAll('[data-auction-end]').forEach((node) => {
      const end = Number(node.dataset.auctionEnd);
      if (end <= Date.now() / 1000) expired = true;
      node.textContent = countdown(end);
    });
    if (expired) {
      render(lastItems);   // remove an expired banner immediately, even before the next network response
      refresh();
    }
  }
  refresh();
  window.setInterval(tick, 1000);
  window.setInterval(refresh, 60000);   // (the countdown ticks by itself; bids don't need a fast refresh)
  window.addEventListener('focus', refresh);
  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'visible') refresh(); });
})();
