// Languages for the website. The words are in website/lang/<code>.json (the
// Player and Studio use the same files, baked in when they're built).
//
// Pages don't need changing to be translated: once the page has drawn, any text
// that exactly matches an English line in the file is swapped for its
// translation (and again whenever the page changes). Text with no translation
// stays English. Anything inside class="notranslate" or translate="no" is left
// alone (names people typed, code).

// Every language there is (code, its own name). Add a line here and a
// <code>.json file to add one.
export const LANGUAGES = [
  ['en', 'English'], ['es', 'Español'], ['pt', 'Português'], ['fr', 'Français'],
  ['de', 'Deutsch'], ['ru', 'Русский'], ['pl', 'Polski'],
];
const KEY = 'gb.lang';
const here = new URL('.', import.meta.url);   // (works from / and from /app/)

let table = null;   // English line -> translation (null = English)

// What the visitor picked ('' = the browser's language).
export function savedLanguage() { try { return localStorage.getItem(KEY) || ''; } catch { return ''; } }
function browserLanguage() {
  for (const l of navigator.languages || [navigator.language || 'en']) {
    const code = String(l).toLowerCase().split('-')[0];
    if (LANGUAGES.some(([c]) => c === code)) return code;
  }
  return 'en';
}
// The language showing now.
export function language() { const s = savedLanguage(); return LANGUAGES.some(([c]) => c === s) ? s : browserLanguage(); }
export function setLanguage(code) {
  try { if (code) localStorage.setItem(KEY, code); else localStorage.removeItem(KEY); } catch { /* private window */ }
  location.reload();
}
export const languageName = (code) => (LANGUAGES.find(([c]) => c === code) || [code, code])[1];

// The translation of an English line (itself if there isn't one).
export function t(text) {
  if (!table || typeof text !== 'string') return text;
  const key = text.replace(/\s+/g, ' ').trim();
  const tr = key && table.get(key);
  if (!tr) return text;
  const lead = text.match(/^\s*/)[0], trail = text.slice(lead.length).match(/\s*$/)[0];
  return lead + tr + trail;
}

const SKIP = new Set(['SCRIPT', 'STYLE', 'TEXTAREA', 'CODE', 'PRE', 'NOSCRIPT']);
const ATTRS = ['placeholder', 'title', 'aria-label', 'alt'];
const left = (el) => el.closest && el.closest('.notranslate, [translate="no"], [contenteditable="true"]');

const written = new WeakMap();   // text we put in ourselves: don't translate it again
function doText(node) {
  const p = node.parentElement;
  if (!p || SKIP.has(p.tagName) || left(p) || written.get(node) === node.nodeValue) return;
  const v = node.nodeValue, tr = t(v);
  if (tr !== v) { node.nodeValue = tr; written.set(node, tr); }
}
function doAttrs(el) {
  if (left(el)) return;
  for (const a of ATTRS) {
    const v = el.getAttribute(a);
    if (v) { const tr = t(v); if (tr !== v) el.setAttribute(a, tr); }
  }
  if (el.tagName === 'INPUT' && (el.type === 'submit' || el.type === 'button') && el.value) {
    const tr = t(el.value); if (tr !== el.value) el.value = tr;
  }
}
function translate(root) {
  if (root.nodeType === Node.TEXT_NODE) { doText(root); return; }
  if (root.nodeType !== Node.ELEMENT_NODE || SKIP.has(root.tagName)) return;
  doAttrs(root);
  const walk = document.createTreeWalker(root, NodeFilter.SHOW_TEXT | NodeFilter.SHOW_ELEMENT);
  for (let n = walk.nextNode(); n; n = walk.nextNode()) {
    if (n.nodeType === Node.TEXT_NODE) doText(n); else doAttrs(n);
  }
}

// Loads the language and starts translating the page. Safe to call more than once.
let started = null;
export function startTranslating() {
  if (started) return started;
  started = (async () => {
    const code = language();
    document.documentElement.lang = code;
    if (code === 'en') return;
    try {
      const r = await fetch(new URL(`${code}.json`, here));
      const data = await r.json();
      table = new Map(Object.entries(data.strings || {}).filter(([en, tr]) => tr && tr !== en));
    } catch { return; }   // offline: English it is
    translate(document.body);
    document.title = t(document.title);
    new MutationObserver((changes) => {
      for (const c of changes) {
        if (c.type === 'characterData') doText(c.target);
        else if (c.type === 'attributes') doAttrs(c.target);
        else c.addedNodes.forEach(translate);
      }
    }).observe(document.body, { subtree: true, childList: true, characterData: true, attributes: true, attributeFilter: ATTRS });
    // Pop-up questions too.
    for (const name of ['alert', 'confirm', 'prompt']) {
      const real = window[name].bind(window);
      window[name] = (msg, ...rest) => real(t(msg), ...rest);
    }
  })();
  return started;
}

// A language list (<select>) that switches the page when changed.
export function languagePicker() {
  const sel = document.createElement('select');
  sel.className = 'lang-pick notranslate';
  sel.setAttribute('aria-label', 'Language');
  const saved = savedLanguage();
  const auto = document.createElement('option');
  auto.value = '';
  auto.textContent = `${t('Same as this browser')} (${languageName(language())})`;
  sel.append(auto);
  for (const [code, name] of LANGUAGES) {
    const o = document.createElement('option');
    o.value = code;
    o.textContent = name;
    sel.append(o);
  }
  sel.value = LANGUAGES.some(([c]) => c === saved) ? saved : '';
  sel.addEventListener('change', () => setLanguage(sel.value));
  return sel;
}
