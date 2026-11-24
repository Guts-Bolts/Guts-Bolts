// What chats and forum posts show inside their text (app.js richText and chatdock.js use it):
//   [media:https://static.klipy.com/...gif]  -> the GIF or sticker itself (media-picker.js)
//   a gutsandbolts.net link                   -> a clickable link, plus a card for a game,
//                                               item, player or forum thread it points at
// Everything else stays plain, escaped text: nobody's message can become HTML.
import { safeMediaUrl } from './media-picker.js';

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const MEDIA = /\[media:(https:\/\/[^\s\]]+)\]/gi;
// The only links the text filter lets through are our own site's (worker/textfilter.js).
const SITE_LINK = /(?:https?:\/\/)?(?:www\.)?gutsandbolts\.net(?:\/[^\s<>"']*)?/gi;
// "#/game/12", "#/item/40", "#/user/7", "#/forum/t/f1a2b3" -> a card for it.
const CARD = /#\/(game|item|user|forum\/t)\/([A-Za-z0-9_-]{1,40})/;
const MAX_CARDS = 3;

// A link to our site, as a link that stays on whatever copy of the site you're on.
function siteHref(link) {
  const hash = link.indexOf('#');
  if (hash >= 0) return link.slice(hash);
  const path = link.replace(/^(?:https?:\/\/)?(?:www\.)?gutsandbolts\.net/i, '') || '/';
  return path.startsWith('/') ? path : '/' + path;
}

// Plain text (no code blocks) -> HTML. opts.media is the picture's class name.
// cards (an array) collects the links worth a card, for embedCards() under the message.
export function renderInline(text, opts = {}, cards = null) {
  const source = String(text ?? '');
  const lines = (s) => esc(s).replace(/\r\n?/g, '\n').replace(/\n/g, '<br>');
  const links = (s) => {
    let out = '', at = 0, m;
    SITE_LINK.lastIndex = 0;
    while ((m = SITE_LINK.exec(s))) {
      let link = m[0];
      const trail = /[.,!?)\]]+$/.exec(link);   // "see gutsandbolts.net/#/game/1." -> leave the dot out
      if (trail) link = link.slice(0, -trail[0].length);
      out += lines(s.slice(at, m.index));
      const href = siteHref(link);
      out += '<a class="gb-link" href="' + esc(href) + '">' + esc(link.replace(/^https?:\/\//i, '')) + '</a>';
      const card = CARD.exec(href);
      if (cards && card && cards.length < MAX_CARDS && !cards.some((c) => c.kind === card[1] && c.id === card[2])) cards.push({ kind: card[1], id: card[2] });
      at = m.index + link.length;
      SITE_LINK.lastIndex = at;
    }
    return out + lines(s.slice(at));
  };
  let out = '', at = 0, m;
  MEDIA.lastIndex = 0;
  while ((m = MEDIA.exec(source))) {
    out += links(source.slice(at, m.index));
    const url = safeMediaUrl(m[1]);
    out += url ? '<img class="' + esc(opts.media || 'gb-media') + '" src="' + esc(url) + '" alt="GIF" loading="lazy" referrerpolicy="no-referrer">' : esc(m[0]);
    at = MEDIA.lastIndex;
  }
  return out + links(source.slice(at));
}

// The cards for the links renderInline found: empty boxes that fillEmbeds() fills in.
export function embedCards(cards) {
  if (!cards || !cards.length) return '';
  return '<div class="gb-embeds">' + cards.map((c) => '<a class="gb-embed loading" data-embed="' + esc(c.kind + ':' + c.id) + '" href="#/' + esc(c.kind) + '/' + esc(c.id) + '">' +
    '<span class="gb-embed-pic"></span><span class="gb-embed-copy"><small>' + esc({ game: 'Game', item: 'Item', user: 'Player', 'forum/t': 'Forum thread' }[c.kind]) + '</small><b>Loading…</b></span></a>').join('') + '</div>';
}

// Fills the cards inside root, using call(op, args) (the page's server call). Answers are
// kept for a few minutes, so a chat redrawing doesn't ask again.
const cache = new Map();
async function lookUp(call, kind, id) {
  const key = kind + ':' + id, hit = cache.get(key);
  if (hit && Date.now() - hit.at < 300000) return hit.value;
  const value = (async () => {
    if (kind === 'game' || kind === 'item') {
      const r = await call('asset.info', { id });
      if (!r.ok) return null;
      const a = r.asset;
      return { title: a.name, sub: (a.kind === 'game' ? 'Game' : (a.kind || 'Item')) + (a.creatorName ? ' by ' + a.creatorName : ''),
        pic: a.icon && a.kind === 'game' ? '/icon/' + encodeURIComponent(a.id) + '?v=' + a.icon : a.thumb ? '/thumb/' + encodeURIComponent(a.id) + '?v=' + a.thumb : '' };
    }
    if (kind === 'user') {
      const r = await call('profile', { id });
      if (!r.ok) return null;
      const u = r.user || {};
      return { title: u.name || 'Player', sub: 'Player #' + (u.userId || id) + (u.status ? ' · ' + String(u.status).slice(0, 60) : ''), pic: '', letter: (u.name || '?').slice(0, 1) };
    }
    if (kind === 'forum/t') {
      const r = await call('forum.thread', { thread: id, page: 0, peek: true });
      if (!r.ok) return null;
      const t = r.thread;
      return { title: t.title, sub: t.boardName + ' · ' + t.replies + (t.replies === 1 ? ' comment' : ' comments') + ' · ' + (t.score || 0) + ' points', pic: t.image || '', letter: '💬' };
    }
    return null;
  })().catch(() => null);
  cache.set(key, { at: Date.now(), value });
  return value;
}
export function fillEmbeds(root, call) {
  if (!root || !call) return;
  root.querySelectorAll('.gb-embed.loading').forEach(async (el) => {
    el.classList.remove('loading');
    const [kind, id] = [el.dataset.embed.slice(0, el.dataset.embed.lastIndexOf(':')), el.dataset.embed.slice(el.dataset.embed.lastIndexOf(':') + 1)];
    const info = await lookUp(call, kind, id);
    const pic = el.querySelector('.gb-embed-pic'), copy = el.querySelector('.gb-embed-copy b');
    if (!info) { copy.textContent = 'Not available'; el.classList.add('gone'); return; }
    copy.textContent = info.title;
    const sub = document.createElement('span'); sub.textContent = info.sub; el.querySelector('.gb-embed-copy').append(sub);
    if (info.pic) { const img = document.createElement('img'); img.src = info.pic; img.alt = ''; img.loading = 'lazy'; img.referrerPolicy = 'no-referrer'; pic.append(img); }
    else pic.textContent = info.letter || '';
  });
}

// A message's text for a one-line preview (the chat list): pictures become "GIF".
export function previewText(text) {
  return String(text ?? '').replace(MEDIA, '[GIF]').replace(/```[\s\S]*?```/g, '[code]').replace(/\s+/g, ' ').trim();
}
