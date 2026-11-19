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
// - Everywhere else (like US states with privacy laws), people can say "don't sell or share my
//   info": they still see ads, but Google is told to show only non-personalized ones. A browser
//   that sends Global Privacy Control (GPC) counts as saying that.
// - Once Google's own messages are on (ADS_GOOGLE_CONSENT, worker/ads.js), Google's
//   Privacy & messaging tag is loaded on every page: it shows the European consent message, the
//   US state "Do not sell or share" link, and the ad-blocking recovery message, whichever ones
//   are made in AdSense.
// - Any link with data-privacy-choices (the "Privacy choices" footer link) opens the choices panel.

export const AD_PAGES = new Set(['home', 'games', 'catalog', 'updates']);
const CHOICE_KEY = 'gb-cookie-choice';   // 'all', 'npa' (non-personalized only) or 'necessary'

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

// May Google personalize the ads? Not if they said no, or their browser sends GPC.
export function personalizedAds(choice, gpc) {
  return choice !== 'npa' && choice !== 'necessary' && !gpc;
}

// Google's Privacy & messaging tag for this publisher ("ers=1" turns on ad-blocking recovery).
export function messagesTagUrl(client) {
  const m = /^ca-pub-(\d{10,20})$/.exec(String(client || ''));
  return m ? 'https://fundingchoicesmessages.google.com/i/pub-' + m[1] + '?ers=1' : '';
}

function gpcOn() {
  try { return navigator.globalPrivacyControl === true; } catch { return false; }
}

let config = null, configLoading = null, scriptAdded = false, messagesAdded = false, lastArgs = null, placeToken = 0;

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
  // Said no to personalized ads (or GPC)? Google must be told before its code runs.
  if (!personalizedAds(readChoice(), gpcOn())) (window.adsbygoogle = window.adsbygoogle || []).requestNonPersonalizedAds = 1;
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
  const cookies = el('a', '', 'Cookie policy');
  cookies.href = '/cookies';
  text.append(more, ' · ', cookies);
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

// Google's Privacy & messaging tag, on every page, once ADS_GOOGLE_CONSENT is on. Not for
// Boltworks Club members (they never get ads, so there's nothing to ask them about).
export async function startGoogleMessages(me) {
  if (messagesAdded || (me && me.club)) return;
  const c = await loadConfig();
  const src = c && c.googleConsent ? messagesTagUrl(c.client) : '';
  if (!src || messagesAdded) return;
  messagesAdded = true;
  window.googlefc = window.googlefc || {};
  window.googlefc.callbackQueue = window.googlefc.callbackQueue || [];
  const s = document.createElement('script');
  s.async = true;
  s.src = src;
  document.head.append(s);
  // Google's tag looks for this hidden frame to know it loaded (it's how ad-block recovery
  // tells "the tag was blocked" apart from "the tag isn't on the page").
  const signal = () => {
    if (window.frames.googlefcPresent) return;
    if (!document.body) { setTimeout(signal, 0); return; }
    const f = document.createElement('iframe');
    f.name = 'googlefcPresent';
    f.title = '';
    f.setAttribute('aria-hidden', 'true');
    f.style.cssText = 'display:none;width:0;height:0;border:0';
    document.body.append(f);
  };
  signal();
}

function closeChoices() {
  const d = document.querySelector('.gb-choices-back');
  if (d) d.remove();
}

