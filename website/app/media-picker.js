// The GIF and sticker picker for chats and the forum, with KLIPY's search (klipy.com).
// KLIPY's rules: the search runs from the visitor's own browser (so the site's security rules
// in worker/security.js let this page talk to api.klipy.com), its pictures are shown as they
// come (never copied or re-hosted), and the picker says "Powered by KLIPY".
//
// A picked picture goes into the message as a token, [media:https://static.klipy.com/...gif],
// which chats and posts show as the picture (embeds.js). The text filter on both servers lets
// these tokens through (MEDIA_TOKEN in worker/textfilter.js), but no other links.
//
// The app key is a public browser key (not a secret). It lives here, not in an inline <script>
// in index.html, because the site's security rules don't run inline scripts.
const DEFAULT_KLIPY_KEY = 'dzEYbFCO0MtIea7Rexu9CzPvkduUWZxcYxOrS9ORXrW9gNUK1GvwU6bgjNuJdsNa';
const KLIPY_KEY = (typeof window !== 'undefined' && typeof window.GB_KLIPY_APP_KEY === 'string' && window.GB_KLIPY_APP_KEY.trim()) || DEFAULT_KLIPY_KEY;
const KLIPY_API = 'https://api.klipy.com/api/v1';
const KLIPY_FILTER = 'medium';
const PAGE_SIZE = 24;

// The emoji reactions people can put on chat messages and forum posts (REACTIONS in
// worker/server.js and reactions() in src/server/ServerUtil.h are the same list).
export const REACTIONS = ['👍', '❤️', '😂', '😮', '😢', '🔥', '🎉', '👀'];

// Only KLIPY's picture servers, over https, and only picture files: the same rule as the
// servers' text filter, so a picture that shows here also survives being sent.
const MEDIA_HOST = /^(?:[a-z0-9-]+\.)*klipy\.com$/i;
const MEDIA_PATH = /^\/[A-Za-z0-9._~\/%-]{1,400}\.(?:gif|png|jpe?g|webp)$/i;
export function safeMediaUrl(value) {
  if (typeof value !== 'string' || value.length > 2048) return '';
  try {
    const url = new URL(value.trim());
    if (url.protocol !== 'https:' || url.username || url.password || url.port || url.search || url.hash) return '';
    if (!MEDIA_HOST.test(url.hostname) || !MEDIA_PATH.test(url.pathname)) return '';
    return url.href;
  } catch { return ''; }
}

// One KLIPY result -> { url (what gets sent), preview (small, for the grid), title, slug, w, h }.
// The sent one is the medium size: big enough to look good, ~10x smaller than "hd".
const pick = (file, sizes, kinds) => {
  for (const size of sizes) for (const kind of kinds) { const u = file && file[size] && file[size][kind]; if (u && u.url) return u; }
  return null;
};
export function normalizeMediaItem(item) {
  if (!item || typeof item !== 'object' || item.type === 'ad') return null;
  const file = item.file || null;
  const main = typeof item.url === 'string' ? { url: item.url } : typeof item.src === 'string' ? { url: item.src }
    : pick(file, ['md', 'hd', 'sm', 'xs'], ['gif', 'webp', 'png', 'jpg', 'jpeg']);
  const url = safeMediaUrl(main && main.url);
  if (!url) return null;
  const small = pick(file, ['sm', 'xs', 'md'], ['webp', 'gif', 'png', 'jpg']);
  const preview = safeMediaUrl(small && small.url) || url;
  const title = typeof item.title === 'string' ? item.title.slice(0, 120) : '';
  return { url, preview, title, slug: typeof item.slug === 'string' ? item.slug : '',
    w: Number(main.width) || 0, h: Number(main.height) || 0 };
}
function extractItems(payload) {
  const candidates = payload?.data?.data || payload?.data?.items || payload?.data?.results || payload?.results || [];
  return Array.isArray(candidates) ? candidates.map(normalizeMediaItem).filter(Boolean) : [];
}
async function klipyFetch(kind, query = '', page = 1) {
  const path = query ? `/${kind}/search` : `/${kind}/trending`;
  const params = new URLSearchParams({ page: String(page), per_page: String(PAGE_SIZE), content_filter: KLIPY_FILTER });
  if (query) params.set('q', query);
  const response = await fetch(`${KLIPY_API}/${encodeURIComponent(KLIPY_KEY)}${path}?${params}`, { headers: { Accept: 'application/json' } });
  if (!response.ok) throw new Error(response.status === 429 ? 'Too many GIF searches right now. Try again in a minute.' : 'The GIF search isn\'t answering right now (' + response.status + ').');
  const payload = await response.json();
  if (payload?.result === false) throw new Error(payload?.message || 'The GIF search didn\'t work. Try again.');
  return { items: extractItems(payload), more: !!payload?.data?.has_next || extractItems(payload).length >= PAGE_SIZE };
}

