// Shared media picker with direct-client KLIPY GIF/sticker search.
// Configure the public KLIPY app key in the page bootstrap as window.GB_KLIPY_APP_KEY.
// KLIPY requires requests and media loads to originate from the end-user browser.
const KLIPY_KEY = typeof window !== 'undefined' && typeof window.GB_KLIPY_APP_KEY === 'string'
  ? window.GB_KLIPY_APP_KEY.trim() : '';
const KLIPY_API = 'https://api.klipy.com/api/v1';
const KLIPY_FILTER = 'medium';
const PAGE_SIZE = 24;

export function safeMediaUrl(value) {
  if (typeof value !== 'string' || value.length > 2048) return '';
  try {
    const url = new URL(value.trim());
    if (url.protocol !== 'https:' || url.username || url.password) return '';
    const ext = url.pathname.toLowerCase().split('.').pop();
    if (!['gif', 'png', 'jpg', 'jpeg', 'webp'].includes(ext)) return '';
    return url.href;
  } catch { return ''; }
}
export function normalizeMediaItem(item) {
  if (!item || typeof item !== 'object' || item.type === 'ad') return null;
  const url = safeMediaUrl(item.url || item.src || item.file?.hd?.gif?.url || item.file?.gif?.url ||
    item.file?.hd?.webp?.url || item.file?.webp?.url || item.file?.hd?.jpg?.url || item.file?.jpg?.url);
  if (!url) return null;
  const title = typeof item.title === 'string' ? item.title.slice(0, 120) : '';
  return { url, title, slug: typeof item.slug === 'string' ? item.slug : '' };
}
function extractItems(payload) {
  const candidates = payload?.data?.data || payload?.data?.items || payload?.data?.results || payload?.results || [];
  return Array.isArray(candidates) ? candidates.map(normalizeMediaItem).filter(Boolean) : [];
}
async function klipyFetch(kind, mode, query = '', page = 1) {
  if (!KLIPY_KEY) throw new Error('KLIPY app key is not configured yet.');
  const path = mode === 'search' ? `/${kind}/search` : `/${kind}/trending`;
  const params = new URLSearchParams({ page: String(page), per_page: String(PAGE_SIZE), content_filter: KLIPY_FILTER, format_filter: 'gif,webp,jpg' });
  if (mode === 'search') params.set('q', query);
  const response = await fetch(`${KLIPY_API}/${encodeURIComponent(KLIPY_KEY)}${path}?${params}`, { headers: { Accept: 'application/json' } });
  if (!response.ok) throw new Error(`KLIPY request failed (${response.status}).`);
  const payload = await response.json();
  if (payload?.result === false) throw new Error(payload?.message || 'KLIPY returned an unsuccessful response.');
  return extractItems(payload);
}
export function openMediaPicker({ providers = [], onSelect, onClose } = {}) {
  if (typeof document === 'undefined') return null;
  const overlay = document.createElement('div');
  overlay.className = 'gb-media-overlay';
  overlay.setAttribute('role', 'dialog');
  overlay.setAttribute('aria-modal', 'true');
  overlay.setAttribute('aria-label', 'Choose media');
  const providerOptions = providers.filter(p => p && typeof p.search === 'function');
  overlay.innerHTML = '<section class="gb-media-picker gb-media-picker-klipy"><header><strong>Add media</strong><button type="button" data-close aria-label="Close">×</button></header>' +
    '<div class="gb-media-tabs" role="tablist"><button type="button" class="active" data-mode="trending">Trending GIFs</button><button type="button" data-mode="search">GIF search</button><button type="button" data-mode="stickers">Stickers</button></div>' +
    '<form class="gb-media-search"><label><span class="sr-only">Search KLIPY</span><input type="search" name="q" placeholder="Search KLIPY" autocomplete="off"></label><button class="btn blue" type="submit">Search</button></form>' +
    '<div class="gb-media-status" role="status" aria-live="polite"></div><div class="gb-media-results" aria-live="polite"></div><div class="gb-media-more"><button type="button" data-more hidden>Load more</button></div>' +
    '<footer><small>Powered by KLIPY</small><button type="button" data-cancel>Cancel</button></footer></section>';
  const results = overlay.querySelector('.gb-media-results');
  const form = overlay.querySelector('.gb-media-search');
  const status = overlay.querySelector('.gb-media-status');
  const more = overlay.querySelector('[data-more]');
  let mode = 'trending', query = '', page = 1, busy = false;
  const close = () => { overlay.remove(); onClose?.(); };
  const buttonFor = (item) => {
    const button = document.createElement('button'); button.type = 'button'; button.className = 'gb-media-result'; button.title = item.title || 'Attach media';
    const img = document.createElement('img'); img.src = item.url; img.alt = item.title || 'KLIPY media preview'; img.loading = 'lazy'; img.referrerPolicy = 'no-referrer';
    button.append(img);
    button.addEventListener('click', () => { onSelect?.({ url: item.url, title: item.title || '', slug: item.slug || '' }); close(); });
    return button;
  };
  const renderItems = (items, append = false) => {
    if (!append) results.replaceChildren();
    if (!items.length && !append) { results.textContent = 'No results found. Try another search.'; return; }
    const fragment = document.createDocumentFragment();
    items.forEach(item => fragment.append(buttonFor(item)));
    results.append(fragment);
  };
  const load = async (append = false) => {
    if (busy) return;
    busy = true; status.textContent = append ? 'Loading more…' : 'Loading…'; more.hidden = true;
    try {
      let items;
      if (mode === 'stickers' || mode === 'sticker-search' || mode === 'trending' || mode === 'search') {
        const kind = mode === 'stickers' || mode === 'sticker-search' ? 'stickers' : 'gifs';
        const apiMode = mode === 'search' || mode === 'sticker-search' ? 'search' : 'trending';
        items = await klipyFetch(kind, apiMode, query, page);
      }
      if (!items.length && providerOptions.length && mode === 'search') {
        const providerItems = await Promise.all(providerOptions.map(p => p.search(query).catch(() => [])));
        items = providerItems.flat().map(normalizeMediaItem).filter(Boolean);
      }
      renderItems(items || [], append);
      status.textContent = '';
      more.hidden = !items?.length;
    } catch (error) {
      status.textContent = error?.message || 'Media search failed.';
      results.replaceChildren();
    } finally { busy = false; }
  };
  overlay.addEventListener('click', e => {
    const target = e.target.closest('[data-mode]');
    if (e.target === overlay || e.target.closest('[data-close], [data-cancel]')) { close(); return; }
    if (target) {
      mode = target.dataset.mode; query = ''; page = 1; form.elements.q.value = '';
      overlay.querySelectorAll('[data-mode]').forEach(b => b.classList.toggle('active', b === target));
      form.hidden = mode === 'trending'; load(); return;
    }
    if (e.target.closest('[data-more]')) { page++; load(true); }
  });
  overlay.addEventListener('keydown', e => { if (e.key === 'Escape') close(); });
  form.addEventListener('submit', e => { e.preventDefault(); query = form.elements.q.value.trim(); if (!query) return; mode = mode === 'stickers' || mode === 'sticker-search' ? 'sticker-search' : 'search'; page = 1; overlay.querySelectorAll('[data-mode]').forEach(b => b.classList.toggle('active', b.dataset.mode === (mode === 'sticker-search' ? 'stickers' : 'search'))); load(); });
  if (!KLIPY_KEY) status.textContent = 'KLIPY app key is not configured. Paste a media URL below to attach it.';
  // Keep manual HTTPS attachment available until the KLIPY key is configured.
  const manual = document.createElement('details'); manual.className = 'gb-media-manual';
  manual.innerHTML = '<summary>Attach by URL instead</summary><form><label>Image or GIF URL<input type="url" name="url" placeholder="https://…/image.gif" autocomplete="url" required></label><button class="btn blue" type="submit">Preview</button></form><div class="gb-media-manual-preview"></div>';
  const manualForm = manual.querySelector('form'), preview = manual.querySelector('.gb-media-manual-preview');
  manualForm.addEventListener('submit', e => {
    e.preventDefault(); const url = safeMediaUrl(manualForm.elements.url.value); preview.replaceChildren();
    if (!url) { preview.textContent = 'Enter a supported HTTPS image/GIF link.'; return; }
    const img = document.createElement('img'); img.src = url; img.alt = 'Media preview'; img.loading = 'lazy'; img.referrerPolicy = 'no-referrer';
    const attach = document.createElement('button'); attach.type = 'button'; attach.textContent = 'Attach this media';
    attach.addEventListener('click', () => { onSelect?.({ url, title: '' }); close(); });
    preview.append(img, attach);
  });
  overlay.querySelector('.gb-media-picker').append(manual);
  if (!KLIPY_KEY) { overlay.querySelector('.gb-media-tabs').hidden = true; form.hidden = true; more.hidden = true; }
  document.body.append(overlay);
  if (KLIPY_KEY) load();
  else manualForm.elements.url.focus();
  return { close };
}
// Compose chat text and media token without allowing the server's 1,000-character
// message filter to truncate the attachment off the end of the message.
export function composeChatMessage(text, attachment = null, maxLength = 1000) {
  const bodyText = typeof text === 'string' ? text : '';
  const url = attachment ? safeMediaUrl(attachment.url) : '';
  if (attachment && !url) return null;
  const token = url ? (bodyText ? '\n' : '') + '[media:' + url + ']' : '';
  const body = bodyText + token;
  return body.length <= maxLength ? body : null;
}