// The "Privacy choices" panel. In Europe: ad cookies yes/no. Elsewhere: personalized ads,
// or "don't sell or share my info" (non-personalized ads only).
export async function openPrivacyChoices() {
  closeChoices();
  const c = await loadConfig();
  const choice = readChoice();
  const gpc = gpcOn();
  const europe = !!(c && c.consent);
  const back = el('div', 'gb-choices-back');
  const box = el('div', 'gb-choices');
  box.setAttribute('role', 'dialog');
  box.setAttribute('aria-modal', 'true');
  box.setAttribute('aria-labelledby', 'gb-choices-title');
  const h = el('h2', '', 'Privacy choices');
  h.id = 'gb-choices-title';
  box.append(h);
  box.append(el('p', '', 'Guts&Bolts is free, and ads from Google help pay for the servers. We never sell your data ourselves. ' +
    'Here you choose what Google may do with cookies when it shows you ads.'));
  const options = europe
    ? [['all', 'Allow ad cookies', 'Google may use cookies to show and measure ads, including personalized ones.'],
      ['necessary', 'Only necessary', 'No ad cookies, and no ads at all for you. The site works the same.']]
    : [['all', 'Personalized ads', 'Google may use cookies and what it knows about you to pick ads.'],
      ['npa', 'Do not sell or share my personal information', 'You still see ads, but Google is told to show only non-personalized ones, not based on your activity on other sites.']];
  const current = europe ? (choice === 'all' ? 'all' : choice ? 'necessary' : '') : (personalizedAds(choice, gpc) ? 'all' : 'npa');
  const form = el('form', 'gb-choices-form');
  for (const [v, label, help] of options) {
    const row = el('label', 'gb-choice');
    const input = document.createElement('input');
    input.type = 'radio';
    input.name = 'gb-choice';
    input.value = v;
    input.checked = v === current;
    const words = el('span');
    words.append(el('b', '', label), el('small', '', help));
    row.append(input, words);
    form.append(row);
  }
  box.append(form);
  if (gpc && !europe) box.append(el('p', 'gb-choices-note', 'Your browser sends Global Privacy Control, so we already treat you as opted out of selling or sharing.'));
  if (c && c.googleConsent && window.googlefc && typeof window.googlefc.showRevocationMessage === 'function' && europe) {
    const g = el('button', 'btn', 'Open Google\'s consent settings');
    g.type = 'button';
    g.addEventListener('click', () => { closeChoices(); window.googlefc.showRevocationMessage(); });
    box.append(g);
  }
  const links = el('p', 'gb-choices-links');
  const a1 = el('a', '', 'Privacy Policy'); a1.href = '/privacy';
  const a2 = el('a', '', 'Cookie Policy'); a2.href = '/cookies';
  links.append(a1, ' · ', a2);
  box.append(links);
  const save = el('button', 'btn green', 'Save');
  const cancel = el('button', 'btn', 'Cancel');
  save.type = cancel.type = 'button';
  cancel.addEventListener('click', closeChoices);
  save.addEventListener('click', () => {
    const picked = form.querySelector('input:checked');
    if (picked) {
      const before = readChoice();
      saveChoice(picked.value);
      // Google's ad code already ran with the old answer: start the page again so it takes the new one.
      if (scriptAdded && picked.value !== before) { location.reload(); return; }
    }
    closeChoices();
    if (lastArgs) placeAds(...lastArgs);
  });
  const buttons = el('div', 'gb-cookies-buttons');
  buttons.append(save, cancel);
  box.append(buttons);
  back.append(box);
  back.addEventListener('click', (e) => { if (e.target === back) closeChoices(); });
  back.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeChoices(); });
  document.body.append(back);
  const first = form.querySelector('input:checked') || form.querySelector('input');
  if (first) first.focus();
}

// Every "Privacy choices" link on any page opens the panel.
if (typeof document !== 'undefined') {
  document.addEventListener('click', (e) => {
    const link = e.target && e.target.closest && e.target.closest('[data-privacy-choices]');
    if (!link) return;
    e.preventDefault();
    openPrivacyChoices();
  });
}

// Called after each page is drawn. page = the page's name ('home', 'games', ...).
export async function placeAds(page, me) {
  lastArgs = [page, me];
  const token = ++placeToken;
  const view = document.getElementById('view');
  if (!view) return;
  view.querySelectorAll('.gb-ad').forEach((x) => x.remove());
  startGoogleMessages(me);
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
