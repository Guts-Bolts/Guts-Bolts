// Banner ads (Google AdSense) on the website, and the cookie question for Europe.
//
// - Ads only go on "clean" pages: Home, Games, Catalog and Updates. Never on the forum, game
//   or item pages (people's own stuff), chats, profiles, Settings or Staff.
// - Boltworks Club members never see ads, and Google's ad code isn't even loaded for them.
// - Under every ad: "Want to remove ads? Join Boltworks Club today!"
// - Until AdSense is set up (worker/ads.js), nobody sees ads. Staff see a grey box where
//   the banner will go, so the spot can be checked.
// - In the EU, EEA, UK and Switzerland, the visitor is asked first (unless Google's own
//   consent message is turned on). "Only necessary" means no ads at all for them.

export const AD_PAGES = new Set(['home', 'games', 'catalog', 'updates']);
const CHOICE_KEY = 'gb-cookie-choice';   // 'all' or 'necessary'

// Should this page show the ad spot for this person? (me.club = their Boltworks Club tier.)
export function adSpotWanted(page, me) {
  if (!AD_PAGES.has(page)) return false;
  if (me && me.club) return false;
  if (me && me.banned) return false;
  return true;
}

// What goes in the spot: 'ad' (a real AdSense banner), 'placeholder' (staff only, before
// AdSense is set up), 'ask' (wait for the cookie answer) or '' (nothing).
export function adMode(config, choice, me) {
  if (!config || !config.client || !config.slot) return me && me.staff ? 'placeholder' : '';
  if (config.consent && !config.googleConsent) {
    if (choice === 'necessary') return '';
    if (choice !== 'all') return 'ask';
  }
  return 'ad';
}

let config = null, configLoading = null, scriptAdded = false, lastArgs = null, placeToken = 0;

function readChoice() {
  try { return localStorage.getItem(CHOICE_KEY) || ''; } catch { return ''; }
}
function saveChoice(v) {
  try { localStorage.setItem(CHOICE_KEY, v); } catch { /* private window: asked again next time */ }
}

async function loadConfig() {
  if (config) return config;
  if (!configLoading) configLoading = fetch('/ads-config', { cache: 'no-store' })
    .then((r) => (r.ok ? r.json() : {})).catch(() => ({})).then((c) => (config = c));
  return configLoading;
}

function addGoogleScript(client) {
  if (scriptAdded) return;
  scriptAdded = true;
  const s = document.createElement('script');
  s.async = true;
  s.src = 'https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js?client=' + encodeURIComponent(client);
  s.crossOrigin = 'anonymous';
  document.head.append(s);
}

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text) e.textContent = text;
  return e;
}

function clubBox() {
  const box = el('div', 'gb-ad-club');
  box.append(el('span', '', 'Want to remove ads? '));
  const a = el('a', '', 'Join Boltworks Club today!');
  a.href = '#/bolts';
  box.append(a);
  return box;
}

function askCookies() {
  if (document.querySelector('.gb-cookies')) return;
  const bar = el('div', 'gb-cookies');
  bar.setAttribute('role', 'dialog');
  bar.setAttribute('aria-label', 'Cookies');
  const text = el('p');
  text.append('Guts&Bolts is free, and ads help pay for the servers. Can our ad partner Google use cookies to show you ads? ');
  const more = el('a', '', 'Privacy policy');
  more.href = '/privacy';
  text.append(more);
  const yes = el('button', 'btn green', 'Accept');
  const no = el('button', 'btn', 'Only necessary');
  const done = (v) => { saveChoice(v); bar.remove(); if (lastArgs) placeAds(...lastArgs); };
  yes.addEventListener('click', () => done('all'));
  no.addEventListener('click', () => done('necessary'));
  const buttons = el('div', 'gb-cookies-buttons');
  buttons.append(yes, no);
  bar.append(text, buttons);
  document.body.append(bar);
}

// Called after each page is drawn. page = the page's name ('home', 'games', ...).
export async function placeAds(page, me) {
  lastArgs = [page, me];
  const token = ++placeToken;
  const view = document.getElementById('view');
  if (!view) return;
  view.querySelectorAll('.gb-ad').forEach((x) => x.remove());
  if (!adSpotWanted(page, me)) return;
  const c = await loadConfig();
  if (token !== placeToken) return;   // they moved on while we waited
  const mode = adMode(c, readChoice(), me);
  if (mode === 'ask') { askCookies(); return; }
  if (!mode || view.querySelector('.gb-ad')) return;

  const spot = el('aside', 'gb-ad');
  spot.setAttribute('aria-label', 'Advertisement');
  spot.append(el('div', 'gb-ad-label', 'Advertisement'));
  if (mode === 'placeholder') {
    spot.append(el('div', 'gb-ad-placeholder', 'Ad spot. Only staff can see this box. Real ads show here once AdSense is set up.'));
  } else {
    const ins = el('ins', 'adsbygoogle');
    ins.dataset.adClient = c.client;
    ins.dataset.adSlot = c.slot;
    ins.dataset.adFormat = 'horizontal';
    ins.dataset.fullWidthResponsive = 'true';
    spot.append(ins);
  }
  spot.append(clubBox());
  view.prepend(spot);
  if (mode === 'ad') {
    addGoogleScript(c.client);
    try { (window.adsbygoogle = window.adsbygoogle || []).push({}); } catch { /* blocked by an ad blocker */ }
  }
}

// For the privacy page's "change my answer" button.
export function forgetCookieChoice() {
  try { localStorage.removeItem(CHOICE_KEY); } catch { /* nothing saved */ }
}