// Opens the picker. onSelect({url, title}) gets the chosen picture.
export function openMediaPicker({ onSelect, onClose } = {}) {
  if (typeof document === 'undefined') return null;
  const overlay = document.createElement('div');
  overlay.className = 'gb-media-overlay';
  overlay.setAttribute('role', 'dialog');
  overlay.setAttribute('aria-modal', 'true');
  overlay.setAttribute('aria-label', 'Choose a GIF or sticker');
  overlay.innerHTML = '<section class="gb-media-picker">' +
    '<header><div class="gb-media-tabs" role="tablist"><button type="button" role="tab" class="active" data-kind="gifs">GIFs</button><button type="button" role="tab" data-kind="stickers">Stickers</button></div>' +
    '<button type="button" class="gb-media-x" data-close aria-label="Close">×</button></header>' +
    '<form class="gb-media-search"><input type="search" name="q" placeholder="Search KLIPY" aria-label="Search GIFs and stickers" autocomplete="off" maxlength="60"></form>' +
    '<div class="gb-media-chips"></div>' +
    '<div class="gb-media-status" role="status" aria-live="polite"></div><div class="gb-media-results"></div>' +
    '<footer><span>Powered by <b>KLIPY</b></span><span class="gb-media-hint">Pick one to add it</span></footer></section>';
  const results = overlay.querySelector('.gb-media-results');
  const form = overlay.querySelector('.gb-media-search');
  const input = form.elements.q;
  const status = overlay.querySelector('.gb-media-status');
  const chips = overlay.querySelector('.gb-media-chips');
  let kind = 'gifs', query = '', page = 1, busy = false, more = false, token = 0;
  const QUICK = ['lol', 'gg', 'wow', 'yes', 'no', 'thanks', 'hype', 'sad', 'facepalm', 'dance'];
  chips.innerHTML = QUICK.map((w) => '<button type="button" data-q="' + w + '">' + w + '</button>').join('');
  const close = () => { overlay.remove(); document.removeEventListener('keydown', onKey, true); onClose?.(); };
  const onKey = (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(); } };
  const buttonFor = (item) => {
    const button = document.createElement('button');
    button.type = 'button'; button.className = 'gb-media-result'; button.title = item.title || 'Add this';
    const img = document.createElement('img');
    img.src = item.preview; img.alt = item.title || (kind === 'stickers' ? 'Sticker' : 'GIF'); img.loading = 'lazy'; img.referrerPolicy = 'no-referrer'; img.decoding = 'async';
    button.append(img);
    button.addEventListener('click', () => { onSelect?.({ url: item.url, title: item.title || '' }); close(); });
    return button;
  };
  const load = async (append = false) => {
    if (busy && append) return;
    const mine = ++token;
    busy = true;
    if (!append) { results.replaceChildren(); results.scrollTop = 0; }
    status.textContent = append ? '' : 'Loading…';
    try {
      const r = await klipyFetch(kind, query, page);
      if (mine !== token) return;
      if (!r.items.length && !append) status.textContent = query ? 'Nothing found for "' + query + '". Try another word.' : 'Nothing here right now.';
      else status.textContent = '';
      const fragment = document.createDocumentFragment();
      r.items.forEach((item) => fragment.append(buttonFor(item)));
      results.append(fragment);
      more = r.more && r.items.length > 0;
    } catch (error) {
      if (mine !== token) return;
      status.textContent = error?.message || 'The GIF search didn\'t work.';
      more = false;
    } finally { if (mine === token) busy = false; }
  };
  const restart = () => { page = 1; more = false; load(); };
  let typing = 0;
  input.addEventListener('input', () => { clearTimeout(typing); typing = setTimeout(() => { const q = input.value.trim(); if (q !== query) { query = q; restart(); } }, 350); });
  form.addEventListener('submit', (e) => { e.preventDefault(); clearTimeout(typing); query = input.value.trim(); restart(); });
  results.addEventListener('scroll', () => { if (more && !busy && results.scrollTop + results.clientHeight > results.scrollHeight - 300) { page++; load(true); } });
  overlay.addEventListener('click', (e) => {
    if (e.target === overlay || e.target.closest('[data-close]')) { close(); return; }
    const tab = e.target.closest('[data-kind]');
    if (tab) { kind = tab.dataset.kind; overlay.querySelectorAll('[data-kind]').forEach((b) => b.classList.toggle('active', b === tab)); restart(); return; }
    const chip = e.target.closest('[data-q]');
    if (chip) { input.value = chip.dataset.q; query = chip.dataset.q; restart(); }
  });
  document.addEventListener('keydown', onKey, true);
  document.body.append(overlay);
  load();
  setTimeout(() => input.focus(), 0);
  return { close };
}

// Text plus a picture token, kept inside the server's length limit (null when it won't fit,
// so the picture never gets cut off the end).
export function composeChatMessage(text, attachment = null, maxLength = 1000) {
  const bodyText = typeof text === 'string' ? text : '';
  const url = attachment ? safeMediaUrl(attachment.url) : '';
  if (attachment && !url) return null;
  const token = url ? (bodyText ? '\n' : '') + '[media:' + url + ']' : '';
  const body = bodyText + token;
  return body.length <= maxLength ? body : null;
}
