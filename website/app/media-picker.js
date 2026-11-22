// Shared media-picker foundation. Provider adapters may implement search/trending later.
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
  if (!item || typeof item !== 'object') return null;
  const url = safeMediaUrl(item.url || item.src);
  if (!url) return null;
  const title = typeof item.title === 'string' ? item.title.slice(0, 120) : '';
  return { url, title };
}

export function openMediaPicker({ providers = [], onSelect, onClose } = {}) {
  if (typeof document === 'undefined') return null;
  const overlay = document.createElement('div');
  overlay.className = 'gb-media-overlay';
  overlay.setAttribute('role', 'dialog');
  overlay.setAttribute('aria-modal', 'true');
  overlay.setAttribute('aria-label', 'Choose media');
  const providerOptions = providers.filter(p => p && typeof p.search === 'function');
  overlay.innerHTML = '<section class="gb-media-picker"><header><strong>Add media</strong><button type="button" data-close aria-label="Close">×</button></header>' +
    '<p>Paste an HTTPS link to a GIF or image. Search providers can be added when their APIs are configured.</p>' +
    '<form><label>Image or GIF URL<input type="url" name="url" placeholder="https://…/image.gif" autocomplete="url" required></label>' +
    '<button class="btn blue" type="submit">Preview</button></form><div class="gb-media-results" aria-live="polite"></div>' +
    '<footer><button type="button" data-cancel>Cancel</button></footer></section>';
  const results = overlay.querySelector('.gb-media-results');
  const form = overlay.querySelector('form');
  const close = () => { overlay.remove(); onClose?.(); };
  overlay.addEventListener('click', e => { if (e.target === overlay || e.target.closest('[data-close], [data-cancel]')) close(); });
  overlay.addEventListener('keydown', e => { if (e.key === 'Escape') close(); });
  form.addEventListener('submit', e => {
    e.preventDefault();
    const url = safeMediaUrl(form.elements.url.value);
    results.replaceChildren();
    if (!url) { results.textContent = 'Use a valid HTTPS URL ending in .gif, .png, .jpg, .jpeg, or .webp.'; return; }
    const img = document.createElement('img'); img.src = url; img.alt = 'Media preview'; img.loading = 'lazy'; img.referrerPolicy = 'no-referrer';
    const select = document.createElement('button'); select.type = 'button'; select.textContent = 'Attach this media';
    select.addEventListener('click', () => { onSelect?.({ url, title: '' }); close(); });
    results.append(img, select);
  });
  document.body.append(overlay);
  form.elements.url.focus();
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
