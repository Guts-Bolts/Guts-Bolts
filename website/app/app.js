// The Guts&Bolts web app: the Player app's site pages (games, catalog,
// create, friends, people, groups, Bolts, sign up / log in) in a browser.
// Pages are picked by the address after '#', like #/games or #/user/12.
import * as gb from './gb.js';
import { mountAvatar, avatarPicture, itemPicture, itemCamera } from './avatar3d.js';
import { startTranslating, languagePicker } from '../lang/i18n.js';

// --- small helpers ------------------------------------------------------------

class Raw { constructor(s) { this.s = s; } }
const raw = (s) => new Raw(s);
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
function put(v) {
  if (v instanceof Raw) return v.s;
  if (Array.isArray(v)) return v.map(put).join('');
  if (v === null || v === undefined || v === false) return '';
  return esc(v);
}
// html`...` escapes everything put into it, so names people type can't become code.
function html(strings, ...vals) {
  let out = '';
  strings.forEach((s, i) => { out += s; if (i < vals.length) out += put(vals[i]); });
  return raw(out);
}

const $ = (sel, root = document) => root.querySelector(sel);

function show(content) {
  view.innerHTML = content.s;
  view.querySelectorAll('.lang-slot:empty').forEach((slot) => slot.append(languagePicker()));
  setTimeout(() => upgradeItemPictures(), 0);   // (item renders on every page)
}
const view = $('#view');
const KINDS = { hat: 'Hat', shirt: 'Shirt', pants: 'Pants', audio: 'Audio', plugin: 'Plugin', game: 'Game', decal: 'Decal', model: 'Model',
  hair: 'Hair', faceacc: 'Face Accessory', neck: 'Neck Accessory', shoulder: 'Shoulder Accessory', waist: 'Waist Accessory', face: 'Face',
  tshirt: 'T-Shirt', gear: 'Gear', animation: 'Animation', emote: 'Emote', animpack: 'Animation Pack', bundle: 'Bundle', bodypart: 'Body Part' };
// Things you wear on the body, made in Studio's Accessory window (old-style hats are just a shape).
const ACCESSORIES = ['hat', 'hair', 'faceacc', 'neck', 'shoulder', 'waist'];
// Bundles: a pack of items sold together (you get them all), maybe with a body change: meta.headless hides the head.
const WEARABLE = ['shirt', 'pants', 'tshirt', 'face', ...ACCESSORIES, 'bundle', 'bodypart'];
const isHeadless = (items) => (items || []).some((it) => it && it.kind === 'bundle' && it.meta && it.meta.headless);
// An asset's ID, the way people see and type it: its number (like Roblox's), or the old ID for anything that hasn't got one.
const assetNum = (a) => String(a.num || a.id);
// Emotes and animation packs: moves for your character, equipped on the Avatar page (8 emote slots, 1 pack).
const isEmoteItem = (k) => k === 'emote' || k === 'animpack';
const kEmoteSlots = 8;
const isCatalogItem = (k) => WEARABLE.includes(k) || k === 'gear' || isEmoteItem(k);   // sold in the catalog (they have #/item pages)
// Only Guts' own accessories and faces can be Limited.
const canBeLimited = (a) => ACCESSORIES.includes(a.kind) || a.kind === 'face';
// Items with a real picture (Studio accessories, faces) show that instead of a drawing.
const FEES = { decal: 5, hat: 10, shirt: 10, pants: 10, audio: 20, plugin: 20, game: 0, hair: 10, faceacc: 10, neck: 10, shoulder: 10, waist: 10, face: 0, tshirt: 10 };

let me = null;          // our account on the server (from "hello")
let pageToken = 0;      // goes up with every page change; a slower old page must not draw over a newer one
class Stale extends Error {}
let serverName = '';
let route = [];

function toast(text, ms = 3500) {
  const t = $('#toast');
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { t.hidden = true; }, ms);
}

function ago(t) {
  const d = Math.floor(Date.now() / 1000) - t;
  if (d < 120) return 'just now';
  if (d < 7200) return Math.floor(d / 60) + ' minutes ago';
  if (d < 172800) return Math.floor(d / 3600) + ' hours ago';
  return Math.floor(d / 86400) + ' days ago';
}

const boltIcon = raw('<svg class="bolt" viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="7" fill="#f2b705" stroke="#a87400"/><path d="M5 7h6M5 9h6" stroke="#7a5300" stroke-width="1.4"/></svg>');
const checkIcon = raw('<svg class="check" viewBox="0 0 16 16" aria-label="Verified"><circle cx="8" cy="8" r="7" fill="#1d9bf0"/><path d="M4.5 8.2l2.3 2.3 4.7-4.9" stroke="#fff" stroke-width="1.8" fill="none"/></svg>');
const bolts = (n) => html`<span class="price">${boltIcon}${Number(n).toLocaleString()}</span>`;
const verified = (v) => (v ? checkIcon : '');
// The Partner badge next to a name: friends of Guts&Bolts (like the people who make Lyreto). Staff give it out.
const partner = (u) => (u && u.partner ? html` <span class="badge-pill partner-pill" title="Official Guts&amp;Bolts partner">&#129309; Partner</span>` : '');
const signedIn = () => me && me.userId > 0;

// A game's picture: a colourful gradient made from its id (like the Player).
function gameColors(id) {
  let h = 2166136261;
  for (const c of String(id)) h = Math.imul(h ^ c.charCodeAt(0), 16777619) >>> 0;
  const col = (shift, base) => base + ((h >>> shift) & 0x5f);
  return `linear-gradient(rgb(${col(0, 40)},${col(8, 60)},${col(16, 110)}), rgb(${col(4, 20)},${col(12, 30)},${col(20, 60)}))`;
}

// Hats, shirts and pants, drawn in their colour.
// An item's picture: never a drawing. Things you wear are a real 3D render of a grey
// mannequin wearing them (upgradeItemPictures fills these in); gear, decals and the rest
// show the picture Studio rendered when they were published.
// Player badges (earned by doing things), each a coloured shield with a white drawing.
const PLAYER_BADGE_LOOK = {
  creator: ['#e8590c', '<rect x="9" y="17" width="14" height="7" rx="1"/><circle cx="12" cy="17" r="1.6"/><circle cx="20" cy="17" r="1.6"/><path d="M19 8 l6 6 -2 2 -6 -6z"/><rect x="11" y="11" width="9" height="2.4" transform="rotate(-45 15 12)"/>'],
  builder: ['#1d6fd8', '<rect x="7" y="14" width="18" height="9" rx="1"/><circle cx="12" cy="14" r="2"/><circle cx="20" cy="14" r="2"/>'],
  architect: ['#6b2fb3', '<path d="M16 6 L26 15 H23 V25 H9 V15 H6 Z"/><rect x="14" y="18" width="4" height="7" fill="#6b2fb3"/>'],
  friendly: ['#16a34a', '<circle cx="12" cy="12" r="3.5"/><circle cx="20" cy="12" r="3.5"/><path d="M5 25 q7-10 14 0z"/><path d="M13 25 q7-10 14 0z"/>'],
  collector: ['#b07800', '<path d="M16 6 l3 6.5 7 .8 -5.2 4.8 1.4 7 -6.2-3.5 -6.2 3.5 1.4-7 -5.2-4.8 7-.8z"/>'],
  oldtimer: ['#5b6472', '<circle cx="16" cy="16" r="9" fill="none" stroke="#fff" stroke-width="2.5"/><path d="M16 10 V16 L20 19" fill="none" stroke="#fff" stroke-width="2.5" stroke-linecap="round"/>'],
};
function playerBadgeIcon(key, size = 48) {
  const [col, art] = PLAYER_BADGE_LOOK[key] || ['#888', ''];
  return raw(`<svg viewBox="0 0 32 36" width="${size}" height="${Math.round(size * 1.125)}" aria-hidden="true">
    <path d="M16 1 L30 6 V18 C30 27 23 32 16 35 C9 32 2 27 2 18 V6 Z" fill="${col}" stroke="rgba(0,0,0,.25)"/>
    <path d="M16 3 L28 7.5 V12 C20 14 12 10 4 12 V7.5 Z" fill="rgba(255,255,255,.18)"/><g fill="#fff">${art}</g></svg>`);
}

function itemIcon(a) {
  // Catalog items are always drawn from the item itself, never from an uploaded picture.
  if (drawable3d(a) || a.kind === 'gear') {
    items3d.set(a.id, a);
    return html`<span class="item-render loading" data-item3d="${a.id}" aria-label="${a.name}"></span>`;
  }
  if (a.thumb) return html`<img class="item-thumb" data-thumb="${a.id}" data-v="${a.thumb}" alt="${a.name}">`;
  return html`<span class="item-render no-picture">${KINDS[a.kind] || ''}</span>`;
}

// A game's picture (set when it's published from Studio), or a colourful card.
function gamePic(g, cls = 'pic') {
  if (g.thumb) return html`<div class="${cls}" style="background:#223 center / cover no-repeat url('/thumb/${encodeURIComponent(g.id)}?v=${g.thumb}')"
    role="img" aria-label="${g.name}"></div>`;
  return html`<div class="${cls}" style="background:${raw(gameColors(g.id))}">${g.name}</div>`;
}

// A game's icon (a small square picture), or its first letter on its colours.
function gameIcon(g, size = 48) {
  if (g.icon) return html`<img class="game-icon" src="/icon/${encodeURIComponent(g.id)}?v=${g.icon}" alt="" width="${size}" height="${size}">`;
  return html`<span class="game-icon" style="width:${size}px;height:${size}px;background:${raw(gameColors(g.id))}">${(g.name || '?').slice(0, 1)}</span>`;
}
const ACCESS_NAMES = { public: 'Public', friends: 'Friends only', private: 'Private' };

// Shrink a picture the visitor picked to w x h (cropping to fit), as JPEG base64.
function pictureBase64(file, w, h) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => {
      const c = document.createElement('canvas');
      c.width = w; c.height = h;
      const k = Math.max(w / img.width, h / img.height);
      const dw = img.width * k, dh = img.height * k;
      const g = c.getContext('2d');
      g.fillStyle = '#fff'; g.fillRect(0, 0, w, h);
      g.drawImage(img, (w - dw) / 2, (h - dh) / 2, dw, dh);
      URL.revokeObjectURL(img.src);
      resolve(c.toDataURL('image/jpeg', 0.88).split(',')[1]);
    };
    img.onerror = () => reject(new Error('That file isn\'t a picture.'));
    img.src = URL.createObjectURL(file);
  });
}

// --- avatars ---
// The same colours and hats as the Player (Player::colorPresets, HatStyle).
const PRESETS = [
  ['Classic Noob', [[245, 205, 48], [13, 105, 172], [245, 205, 48], [245, 205, 48], [75, 151, 75], [75, 151, 75]]],
  ['Guest', [[163, 163, 168], [31, 31, 36], [163, 163, 168], [163, 163, 168], [217, 217, 222], [217, 217, 222]]],
  ['Builder', [[245, 204, 166], [237, 140, 41], [245, 204, 166], [245, 204, 166], [89, 69, 51], [89, 69, 51]]],
  ['Ninja', [[31, 31, 36], [31, 31, 36], [31, 31, 36], [31, 31, 36], [31, 31, 36], [31, 31, 36]]],
  ['Red Team', [[245, 204, 166], [204, 46, 41], [245, 204, 166], [245, 204, 166], [31, 31, 36], [31, 31, 36]]],
  ['Blue Team', [[245, 204, 166], [41, 92, 217], [245, 204, 166], [245, 204, 166], [31, 31, 36], [31, 31, 36]]],
  ['Robot', [[184, 189, 199], [115, 122, 135], [184, 189, 199], [184, 189, 199], [115, 122, 135], [115, 122, 135]]],
];
const PARTS = ['head', 'torso', 'leftArm', 'rightArm', 'leftLeg', 'rightLeg'];
const PART_NAMES = { head: 'Head', torso: 'Torso', leftArm: 'Left Arm', rightArm: 'Right Arm', leftLeg: 'Left Leg', rightLeg: 'Right Leg' };
const HAT_COLORS = { 1: [30, 30, 36], 2: [204, 46, 41], 3: [240, 190, 40], 4: [219, 51, 158] };   // a hat's normal colours

function defaultAvatar() {
  const a = { hat: 0, hatColor: [-1, -1, -1], wearing: [] };
  PARTS.forEach((p, i) => { a[p] = PRESETS[0][1][i].slice(); });
  return a;
}
const rgbCss = (c) => `rgb(${c.map((x) => Math.max(0, Math.min(255, Number(x) | 0))).join(',')})`;
const hexOf = (c) => '#' + c.map((x) => (Math.max(0, Math.min(255, x | 0))).toString(16).padStart(2, '0')).join('');
const fromHex = (h) => [1, 3, 5].map((i) => parseInt(h.substr(i, 2), 16));

// A blocky character, front view. Clothes from the catalog colour the parts they cover.
// `head`: just the head and hat, square (the round faces on server cards).
function avatarSvg(av, size = 160, items = [], head = false) {
  const a = Object.assign(defaultAvatar(), av || {});
  const col = {};
  PARTS.forEach((p) => { col[p] = rgbCss(Array.isArray(a[p]) ? a[p] : defaultAvatar()[p]); });
  let hat = a.hat | 0, hatCol = Array.isArray(a.hatColor) && a.hatColor[0] >= 0 ? rgbCss(a.hatColor) : rgbCss(HAT_COLORS[hat] || [0, 0, 0]);
  for (const it of items) {
    const c = it.meta && Array.isArray(it.meta.color) ? rgbCss(it.meta.color) : null;
    if (!c) continue;
    if (it.kind === 'shirt') { col.torso = c; col.leftArm = c; col.rightArm = c; }
    if (it.kind === 'pants') { col.leftLeg = c; col.rightLeg = c; }
    if (it.kind === 'hat') { hat = Number(it.meta.style) || 2; hatCol = c; }
  }
  const hats = {
    1: `<rect x="37" y="-6" width="26" height="22" rx="2" fill="${hatCol}"/><rect x="30" y="13" width="40" height="5" rx="2" fill="${hatCol}"/><rect x="37" y="9" width="26" height="4" fill="rgba(0,0,0,.25)"/>`,
    2: `<path d="M34 17 Q35 3 50 3 Q65 3 66 17 Z" fill="${hatCol}"/><path d="M50 15 L76 15 Q76 19 70 19 L50 19 Z" fill="${hatCol}"/>`,
    3: `<path d="M35 16 L36 2 L43 9 L50 0 L57 9 L64 2 L65 16 Z" fill="${hatCol}"/><rect x="35" y="13" width="30" height="4" fill="rgba(0,0,0,.2)"/>`,
    4: `<path d="M35 20 Q35 9 50 9 Q65 9 65 20 Q58 14 50 15 Q42 14 35 20 Z" fill="${hatCol}"/><circle cx="50" cy="6" r="6" fill="${hatCol}"/>`,
  };
  const box = head ? '28 -6 44 44' : '0 -8 100 128', tall = head ? size : size * 1.28;
  const headPart = isHeadless(items) ? '' : `<rect x="37" y="12" width="26" height="22" rx="4" fill="${col.head}" stroke="rgba(0,0,0,.25)"/>
    <ellipse cx="47.5" cy="19.5" rx="1.3" ry="2.4" fill="#111"/><ellipse cx="52.5" cy="19.5" rx="1.3" ry="2.4" fill="#111"/>
    <path d="M44.5 24.5 Q46 30.5 50 30.5 Q54 30.5 55.5 24.5" stroke="#111" stroke-width="1.8" fill="none" stroke-linecap="round"/>`;
  const svg = `<svg viewBox="${box}" width="${size}" height="${tall}" role="img" aria-label="Avatar">
    ${headPart}
    <rect x="30" y="35" width="40" height="38" fill="${col.torso}" stroke="rgba(0,0,0,.25)"/>
    <rect x="12" y="35" width="17" height="38" fill="${col.rightArm}" stroke="rgba(0,0,0,.25)"/>
    <rect x="71" y="35" width="17" height="38" fill="${col.leftArm}" stroke="rgba(0,0,0,.25)"/>
    <rect x="30" y="74" width="19.5" height="40" fill="${col.rightLeg}" stroke="rgba(0,0,0,.25)"/>
    <rect x="50.5" y="74" width="19.5" height="40" fill="${col.leftLeg}" stroke="rgba(0,0,0,.25)"/>
    ${hats[hat] || ''}</svg>`;
  return raw(svg);
}

// How many people are in each game's servers right now (game id -> players), for the cards.
let playingNow = {};
async function loadPlaying() {
  const r = await gb.call('servers.list', {});
  playingNow = {};
  if (r.ok) for (const s of r.servers) playingNow[s.game] = (playingNow[s.game] || 0) + (s.players || 0);
}

// Genres a game can pick (the server has the same list).
const GENRES = ['Adventure', 'Obby', 'Fighting', 'Horror', 'Roleplay', 'Simulator', 'Tycoon', 'Racing', 'Sports',
  'Shooter', 'Puzzle', 'Survival', 'Comedy', 'Building', 'Sandbox', 'Showcase', 'Town and City', 'Destruction'];
// "87%" liked, or '' before anyone has voted.
const likedPercent = (g) => (g.likes || 0) + (g.dislikes || 0) ? Math.round(100 * g.likes / (g.likes + g.dislikes)) + '%' : '';
// Library models' "Best rated": the Wilson score (the lowest the real like rate is likely to be,
// 95% sure), so one vote at 100% doesn't beat 500 at 95%. The server sorts the same way.
function wilsonScore(up, down) {
  const n = up + down;
  if (!n) return 0;
  const z = 1.96, p = up / n;
  return (p + z * z / (2 * n) - z * Math.sqrt((p * (1 - p) + z * z / (4 * n)) / n)) / (1 + z * z / n);
}
const voteCount = (a) => (a.likes || 0) + (a.dislikes || 0);
// "92% liked · 25 votes" (or "No ratings yet") under a Library model.
const ratingLine = (a) => voteCount(a)
  ? html`<span class="rating" title="${a.likes || 0} thumbs up, ${a.dislikes || 0} thumbs down">&#128077; ${likedPercent(a)} <span class="muted">· ${voteCount(a).toLocaleString()} vote${voteCount(a) === 1 ? '' : 's'}</span></span>`
  : html`<span class="rating muted">No ratings yet</span>`;

function gameCard(g) {
  const n = playingNow[g.id] || g.playing || 0, liked = likedPercent(g);
  return html`<a class="card" href="#/game/${g.id}">
    ${gamePic(g)}
    <div class="name">${g.featured ? html`<span title="Featured by staff">&#11088;</span> ` : ''}${g.name}</div>
    <div class="by">by ${g.group ? g.group.name : html`${g.creatorName}${verified(g.creatorVerified)}`}</div>
    <div class="by">${liked ? html`<span class="liked" title="${g.likes} likes, ${g.dislikes} dislikes">&#128077; ${liked}</span> · ` : ''}${n ? html`<b class="playing">${n} playing</b>` : html`${g.plays || 0} visits`}</div>
    ${(g.genres || []).length ? html`<div class="by small">${g.genres.join(' · ')}</div>` : ''}</a>`;
}

// Catalog items in 3D: each one worn by a plain grey mannequin (the drawing is
// swapped in once WebGL has made the picture; itemIcon's flat drawing until then).
const MANNEQUIN = { head: [205, 207, 212], torso: [205, 207, 212], leftArm: [205, 207, 212], rightArm: [205, 207, 212],
  leftLeg: [190, 192, 198], rightLeg: [190, 192, 198], hat: 0, hatColor: [-1, -1, -1], wearing: [] };
const items3d = new Map();   // id -> item, for the pictures below
// Everything you wear: the 3D mannequin wears it (with its pictures and Studio-made
// shapes on, the way the game shows them).
const drawable3d = (a) => !!a && WEARABLE.includes(a.kind);   // all of it: clothes, faces, hats, hair and accessories

// Fills in <img data-thumb="id" data-v="when it changed"> with the item's picture. It loads from
// the picture's own address, which the browser keeps, so it's instant the next time. If that
// doesn't work (your own upload still waiting for a staff check, or a server on a computer),
// it asks the server for it instead.
function loadThumbs(root = view) {
  root.querySelectorAll('img[data-thumb]').forEach((img) => {
    if (img.src || img.dataset.loading) return;
    img.dataset.loading = '1';
    img.addEventListener('error', async () => {
      const t = await call('thumb.get', { id: img.dataset.thumb });
      if (t.ok && t.data) img.src = 'data:image/png;base64,' + t.data;
      else img.removeAttribute('src');
    }, { once: true });
    img.src = '/thumb/' + encodeURIComponent(img.dataset.thumb) + (img.dataset.v ? '?v=' + img.dataset.v : '');
  });
}

function upgradeItemPictures() {
  loadThumbs();
  view.querySelectorAll('[data-item3d]').forEach(async (el) => {
    if (el.dataset.rendering) return;
    el.dataset.rendering = '1';
    const it = items3d.get(el.dataset.item3d);
    if (!it || !(drawable3d(it) || it.kind === 'gear')) return;
    const url = await itemPicture(it, 150).catch(() => null);
    if (!el.isConnected) return;
    el.classList.remove('loading');
    if (url) el.innerHTML = html`<img class="item3d" src="${url}" alt="${it.name}">`.s;
    else { el.classList.add('no-picture'); el.textContent = KINDS[it.kind] || ''; }
  });
}

// The gold badge on things made by Guts&Bolts staff: official, safe to use (like Roblox's).
function officialBadge(a) {
  if (!a.creatorStaff) return '';
  return raw(`<span class="official-badge" title="Official: made by Guts&amp;Bolts staff, safe to use"><svg viewBox="0 0 24 26" width="24" height="26" aria-label="Official">
    <path d="M1 1 H23 V14 L12 25 L1 14 Z" fill="#f5ac26" stroke="#fff" stroke-width="2"/>
    <circle cx="8.5" cy="8" r="3" fill="#fff"/><path d="M16 4 L20 11 L12 11 Z" fill="#fff"/><rect x="10" y="12.5" width="5.5" height="5.5" fill="#fff"/></svg></span>`);
}

function itemCard(a) {
  items3d.set(a.id, a);
  return html`<a class="card square" href="#/item/${a.id}">
    <div class="pic-wrap"><div class="pic">${itemIcon(a)}</div>${officialBadge(a)}</div>${a.limited ? html`<span class="limited-tag">LIMITED</span>` : auctionOpen(a) ? html`<span class="timed-tag">AUCTION</span>` : a.exclusive ? html`<span class="timed-tag">EXCLUSIVE</span>` : a.offsaleAt ? html`<span class="timed-tag">${a.offsale ? 'OFF SALE' : 'TIMED'}</span>` : ''}
    <div class="name">${a.name}</div>
    <div class="by">${a.limited && a.limited.left <= 0 ? (a.limited.lowest ? html`from ${bolts(a.limited.lowest)}` : raw('<span class="muted">Sold out</span>'))
      : a.price > 0 ? bolts(a.price) : raw('<span class="muted">Free</span>')} · by ${a.creatorName}${verified(a.creatorVerified)}</div></a>`;
}

// A game pass's picture: the one its creator gave it, or a ticket.
const PASS_SVG = '<svg viewBox="0 0 64 64" width="100%" height="100%" aria-hidden="true"><rect x="6" y="16" width="52" height="32" rx="5" fill="#2f7fd1"/>'
  + '<circle cx="6" cy="32" r="6" fill="#fff"/><circle cx="58" cy="32" r="6" fill="#fff"/><path d="M22 16v32" stroke="#fff" stroke-width="2" stroke-dasharray="3 3"/>'
  + '<path d="M38 24l2.4 5 5.4.6-4 3.7 1.1 5.4-4.9-2.8-4.9 2.8 1.1-5.4-4-3.7 5.4-.6z" fill="#ffd34d"/></svg>';
function passPic(p) {
  return p.thumb ? html`<img class="lib-thumb" data-thumb="${p.id}" data-v="${p.thumb}" alt="">` : raw(PASS_SVG);
}
// A pass on a game's page: picture, name, price and Buy (or Owned).
function passCard(p) {
  return html`<div class="pass-card"><div class="pass-pic">${passPic(p)}</div>
    <b>${p.name}</b>${p.description ? html`<div class="small muted">${p.description}</div>` : ''}
    <div>${p.price > 0 ? bolts(p.price) : raw('<b>Free</b>')}</div>
    ${p.owned ? html`<span class="badge-pill">Owned</span>`
      : p.offsale ? html`<span class="muted small">Off sale</span>`
      : html`<button class="btn green small" data-act="buyPass" data-id="${p.id}" data-name="${p.name}" data-price="${p.price}">Buy</button>`}</div>`;
}

// A game badge: a coloured medal with a star (games' own badges, made by their creators).
function gameBadgeIcon(b, size = 56) {
  const c = rgbCss(Array.isArray(b.color) ? b.color : [240, 180, 40]);
  return raw(`<svg viewBox="0 0 60 60" width="${size}" height="${size}" aria-hidden="true">
    <circle cx="30" cy="30" r="27" fill="${c}" stroke="rgba(0,0,0,.3)" stroke-width="2"/>
    <circle cx="30" cy="30" r="20" fill="rgba(255,255,255,.18)"/>
    <path d="M30 15 L34.4 25.2 L45.5 26.2 L37.1 33.5 L39.6 44.4 L30 38.7 L20.4 44.4 L22.9 33.5 L14.5 26.2 L25.6 25.2 Z" fill="#fff" opacity=".9"/></svg>`);
}

// Why staff can ban someone (the server has the same list).
const BAN_REASONS = [
  ['sexual', 'Sexual content'], ['extremism', 'Violent extremism'], ['harassment', 'Harassment or bullying'],
  ['hate', 'Hate speech or discrimination'], ['threats', 'Threats of violence'], ['selfharm', 'Promoting self-harm'],
  ['scam', 'Scamming or phishing'], ['personal', 'Sharing personal information'], ['exploit', 'Cheating or exploiting'],
  ['spam', 'Spam'], ['impersonation', 'Impersonation'], ['inappropriate', 'Inappropriate content'],
  ['underage', 'Underage safety violation'], ['other', 'Breaking the rules'],
];
// What players pick from when they report something (the same keys, in plainer words).
const REPORT_REASONS = [
  ['harassment', 'Bullying or harassment'], ['hate', 'Hate speech'], ['threats', 'Threats'], ['scam', 'Scam or phishing'],
  ['personal', 'Sharing personal info'], ['sexual', 'Sexual content'], ['extremism', 'Violent extremism'],
  ['selfharm', 'Encouraging self-harm'], ['exploit', 'Cheating or exploiting'], ['spam', 'Spam'],
  ['impersonation', 'Pretending to be someone else'], ['inappropriate', 'Inappropriate content'],
  ['underage', 'Someone under 18'], ['other', 'Something else'],
];

// A classic white popup over a dark page. `body` is html``; returns the box.
function popup(body, cls = '') {
  document.querySelectorAll('.modal').forEach((m) => m.remove());
  const box = document.createElement('div');
  box.className = 'modal';
  box.innerHTML = html`<div class="popup ${cls}"><button class="popup-x" data-act="closeModal" aria-label="Close">&times;</button>${body}</div>`.s;
  box.addEventListener('click', (e) => { if (e.target === box) box.remove(); });
  document.body.appendChild(box);
  return box;
}

// The two guest looks (the app has the same): black clothes, and a black cap or a ponytail.
const GUEST_LOOKS = {
  boy: { head: [240, 240, 235], torso: [27, 27, 30], leftArm: [27, 27, 30], rightArm: [27, 27, 30], leftLeg: [27, 27, 30],
    rightLeg: [27, 27, 30], hat: 2, hatColor: [25, 25, 28], wearing: [] },
  girl: { head: [240, 240, 235], torso: [27, 27, 30], leftArm: [27, 27, 30], rightArm: [27, 27, 30], leftLeg: [27, 27, 30],
    rightLeg: [27, 27, 30], hat: 4, hatColor: [-1, -1, -1], wearing: [] },
};
function guestPicker(id, name) {
  const pick = (guest, label) => html`<a href="#" class="charpick-one" data-act="pickGuest" data-guest="${guest}" data-id="${id}" data-name="${name}">
      <span class="charpick-pic" data-guest-pic="${guest}">${avatarSvg(GUEST_LOOKS[guest], 120)}</span><b>${label}</b></a>`;
  popup(html`<h1 class="popup-title">Choose Your Character:</h1>
    <div class="charpick">${pick('boy', 'Play As Boy')}${pick('girl', 'Play As Girl')}</div>
    <p><a class="charpick-account" href="#/login">Have an Account?</a></p>`, 'wide');
  for (const g of ['boy', 'girl'])
    avatarPicture(GUEST_LOOKS[g], [], 150).then((url) => {
      const el = document.querySelector(`[data-guest-pic="${g}"]`);
      if (url && el) el.innerHTML = html`<img src="${url}" alt="" width="150" height="188">`.s;
    }).catch(() => {});
}
// Open the app on this game (if it's installed), with a way to get it if not.
function launchGame(id, name, guest, server = '') {
  const q = [guest ? 'guest=' + guest : '', server ? 'server=' + encodeURIComponent(server) : ''].filter(Boolean).join('&');
  const url = 'gutsandbolts://play/' + encodeURIComponent(id) + (q ? '?' + q : '');
  popup(html`<h1 class="popup-title">Starting Guts&amp;Bolts...</h1>
    <div class="launch-spin" aria-hidden="true"></div>
    <p>Opening <b>${name}</b> in the Guts&amp;Bolts app${guest ? ' as a guest' : ''}.</p>
    <p class="small muted">If your browser asks, choose <b>Open Guts&amp;Bolts</b>.</p>
    <p class="popup-buttons"><a class="btn green" href="${url}">Try again</a> <a class="btn" href="../#download">Download the app</a></p>
    <p class="small muted">Don't have it yet? Download it (Windows, Mac, Linux or Android), open it once, then press Play again.</p>`);
  location.href = url;
}

// Visitors can look at everything; doing things needs an account.
function loginPopup(what) {
  popup(html`<h1 class="popup-title">You need to log in</h1>
    <p>Log in or sign up (it's free!) to ${what}.</p>
    <p class="popup-buttons"><a class="btn green big" href="#/signup">Sign Up</a> <a class="btn blue big" href="#/login">Log In</a></p>
    <p><a href="#" data-act="closeModal">Not now</a></p>`);
}
// Which clicks and forms need an account, and what to say.
const NEEDS_ACCOUNT = {
  boltworksRegister: 'register for the Boltworks Club launch discount',
  buy: 'get items from the catalog', gift: 'send gifts', modelVote: 'rate models', buyPass: 'buy game passes', buyPrivate: 'buy private servers', daily: 'claim your daily Bolts', saveAvatar: 'save your avatar',
  friend: 'add friends', follow: 'follow people', group: 'join groups', redeem: 'redeem codes', upload: 'upload things and publish games',
  groupCreate: 'make a group', groupPost: 'post on group walls', groupShout: 'shout to a group',
  favorite: 'favourite games', itemFavorite: 'favourite items', outfitSave: 'save outfits', sendMessage: 'send messages', statusSet: 'post a status', blurbSet: 'edit your profile',
  report: 'report things to staff', block: 'block people', forumPost: 'post on the forum', forumReply: 'post on the forum',
};

// A small "Report" link for anything people can report (the server takes user, game, item, message and group).
const reportLink = (kind, id, name) => html`<a href="#" class="report-link small" data-act="report" data-kind="${kind}" data-id="${id}" data-name="${name}">Report</a>`;

function needSignIn(what) {
  return html`<div class="box info">You need to be signed in to ${what}.
    <a class="btn blue small" href="#/login">Log in</a> <a class="btn green small" href="#/signup">Sign up</a></div>`;
}

// --- talking to the server -------------------------------------------------------

async function call(op, args) {
  const r = await gb.call(op, args);
  if (r.me) setMe(r.me);
  return r;
}

// The same, for loading a page: if the visitor has moved on to another page by
// the time the answer comes, this page stops (so it can't draw over the new one).
async function pageCall(op, args) {
  const token = pageToken;
  const r = await call(op, args);
  if (token !== pageToken) throw new Stale();
  return r;
}

const BELL_SVG = '<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path fill="currentColor" d="M12 22a2.5 2.5 0 0 0 2.45-2h-4.9A2.5 2.5 0 0 0 12 22zm7-6V11a7 7 0 0 0-5.5-6.84V3.5a1.5 1.5 0 0 0-3 0v.66A7 7 0 0 0 5 11v5l-2 2v1h18v-1z"/></svg>';

function setMe(m) {
  me = m;
  const msgLink = $('#nav a[data-page=messages]');
  if (msgLink) {
    const n = signedIn() ? me.unreadMessages || 0 : 0;
    msgLink.innerHTML = html`Messages${n ? html` <span class="unread">${n}</span>` : ''}`.s;
  }
  const staffLink = $('#nav a[data-page=staff]');
  if (staffLink) staffLink.hidden = !(signedIn() && me.staff);
  const box = $('#me');
  if (signedIn()) {
    const notes = me.unreadNotes || 0;
    box.innerHTML = html`<a class="bell" href="#/notifications" title="Notifications" aria-label="Notifications">${raw(BELL_SVG)}${notes ? html`<span class="unread">${notes > 99 ? '99+' : notes}</span>` : ''}</a>
      Hi, <a href="#/user/${me.userId}">${me.username}</a>${verified(me.verified)}
      | <a href="#/bolts">${bolts(me.bolts)}</a> | <a href="#/settings">Settings</a> | <button class="btn small logout-button" type="button" data-act="logout">Log out</button>`.s;
  } else {
    box.innerHTML = html`<a href="#/signup">Sign Up</a> | <a href="#/login">Login</a>`.s;
  }
  showWarning();
}

// --- Moderation: bans and staff warnings ---------------------------------------------

const banned = () => !!(me && me.ban);
const fileSize = (n) => n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB';
const dateText = (t) => new Date(t * 1000).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' });

// A banned account sees only this (like Roblox's "Banned" page): why, the note from
// staff, when, and when it ends.
function banPage() {
  const b = me.ban;
  show(html`<div class="box ban-box">
      <h1>${b.until ? 'Banned for ' + Math.max(1, Math.round((b.until - b.at) / 86400)) + ' day' + (Math.round((b.until - b.at) / 86400) === 1 ? '' : 's') : 'Account Banned'}</h1>
      <p>Our moderators have found that your account broke the Guts&amp;Bolts rules.</p>
      <table class="ban-table">
        <tr><th>Reason</th><td><b>${b.title}</b></td></tr>
        ${b.note ? html`<tr><th>Note from staff</th><td>${b.note}</td></tr>` : ''}
        ${b.at ? html`<tr><th>Banned on</th><td>${dateText(b.at)}</td></tr>` : ''}
        <tr><th>Can play again</th><td>${b.until ? dateText(b.until) : 'Never: this ban is for good.'}</td></tr>
      </table>
      <p class="small muted">${b.until ? 'When the ban ends, just come back and log in again.' : ''}
        Please keep Guts&amp;Bolts a fun, safe place for everyone.</p>
      ${appealPart(b)}
      <p><button class="btn" data-act="logout">Log out</button></p></div>`);
}
// Think the ban is a mistake? One short note to staff per ban; their answer shows here.
function appealPart(b) {
  const a = b.appeal;
  const answer = a && a.status !== 'open' ? html`<div class="appeal-answer">
      <p><b>${a.status === 'denied' ? 'Staff looked at your appeal and kept the ban.' : 'Staff accepted your appeal.'}</b>
        <span class="small muted">${dateText(a.answeredAt)}</span></p>
      ${a.note ? html`<p>"${a.note}"</p>` : ''}</div>` : '';
  if (a && a.status === 'open') return html`<div class="appeal-box"><h2>Your appeal</h2>
      <p class="report-note">"${a.text}"</p>
      <p class="small muted">Sent ${dateText(a.at)}. Staff will read it and answer here.</p></div>`;
  if (!b.canAppeal) return answer;
  return html`<div class="appeal-box">${answer}<h2>${a ? 'Appeal again' : 'Think this is a mistake?'}</h2>
      <form class="form" data-form="appeal">
        <label>Tell staff why the ban should be lifted <span class="small muted">(up to 500 letters, once per ban)</span></label>
        <textarea name="text" maxlength="500" rows="4" required minlength="10"></textarea>
        <p><button class="btn blue">Send appeal</button> <span id="appealMsg"></span></p></form></div>`;
}

// A staff warning, shown once until you say you understand.
let warningShown = '';
function showWarning() {
  if (!signedIn() || banned() || !me.warnings || !me.warnings.length) return;
  const w = me.warnings[0];
  if (warningShown === w.id) return;
  warningShown = w.id;
  const box = popup(html`<div class="popup-title" style="color:#c0392b">Warning</div>
    <p>A moderator has warned your account for:</p>
    <p style="font-size:18px"><b>${w.title}</b></p>
    ${w.note ? html`<p>"${w.note}"</p>` : ''}
    <p class="small muted">${dateText(w.at)}. More breaks of the rules can get your account banned.</p>
    <div class="popup-buttons"><button class="btn blue" data-act="ackWarning" data-id="${w.id}">I understand</button></div>`, 'warning');
  box.querySelector('.popup-x').remove();   // no sneaking past it
  box.onclick = null;
}

// How many friend requests are waiting (shown on the Friends link).
async function checkRequests() {
  const link = $('#nav a[data-page=friends]');
  if (!link) return;
  let n = 0;
  if (signedIn()) {
    const r = await gb.call('friends.list', {});
    if (r.ok) n = r.incoming.length;
  }
  link.innerHTML = 'Friends' + (n ? ` <span class="badge">${n}</span>` : '');
}

// Once a minute: has anything happened to the account (a warning, a ban)?
async function keepFresh() {
  if (!signedIn()) return;
  const wasBanned = banned();
  const r = await gb.call('ping', {});
  if (r.ok && r.me) setMe(r.me);
  else if (!r.ok && /^This account has been banned/.test(r.error || '')) await hello();
  else if (!r.ok && r.error === 'This account was deleted.') { await hello(); render(); return; }
  if (banned() !== wasBanned) render();
}

async function hello() {
  let r = await gb.call('hello', { name: signedIn() ? me.username : '', grants: [], protocol: 1 });
  if (!r.ok && r.error === 'This account was deleted.') {   // (deleted on another device)
    await gb.forgetKey();
    r = await gb.call('hello', { name: '', grants: [], protocol: 1 });
  }
  const st = $('#status');
  if (r.ok) {
    serverName = (r.server && r.server.name) || 'Online';
    st.className = 'status on';
    st.querySelector('b').textContent = serverName;
    setMe(r.me);
  } else {
    st.className = 'status off';
    st.querySelector('b').textContent = 'Server offline';
    me = null;
    $('#me').innerHTML = html`<span class="muted">Offline</span>`.s;
  }
  return r;
}

// --- pages ---------------------------------------------------------------------------

const pages = {};

function auctionBanner(a) {
  const au = a.auction;
  if (!auctionOpen(a)) return '';
  items3d.set(a.id, a);
  const urgent = au.endsAt - Date.now() / 1000 <= 3600;
  return html`<a class="auction-home-banner${urgent ? ' urgent' : ''}" href="#/item/${a.id}" aria-label="Auction: ${a.name}">
    <div class="auction-home-ribbon"><span>AUCTION</span><b>${urgent ? 'ENDING SOON' : 'LIVE BIDDING'}</b></div>
    <div class="auction-home-picture"><div class="pic">${itemIcon(a)}</div></div>
    <div class="auction-home-copy"><span class="auction-home-kicker">THE CLOCK IS TICKING</span><h2>${a.name}</h2><p>${a.description || 'A limited-time chance to claim this exclusive item.'}</p>
      <div class="auction-home-bid"><span>CURRENT HIGH BID</span><strong>${bolts(au.bids.length ? Math.max(...au.bids.map((b) => b.amount)) : 0)}</strong></div></div>
    <div class="auction-home-clock"><span>TIME REMAINING</span><strong data-countdown="${au.endsAt}">${countdown(au.endsAt)}</strong><b class="auction-home-cta">BID NOW <span aria-hidden="true">&rarr;</span></b></div>
  </a>`;
}
pages.home = async () => {
  // Everything the page needs is asked for at once, so it waits for one round trip, not four.
  const [games, items, fr, recent, favs, featured, , feedR, upd, mine, auctions] = await Promise.all([
    pageCall('list', { kind: 'game', sort: 'popular', limit: 8 }),
    pageCall('list', { kind: 'clothing', limit: 6 }),
    signedIn() ? pageCall('friends.list', {}) : Promise.resolve({ ok: false }),
    signedIn() ? pageCall('games.mine', { which: 'recent', limit: 8 }) : Promise.resolve({ ok: false }),
    signedIn() ? pageCall('games.mine', { which: 'favorites', limit: 8 }) : Promise.resolve({ ok: false }),
    pageCall('list', { kind: 'game', sort: 'featured', limit: 8 }),
    loadPlaying(),
    signedIn() ? pageCall('feed.list', {}) : Promise.resolve({ ok: false }),
    pageCall('updates.list', { limit: 1 }),
    signedIn() ? pageCall('profile', { id: me.id }) : Promise.resolve({ ok: false }),   // (what you're wearing)
    pageCall('list', { kind: 'clothing', auctions: true, limit: 12 }),
  ]);
  // "Continue playing" and your favourites (only when there's something in them).
  const mineBox = (title, r, more) => r.ok && r.assets.length ? html`<div class="box"><h2 class="boxhead">${title}
      <a href="${more}" style="float:right">See all &raquo;</a></h2><div class="grid">${r.assets.map(gameCard)}</div></div>` : '';
  const continueBox = mineBox('Continue Playing', recent, '#/games/mine/recent');
  const feedBox = signedIn() ? html`<div class="box feed"><h2 class="boxhead">My Feed</h2>
      <form class="row" data-form="statusSet"><input type="text" name="status" maxlength="140" placeholder="Right now I'm..." style="flex:1">
        <button class="btn blue small">Share</button></form>
      ${feedR.ok && feedR.feed.length ? html`<div class="feed-list">${feedR.feed.map((p) => html`<div class="feed-item">
          <a href="#/user/${p.user.userId}" class="feed-pic" data-feed-avatar="${p.user.id}">${avatarSvg(p.user.avatar, 40)}</a>
          <div><a class="link" href="#/user/${p.user.userId}">${p.user.name}</a>${verified(p.user.verified)}
            <div>"${p.text}"</div><div class="small muted">${ago(p.at)}</div></div></div>`)}</div>`
        : html`<p class="muted small">Nothing yet. What your friends and the people you follow are up to shows up here.</p>`}</div>` : '';
  const favBox = mineBox('Favorites', favs, '#/games/mine/favorites');
  const gameBox = (title, r, more) => html`<div class="box"><h2 class="boxhead">${title}
      <a href="${more}" style="float:right">See more &raquo;</a></h2>
    ${r.ok && r.assets.length ? html`<div class="grid">${r.assets.map(gameCard)}</div>`
      : html`<p class="muted small">${r.ok ? 'No games published yet. Publish one from Studio!' : r.error}</p>`}</div>`;
  // Featured: games staff picked (only shown when there are some).
  const featuredBox = featured.ok && featured.assets.length ? html`<div class="box featured-box"><h2 class="boxhead">&#11088; Featured Games
      <a href="#/games?sort=featured" style="float:right">See all &raquo;</a></h2><div class="grid">${featured.assets.map(gameCard)}</div></div>` : '';
  const shopBox = html`<div class="box"><h2 class="boxhead">New in the Catalog
      <a href="#/catalog" style="float:right">See more &raquo;</a></h2>
    ${items.ok && items.assets.length ? html`<div class="grid">${items.assets.map(itemCard)}</div>`
      : html`<p class="muted small">Nothing here yet.</p>`}</div>`;
  const latest = upd.ok && upd.updates.length ? upd.updates[0] : null;
  const activeAuctions = auctions.ok ? auctions.assets.filter(auctionOpen).sort((a, b) => a.auction.endsAt - b.auction.endsAt) : [];
  const auctionBox = activeAuctions.length ? html`<section class="auction-home-section"><div class="auction-home-section-head"><h2>LIVE AUCTIONS</h2><a href="#/catalog?cat=auctions">VIEW ALL AUCTIONS &raquo;</a></div>${auctionBanner(activeAuctions[0])}${activeAuctions.length > 1 ? html`<div class="auction-home-more"><span>MORE LIVE AUCTIONS</span>${activeAuctions.slice(1, 4).map((a) => html`<a href="#/item/${a.id}"><span class="auction-mini-tag">AUCTION</span><b>${a.name}</b><span data-countdown="${a.auction.endsAt}">${countdown(a.auction.endsAt)}</span></a>`)}</div>` : ''}</section>` : '';
  const updateBox = latest ? html`<div class="box latest-update"><h2 class="boxhead">Latest update
      <a href="#/updates" style="float:right">All updates &raquo;</a></h2>
    <p><b>${latest.name}</b>${latest.version ? html` <span class="small muted">v${latest.version}</span>` : ''}
      <span class="small muted">&middot; ${ago(latest.time)}</span></p>
    ${latest.summary ? html`<p class="small">${latest.summary}</p>` : ''}</div>` : '';
  if (!signedIn()) {
    show(html`<div class="welcome">
        <div><h1>Welcome to Guts&amp;Bolts!</h1>
          <p>Build games, play them with your friends and fall apart spectacularly.
            Sign up to get <b>100 Bolts</b>, dress up your character, make friends and upload your own stuff.</p>
          <p><a class="btn green big" href="#/signup">Sign Up and Play</a>
            <a class="btn" href="#/login">Login</a></p></div>
        <div class="welcome-guy" id="homeAvatar">${avatarSvg(defaultAvatar(), 150)}</div></div>
      ${updateBox}${auctionBox}${featuredBox}${gameBox('Best of Guts&Bolts', games, '#/games')}${shopBox}`);
    tickCountdowns();
    mountAvatar($('#homeAvatar'), defaultAvatar(), [], { width: 170 }).catch(() => {});
    return;
  }
  const friends = fr.ok ? fr.friends || [] : [];
  const onlineFriends = friends.filter((p) => p.online);
  show(html`<div class="home">
      <div class="home-left">
        <div class="box avatar-box"><h2 class="boxhead">Hi, ${me.username}!</h2>
          <div id="homeAvatar">${avatarSvg(me.avatar || defaultAvatar(), 150)}</div>
          <p class="small"><a href="#/avatar">Change your avatar</a> | <a href="#/user/${me.userId}">Profile</a></p></div>
        <div class="box"><h2 class="boxhead">Your Bolts</h2>
          <div style="font-size:18px">${bolts(me.bolts)}</div>
          ${me.canDaily ? html`<p class="small">Your daily Bolts are ready!</p>
              <button class="btn green" data-act="daily">Claim 25 Bolts</button>`
            : html`<p class="small muted">Come back tomorrow for 25 more.</p>`}</div>
        <div class="box"><h2 class="boxhead">Friends online (${onlineFriends.length})</h2>
          ${onlineFriends.length ? html`<div class="list">${onlineFriends.slice(0, 8).map((p) => html`<div>
              <span class="dot on"></span><a class="grow" href="#/user/${p.userId || p.id}">${p.username || p.name}</a></div>`)}</div>`
            : html`<p class="small muted">${friends.length ? 'None of your friends are on right now.' : html`No friends yet. <a href="#/people">Find some!</a>`}</p>`}</div>
      </div>
      <div class="home-right">${updateBox}${auctionBox}${featuredBox}${feedBox}${continueBox}${favBox}${gameBox('Best of Guts&Bolts', games, '#/games')}${shopBox}</div>
    </div>`);
  tickCountdowns();
  // Dressed in what you're wearing (the server says which items those are).
  const worn = mine.ok ? mine.wearing || [] : [];
  if ($('#homeAvatar')) mountAvatar($('#homeAvatar'), me.avatar || defaultAvatar(), worn, { width: 170 }).catch(() => {});
  // My Feed: each person's character next to what they said.
  const seen = new Set();
  for (const p of feedR.ok ? feedR.feed : []) {
    if (seen.has(p.user.id)) continue;
    seen.add(p.user.id);
    avatarPicture(p.user.avatar, [], 40).then((url) => {
      if (url) view.querySelectorAll(`[data-feed-avatar="${p.user.id}"]`).forEach((el) => { el.innerHTML = html`<img src="${url}" alt="" width="40" height="50">`.s; });
    }).catch(() => {});
  }
};

// --- Updates: the update log, newest first. It checks for new ones by itself while
// you're looking, and the Updates link lights up when there's one you haven't seen.
const UPDATE_TAG_COLORS = { Engine: '#2a7de1', Studio: '#8a4fd1', Website: '#1f9d55', Player: '#b86e00', Server: '#4a5a70', Fix: '#d33c3c' };
// Gutstober: Guts&Bolts' Halloween month (October). The site dresses up for it (orange
// and purple, pumpkins and bats) unless you turn it off in Settings. Your choice is
// kept in this browser only.
function isGutstober(d = new Date()) { return d.getMonth() === 9; }
function gutstoberWanted() { try { return localStorage.getItem('gb.gutstober') !== 'off'; } catch { return true; } }
function applyTheme() {
  const on = isGutstober() && gutstoberWanted();
  document.documentElement.classList.toggle('gutstober', on);
  let banner = document.getElementById('gutstober');
  if (on && !banner) {
    banner = document.createElement('div');
    banner.id = 'gutstober';
    banner.className = 'gutstober-banner';
    banner.innerHTML = '<div class="wrap"><span class="gt-pumpkin" aria-hidden="true"></span>' +
      '<b>Happy Gutstober!</b> <span>Spooky games and Halloween items in the <a href="#/catalog">catalog</a> for a limited time only.</span>' +
      '<span class="gt-bats" aria-hidden="true"><i></i><i></i><i></i></span></div>';
    document.getElementById('nav').after(banner);
  } else if (!on && banner) banner.remove();
}
applyTheme();
function localInput(t) {   // seconds -> the value a datetime-local box wants (local time)
  const d = new Date(t * 1000);
  return new Date(d.getTime() - d.getTimezoneOffset() * 60000).toISOString().slice(0, 16);
}
function offsaleText(a) {   // timed items: "Off sale" or when they go off sale
  if (!a.offsaleAt) return '';
  if (a.offsale) return 'Off sale';
  const days = Math.ceil((a.offsaleAt - Date.now() / 1000) / 86400);
  return days <= 1 ? 'Off sale in less than a day' : 'Off sale in ' + days + ' days';
}

function seenUpdate() { try { return localStorage.getItem('gb.seenUpdate') || ''; } catch { return ''; } }
function markUpdateSeen(id) { try { localStorage.setItem('gb.seenUpdate', id); } catch { /* private window */ } }
function updateCard(u, fresh) {
  const color = UPDATE_TAG_COLORS[u.tag] || '#4a5a70';
  return html`<article class="box update${fresh ? ' fresh' : ''}" id="upd-${u.id}">
    <div class="update-head">
      <h2>${u.name}${fresh ? html` <span class="badge">NEW</span>` : ''}</h2>
      <span class="update-tag" style="background:${color}">${u.tag}</span>
    </div>
    <p class="small muted">${u.version ? html`Version ${u.version} &middot; ` : ''}${new Date(u.time * 1000).toLocaleDateString()} (${ago(u.time)})${u.by ? html` &middot; posted by ${u.by}` : ''}</p>
    ${u.summary ? html`<p class="update-summary">${u.summary}</p>` : ''}
    ${(u.items || []).length ? html`<ul class="update-items">${u.items.map((i) => html`<li>${i}</li>`)}</ul>` : ''}
    ${me && me.staff && u.id.startsWith('u-') ? html`<p><button class="btn small" data-act="deleteUpdate" data-id="${u.id}">Delete</button></p>` : ''}
  </article>`;
}
function setUpdatesLink(latest) {
  const link = $('#nav a[data-page=updates]');
  if (link) link.innerHTML = 'Updates' + (latest && latest !== seenUpdate() ? ' <span class="badge">NEW</span>' : '');
}
async function checkUpdates() {
  const r = await gb.call('updates.list', { limit: 1 });
  if (r.ok) setUpdatesLink(r.latest);
  return r;
}

// --- The forum, like the old Roblox forum: boards, then threads, then posts. ---
// #/forum (the boards), #/forum/help[/page] (a board's threads), #/forum/t/<thread>[/page] (one thread).
const forumPager = (base, page, pages) => pages <= 1 ? '' : html`<p class="pager">
  ${page > 0 ? html`<a class="btn small" href="${base}/${page}">&lt; Back</a>` : ''}
  <span class="muted small">Page ${page + 1} of ${pages}</span>
  ${page + 1 < pages ? html`<a class="btn small" href="${base}/${page + 2}">Next &gt;</a>` : ''}</p>`;
const forumWho = (u) => html`<a href="#/user/${u.userId || u.id}"><b>${u.name}</b></a>${verified(u.verified)}${u.staff ? html` <span class="badge-pill staff">Staff</span>` : ''}${partner(u)}`;
// Use the same rendered 2D PNG as the avatar page's 2D mode, with SVG as a fast fallback.
const forumAvatarUsers = new Map();
const forumAvatar = (u, size) => {
  const id = String(u && (u.userId || u.id) || '');
  if (id) forumAvatarUsers.set(id, u);
  return html`<span data-forum-avatar="${id}" data-size="${size}">${avatarSvg(u && u.avatar, size, u && Array.isArray(u.wearing) ? u.wearing : [], false)}</span>`;
};
function loadForumAvatars(root = view) {
  root.querySelectorAll('[data-forum-avatar]').forEach(async (el) => {
    if (el.dataset.loading) return;
    const key = el.dataset.forumAvatar;
    const listedUser = forumAvatarUsers.get(key);
    if (!listedUser) return;
    el.dataset.loading = '1';
    const size = Number(el.dataset.size) || 72;
    try {
      // Use the same profile lookup and worn-item resolution as the actual player page.
      // This is a background avatar request, so it must not be cancelled by pageToken.
      const ids = [...new Set([listedUser.userId, listedUser.id, key].filter((x) => x != null && String(x)))];
      let profile = null;
      let lastError = null;
      for (const id of ids) {
        try {
          const r = await call('profile', { id: String(id) });
          if (r.ok && r.user && r.user.avatar && typeof r.user.avatar === 'object') {
            profile = r;
            break;
          }
          lastError = new Error(r.error || 'Profile response did not include avatar data');
        } catch (err) { lastError = err; }
      }
      if (!profile) throw lastError || new Error('Could not load player avatar profile');
      const user = profile.user;
      let worn = profile.wearing;
      if (!worn) {
        const idsToResolve = user.avatar && Array.isArray(user.avatar.wearing) ? user.avatar.wearing : [];
        worn = idsToResolve.length
          ? ((await call('list', { kind: 'clothing', limit: 100 })).assets || []).filter((item) => idsToResolve.includes(item.id))
          : [];
      }
      const url = await avatarPicture(user.avatar, worn, size);
      if (!el.isConnected) return;
      if (url) {
        el.innerHTML = html`<img src="${url}" alt="${user.username || user.name || listedUser.name || 'Player'}" width="${size}" height="${Math.round(size * 1.28)}" style="display:block;object-fit:contain">`.s;
      } else {
        el.innerHTML = avatarSvg(user.avatar, size, worn, false).s;
      }
    } catch (err) {
      console.warn('Forum avatar render failed for player', key, err);
      // Keep the initial avatar placeholder rather than incorrectly presenting a default noob as their real avatar.
    }
  });
}

function forumSearchForm(boards, selected = 'all', query = '') {
  return html`<form class="forum-search form" data-form="forumSearch">
    <label for="forumSearchQuery">Search discussions</label>
    <div class="forum-search-row">
      <input id="forumSearchQuery" type="search" name="q" value="${query}" maxlength="100" placeholder="Thread titles and post text…" required>
      <select name="board" aria-label="Search board">
        <option value="all" ${selected === 'all' ? 'selected' : ''}>All boards</option>
        ${boards.map((b) => html`<option value="${b.id}" ${selected === b.id ? 'selected' : ''}>${b.name}</option>`)}
      </select><button class="btn blue">Search</button>
    </div>
  </form>`;
}
pages.forumsearch = async (query, board = 'all', page = '1') => {
  query = String(query || '').trim().slice(0, 100); board = board || 'all';
  const p = Math.max(0, (Number(page) || 1) - 1);
  const [br, r] = await Promise.all([pageCall('forum.boards', {}), pageCall('forum.search', { q: query, board, page: p })]);
  if (!br.ok || !r.ok) { show(html`<h1>Forum search</h1><p class="error">${!br.ok ? br.error : r.error}</p><p><a href="#/forum">Back to forums</a></p>`); return; }
  const base = '#/forumsearch/' + encodeURIComponent(query) + '/' + encodeURIComponent(board);
  show(html`<p class="forum-retro-breadcrumb"><a href="#/forum">Forum index</a> &rsaquo; Search</p>
    <div class="forum-retro-masthead"><div><span class="forum-retro-kicker">SEARCH THE ARCHIVES</span><h1>Forum Search</h1><p>Find old arguments, answers, and half-forgotten threads.</p></div></div>
    ${forumSearchForm(br.boards, board, query)}
    ${r.total ? html`<p class="small muted">Showing ${r.results.length} of ${r.total} matching discussions · Page ${r.page + 1} of ${r.pages}</p>
      <div class="forum-search-results">${r.results.map((x) => html`<article class="forum-search-result">
        <div class="forum-search-result-main"><span class="forum-retro-kicker">${x.boardName} · ${x.match === 'title' ? 'TITLE MATCH' : 'POST MATCH'}</span>
          <h2><a href="#/forum/t/${x.id}/${x.postPage + 1}">${x.title}</a></h2><p>${x.snippet}</p>
          <span class="small muted">Started by ${forumWho(x.by)} · ${x.replies} replies · last activity ${ago(x.last)}</span>
        </div><a class="forum-search-open" href="#/forum/t/${x.id}/${x.postPage + 1}">Open thread &raquo;</a>
      </article>`)}</div>
      ${r.pages > 1 ? html`<p class="pager">${r.page > 0 ? html`<a class="btn small" href="${base}/${r.page}">&lt; Back</a>` : ''}<span class="muted small">Page ${r.page + 1} of ${r.pages}</span>${r.page + 1 < r.pages ? html`<a class="btn small" href="${base}/${r.page + 2}">Next &gt;</a>` : ''}</p>` : ''}`
      : html`<div class="box forum-search-empty"><h2>No matching discussions</h2><p class="muted">Try a shorter phrase or search all boards.</p></div>`}`);
};
pages.forum = async (board, a, b) => {
  if (board === 't') { await forumThread(a, Math.max(0, (Number(b) || 1) - 1)); return; }
  if (!board) {
    const r = await pageCall('forum.boards', {});
    if (!r.ok) { show(html`<h1>Forum</h1><p class="error">${r.error}</p>`); return; }
    show(html`<div class="forum-retro-masthead"><div><span class="forum-retro-kicker">GUTS &amp; BOLTS COMMUNITY · EST. 2004</span><h1>Forum</h1><p>Games, questions, arguments, and whatever else is on your mind.</p></div><span class="forum-retro-stamp">STILL<br>POSTING</span></div><p class="muted forum-intro">Old-school boards, real player avatars. Be kind: the forum follows the same <a href="#/terms">rules</a> as chat.</p>
      ${forumSearchForm(r.boards)}
      <div class="table-wrap"><table class="forum">
        <thead><tr><th>Board</th><th class="num">Threads</th><th class="num">Posts</th><th>Last post</th></tr></thead>
        <tbody>${r.boards.map((x) => html`<tr>
          <td><a href="#/forum/${x.id}"><b>${x.name}</b></a><br><span class="small muted">${x.about}</span></td>
          <td class="num">${x.threads}</td><td class="num">${x.posts}</td>
          <td class="small">${x.last ? html`<a href="#/forum/t/${x.last.thread}/9999">${x.last.title}</a><br><span class="muted">${ago(x.last.at)} by ${x.last.by.name}</span>`
            : html`<span class="muted">Nothing yet</span>`}</td></tr>`)}</tbody></table></div>`);
    return;
  }
  const page = Math.max(0, (Number(a) || 1) - 1);
  const r = await pageCall('forum.list', { board, page });
  if (!r.ok) { show(html`<h1>Forum</h1><p class="error">${r.error}</p><p><a href="#/forum">&lt; All boards</a></p>`); return; }
  const base = '#/forum/' + r.board.id;
  show(html`<p class="forum-retro-breadcrumb"><a href="#/forum">Forum index</a> &rsaquo; ${r.board.name}</p><div class="forum-retro-masthead forum-retro-board-head"><div><span class="forum-retro-kicker">COMMUNITY BOARD</span><h1>${r.board.name}</h1><p>${r.board.about}</p></div><a class="btn small" href="#/forum">All boards</a></div>
    ${forumSearchForm((await pageCall('forum.boards', {})).boards || [], r.board.id)}
    ${r.threads.length ? html`<div class="table-wrap"><table class="forum">
      <thead><tr><th>Thread</th><th class="num">Replies</th><th class="num">Views</th><th>Last post</th></tr></thead>
      <tbody>${r.threads.map((t) => html`<tr class="${t.pinned ? 'pinned' : ''}">
        <td>${t.pinned ? html`<span class="badge-pill" title="Pinned by staff">Pinned</span> ` : ''}${t.locked ? html`<span class="badge-pill" title="Nobody can reply">Locked</span> ` : ''}
          <span class="forum-list-avatar">${forumAvatar(t.by, 40)}</span><span class="forum-list-thread"><a href="#/forum/t/${t.id}"><b>${t.title}</b></a><br><span class="small muted">by ${forumWho(t.by)}, ${ago(t.at)}</span></span></td>
        <td class="num">${t.replies}</td><td class="num">${t.views}</td>
        <td class="small"><a href="#/forum/t/${t.id}/9999">${ago(t.last)}</a><br><span class="muted">by ${t.lastBy.name}</span></td></tr>`)}</tbody></table></div>`
      : html`<p class="muted">No threads here yet. Be the first!</p>`}
    ${forumPager(base, r.page, r.pages)}
    <h2>New thread</h2>
    ${r.board.staffOnly && !r.canPost ? html`<p class="muted">Only Guts&amp;Bolts staff post here.</p>`
      : signedIn() ? html`<form class="form" data-form="forumPost"><input type="hidden" name="board" value="${r.board.id}">
        <label>Title</label><input type="text" name="title" maxlength="80" required>
        <label>Message</label><textarea name="text" maxlength="3000" rows="7" required></textarea>
        <p><button class="btn green">Post thread</button> <span class="small muted">Be kind, and don't share personal info.</span></p></form>`
      : html`<p class="muted"><a href="#/login">Log in</a> or <a href="#/signup">sign up</a> to post.</p>`}`);
  loadForumAvatars();
};
async function forumThread(id, page) {
  const r = await pageCall('forum.thread', { thread: id, page: page >= 9998 ? -1 : page });
  if (!r.ok) { show(html`<h1>Forum</h1><p class="error">${r.error}</p><p><a href="#/forum">&lt; All boards</a></p>`); return; }
  const t = r.thread, base = '#/forum/t/' + t.id;
  const post = (p) => html`<div class="forum-post" data-post="${p.id}">
    <div class="forum-author"><a class="forum-player-avatar" href="#/user/${p.by.userId || p.by.id}" title="View ${p.by.name}’s profile">${forumAvatar(p.by, 72)}</a>${forumWho(p.by)}<span class="forum-player-number">PLAYER #${p.by.userId || p.by.id}</span></div>
    <div class="grow"><p class="small muted">${ago(p.at)}</p>
      <div class="forum-text">${p.text}</div>
      <p class="small">${p.canDelete ? html`<a href="#" data-act="forumDelete" data-thread="${t.id}" data-post="${p.id}" data-first="${p.first ? 1 : ''}" data-page="${r.page}">${p.first ? 'Delete thread' : 'Delete'}</a> ` : ''}
        ${signedIn() && p.by.id !== me.id ? reportLink('forum', t.id + ':' + p.id, p.by.name) : ''}</p></div></div>`;
  show(html`<p class="forum-retro-breadcrumb"><a href="#/forum">Forum index</a> &rsaquo; <a href="#/forum/${t.board}">${t.boardName}</a></p>
    <div class="forum-retro-masthead forum-retro-thread-head"><div><span class="forum-retro-kicker">${t.pinned ? 'PINNED DISCUSSION' : 'COMMUNITY DISCUSSION'}${t.locked ? ' · LOCKED' : ''}</span><h1>${t.title}</h1><p>Posted in <a href="#/forum/${t.board}">${t.boardName}</a></p></div></div>
    ${r.canModerate ? html`<p class="row"><button class="btn small" data-act="forumMod" data-thread="${t.id}" data-pinned="${t.pinned ? '' : 1}" data-locked="${t.locked ? 1 : ''}" data-page="${r.page}">${t.pinned ? 'Unpin' : 'Pin'}</button>
      <button class="btn small" data-act="forumMod" data-thread="${t.id}" data-pinned="${t.pinned ? 1 : ''}" data-locked="${t.locked ? '' : 1}" data-page="${r.page}">${t.locked ? 'Unlock' : 'Lock'}</button></p>` : ''}
    ${forumPager(base, r.page, r.pages)}
    <div class="forum-posts">${r.posts.map(post)}</div>
    ${forumPager(base, r.page, r.pages)}
    ${r.canReply ? html`<h2>Reply</h2><form class="form" data-form="forumReply"><input type="hidden" name="thread" value="${t.id}">
        <textarea name="text" maxlength="3000" rows="5" required></textarea>
        <p><button class="btn blue">Post reply</button></p></form>`
      : t.locked ? html`<p class="muted">This thread is locked, so nobody can reply.</p>`
      : html`<p class="muted"><a href="#/login">Log in</a> or <a href="#/signup">sign up</a> to reply.</p>`}`);
  loadForumAvatars();
}

pages.updates = async () => {
  const r = await pageCall('updates.list', { limit: 100 });
  if (!r.ok) { show(html`<h1>Updates</h1><p class="error">${r.error}</p>`); return; }
  const before = seenUpdate();
  let known = new Set(r.updates.map((u) => u.id));
  const staffForm = me && me.staff ? html`<details class="box"><summary><b>Post an update</b> (staff)</summary>
      <form data-form="postUpdate" class="form">
        <label>Name <input type="text" name="name" maxlength="40" required placeholder="Give it a cool name, like Glass Lagoon"></label>
        <label>Version <input type="text" name="version" maxlength="12" placeholder="0.6.2"></label>
        <label>Kind <select name="tag">${['Engine', 'Studio', 'Website', 'Player', 'Server', 'Fix'].map((t) => html`<option>${t}</option>`)}</select></label>
        <label>Summary <input type="text" name="summary" maxlength="200" placeholder="One line about it"></label>
        <label>What changed (one per line) <textarea name="items" rows="5"></textarea></label>
        <p><button class="btn green">Post update</button></p>
      </form></details>` : '';
  // The newest one you hadn't seen yet (and anything newer) is marked NEW.
  const firstSeen = r.updates.findIndex((u) => u.id === before);
  const isFresh = (i) => before && (firstSeen < 0 ? false : i < firstSeen);
  show(html`<h1>Updates</h1>
    <p class="muted">What's new in Guts&amp;Bolts, newest first. This page checks for new updates by itself.
      <span class="live-dot" title="Live"></span></p>
    ${staffForm}
    <div id="updateList">${r.updates.map((u, i) => updateCard(u, isFresh(i)))}</div>`);
  if (r.latest) { markUpdateSeen(r.latest); setUpdatesLink(r.latest); }
  // Live: look again every 20 seconds while this page is open; new ones slide in on top.
  const token = pageToken;
  const poll = setInterval(async () => {
    if (token !== pageToken) { clearInterval(poll); return; }
    if (document.visibilityState !== 'visible') return;
    const n = await gb.call('updates.list', { limit: 20 });
    if (token !== pageToken || !n.ok) return;
    const list = $('#updateList');
    if (!list) return;
    const added = n.updates.filter((u) => !known.has(u.id));
    for (const u of added.reverse()) {
      list.insertAdjacentHTML('afterbegin', updateCard(u, true).s);
      toast('New update: ' + u.name + '!');
    }
    // (and ones staff deleted disappear)
    const now = new Set(n.updates.map((u) => u.id));
    const oldest = n.updates.length ? n.updates[n.updates.length - 1].time : 0;
    for (const id of known) if (!now.has(id) && id.startsWith('u-')) {
      const u = r.updates.find((x) => x.id === id);
      if (!u || u.time >= oldest) { const el = document.getElementById('upd-' + id); if (el) el.remove(); }
    }
    known = new Set([...known, ...now]);
    if (n.latest) { markUpdateSeen(n.latest); setUpdatesLink(n.latest); }
  }, 20000);
};

pages.games = async (mine, which) => {
  if (mine === 'mine') {
    const fav = which === 'favorites';
    if (!signedIn()) { show(html`<h1>${fav ? 'Favorites' : 'Continue Playing'}</h1>${needSignIn('see your games')}`); return; }
    const r = await pageCall('games.mine', { which: fav ? 'favorites' : 'recent', limit: 200 });
    show(html`<p><a href="#/games">&lt; Games</a></p><h1>${fav ? 'My Favorites' : 'Continue Playing'}</h1>
      <div class="tabs"><a class="btn ${fav ? '' : 'blue'}" href="#/games/mine/recent">Recently played</a>
        <a class="btn ${fav ? 'blue' : ''}" href="#/games/mine/favorites">Favorites</a></div>
      ${r.ok ? (r.assets.length ? html`<div class="grid">${r.assets.map(gameCard)}</div>`
        : html`<p class="muted">${fav ? 'No favourites yet. Press the star on a game\'s page to add it here.' : 'Games you play show up here.'}</p>`)
        : html`<p class="error">${r.error}</p>`}`);
    return;
  }
  const q = new URLSearchParams(location.hash.split('?')[1] || '');
  const sort = q.get('sort') || 'popular', query = q.get('q') || '', genre = q.get('genre') || '';
  show(html`<h1>Games</h1><p class="muted">Loading...</p>`);
  const [r] = await Promise.all([pageCall('list', { kind: 'game', sort, query, genre, limit: 100 }), loadPlaying()]);
  const sorts = [['popular', 'Most played'], ['featured', 'Featured'], ['playing', 'Playing now'], ['rated', 'Top rated'], ['new', 'Newest'], ['updated', 'Recently updated']];
  const link = (over) => '#/games?' + new URLSearchParams(Object.assign({ q: query, sort, genre }, over));
  show(html`<h1>Games${genre ? html` <span class="muted">· ${genre}</span>` : ''}</h1>
    <form class="row" data-form="gameSearch"><input type="hidden" name="genre" value="${genre}">
      <input type="search" name="q" placeholder="Search games, genres, descriptions" value="${query}" style="max-width:280px">
      <select name="sort" style="width:auto">${sorts.map(([k, l]) => html`<option value="${k}" ${sort === k ? 'selected' : ''}>${l}</option>`)}</select>
      <button class="btn blue">Search</button></form>
    <div class="genre-chips"><a class="chip ${genre ? '' : 'on'}" href="${link({ genre: '' })}">All</a>
      ${GENRES.map((gn) => html`<a class="chip ${genre === gn ? 'on' : ''}" href="${link({ genre: gn })}">${gn}</a>`)}</div>
    ${r.ok ? (r.assets.length ? html`<div class="grid">${r.assets.map(gameCard)}</div>`
        : html`<p class="muted">${sort === 'featured' && !query && !genre ? 'No featured games right now.' : 'No games found' + (genre ? ' in ' + genre : '') + '.'} ${genre || query ? html`<a href="#/games">See all games</a>` : ''}</p>`)
      : html`<p class="error">${r.error}</p>`}`);
};

// The old catalog look, shared by the Catalog and the Library: a search bar with a category box
// on top, "Browse by Category" down the left with filters under it, then rows of item tiles that
// say more about a thing when you point at it.
const BROWSE_PAGE = 42;
// Everything of a kind (the server hands out 100 at a time).
async function listAll(args) {
  const all = [];
  for (let offset = 0; offset < 1000; offset += 100) {
    const r = await pageCall('list', { ...args, offset, limit: 100 });
    if (!r.ok) return r;
    all.push(...r.assets);
    if (r.assets.length < 100) break;
  }
  return { ok: true, assets: all };
}
const hashParams = () => new URLSearchParams(location.hash.split('?')[1] || '');
// The page's address with some settings changed (going back to page 1 unless the page is what changed).
function browseLink(changes) {
  const p = hashParams();
  if (!('page' in changes)) p.delete('page');
  for (const [k, v] of Object.entries(changes)) { if (v === '' || v == null) p.delete(k); else p.set(k, v); }
  const s = p.toString();
  return location.hash.split('?')[0] + (s ? '?' + s : '');
}
const BROWSE_SORTS = [['', 'Relevance'], ['popular', 'Most Popular'], ['new', 'Recently Updated'], ['low', 'Price (Low to High)'], ['high', 'Price (High to Low)']];
function browseSort(list, sort) {
  const by = {
    rated: (x, y) => wilsonScore(y.likes || 0, y.dislikes || 0) - wilsonScore(x.likes || 0, x.dislikes || 0)
      || voteCount(y) - voteCount(x) || (y.sales || 0) - (x.sales || 0),
    popular: (x, y) => (y.sales + y.plays) - (x.sales + x.plays),
    favorited: (x, y) => (y.favorites || 0) - (x.favorites || 0) || (y.sales + y.plays) - (x.sales + x.plays),
    new: (x, y) => (y.updated || y.created) - (x.updated || x.created),
    low: (x, y) => (x.price || 0) - (y.price || 0),
    high: (x, y) => (y.price || 0) - (x.price || 0),
  }[sort];
  return by ? [...list].sort(by) : list;
}
// Creators filter: everyone, Guts&Bolts staff (official), or one person by name.
function creatorMatch(a, who) {
  if (!who) return true;
  if (who === 'official') return a.creatorStaff;
  return lower(a.creatorName) === lower(who);
}
const lower = (s) => String(s || '').toLowerCase();
// A tile: picture, blue name, price; pointing at it shows who made it, when, and how many sold.
function browseCard(a, href, price) {
  return html`<div class="card square bcard">
    <a class="pic-wrap" href="${href}"><div class="pic">${a.kind === 'decal' ? html`<img class="thumb" data-decal="${a.id}" alt="" style="width:100%;height:100%;object-fit:contain;border:0">`
      : a.kind === 'model' && a.thumb ? html`<img class="lib-thumb" data-thumb="${a.id}" data-v="${a.thumb}" alt="">`
      : a.kind === 'audio' || a.kind === 'plugin' || a.kind === 'model' || a.kind === 'animation' ? html`<span class="lib-kind">${a.kind === 'audio' ? raw(SPEAKER_SVG) : ''}${KINDS[a.kind] || a.kind}</span>`
      : itemIcon(a)}</div>${officialBadge(a)}${a.limited ? html`<span class="limited-tag">LIMITED</span>` : auctionOpen(a) ? html`<span class="timed-tag">AUCTION</span>` : a.exclusive ? html`<span class="timed-tag">EXCLUSIVE</span>` : a.offsaleAt ? html`<span class="timed-tag">${a.offsale ? 'OFF SALE' : 'TIMED'}</span>` : ''}</a>
    <a class="name" href="${href}">${a.name}</a>
    <div class="bprice">${price}</div>
    ${a.kind === 'model' ? html`<div class="brating">${ratingLine(a)}</div>` : ''}
    <div class="btip"><div>Creator: <a href="#/user/${a.creator}">${a.creatorName}</a>${verified(a.creatorVerified)}</div>
      <div>Updated: ${ago(a.updated || a.created)}</div><div>${a.kind === 'model' || a.kind === 'animation' ? 'Taken' : 'Sales'}: ${(a.sales || 0).toLocaleString()}</div>
      ${isCatalogItem(a.kind) ? html`<div>Favorited: ${(a.favorites || 0).toLocaleString()}</div>` : ''}
      ${isCatalogItem(a.kind) ? '' : html`<div>ID: ${assetNum(a)} <button class="btn small" data-act="copyId" data-id="${assetNum(a)}">Copy</button></div>`}</div></div>`;
}
// cats: [key, label, [kids]]; the page itself fetches, filters and passes the tiles in.
function browsePage({ title, cats, cat, heading, shown, total, filters, side = '', sorts = BROWSE_SORTS }) {
  const q = hashParams(), query = q.get('q') || '', sort = q.get('sort') || '', page = Math.max(1, Number(q.get('page')) || 1);
  const catLink = (k) => browseLink({ cat: k, q: '' });
  const open = (c) => c[0] === cat || (c[2] || []).some((k) => k[0] === cat);
  const catRow = (c) => html`<div class="bcat ${open(c) ? 'open' : ''}">
      <a class="${c[0] === cat ? 'on' : ''}" href="${catLink(c[0])}">${c[1]}${c[2] ? html`<span class="arrow">&#9654;</span>` : ''}</a>
      ${c[2] ? html`<div class="bkids">${c[2].map((k) => html`<a class="${k[0] === cat ? 'on' : ''}" href="${catLink(k[0])}">${k[1]}</a>`)}</div>` : ''}</div>`;
  const flat = cats.flatMap((c) => c === '-' ? [] : [[c[0], c[1]], ...(c[2] || [])]);
  const from = (page - 1) * BROWSE_PAGE, to = Math.min(total, from + BROWSE_PAGE);
  const pager = total > BROWSE_PAGE ? html`<div class="bpager">
      ${page > 1 ? html`<a class="btn" href="${browseLink({ page: page - 1 })}">&lt; Previous</a>` : ''}
      <span>Page ${page} of ${Math.ceil(total / BROWSE_PAGE)}</span>
      ${to < total ? html`<a class="btn" href="${browseLink({ page: page + 1 })}">Next &gt;</a>` : ''}</div>` : '';
  return html`<div class="btop"><h1>${title}</h1>
      <form class="bsearch" data-form="browseSearch"><input type="search" name="q" value="${query}" aria-label="Search ${title}">
        <select name="cat">${flat.map(([k, l]) => html`<option value="${k}" ${k === cat ? 'selected' : ''}>${l}</option>`)}</select>
        <button class="btn">Search</button></form></div>
    <div class="browse">
      <aside class="bside">
        <div class="bcats"><div class="bcats-head"><small>Browse by</small>Category</div>
          ${cats.map((c) => c === '-' ? raw('<hr>') : catRow(c))}</div>
        ${filters.length ? html`<h3 class="bfilters-head">Filters</h3>` : ''}
        ${filters.map((f) => html`<div class="bfilter"><b>${f.title}</b>
          ${f.options.map(([v, l]) => html`<a class="filt ${(q.get(f.key) || '') === v ? 'on' : ''}" href="${browseLink({ [f.key]: v })}"><span class="dot"></span>${l}</a>`)}
          ${f.extra || ''}</div>`)}
        ${side}
      </aside>
      <section class="bmain">
        <div class="bhead"><div><b class="bcaps">${heading}</b><br>
          <span class="small muted">${total ? `Showing ${from + 1} - ${to} of ${total.toLocaleString()} result${total === 1 ? '' : 's'}` : 'No results'}${query ? html` for <b>${query}</b>` : ''}</span></div>
          <label class="small">Sort by: <select data-go>${sorts.map(([k, l]) => html`<option value="${browseLink({ sort: k })}" ${sort === k ? 'selected' : ''}>${l}</option>`)}</select></label></div>
        ${shown.length ? html`<div class="bgrid">${shown}</div>${pager}` : html`<p class="muted">Nothing here yet.</p>`}
      </section></div>`;
}
// Cut a filtered, sorted list down to the page being looked at.
function browseSlice(list) {
  const page = Math.max(1, Number(hashParams().get('page')) || 1);
  return list.slice((page - 1) * BROWSE_PAGE, page * BROWSE_PAGE);
}
const creatorFilter = () => ({ title: 'Creators', key: 'creator', options: [['', 'All Creators'], ['official', 'Guts&Bolts']],
  extra: html`<form class="row bcreator" data-form="browseCreator"><input type="text" name="name" placeholder="Name" value="${(() => { const c = hashParams().get('creator') || ''; return c === 'official' ? '' : c; })()}"><button class="btn small">Go</button></form>` });

// Create > Library: everything people have made public, to use in your games.
const LIBRARY_CATS = [['model', 'Models'], ['decal', 'Decals'], ['audio', 'Audio'], ['animation', 'Animations'], ['plugin', 'Plugins']];
async function libraryPage(head) {
  const q = hashParams();
  const cat = LIBRARY_CATS.some((c) => c[0] === (q.get('cat') || q.get('kind'))) ? (q.get('cat') || q.get('kind')) : 'model';
  const r = await listAll({ kind: cat, query: q.get('q') || '', sort: 'popular' });
  if (!r.ok) { show(html`${head}<p class="error">${r.error}</p>`); return; }
  const list = browseSort(r.assets.filter((a) => creatorMatch(a, q.get('creator'))), q.get('sort'));
  const side = html`<div class="box small">Use anything here in your games: in Studio, open the <b>Toolbox</b> (the <b>Library</b> tab),
      or copy an ID into a Decal's Texture / a Sound's File.
      <form class="row" data-form="assetGo" style="margin-top:8px"><input type="text" name="id" placeholder="Got an ID? (like 123)" style="flex:1;min-width:0">
        <button class="btn small">Go</button></form></div>`;
  show(html`${head}${browsePage({ title: 'Library', cats: LIBRARY_CATS, cat, heading: LIBRARY_CATS.find((c) => c[0] === cat)[1],
    shown: browseSlice(list).map((a) => browseCard(a, '#/library/' + assetNum(a), a.price > 0 ? bolts(a.price) : raw('<span class="free">Free</span>'))),
    total: list.length, filters: [creatorFilter()], side,
    sorts: cat === 'model' ? [...BROWSE_SORTS.filter((x) => x[0] !== 'low' && x[0] !== 'high'), ['rated', 'Best rated']] : BROWSE_SORTS })}`);
  view.querySelectorAll('img[data-decal]').forEach(decalPicture);
  loadThumbs();
}

// A speaker, for audio in the Library.
const SPEAKER_SVG = '<svg viewBox="0 0 24 24" width="40" height="40" aria-hidden="true"><path d="M3 9h4l5-4v14l-5-4H3z" fill="currentColor"/>'
  + '<path d="M15.5 8.5a5 5 0 0 1 0 7M18 6a8.5 8.5 0 0 1 0 12" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>';

// Library files (decal pictures, audio) as something the page can show or play: fetched once.
const assetUrls = new Map();
function assetUrl(id) {
  if (!assetUrls.has(id)) {
    assetUrls.set(id, (async () => {
      const r = await gb.call('get', { id });
      if (!r.ok) { assetUrls.delete(id); return { error: r.error }; }
      const ext = (r.asset.meta && r.asset.meta.ext) || '';
      const type = { jpg: 'image/jpeg', png: 'image/png', mp3: 'audio/mpeg', wav: 'audio/wav', ogg: 'audio/ogg', flac: 'audio/flac' }[ext] || '';
      return { url: URL.createObjectURL(new Blob([gb.base64Bytes(r.data)], type ? { type } : {})) };
    })());
  }
  return assetUrls.get(id);
}

// Upload review: a new decal, sound or T-shirt from a creator who isn't Verified waits for staff
// to look at it. Only its creator (and staff) can see it until then, so tell them.
function reviewBanner(a) {
  if (a.review === 'pending') return html`<p class="box gold small"><b>Waiting for a staff check.</b> Only you can see this until
    Guts&amp;Bolts staff look at it (Verified creators skip this).</p>`;
  if (a.review === 'rejected') return html`<p class="box small error"><b>This didn't pass the staff check</b>, so nobody else can see it.
    ${a.reviewNote ? html`Staff said: "${a.reviewNote}"` : ''}</p>`;
  return '';
}

// #/library/<id>: one thing from the Library on its own page, found by its ID (the same
// "gb:..." ID you paste into a game). Look at a decal, listen to audio, copy the ID.
pages.library = async (id = '') => {
  id = id.trim().replace(/^gb:/, '');
  if (!id) { location.hash = '#/create/library'; return; }
  const r = await pageCall('asset.info', { id });
  if (!r.ok) { show(html`<p><a href="#/create/library">&lt; Library</a></p><h1>Not found</h1><p class="muted">${r.error}</p>`); return; }
  const a = r.asset;
  // Catalog items and games have their own pages already.
  if (isCatalogItem(a.kind)) { location.replace('#/item/' + a.id); return; }
  if (a.kind === 'game') { location.replace('#/game/' + a.id); return; }
  const useIt = {
    decal: 'In Studio, select a Decal (or a Part) and paste the ID into its Texture. Scripts can use it too: decal.Texture = "' + assetNum(a) + '"',
    audio: 'In Studio, select a Sound and paste the ID into its File (SoundId). Scripts can use it too: sound.SoundId = "' + assetNum(a) + '"',
    model: 'In Studio, open the Toolbox, pick the Library tab and search for it to insert it into your game.',
    plugin: 'In Studio, open the Toolbox\'s Plugins tab and install it from there.',
    animation: 'In Studio\'s Animation Editor, press ... > Import > From the Library and paste its ID (or search for it).',
  }[a.kind] || '';
  const canDelete = signedIn() && (a.creator === me.id || me.staff);
  show(html`<p><a href="#/create/library?cat=${a.kind}">&lt; Library</a></p>
    <div class="item-page">
      <h1 class="item-title">${a.name}</h1>
      <div class="item-sub">Guts&amp;Bolts ${KINDS[a.kind] || a.kind}${a.access === 'private' ? ' / Private' : ''}
        ${canDelete ? '' : html` · ${reportLink('item', a.id, a.name)}`}</div>
      ${reviewBanner(a)}
      <div class="item-cols asset-cols">
        <div class="item-pic">
          <div class="pic" id="assetPic">${a.kind === 'decal' ? html`<span class="muted">Loading...</span>`
            : a.kind === 'model' && a.thumb ? html`<img class="lib-thumb" data-thumb="${a.id}" data-v="${a.thumb}" alt="">`
            : a.kind === 'audio' ? html`<span class="lib-kind audio-big">${raw(SPEAKER_SVG)}Audio</span>`
            : html`<span class="lib-kind">${KINDS[a.kind] || a.kind}</span>`}</div>
          ${officialBadge(a)}
        </div>
        <div class="item-info">
          <div class="item-creator">
            <a href="#/user/${a.creator}" class="item-creator-pic" id="creatorPic"></a>
            <table>
              <tr><td>Creator:</td><td><a href="#/user/${a.creator}">${a.creatorName}</a>${verified(a.creatorVerified)}</td></tr>
              <tr><td>Created:</td><td>${a.created ? new Date(a.created * 1000).toLocaleDateString() : '?'}</td></tr>
              <tr><td>Updated:</td><td>${ago(a.updated || a.created)}</td></tr>
            </table>
          </div>
          <p class="item-desc">${a.description || html`<span class="muted">No description.</span>`}</p>
          ${a.kind === 'model' ? modelVotes(a, r.used) : ''}
          ${a.kind === 'audio' ? html`<div id="assetAudio"><button class="btn green" data-act="assetListen" data-id="${a.id}">&#9654; Listen</button></div>` : ''}
          <hr>
          <p class="small muted">Type: ${KINDS[a.kind] || a.kind}${a.size ? ' · ' + Math.max(1, Math.round(a.size / 1024)) + ' KB' : ''}</p>
          ${useIt ? html`<p class="small">${useIt}</p>` : ''}
        </div>
        <div class="item-side">
          <div class="item-buybox asset-idbox">
            <div class="small">Asset ID</div>
            <input type="text" class="asset-id" value="${assetNum(a)}" readonly>
            <button class="btn blue big buy" data-act="copyId" data-id="${assetNum(a)}">Copy ID</button>
            ${a.kind === 'decal' ? html`<p class="small"><a id="decalOpen" target="_blank" rel="noopener" hidden>Open full size</a></p>` : ''}
          </div>
          ${canDelete ? html`<p><button class="btn small red" data-act="deleteAsset" data-id="${a.id}" data-name="${a.name}">Delete</button></p>` : ''}
        </div>
      </div>
    </div>`);
  loadThumbs();
  $('.asset-id').addEventListener('focus', (e) => e.target.select());
  if (a.kind === 'decal') {
    assetUrl(a.id).then((f) => {
      const el = $('#assetPic');
      if (!el) return;
      el.innerHTML = f.url ? html`<img class="item-thumb" src="${f.url}" alt="${a.name}">`.s : html`<span class="muted">${f.error}</span>`.s;
      const open = $('#decalOpen');
      if (open && f.url) { open.href = f.url; open.hidden = false; }
    });
  }
  call('profile', { id: a.creator }).then((p) => {
    const el = $('#creatorPic');
    if (!p.ok || !el) return;
    avatarPicture(p.user.avatar, p.wearing || [], 48).then((url) => { if (url && el.isConnected) el.innerHTML = html`<img src="${url}" alt="">`.s; });
  }).catch(() => {});
};

// Thumbs up / down on a Library model (one vote each; press yours again to take it back). Only
// people who've inserted it in Studio can vote, and never on their own model.
function modelVotes(a, used) {
  const own = signedIn() && a.creator === me.id;
  const can = signedIn() && !own && used;
  const why = !signedIn() ? html`<a href="#/login">Log in</a> to rate it.` : own ? 'This is your model, so you can\'t rate it.'
    : used ? 'Did it work well in your game? Give it a thumbs up or down.' : 'Insert it from Studio\'s Toolbox first, then you can rate it here.';
  const pct = voteCount(a) ? Math.round(100 * (a.likes || 0) / voteCount(a)) : 50;
  return html`<div class="model-rating">
    <div class="votes">
      <button class="btn small ${a.myVote === 1 ? 'green' : ''}" data-act="modelVote" data-id="${a.id}" data-vote="${a.myVote === 1 ? 0 : 1}"
        title="${a.myVote === 1 ? 'Take your thumbs up back' : 'Thumbs up'}" ${can ? '' : 'disabled'}>&#128077; ${a.likes || 0}</button>
      <div class="vote-bar ${voteCount(a) ? '' : 'none'}"><div style="width:${pct}%"></div></div>
      <button class="btn small ${a.myVote === -1 ? 'red' : ''}" data-act="modelVote" data-id="${a.id}" data-vote="${a.myVote === -1 ? 0 : -1}"
        title="${a.myVote === -1 ? 'Take your thumbs down back' : 'Thumbs down'}" ${can ? '' : 'disabled'}>&#128078; ${a.dislikes || 0}</button></div>
    <p class="small">${ratingLine(a)}</p>
    <p class="small muted">${why}</p></div>`;
}

// Create > Accessories: hats, hair and the rest, made and placed in Studio.
async function myAccessoriesPage(head) {
  if (!signedIn()) { show(html`${head}${needSignIn('see your accessories')}`); return; }
  const r = await pageCall('list', { creator: me.id, limit: 100 });
  const list = (r.assets || []).filter((a) => ACCESSORIES.includes(a.kind));
  show(html`${head}
    <div class="box">${me.verified || me.staff ? html`Make accessories in <b>Studio</b>: build it, open <b>Avatar &gt; Accessories</b>, pick the type
      (hat, hair, face, neck, shoulder or waist), move it into place on the mannequin, <b>Save position</b>, then <b>Upload</b>.`
      : html`<b>Only Verified creators can make accessories.</b> You can still make <a href="#/create/shirt">shirts</a> and <a href="#/create/pants">pants</a>!`}</div>
    <h2>My accessories</h2>
    ${list.length ? html`<div class="grid">${list.map(itemCard)}</div>` : html`<p class="muted">Nothing yet.</p>`}`);
  upgradeItemPictures();
}

// Create > Models: the models you published from Studio, public or private.
async function myModelsPage(head) {
  if (!signedIn()) { show(html`${head}${needSignIn('see your models')}`); return; }
  const r = await pageCall('list', { creator: me.id, kind: 'model', limit: 100 });
  const left = me.publicModelsLeft;
  show(html`${head}
    <div class="box">Publish models from <b>Studio</b>: select objects, then <b>File &gt; Publish Selection to Library</b>.
      Public models show in everyone's Library; private ones only for you.
      ${left >= 0 ? html`<br><span class="muted">You can make ${left} more model${left === 1 ? '' : 's'} public this week (Verified creators have no limit).</span>` : ''}</div>
    <h2>My Models</h2>
    ${r.ok && r.assets.length ? html`<div class="list">${r.assets.map((a) => html`<div>
        <span class="grow"><b><a href="#/library/${assetNum(a)}">${a.name}</a></b> <span class="small muted">${a.id}</span><br><span class="small">${ratingLine(a)}</span></span>
        <span class="badge-pill">${a.access === 'private' ? 'Private' : 'Public'}</span>
        <button class="btn small" data-act="modelAccess" data-id="${a.id}" data-access="${a.access === 'private' ? 'public' : 'private'}">Make ${a.access === 'private' ? 'public' : 'private'}</button>
        <button class="btn small red" data-act="deleteAsset" data-id="${a.id}" data-name="${a.name}">Delete</button></div>`)}</div>`
      : html`<p class="muted">No models yet.</p>`}`);
}

let serverPage = 1;   // which page of a game's server cards
// Paid private servers: the creator sets a price; buying one lasts 30 days, and friends join free with its code.
// Lyreto (lyreto.com) is the chat app Guts&Bolts partners with. Games and groups can link their
// Lyreto server; the card shows the server's name, picture and blurb from its invite link's preview
// (the server fetches it), or just the link when there isn't one yet.
function lyretoCard(l, whose) {
  if (!l || !l.link) return '';
  return html`<div class="lyreto-card">
    ${l.image ? html`<img class="lyreto-pic" src="${l.image}" alt="" loading="lazy" referrerpolicy="no-referrer">` : html`<div class="lyreto-pic lyreto-mark">L</div>`}
    <div class="grow"><div class="lyreto-kicker">Lyreto server</div>
      <b>${l.title || 'Chat with ' + whose + ' on Lyreto'}</b>
      ${l.description ? html`<p>${l.description}</p>` : html`<p>Chat, voice and hang out with other players.</p>`}</div>
    <a class="btn lyreto-join" href="${l.link}" target="_blank" rel="noopener noreferrer">Join on Lyreto</a></div>`;
}
const LYRETO_FIELD_NOTE = 'Paste your Lyreto server\'s invite link (it starts with https://lyreto.com/). Its page shows it as a card. Leave it empty for none.';

function privateServerBox(g, s) {
  const price = (s.ok && s.privatePrice) || g.privatePrice || 0;
  if (!price) return '';
  const until = (s.ok && s.privateUntil) || 0, free = s.ok && !s.privateNeedsBuy && !until;
  return html`<h2>Private servers</h2><div class="box private-box">
    <p>Your own server of this game, for you and your friends. It costs ${bolts(price)} for 30 days, and friends join free with its code.</p>
    ${free ? html`<p class="muted">${signedIn() && g.creator === me.id ? 'It\'s your game, so private servers are free for you.' : 'Private servers are free for you.'}</p>`
      : until ? html`<p><b>You have one until ${new Date(until * 1000).toLocaleDateString()}.</b> Start it from the Servers button in the app.</p>
        <button class="btn small" data-act="buyPrivate" data-id="${g.id}" data-name="${g.name}" data-price="${price}">Add 30 days for ${price} Bolts</button>`
      : html`<button class="btn green" data-act="buyPrivate" data-id="${g.id}" data-name="${g.name}" data-price="${price}">Buy a private server</button>`}</div>`;
}

// A game's pictures on its page: the thumbnail, then the creator's screenshots (up to 5), with
// arrows, a strip to pick one, and a click (or tap) for a big view. Swipe works on phones.
const shotUrl = (g, s) => `/shot/${encodeURIComponent(g.id)}/${encodeURIComponent(s)}`;
// Screenshot limits (the server has the same: kMaxShots, kShotSize).
const SHOT_MOST = 5, SHOT_MB = 2;
function shotProblem(file) {
  const okType = ['image/png', 'image/jpeg', 'image/webp'].includes(file.type) || /\.(png|jpe?g|webp)$/i.test(file.name);
  if (!okType) return `"${file.name}" isn't a .png, .jpg or .webp picture.`;
  if (file.size > SHOT_MB * 1024 * 1024) return `"${file.name}" is too big (${(file.size / 1048576).toFixed(1)} MB; the most is ${SHOT_MB} MB). Try a smaller picture, or save it as a .jpg.`;
  return '';
}
let gallery = { pics: [], i: 0 };
function gameGallery(g) {
  const shots = g.shots || [];
  if (!shots.length) return gamePic(g);
  const pics = [...(g.thumb ? [{ src: `/thumb/${encodeURIComponent(g.id)}?v=${g.thumb}`, alt: g.name }] : []),
    ...shots.map((s, i) => ({ src: shotUrl(g, s), alt: `Screenshot ${i + 1} of ${g.name}` }))];
  gallery = { pics, i: 0 };
  return html`<div class="gallery">
    <div class="gallery-stage" data-swipe="gallery">
      <button type="button" class="gallery-main" data-act="galleryOpen" aria-label="Show this picture bigger">
        <img id="galleryImg" src="${pics[0].src}" alt="${pics[0].alt}"></button>
      ${pics.length > 1 ? html`<button type="button" class="gallery-arrow prev" data-act="galleryStep" data-step="-1" aria-label="Previous picture">&lsaquo;</button>
        <button type="button" class="gallery-arrow next" data-act="galleryStep" data-step="1" aria-label="Next picture">&rsaquo;</button>
        <span class="gallery-count" id="galleryCount">1 / ${pics.length}</span>` : ''}</div>
    ${pics.length > 1 ? html`<div class="gallery-strip">${pics.map((p, i) => html`<button type="button" class="gallery-thumb ${i ? '' : 'on'}"
        data-act="galleryShow" data-i="${i}" aria-label="Picture ${i + 1} of ${pics.length}"><img src="${p.src}" alt="" loading="lazy"></button>`)}</div>` : ''}</div>`;
}
function galleryGo(i) {
  const n = gallery.pics.length;
  if (!n) return;
  gallery.i = ((i % n) + n) % n;
  const p = gallery.pics[gallery.i];
  for (const img of document.querySelectorAll('#galleryImg, #lightboxImg')) { img.src = p.src; img.alt = p.alt; }
  for (const c of document.querySelectorAll('#galleryCount, #lightboxCount')) c.textContent = (gallery.i + 1) + ' / ' + n;
  document.querySelectorAll('.gallery-thumb').forEach((b, k) => b.classList.toggle('on', k === gallery.i));
}
function galleryLightbox() {
  document.querySelectorAll('.lightbox').forEach((m) => m.remove());
  const p = gallery.pics[gallery.i], many = gallery.pics.length > 1;
  const box = document.createElement('div');
  box.className = 'modal lightbox';
  box.setAttribute('role', 'dialog');
  box.setAttribute('aria-modal', 'true');
  box.setAttribute('aria-label', 'Pictures');
  box.dataset.swipe = 'gallery';
  box.innerHTML = html`<button type="button" class="lightbox-x" data-act="galleryClose" aria-label="Close">&times;</button>
    <img id="lightboxImg" src="${p.src}" alt="${p.alt}">
    ${many ? html`<button type="button" class="gallery-arrow prev" data-act="galleryStep" data-step="-1" aria-label="Previous picture">&lsaquo;</button>
      <button type="button" class="gallery-arrow next" data-act="galleryStep" data-step="1" aria-label="Next picture">&rsaquo;</button>
      <span class="gallery-count" id="lightboxCount">${gallery.i + 1} / ${gallery.pics.length}</span>` : ''}`.s;
  box.addEventListener('click', (e) => { if (e.target === box) box.remove(); });   // a click outside the picture closes it
  document.body.appendChild(box);
  box.querySelector('.lightbox-x').focus();
}
document.addEventListener('keydown', (e) => {
  if (!document.querySelector('.lightbox')) return;
  if (e.key === 'Escape') document.querySelectorAll('.lightbox').forEach((m) => m.remove());
  else if (e.key === 'ArrowLeft') galleryGo(gallery.i - 1);
  else if (e.key === 'ArrowRight') galleryGo(gallery.i + 1);
});
// Swiping left or right on the picture (phones) moves through them.
let swipeStart = null;
document.addEventListener('touchstart', (e) => {
  swipeStart = e.target.closest && e.target.closest('[data-swipe=gallery]') && e.touches.length === 1
    ? { x: e.touches[0].clientX, y: e.touches[0].clientY } : null;
}, { passive: true });
document.addEventListener('touchend', (e) => {
  if (!swipeStart || !e.changedTouches.length) return;
  const dx = e.changedTouches[0].clientX - swipeStart.x, dy = e.changedTouches[0].clientY - swipeStart.y;
  swipeStart = null;
  if (Math.abs(dx) > 40 && Math.abs(dx) > Math.abs(dy) * 1.5) galleryGo(gallery.i + (dx < 0 ? 1 : -1));
}, { passive: true });

pages.game = async (id) => {
  const [r, s] = await Promise.all([pageCall('list', { kind: 'game', limit: 100 }), pageCall('servers.list', { game: id })]);
  const g = r.ok && r.assets.find((a) => a.id === id || String(a.num) === id);
  if (!g) { show(html`<h1>Game not found</h1><p class="muted">${r.ok ? 'It may have been deleted, or its creator made it private.' : r.error}</p>`); return; }
  const [pr, cr] = await Promise.all([pageCall('pass.list', { game: g.id }), pageCall('comments.list', { game: g.id })]);
  const passes = pr.ok ? pr.passes.filter((p) => !p.offsale || p.owned) : [];
  const mine = signedIn() && (g.creator === me.id || me.staff);
  const servers = s.ok ? s.servers : [];
  const shared = new URLSearchParams(location.hash.split('?')[1] || '').get('server') || '';
  // Roblox-style server cards: faces of who's in it, how full it is, Join and Share. 8 a page.
  const perPage = 8, pages = Math.max(1, Math.ceil(servers.length / perPage));
  serverPage = Math.min(Math.max(1, serverPage), pages);
  const shown = servers.slice((serverPage - 1) * perPage, serverPage * perPage);
  const card = (sv) => {
    const people = sv.people || [], more = (sv.players || 1) - people.length;
    return html`<div class="server-card ${sv.id === shared ? 'shared' : ''}">
      <div class="server-faces">${people.map((p) => html`<a class="face" href="#/user/${p.id}" title="${p.name}">${avatarSvg(p.avatar, 52, [], true)}</a>`)}
        ${more > 0 ? html`<span class="face more">+${more}</span>` : ''}</div>
      <p class="server-count">${sv.players || 1} of ${sv.max || '?'} people max${sv.private ? html` <span class="badge-pill">Private</span>` : ''}</p>
      ${sv.friends ? html`<p class="small muted">${sv.friends} friend${sv.friends === 1 ? '' : 's'} here</p>` : ''}
      <div class="server-buttons"><button class="btn green small" data-act="joinServer" data-id="${g.id}" data-name="${g.name}" data-server="${sv.id}">Join</button>
        <button class="btn small" data-act="shareServer" data-id="${g.id}" data-server="${sv.id}">Share</button></div>
      <p class="small muted">ID: ${sv.id.replace(/^s-/, '')}</p></div>`;
  };
  const pager = pages > 1 ? html`<div class="pager">
      <button class="btn small" data-act="serverPage" data-to="1" ${serverPage === 1 ? 'disabled' : ''}>&laquo;</button>
      <button class="btn small" data-act="serverPage" data-to="${serverPage - 1}" ${serverPage === 1 ? 'disabled' : ''}>&lsaquo;</button>
      <span>Page ${serverPage} of ${pages}</span>
      <button class="btn small" data-act="serverPage" data-to="${serverPage + 1}" ${serverPage === pages ? 'disabled' : ''}>&rsaquo;</button>
      <button class="btn small" data-act="serverPage" data-to="${pages}" ${serverPage === pages ? 'disabled' : ''}>&raquo;</button></div>` : '';
  show(html`<p><a href="#/games">&lt; Games</a></p>
    <div class="hero">${gameGallery(g)}
      <div><h1 class="game-title">${gameIcon(g, 40)} ${g.name}</h1>
        ${g.access && g.access !== 'public' ? html`<p><span class="badge-pill">${ACCESS_NAMES[g.access]}</span></p>` : ''}
        ${g.featured ? html`<p><span class="badge-pill featured-pill">&#11088; Featured</span></p>` : ''}
        <p>by ${g.group ? html`<a href="#/group/${g.group.id}">${g.group.name}</a> <span class="muted small">(made by <a href="#/user/${g.creator}">${g.creatorName}</a>)</span>`
          : html`<a href="#/user/${g.creator}">${g.creatorName}</a>${verified(g.creatorVerified)}`}</p>
        ${(g.genres || []).length ? html`<p>${g.genres.map((gn) => html`<a class="chip" href="#/games?genre=${encodeURIComponent(gn)}">${gn}</a> `)}</p>` : ''}
        <div class="votes">
          <button class="btn small ${g.myVote === 1 ? 'green' : ''}" data-act="vote" data-id="${g.id}" data-vote="${g.myVote === 1 ? 0 : 1}" title="I like it">&#128077; ${g.likes || 0}</button>
          <div class="vote-bar"><div style="width:${(g.likes || 0) + (g.dislikes || 0) ? Math.round(100 * g.likes / (g.likes + g.dislikes)) : 50}%"></div></div>
          <button class="btn small ${g.myVote === -1 ? 'red' : ''}" data-act="vote" data-id="${g.id}" data-vote="${g.myVote === -1 ? 0 : -1}" title="Not for me">&#128078; ${g.dislikes || 0}</button>
          <button class="btn small fav ${g.myFavorite ? 'on' : ''}" data-act="favorite" data-id="${g.id}" data-on="${g.myFavorite ? '' : '1'}"
            title="${g.myFavorite ? 'Take it out of your favourites' : 'Add to your favourites'}">${g.myFavorite ? raw('&#9733;') : raw('&#9734;')} ${g.favorites || 0}</button></div>
        <table class="stats game-stats">
          <tr><td>Playing</td><td>${g.playing || 0}</td><td>Visits</td><td>${g.plays || 0}</td></tr>
          <tr><td>Created</td><td>${new Date(g.created * 1000).toLocaleDateString()}</td><td>Updated</td><td>${ago(g.updated || g.created)}</td></tr>
          <tr><td>Server size</td><td>${g.maxPlayers || 12}</td><td>Genre</td><td>${(g.genres || []).join(', ') || 'All'}</td></tr></table>
        <button class="btn green big" data-act="play" data-id="${g.id}" data-name="${g.name}">Play</button>
        ${mine ? html` <a class="btn" href="#/configure/${g.id}">Configure this game</a>
          <button class="btn" data-act="editInStudio" data-id="${g.id}" data-name="${g.name}">Edit in Studio</button>` : ''}
        ${signedIn() && me.staff ? html`<p><button class="btn small ${g.featured ? '' : 'gold'}" data-act="featureGame" data-id="${g.id}" data-on="${g.featured ? '' : '1'}">
          ${g.featured ? 'Stop featuring' : 'Feature this game'}</button> <span class="small muted">(staff)</span></p>` : ''}
        <p class="small muted">Games run in the Guts&amp;Bolts app (Windows, Mac, Linux and Android).${mine ? '' : html` ${reportLink('game', g.id, g.name)}`}</p></div></div>
    <h2>Description</h2><p style="white-space:pre-wrap">${g.description || 'No description yet.'}</p>
    ${g.lyreto ? html`<h2>Community</h2>${lyretoCard(g.lyreto, 'the players')}` : ''}
    ${(g.badges || []).length ? html`<h2>Badges</h2><div class="list">${g.badges.map((b) => html`<div>
        ${gameBadgeIcon(b, 44)}<span class="grow"><b>${b.name}</b><br><span class="small muted">${b.description || ''}</span></span>
        <span class="small muted">Won ${b.awarded || 0} time${b.awarded === 1 ? '' : 's'}</span></div>`)}</div>` : ''}
    ${passes.length ? html`<h2>Passes</h2><div class="pass-grid">${passes.map(passCard)}</div>` : ''}
    ${privateServerBox(g, s)}
    <h2>Servers</h2>
    ${servers.length ? html`<div class="server-grid">${shown.map(card)}</div>${pager}`
      : html`<p class="muted">Nobody's playing right now. Be the first!</p>`}
    ${commentsBlock(g, cr)}`);
};

// Comments under a game, newest first, 20 at a time.
const commentRow = (g, c) => html`<div class="comment" data-comment="${c.id}">
    <a class="face" href="#/user/${c.by.userId || c.by.id}">${c.by.name.slice(0, 1).toUpperCase()}</a>
    <div class="grow"><p class="comment-head"><a href="#/user/${c.by.userId || c.by.id}"><b>${c.by.name}</b></a>${verified(c.by.verified)}${partner(c.by)}
        ${c.creator ? html` <span class="badge-pill" title="Made this game">Creator</span>` : ''}
        <span class="small muted">${ago(c.at)}</span></p>
      <p class="comment-text">${c.text}</p>
      <p class="small">${c.canDelete ? html`<a href="#" class="small" data-act="deleteComment" data-game="${g.id}" data-id="${c.id}">Delete</a> ` : ''}
        ${signedIn() && c.by.id !== me.id ? reportLink('comment', g.id + ':' + c.id, c.by.name) : ''}</p></div></div>`;
function commentsBlock(g, cr) {
  if (!cr || !cr.ok) return '';
  const box = cr.off ? html`<p class="muted">Comments are turned off for this game.</p>`
    : signedIn() ? html`<form class="comment-form" data-form="comment"><input type="hidden" name="game" value="${g.id}">
        <textarea name="text" maxlength="200" rows="2" placeholder="Say something nice about ${g.name}..." required></textarea>
        <button class="btn blue">Post</button><span class="small muted">Up to 200 letters. Be kind.</span></form>`
    : html`<p class="muted"><a href="#/login">Log in</a> or <a href="#/signup">sign up</a> to comment.</p>`;
  return html`<h2 id="comments">Comments <span class="small muted">(${cr.count})</span></h2>${box}
    <div class="comments">${cr.comments.length ? cr.comments.map((c) => commentRow(g, c)) : html`<p class="muted">No comments yet.</p>`}</div>
    ${cr.more ? html`<p><button class="btn small" data-act="moreComments" data-game="${g.id}" data-before="${cr.comments[cr.comments.length - 1].id}">Show more</button></p>` : ''}`;
}

// The Catalog, laid out like the old one (see browsePage).
const CATALOG_CATS = [['featured', 'Featured'], ['collectibles', 'Collectibles'], ['auctions', 'Auctions'], ['favorited', 'Most Favorited'],
  ['myfavorites', 'My Favorites'], '-', ['all', 'All Categories'],
  ['clothes', 'Clothing', [['shirt', 'Shirts'], ['tshirt', 'T-Shirts'], ['pants', 'Pants']]],
  ['body', 'Body Parts', [['face', 'Faces'], ['bundle', 'Bundles'], ['bodypart', 'Individual Parts']]], ['gear', 'Gear'],
  ['emotes', 'Emotes', [['emote', 'Emotes'], ['animpack', 'Animation Packs']]],
  ['accessories', 'Accessories', [['hat', 'Hats'], ['hair', 'Hair'], ['faceacc', 'Face'], ['neck', 'Neck'], ['shoulder', 'Shoulder'], ['waist', 'Waist']]]];
const CATALOG_PICK = {
  featured: (a) => a.creatorStaff, collectibles: (a) => !!a.limited, auctions: () => true, all: () => true,
  favorited: (a) => (a.favorites || 0) > 0, myfavorites: () => true,
  clothes: (a) => ['shirt', 'tshirt', 'pants'].includes(a.kind), body: (a) => a.kind === 'face' || a.kind === 'bundle' || a.kind === 'bodypart', accessories: (a) => ACCESSORIES.includes(a.kind),
  emotes: (a) => isEmoteItem(a.kind),
};
const PRICES = { free: [0, 0], '1-100': [1, 100], '101-1000': [101, 1000], over: [1001, Infinity] };
pages.catalog = async () => {
  const q = hashParams();
  const flat = CATALOG_CATS.flatMap((c) => c === '-' ? [] : [c, ...(c[2] || [])]);
  let cat = q.get('cat') || q.get('kind') || 'all';
  if (!flat.some((c) => c[0] === cat)) cat = 'all';
  if (cat === 'myfavorites' && !signedIn()) { show(html`<h1>My Favorites</h1>${needSignIn('see your favourite items')}`); return; }
  // (Auctions are asked for by name: they're Exclusive items, which the normal list leaves out.
  // My Favorites is your wishlist: the items you hearted on their pages.)
  const r = await listAll({ kind: 'clothing', query: q.get('q') || '', ...(cat === 'auctions' ? { auctions: true } : {}),
    ...(cat === 'myfavorites' ? { favorites: true } : {}) });
  if (!r.ok) { show(html`<h1>Catalog</h1><p class="error">${r.error}</p>`); return; }
  const pick = CATALOG_PICK[cat] || ((a) => a.kind === cat);
  const price = PRICES[q.get('price')];
  const list = browseSort(r.assets.filter((a) => pick(a) && creatorMatch(a, q.get('creator')) &&
    (!price || ((a.price || 0) >= price[0] && (a.price || 0) <= price[1]))), q.get('sort') || (cat === 'favorited' ? 'favorited' : ''));
  const priceOf = (a) => auctionOpen(a) ? html`bid ${bolts(a.auction.need)}`
    : a.limited && a.limited.left <= 0 ? (a.limited.lowest ? html`from ${bolts(a.limited.lowest)}` : raw('<span class="muted">Sold out</span>'))
    : a.offsale ? raw('<span class="muted">Off sale</span>') : a.price > 0 ? bolts(a.price) : raw('<span class="free">Free</span>');
  show(browsePage({ title: 'Catalog', cats: CATALOG_CATS, cat, heading: flat.find((c) => c[0] === cat)[1],
    shown: browseSlice(list).map((a) => browseCard(a, '#/item/' + a.id, priceOf(a))), total: list.length,
    sorts: [...BROWSE_SORTS.slice(0, 2), ['favorited', 'Most Favorited'], ...BROWSE_SORTS.slice(2)],
    filters: [{ title: 'Price', key: 'price', options: [['', 'Any Price'], ['free', 'Free'], ['1-100', '1 - 100'], ['101-1000', '101 - 1,000'], ['over', 'Over 1,000']] },
      creatorFilter()] }));
};

// Pick which emote slot (1-8) an emote goes in, or take it out.
function emoteSlotPicker(id, slot) {
  return html`<div class="emote-slots small">${slot >= 0 ? html`In slot ${slot + 1}. ` : 'Put it in a slot: '}
    <span class="row">${Array.from({ length: kEmoteSlots }, (_, i) => html`<button class="btn small ${i === slot ? 'green' : ''}"
      data-act="emoteEquip" data-slot="${i + 1}" data-id="${id}" title="Slot ${i + 1}">${i + 1}</button>`)}
    ${slot >= 0 ? html`<button class="btn small" data-act="emoteEquip" data-slot="${slot + 1}" data-id="">Take out</button>` : ''}</span></div>`;
}

pages.item = async (id) => {
  // (Asked for by ID: Exclusive items aren't in the catalog's list, and it only holds the newest 100.)
  const r = await pageCall('asset.info', { id });
  const a = r.ok && isCatalogItem(r.asset.kind) ? r.asset : null;
  if (!a) { show(html`<h1>Item not found</h1>`); return; }
  const owned = signedIn() && (me.owned || []).includes(a.id);
  const gearOn = a.kind === 'gear' && signedIn() && (me.gear || []).includes(a.id);
  const emoteSlot = a.kind === 'emote' && signedIn() ? (me.emotes || []).indexOf(a.id) : -1;   // 0-7, or -1
  const packOn = a.kind === 'animpack' && signedIn() && me.animPack === a.id;
  const canEdit = signedIn() && (a.creator === me.id || me.staff);
  const L = a.limited;
  const copies = L ? ((await pageCall('item.copies', { id: a.id })).copies || []) : [];
  // A bundle: what's in it (each one gets its own page).
  const inBundle = a.kind === 'bundle' ? (await Promise.all(((a.meta && a.meta.items) || []).map((i) => pageCall('asset.info', { id: i }))))
    .filter((x) => x.ok).map((x) => x.asset) : [];
  const forSale = copies.filter((c) => c.price > 0 && !c.mine).sort((x, y) => x.price - y.price);
  const mineCopies = copies.filter((c) => c.mine);
  const soldOut = L && L.left <= 0;
  const offsale = !!a.offsale;
  const isNew = Date.now() / 1000 - (a.created || 0) < 7 * 86400;
  const timed = a.offsaleAt && !offsale && !a.exclusive;   // (an Exclusive item has no sale to end)
  const au = a.auction, auOpen = auctionOpen(a);
  // The buy box (like the 2016 catalog): what's left, the price, the button, how many sold.
  const buyButton = owned && a.kind === 'gear'
    ? html`<p class="ok"><b>You own this.</b></p>
      <button class="btn ${gearOn ? '' : 'green'} big buy" data-act="gearEquip" data-id="${a.id}" data-on="${gearOn ? '' : '1'}">${gearOn ? 'Unequip' : 'Equip'}</button>`
    : owned && a.kind === 'emote'
    ? html`<p class="ok"><b>You own this.</b></p>${emoteSlotPicker(a.id, emoteSlot)}`
    : owned && a.kind === 'animpack'
    ? html`<p class="ok"><b>You own this.</b></p>
      <button class="btn ${packOn ? '' : 'green'} big buy" data-act="animpackEquip" data-id="${packOn ? '' : a.id}">${packOn ? 'Take off' : 'Use this pack'}</button>`
    : owned ? html`<p class="ok"><b>You own this${mineCopies.length ? ' (#' + mineCopies.map((c) => c.serial).join(', #') + ')' : ''}.</b></p>`
    : soldOut ? html`<button class="btn big buy" disabled>Sold out</button>`
    : auOpen ? auctionBidBox(a)
    : a.exclusive ? html`<button class="btn big buy" disabled>Exclusive</button>`
    : offsale ? html`<button class="btn big buy" disabled>Off sale</button>`
    : html`<button class="btn green big buy" data-act="buy" data-id="${a.id}">${a.price > 0 ? html`Buy with ${boltIcon}` : 'Get it free'}</button>`;
  // Paid items that aren't Limited can be bought for a friend (even if you have one yourself).
  const giftable = a.price > 0 && !L && !offsale && !a.review && !a.exclusive && !(a.meta && a.meta.award);
  const giftButton = giftable ? html`<button class="btn big gift" data-act="gift" data-id="${a.id}" data-name="${a.name}" data-price="${a.price}">&#127873; Buy as a gift</button>` : '';
  show(html`<p><a href="#/catalog">&lt; Catalog</a></p>
    <div class="item-page">
      <h1 class="item-title">${a.name}</h1>
      <div class="item-sub">Guts&amp;Bolts ${KINDS[a.kind]}${L ? ' / Collectible Item / Limited Edition' : a.exclusive ? ' / Exclusive Item' : timed ? ' / Timed Item' : ''}
        ${canEdit ? '' : html` · ${reportLink('item', a.id, a.name)}`}</div>
      ${reviewBanner(a)}
      <div class="item-cols">
        <div class="item-pic">
          <div class="pic" id="item3d">${itemIcon(a)}</div>
          ${drawable3d(a) ? html`<button class="btn blue item-try-on" id="itemTryOn" type="button">Try On</button>` : ''}
          ${isNew ? html`<span class="item-ribbon">NEW</span>` : ''}
          ${timed ? html`<span class="item-clock" title="${offsaleText(a)}"><i></i></span>` : ''}
          ${L ? html`<span class="item-limited">LIMITED<b>U</b></span>` : ''}
          ${drawable3d(a) ? html`<button class="item-3d" id="item3dToggle" type="button" title="Switch to the 3D preview" aria-pressed="false">3D</button>` : ''}
        </div>
        <div class="item-info">
          <div class="item-creator">
            <a href="#/user/${a.creator}" class="item-creator-pic" id="creatorPic"></a>
            <table>
              <tr><td>Creator:</td><td><a href="#/user/${a.creator}">${a.creatorName}</a>${verified(a.creatorVerified)}</td></tr>
              <tr><td>Created:</td><td>${a.created ? new Date(a.created * 1000).toLocaleDateString() : '?'}</td></tr>
              <tr><td>Updated:</td><td>${ago(a.updated || a.created)}</td></tr>
            </table>
          </div>
          <p class="item-desc">${a.description || html`<span class="muted">No description.</span>`}</p>
          <hr>
          <p class="small muted">Type: ${KINDS[a.kind]} · Asset ID ${assetNum(a)}</p>
          ${auOpen ? html`<p class="small"><b>🔨 Auction:</b> the top ${au.copies === 1 ? 'bidder gets' : au.copies + ' bidders get'} this Exclusive item when time runs out. If someone outbids you, your Bolts come straight back.</p>`
            : a.exclusive ? html`<p class="small"><b>Exclusive:</b> not in the catalog and can't be bought. It comes from events, gift boxes and auctions.</p>` : ''}
          ${a.gift ? html`<p class="small"><b>🎁 Gift box:</b> everyone who owns this when it goes off sale gets a surprise item.${a.giftItem ? html` <span class="muted">(Staff: it gives <a href="#/item/${a.giftItem.id}">${a.giftItem.name}</a>.)</span>` : ''}</p>` : ''}
          ${a.kind === 'bundle' ? html`<p class="small"><b>In this bundle:</b> ${inBundle.length
              ? inBundle.map((x, i) => html`${i ? ', ' : ''}<a href="#/item/${x.id}">${x.name}</a>`) : html`<span class="muted">just itself</span>`}.
            ${a.meta && a.meta.headless ? 'Wearing it hides your head and face.' : ''} Buying it gives you everything in it; wear it on the <a href="#/avatar">Avatar</a> page.</p>` : ''}
          ${a.kind === 'bodypart' && a.meta && a.meta.bodyPart ? html`<p class="small"><b>Body part:</b> <span class="notranslate">${a.meta.bodyPart}</span>. <span>Wear different body parts together, even over a bundle's body.</span></p>` : ''}
          ${a.kind === 'gear' ? html`<p class="small muted">Equipped gear goes in your backpack in games that allow gear. Equip as many owned gear items as you want.</p>`
            : a.kind === 'emote' ? html`<p class="small muted">Put it in one of your 8 emote slots, then press B in any game (or tap the smiley on a phone) and pick it. You can also type /e and its name in the chat.</p>`
            : a.kind === 'animpack' ? html`<p class="small muted">An animation pack changes how your character stands, walks, runs, jumps and falls, in every game.</p>`
            : owned ? html`<p class="small muted">Wear it from the Avatar page.</p>` : ''}
        </div>
        <div class="item-side">
          <div class="item-buybox">
            ${L ? (soldOut ? html`<div class="item-left">Sold out</div>` : html`<div class="item-left">${L.left} Remaining</div>`) : ''}
            ${auOpen ? html`<div class="item-offsale">Auction ends in <span data-countdown="${au.endsAt}">${countdown(au.endsAt)}</span></div>` : ''}
            ${timed ? html`<div class="item-offsale">Offsale in <span data-countdown="${a.offsaleAt}">${countdown(a.offsaleAt)}</span></div>`
              : offsale ? html`<div class="item-left">Off sale</div>` : ''}
            ${auOpen ? '' : html`<div class="item-price">Price: ${a.price > 0 ? bolts(a.price) : html`<b class="ok">Free</b>`}</div>`}
            ${buyButton}
            ${giftButton}
            <div class="item-sold">( ${a.sales || 0} Sold )</div>
            <button class="btn small item-fav ${a.myFavorite ? 'on' : ''}" data-act="itemFavorite" data-id="${a.id}" data-on="${a.myFavorite ? '' : '1'}"
              title="${a.myFavorite ? 'Take it out of your favourites' : 'Add it to your favourites (Catalog > My Favorites)'}">${a.myFavorite ? '\u2665' : '\u2661'} <span>${a.myFavorite ? 'Favorited' : 'Favorite'}</span> (${(a.favorites || 0).toLocaleString()})</button>
            ${L && L.resellers ? html`<div class="small">${L.resellers} for resale from ${bolts(L.lowest)}</div>` : ''}
          </div>
          ${canEdit ? html`<p><button class="btn" data-act="toggle" data-target="#itemEdit">Edit item</button></p>` : ''}
        </div>
      </div>
    </div>
    ${canEdit ? html`<div class="box" id="itemEdit" hidden><h2 class="boxhead">Edit item</h2>
      <form class="form" data-form="itemEdit"><input type="hidden" name="id" value="${a.id}">
        <label>Name</label><input type="text" name="name" maxlength="50" value="${a.name}" required>
        <label>Description</label><textarea name="description" maxlength="1000">${a.description || ''}</textarea>
        <label>Price (Bolts)</label><input type="number" name="price" min="0" value="${a.price || 0}" style="max-width:140px">
        ${a.kind === 'face' ? html`<label>New picture <span class="muted small">(optional, a .png face)</span></label><input type="file" name="picture" accept="image/png">`
          : a.kind === 'tshirt' ? html`<label>New picture <span class="muted small">(optional, a .png or .jpg)</span></label><input type="file" name="picture" accept="image/png,image/jpeg">`
          : a.kind === 'gear' ? html`<p class="small muted">To change the tool itself, publish it again from Studio.</p>`
          : isEmoteItem(a.kind) ? html`<p class="small muted">The moves themselves can't be changed here.</p>`
          : !drawable3d(a) ? html`<p class="small muted">To change how it looks or where it sits, open it in Studio's Accessory window and upload it again.</p>`
          : a.kind === 'hat' ? html`<label>Shape</label><select name="style">${[[1, 'Top Hat'], [2, 'Cap'], [3, 'Crown']].map(([v, l]) => html`<option value="${v}" ${Number(a.meta && a.meta.style) === v ? 'selected' : ''}>${l}</option>`)}</select>`
          : html`<label>New picture <span class="muted small">(optional, from the <a href="templates/${a.kind}_template.png" download>template</a>)</span></label><input type="file" name="picture" accept="image/png">`}
        ${canEdit ? html`<label>Goes off sale <span class="muted small">(a timed item; leave empty to sell it for good)</span></label>
          <input type="datetime-local" name="offsaleAt" value="${a.offsaleAt ? localInput(a.offsaleAt) : ''}" style="max-width:220px">` : ''}
        ${me.staff ? html`<label><input type="checkbox" name="exclusive" ${a.exclusive ? 'checked' : ''}> Exclusive <span class="muted small">(staff only: hidden from the catalog and can't be bought; give it out with a gift box or the box below)</span></label>
          <label>Gift box: gives item ID <span class="muted small">(staff only: when this goes off sale, everyone who owns it gets that item; leave empty for a normal item)</span></label>
          <input type="text" name="giftItem" value="${a.giftItem ? a.giftItem.num || a.giftItem.id : ''}" placeholder="like 86" style="max-width:220px">` : ''}
        <p><button class="btn green">Save</button> <span id="itemEditMsg"></span></p></form>
      ${me.staff && !L ? html`<form class="form" data-form="itemGive"><input type="hidden" name="id" value="${a.id}">
        <label>Give it to someone <span class="muted small">(staff only: free, for event prizes; their user number or username)</span></label>
        <input type="text" name="to" placeholder="like 5 or @username" style="max-width:220px">
        <p><button class="btn blue">Give</button> <span id="itemGiveMsg"></span></p></form>` : ''}
      ${me.staff && auOpen ? html`<p><button class="btn red" data-act="auctionCancel" data-id="${a.id}">Cancel the auction</button>
        <span class="muted small">(everyone gets their Bolts back)</span></p>`
      : me.staff && a.exclusive && !L ? html`<form class="form" data-form="auctionStart"><input type="hidden" name="id" value="${a.id}">
        <label>Start an auction <span class="muted small">(staff only: the top bidders get it when time runs out)</span></label>
        <div class="auction-start"><div><label class="small">Copies</label><input type="number" name="copies" min="1" max="100" value="1"></div>
          <div><label class="small">Starting bid</label><input type="number" name="minBid" min="1" value="100"></div>
          <div><label class="small">Hours</label><input type="number" name="hours" min="1" max="720" value="24"></div></div>
        <p><button class="btn gold">Start auction</button> <span id="auctionMsg"></span></p></form>` : ''}
      ${me.official && a.creator === me.id && canBeLimited(a) ? html`<form class="form" data-form="itemLimited"><input type="hidden" name="id" value="${a.id}">
        <label>${L ? 'Change the stock' : 'Make it Limited'} <span class="muted small">(only Guts can do this)</span></label>
        <input type="number" name="stock" min="1" value="${L ? L.stock : 100}" style="max-width:140px">
        <p class="small muted">A Limited has a fixed number of numbered copies. When they're sold out, people buy and sell them from each other, and trade them.</p>
        <p><button class="btn gold">${L ? 'Save stock' : 'Make Limited'}</button></p></form>` : ''}</div>` : ''}
    ${au && !au.cancelled ? auctionBidders(au, auOpen) : ''}
    ${L ? html`<h2>Resellers</h2>${forSale.length ? html`<table class="stats resellers"><tr><th>Seller</th><th>Copy</th><th>Price</th><th></th></tr>
        ${forSale.map((c) => html`<tr><td><a href="#/user/${c.owner}">${c.ownerName}</a></td><td>#${c.serial}</td><td>${bolts(c.price)}</td>
          <td><button class="btn green small" data-act="resaleBuy" data-id="${a.id}" data-serial="${c.serial}" data-price="${c.price}">Buy</button></td></tr>`)}</table>`
        : html`<p class="muted">Nobody is selling a copy right now.</p>`}
      ${mineCopies.length ? html`<h2>Your copies</h2>${mineCopies.map((c) => html`<form class="row" data-form="resaleList"><input type="hidden" name="id" value="${a.id}">
          <input type="hidden" name="serial" value="${c.serial}"><b>#${c.serial}</b>
          <input type="number" name="price" min="0" value="${c.price || ''}" placeholder="Price in Bolts" style="max-width:150px">
          <button class="btn small ${c.price ? '' : 'blue'}">${c.price ? 'Change price' : 'Sell'}</button>
          ${c.price ? html`<span class="muted small">On sale for ${c.price}. Set 0 to take it off sale.</span>` : html`<span class="muted small">You get ${70}% when it sells.</span>`}</form>`)}` : ''}` : ''}`);
  // Catalog pages show the item preview rotating gently on its own (hats, hair, gear and
  // model accessories); other wearables start as their 2D picture. The 3D/2D button in the
  // corner flips between the two. The user's avatar is only used when they ask for 3D.
  const spinsByItself = drawable3d(a) && (['gear', 'hat', 'hair'].includes(a.kind) || (a.meta && a.meta.model && ['faceacc', 'neck', 'shoulder', 'waist'].includes(a.kind)));
  const item3dToggle = $('#item3dToggle');
  let itemShowing3d = false, itemViewTurn = 0;   // itemViewTurn: a newer press wins over a slow, older load
  const setItemToggle = () => {
    if (!item3dToggle) return;
    item3dToggle.textContent = itemShowing3d ? '2D' : '3D';
    item3dToggle.title = itemShowing3d ? 'Switch to the 2D picture' : 'Switch to the 3D preview';
    item3dToggle.setAttribute('aria-pressed', itemShowing3d ? 'true' : 'false');
  };
  const showItem2d = () => {
    const el = $('#item3d');
    itemViewTurn++;
    itemShowing3d = false;
    if (el) { el.innerHTML = itemIcon(a).s; upgradeItemPictures(); }
    setItemToggle();
  };
  const showItem3d = async () => {
    const el = $('#item3d');
    if (!el) return;
    const turn = ++itemViewTurn;
    let viewer = null;
    if (spinsByItself) {
      viewer = await mountAvatar(el, {}, [a], { width: 328, height: 328, cam: itemCamera(a.kind), autoSpin: true, itemOnly: true }).catch(() => null);
    } else {
      // Clothes, faces and bundles: show them on your own avatar (or the default one).
      let avatar = defaultAvatar(), wearing = [];
      if (signedIn()) {
        const profile = await call('profile', { id: me.userId }).catch(() => null);
        if (profile && profile.ok && profile.user) {
          avatar = profile.user.avatar || defaultAvatar();
          wearing = Array.isArray(profile.wearing) ? profile.wearing : [];
        }
      }
      if (turn !== itemViewTurn) return;
      const previewItems = [...wearing.filter((item) => String(item.id) !== String(a.id)), a];
      viewer = await mountAvatar(el, avatar, previewItems, { width: 328, height: 328, cam: itemCamera(a.kind) }).catch(() => null);
    }
    if (turn !== itemViewTurn) return;
    if (!viewer) { showItem2d(); throw new Error('3D preview is unavailable in this browser. Try enabling WebGL.'); }
    itemShowing3d = true;
    setItemToggle();
  };
  if (spinsByItself) showItem3d().catch(() => showItem2d());
  const tryOnButton = $('#itemTryOn');
  if (tryOnButton) tryOnButton.addEventListener('click', async () => {
    if (!me) { loginPopup('try on catalog items'); return; }
    tryOnButton.disabled = true;
    tryOnButton.textContent = 'Loading avatar…';
    try {
      const profile = await call('profile', { id: me.userId });
      if (!profile.ok || !profile.user) throw new Error(profile.error || 'Could not load your avatar.');
      const overlay = document.createElement('div');
      overlay.className = 'try-on-overlay';
      overlay.innerHTML = '<section class="try-on-dialog" role="dialog" aria-modal="true" aria-labelledby="tryOnTitle"><button type="button" class="try-on-close" aria-label="Close preview">×</button><h2 id="tryOnTitle">Try on this item</h2><p class="small muted">Drag to rotate horizontally and vertically. Double-click the avatar to reset the view.</p><div class="try-on-render"></div><div class="try-on-actions"><button type="button" class="btn" data-try-reset>Reset view</button><button type="button" class="btn blue" data-try-close>Close preview</button></div></section>';
      document.body.appendChild(overlay);
      const close = () => { overlay.remove(); };
      overlay.querySelectorAll('.try-on-close,[data-try-close]').forEach((el) => el.addEventListener('click', close));
      overlay.addEventListener('click', (event) => { if (event.target === overlay) close(); });
      const render = overlay.querySelector('.try-on-render');
      const wearing = Array.isArray(profile.wearing) ? profile.wearing : [];
      const previewItems = [...wearing.filter((item) => String(item.id) !== String(a.id)), a];
      const viewer = await mountAvatar(render, profile.user.avatar || defaultAvatar(), previewItems, { width: 360, height: 430, cam: itemCamera(a.kind) });
      overlay.querySelector('[data-try-reset]').addEventListener('click', () => {
        if (viewer) viewer.set(profile.user.avatar || defaultAvatar(), previewItems);
        if (viewer) render.querySelector('canvas')?.dispatchEvent(new MouseEvent('dblclick'));
      });
    } catch (error) {
      alert(error.message || 'Could not load the try-on preview.');
    } finally {
      if (tryOnButton.isConnected) { tryOnButton.disabled = false; tryOnButton.textContent = 'Try On'; }
    }
  });
  if (item3dToggle) item3dToggle.addEventListener('click', async () => {
    if (itemShowing3d) { showItem2d(); return; }
    item3dToggle.disabled = true;
    item3dToggle.textContent = '…';
    try {
      await showItem3d();
    } catch (error) {
      alert(error.message || 'Could not load the 3D preview.');
    } finally {
      item3dToggle.disabled = false;
      setItemToggle();
    }
  });
  tickCountdowns();
  // The creator's character, small, next to their name.
  call('profile', { id: a.creator }).then((p) => {
    const el = $('#creatorPic');
    if (!p.ok || !el) return;
    avatarPicture(p.user.avatar, p.wearing || [], 48).then((url) => { if (url && el.isConnected) el.innerHTML = html`<img src="${url}" alt="">`.s; });
  }).catch(() => {});
};

// Auctions (worker/server.js auction.*): staff put an Exclusive item up for bids, and the top
// bidders get it when time runs out.
const auctionOpen = (a) => !!a.auction && !a.auction.done && a.auction.endsAt > Date.now() / 1000;
function auctionBidBox(a) {
  const au = a.auction;
  const winning = au.myBid > 0;
  return html`<div class="item-price">${winning ? html`Your bid: ${bolts(au.myBid)}` : html`Bid at least: ${bolts(au.need)}`}</div>
    ${winning ? html`<p class="ok small"><b>You're winning!</b> You'll get it if nobody outbids you.</p>` : ''}
    ${signedIn() ? html`<form class="form auction-bid" data-form="auctionBid"><input type="hidden" name="id" value="${a.id}">
        <input type="number" name="amount" min="${winning ? au.myBid + 1 : au.need}" value="${winning ? au.myBid + 1 : au.need}" aria-label="Your bid in Bolts">
        <button class="btn green big buy">${winning ? 'Raise bid' : 'Place bid'}</button></form>`
      : html`<button class="btn green big buy" data-act="auctionBid">Place bid</button>`}`;
}
function auctionBidders(au, open) {
  const top = au.bids;
  return html`<h2>${open ? 'Top bidders' : 'Auction winners'} <span class="small muted">(${au.copies === 1 ? '1 copy' : au.copies + ' copies'})</span></h2>
    ${top.length ? html`<table class="stats"><tr><th>#</th><th>Bidder</th><th>Bid</th><th></th></tr>
      ${top.map((b, i) => html`<tr class="${signedIn() && b.id === me.id ? 'me-row' : ''}"><td>${i + 1}</td><td><a href="#/user/${b.id}">${b.name}</a></td><td>${bolts(b.amount)}</td><td class="small muted">${ago(b.at)}</td></tr>`)}</table>
      ${open && top.length >= au.copies ? html`<p class="small muted">To get in, bid more than the lowest bid here.</p>` : ''}`
    : html`<p class="muted">${open ? 'No bids yet. Be the first!' : 'Nobody bid on it.'}</p>`}`;
}

// "23 h 53 m 35 s" until a timed item goes off sale (days too, when it's further off).
function countdown(at) {
  let left = Math.max(0, Math.floor(at - Date.now() / 1000));
  const d = Math.floor(left / 86400); left %= 86400;
  const h = Math.floor(left / 3600), m = Math.floor((left % 3600) / 60), sec = left % 60;
  const n = (v, u) => html`<b>${v}</b> ${u} `;
  return html`${d ? n(d, 'd') : ''}${n(h, 'h')}${n(m, 'm')}${d ? '' : n(sec, 's')}`;
}
let countdownTimer = 0;
function tickCountdowns() {
  clearInterval(countdownTimer);
  countdownTimer = setInterval(() => {
    const els = document.querySelectorAll('[data-countdown]');
    if (!els.length) { clearInterval(countdownTimer); return; }
    for (const el of els) {
      const at = Number(el.dataset.countdown);
      if (Date.now() / 1000 >= at) { clearInterval(countdownTimer); render(); return; }   // it just went off sale
      el.innerHTML = countdown(at).s;
    }
  }, 1000);
}

// Decal pictures: fetched once, shown from memory.
const pictures = new Map();
async function decalPicture(img) {
  const id = img.dataset.decal;
  if (!pictures.has(id)) {
    pictures.set(id, (async () => {
      const r = await gb.call('get', { id });
      if (!r.ok) return '';
      const type = (r.asset.meta && r.asset.meta.ext) === 'jpg' ? 'image/jpeg' : 'image/png';
      return URL.createObjectURL(new Blob([gb.base64Bytes(r.data)], { type }));
    })());
  }
  const url = await pictures.get(id);
  if (url) img.src = url;
}

// The public roadmap (Create > Roadmap): what's being worked on, what's planned and what's done,
// and ideas from developers that everyone votes on. Staff edit it right here and answer ideas
// (here or on the Staff page). worker/roadmap.js has the rules.
const ROAD_COLUMNS = [['working', 'Working on it'], ['planned', 'Planned'], ['done', 'Done']];
function roadItemForm(x) {
  return html`<form class="form road-form" data-form="roadSave"><input type="hidden" name="id" value="${x ? x.id : ''}">
      <label>Title</label><input type="text" name="title" maxlength="80" required value="${x ? x.title : ''}">
      <label>What it is <span class="muted small">(optional)</span></label><textarea name="text" maxlength="600">${x ? x.text : ''}</textarea>
      <div class="row"><div><label>Where</label><select name="status">${ROAD_COLUMNS.map(([k, l]) => html`<option value="${k}" ${x && x.status === k ? 'selected' : ''}>${l}</option>`)}</select></div>
        <div><label>Version <span class="muted small">(optional)</span></label><input type="text" name="version" maxlength="20" placeholder="0.7.9" value="${x ? x.version : ''}"></div></div>
      <p><button class="btn green">${x ? 'Save' : 'Add to the roadmap'}</button></p></form>`;
}
// One idea, with its vote button (and staff's answer buttons).
function ideaRow(x, staff) {
  const who = x.by ? html`<a href="#/user/${x.by.userId || x.by.id}">${x.by.name}</a>` : 'someone';
  const answered = x.status !== 'open';
  return html`<div class="idea">
      <button class="vote ${x.voted ? 'on' : ''}" data-act="ideaVote" data-id="${x.id}" data-on="${x.voted ? '' : '1'}" ${answered ? 'disabled' : ''}
        title="${x.voted ? 'Take your vote back' : 'Vote for this'}"><span aria-hidden="true">&#9650;</span><b>${x.votes}</b></button>
      <div class="grow"><b class="notranslate">${x.title}</b>
        ${answered ? html` <span class="badge-pill ${x.status === 'planned' ? 'ok' : ''}">${x.status === 'planned' ? 'On the roadmap' : 'Not now'}</span>` : ''}
        ${x.text ? html`<p class="small notranslate">${x.text}</p>` : ''}
        <p class="small muted">Suggested by ${who} · ${ago(x.at)}</p>
        ${x.note ? html`<p class="small idea-note"><b>Staff:</b> <span class="notranslate">${x.note}</span></p>` : ''}
        ${staff ? html`<p>${answered ? html`<button class="btn small" data-act="ideaAnswer" data-id="${x.id}" data-answer="open">Reopen</button>`
          : html`<button class="btn small green" data-act="ideaAnswer" data-id="${x.id}" data-answer="planned">Put on the roadmap</button>
            <button class="btn small" data-act="ideaAnswer" data-id="${x.id}" data-answer="declined">Say no</button>`}
          <button class="btn small red" data-act="ideaRemove" data-id="${x.id}">Delete</button></p>` : ''}</div></div>`;
}
async function roadmapPage(head) {
  const r = await pageCall('roadmap.get', {});
  if (!r.ok) { show(html`${head}<p class="error">${r.error}</p>`); return; }
  const staff = signedIn() && me.staff;
  const item = (x) => html`<div class="road-item"><b class="notranslate">${x.title}</b>${x.version ? html` <span class="badge-pill notranslate">${x.version}</span>` : ''}
      ${x.text ? html`<p class="small notranslate">${x.text}</p>` : ''}
      ${x.pr ? html`<p class="small"><a href="https://github.com/Guts-Bolts/Guts-Bolts/pull/${x.pr}" target="_blank" rel="noopener">See the work</a> <span class="muted notranslate">#${x.pr}</span></p>` : ''}
      ${staff && !x.builtIn ? html`<details class="small"><summary>Edit</summary>${roadItemForm(x)}
        <p><button class="btn small red" data-act="roadRemove" data-id="${x.id}">Remove</button></p></details>` : ''}</div>`;
  show(html`${head}
    <p class="muted">What we're making next for Guts&amp;Bolts. Have an idea? Suggest it below, and vote for the ones you want most.</p>
    ${staff ? html`<details class="box"><summary><b>Add to the roadmap</b> (staff)</summary>${roadItemForm(null)}</details>` : ''}
    <div class="roadmap">${ROAD_COLUMNS.map(([k, l]) => {
      const list = r.items.filter((x) => x.status === k);
      return html`<section class="road-col road-${k}"><h2>${l} <span class="muted small">(${list.length})</span></h2>
        ${list.length ? list.map(item) : html`<p class="muted small">Nothing here yet.</p>`}</section>`;
    })}</div>
    <h2>Ideas from developers</h2>
    ${signedIn() ? html`<details class="box"><summary><b>Suggest an update</b></summary>
        <form class="form" data-form="ideaSuggest">
          <label>Your idea, in a few words</label><input type="text" name="title" maxlength="80" required placeholder="like: Horses you can ride">
          <label>More about it <span class="muted small">(optional)</span></label><textarea name="text" maxlength="600"></textarea>
          <p class="small muted">You can have 3 ideas waiting at a time. Staff read them all and answer the ones that get the most votes first.</p>
          <p><button class="btn green">Suggest it</button></p></form></details>`
      : html`<p class="box"><a href="#/signup">Sign up</a> or <a href="#/login">log in</a> to suggest ideas and vote.</p>`}
    ${r.ideas.length ? html`<div class="ideas">${r.ideas.map((x) => ideaRow(x, staff))}</div>`
      : html`<p class="muted">No ideas waiting. Be the first!</p>`}
    ${r.answered.length ? html`<h2>Answered lately</h2><div class="ideas">${r.answered.map((x) => ideaRow(x, staff))}</div>` : ''}`);
}

pages.create = async (tab = 'games') => {
  const tabs = [['games', 'My Games'], ['model', 'Models'], ['decal', 'Decals'], ['audio', 'Audio'], ['hat', 'Hats'], ['accessory', 'Accessories'],
    ['shirt', 'Shirts'], ['tshirt', 'T-Shirts'], ['pants', 'Pants'], ['gear', 'Gear'],  ...(signedIn() && me.official ? [['face', 'Faces']] : []), ...(signedIn() && me.staff ? [['bundle', 'Bundles']] : []), ['plugin', 'Plugins'], ['library', 'Library'], ['stats', 'Stats'], ['roadmap', 'Roadmap']];
  const head = html`<h1>Create</h1><div class="tabs">${tabs.map(([k, l]) => html`<a class="btn ${tab === k ? 'blue' : ''}" href="#/create/${k}">${l}</a>`)}</div>`;
  if (tab === 'library') { await libraryPage(head); return; }
  if (tab === 'stats') { await statsPage(head); return; }
  if (tab === 'roadmap') { await roadmapPage(head); return; }
  if (tab === 'model') { await myModelsPage(head); return; }
  if (tab === 'accessory') { await myAccessoriesPage(head); return; }
  const r = signedIn() ? await pageCall('list', { creator: me.id, limit: 100 }) : { ok: true, assets: [] };
  const mine = r.ok ? r.assets : [];
  const kind = tab === 'games' ? 'game' : tab;
  const list = mine.filter((a) => a.kind === kind);
  const costs = !signedIn() ? html`Make games in <b>Studio</b> and publish them here, and upload decals, audio, clothes and plugins.
      <a href="#/signup">Sign up</a> or <a href="#/login">log in</a> to start.`
    : me.verified ? html`<b>You're Verified!</b> Uploading is free, with no daily limit, and you can sell what you make.`
    : html`Uploading costs a few Bolts (decals 5, clothes 10, audio 20, plugins 20; games are free).
      <span class="muted">${me.uploadsLeft} uploads left today.</span>`;
  const clothing = ['shirt', 'pants'].includes(kind), face = kind === 'face', tee = kind === 'tshirt', bundle = kind === 'bundle';
  const accept = { decal: '.png,.jpg,.jpeg', audio: '.mp3,.wav,.ogg,.flac', plugin: '.lua', game: '.gbscene', gear: '.gbmodel,.json' }[kind] || '';
  const form = html`<h2>${kind === 'game' ? 'Publish a game' : 'Upload a new ' + KINDS[kind]}</h2>
    <form class="form" data-form="upload"><input type="hidden" name="kind" value="${kind}">
      <label>Name</label><input type="text" name="name" maxlength="50" required>
      <label>Description</label><textarea name="description" maxlength="1000"></textarea>
      ${clothing ? html`<label>Picture</label><input type="file" name="picture" accept="image/png" required>
          <p class="small muted">A 585 x 559 .png painted on the
            <a href="templates/${kind}_template.png" download>${kind} template</a>. See-through bits show the body underneath.</p>`
      : tee ? html`<label>Picture</label><input type="file" name="file" accept="image/png,image/jpeg" required>
        <p class="small muted">Any .png or .jpg (up to 1024 x 1024). It's worn flat on the front of the torso, over your shirt.
          Square pictures fit best; see-through bits show the shirt underneath.</p>`
      : face ? html`<label>Picture</label><input type="file" name="file" accept="image/png" required>
        <p class="small muted">A square .png of the face, see-through around the eyes and mouth (like 256 x 256). It's drawn on the front of the head.</p>`
      : bundle ? html`<label>Items in it <span class="muted small">(their IDs, with commas between)</span></label>
        <input type="text" name="items" placeholder="like 85, 102, 230">
        <p class="small muted">Up to 12 catalog items. Buying the bundle gives you all of them, and wearing it puts them all on.</p>
        <label style="font-weight:normal"><input type="checkbox" name="headless"> Headless: wearing it hides the head and face</label>`
      : html`<label>File</label><input type="file" name="file" accept="${accept}" required>
        <p class="small muted">${{ decal: 'A .png or .jpg picture (up to 4 MB).', audio: 'An .mp3, .wav, .ogg or .flac file (up to 6 MB).',
          plugin: 'A Lua plugin for Studio.', game: 'A .gbscene file saved from Studio (or use File > Publish in Studio).', gear: 'A .gbmodel file exported from Studio containing one Tool with a Handle. Gear is equipped from the catalog and appears in games that allow gear.' }[kind]}</p>`}
      ${['decal', 'audio'].includes(kind) ? html`<p class="small muted">${kind === 'decal' ? 'Decals' : 'Sounds'} are always free: anyone can use them in their games.</p>`
        : (me.verified || face || bundle) && kind !== 'game' ? html`<label>Price (Bolts)</label><input type="number" name="price" min="0" value="0">` : ''}
      ${kind === 'decal' || face || tee ? html`<img class="thumb" id="preview" alt="Preview" hidden style="width:120px;height:120px;margin-top:10px">` : ''}
      <p><button class="btn green">${FEES[kind] && !me.verified ? 'Upload for ' + FEES[kind] + ' Bolts' : 'Upload (free)'}</button>
        <span id="uploadMsg"></span></p></form>`;
  // Hats are for Verified creators; shirts and pants are open to everyone.
  const hatLocked = kind === 'hat' && signedIn() && !me.verified;
  const gearLocked = kind === 'gear' && signedIn() && !me.verified;
  const shownForm = gearLocked ? html`<h2>Upload new Gear</h2><p class="error">Only Verified creators can upload gear.</p><p class="muted">Make a Tool in Studio, publish/export it as a model, and upload the .gbmodel file here. Your gear will appear in the Catalog's Gear category.</p>`
    : hatLocked ? html`<h2>Upload a new Hat</h2><p class="error">Only Verified creators can make hats.</p>
    <p class="muted">You can still make <a href="#/create/shirt">shirts</a> and <a href="#/create/pants">pants</a>!</p>`
    : face && !(signedIn() && me.official) ? html`<p class="error">Only Guts can make faces.</p>`
    : bundle && !(signedIn() && me.staff) ? html`<p class="error">Only Guts&Bolts staff can make bundles.</p>`
    : kind === 'hat' ? html`<h2>Upload a new Hat</h2><p>Hats are made in <b>Studio</b>: build the hat, then use
      <b>Avatar &gt; Accessories</b> to place it on the head and upload it from there.</p>` : form;
  const row = (a) => html`<div>
      ${a.kind === 'game' ? gameIcon(a, 48) : ''}
      ${a.kind === 'decal' ? html`<img class="thumb" data-decal="${a.id}" alt="">` : ''}
      ${clothing || face || bundle || kind === 'hat' || kind === 'gear' ? html`<div class="thumb" style="display:flex;align-items:center;justify-content:center;background:#fff">${itemIcon(a)}</div>` : ''}
      <div class="grow"><b>${a.kind === 'decal' || a.kind === 'audio' || a.kind === 'plugin' ? html`<a href="#/library/${assetNum(a)}">${a.name}</a>` : a.name}</b><br><span class="small muted">
        ${a.kind === 'game' ? html`${a.plays} plays · ${ACCESS_NAMES[a.access || 'public']}` : html`${a.price > 0 ? bolts(a.price) : 'free'} · ${a.sales} sold`}
        ${a.kind === 'decal' || a.kind === 'audio' ? html` · ID ${assetNum(a)}` : ''}</span></div>
      ${a.kind === 'decal' || a.kind === 'audio' ? html`<button class="btn small" data-act="copyId" data-id="${assetNum(a)}">Copy ID</button>` : ''}
      ${a.kind === 'game' ? html`<a class="btn small" href="#/game/${a.id}">View</a>
        <a class="btn small blue" href="#/configure/${a.id}">Configure</a>
        <button class="btn small" data-act="editInStudio" data-id="${a.id}" data-name="${a.name}">Edit in Studio</button>` : ''}
      <button class="btn small red" data-act="deleteAsset" data-id="${a.id}" data-name="${a.name}">Delete</button></div>`;
  show(html`${head}<div class="box">${costs}</div>
    ${kind === 'game' ? html`<h2>My published games</h2>` : html`<h2>My ${KINDS[kind]}${kind === 'pants' ? '' : 's'}</h2>`}
    ${list.length ? html`<div class="list">${list.map(row)}</div>`
      : html`<p class="muted">${signedIn() ? 'Nothing yet.' : 'Log in to see what you\'ve made.'}</p>`}
    ${shownForm}`);
  view.querySelectorAll('img[data-decal]').forEach(decalPicture);
  loadThumbs();
  const file = view.querySelector('input[type=file]'), prev = $('#preview');
  if (file && prev) file.addEventListener('change', () => { if (file.files[0]) { prev.src = URL.createObjectURL(file.files[0]); prev.hidden = false; } });
};

// Creator stats: how your games and items are doing, with the last 30 days as little bar charts.
const statBars = (vals, days, unit) => {
  const top = Math.max(1, ...vals), w = 6, gap = 2, h = 40;
  return html`<svg class="bars" viewBox="0 0 ${vals.length * (w + gap)} ${h}" preserveAspectRatio="none" role="img"
    aria-label="${vals.reduce((a, b) => a + b, 0)} ${unit} in the last 30 days">${vals.map((v, i) =>
      html`<rect x="${i * (w + gap)}" y="${h - Math.max(v ? 2 : 0, (v / top) * h)}" width="${w}" height="${Math.max(v ? 2 : 0, (v / top) * h)}">
        <title>${days[i]}: ${v} ${unit}</title></rect>`)}</svg>`;
};
const statsPage = async (head) => {
  if (!signedIn()) { show(html`${head}${needSignIn('see how your games are doing')}`); return; }
  const r = await pageCall('creator.stats', {});
  if (!r.ok) { show(html`${head}<p class="error">${r.error}</p>`); return; }
  const sum = (a) => a.reduce((x, y) => x + y, 0);
  const games = r.items.filter((a) => a.kind === 'game'), sold = r.items.filter((a) => a.kind !== 'game' && (a.price > 0 || a.sales > 0));
  const all = (f) => sum(r.items.filter((a) => a.kind === 'game' || !a.kind.match(/gamepass|devproduct/)).map((a) => sum(a[f])));
  const tile = (n, label) => html`<div class="stat"><b>${n.toLocaleString()}</b><span class="small muted">${label}</span></div>`;
  const card = (a) => html`<div class="statcard">
      <div class="row"><b class="grow">${a.kind === 'game' ? html`<a href="#/game/${a.id}">${a.name}</a>` : a.name}</b>
        <span class="small muted">${KINDS[a.kind] || a.kind}</span></div>
      <div class="small muted">${a.kind === 'game'
        ? html`${a.plays.toLocaleString()} plays · ${a.playing} playing now · ${a.favorites} favorites · 👍 ${a.likes} 👎 ${a.dislikes}`
        : html`${a.sales.toLocaleString()} sold · ${a.price > 0 ? bolts(a.price) + ' each' : 'free'}`}
        · ${bolts(a.bolts60)} earned (60 days)</div>
      <div class="charts">
        ${a.kind === 'game' ? html`<div><span class="small muted">Plays, last 30 days: ${sum(a.plays30)}</span>${statBars(a.plays30, r.days, 'plays')}</div>` : ''}
        <div><span class="small muted">${a.kind === 'game' ? 'Things bought in it' : 'Sales'}, last 30 days: ${sum(a.sales30)}</span>${statBars(a.sales30, r.days, 'sales')}</div>
        <div><span class="small muted">Bolts earned, last 30 days: ${sum(a.bolts30)}</span>${statBars(a.bolts30, r.days, 'Bolts')}</div>
      </div></div>`;
  show(html`${head}
    <div class="stats-tiles">${tile(all('plays30'), 'plays in 30 days')}${tile(all('sales30'), 'sales in 30 days')}${tile(all('bolts30'), 'Bolts earned in 30 days')}</div>
    <p class="small muted">Plays count when someone else opens your game. Passes and products bought inside a game count on that game too.
      Days are in UTC; hover a bar to see its day.</p>
    <h2>Games</h2>${games.length ? games.map(card) : html`<p class="muted">You haven't published a game yet.</p>`}
    <h2>Things you sell</h2>${sold.length ? sold.map(card) : html`<p class="muted">Nothing sold yet.</p>`}`);
};

// Configure a game: name, description, who can play, thumbnail, icon, new version.
pages.configure = async (id) => {
  if (!signedIn()) { show(html`<h1>Configure game</h1>${needSignIn('change your games')}`); return; }
  const r = await pageCall('list', { creator: me.id, kind: 'game', limit: 100 });
  const g = r.ok && r.assets.find((a) => a.id === id || String(a.num) === id);
  if (!g) { show(html`<h1>Configure game</h1><p class="error">${r.ok ? 'That isn\'t one of your games.' : r.error}</p>`); return; }
  const access = g.access || 'public';
  const [pr, dr, gr, vr] = await Promise.all([pageCall('pass.list', { game: g.id }), pageCall('pass.list', { game: g.id, products: true }),
    pageCall('groups.mine', {}), pageCall('game.versions', { id: g.id })]);
  const versions = vr.ok ? vr.versions : [];
  // Groups this game can go in: ones where your rank can add games (and the one it's in now).
  const myGroups = (gr.ok ? gr.groups : []).filter((x) => (x.perms || []).includes('games') || (g.group && x.id === g.group.id));
  if (g.group && !myGroups.some((x) => x.id === g.group.id)) myGroups.push(g.group);
  const passes = pr.ok ? pr.passes : [], products = dr.ok ? dr.passes : [];
  const shots = g.shots || [];
  const choice = (v, label, note) => html`<label class="choice"><input type="radio" name="access" value="${v}" ${access === v ? 'checked' : ''}>
    <b>${label}</b> <span class="muted small">${note}</span></label>`;
  show(html`<p><a href="#/create/games">&lt; My Games</a></p>
    <h1>Configure: ${g.name}</h1>
    <form class="configure" data-form="configure"><input type="hidden" name="id" value="${g.id}">
      <div class="box"><h2 class="boxhead">Basic settings</h2>
        <label>Name</label><input type="text" name="name" maxlength="50" value="${g.name}" required>
        <label>Description</label><textarea name="description" maxlength="1000" rows="5">${g.description || ''}</textarea></div>
      <div class="box"><h2 class="boxhead">Genres and players</h2>
        <p class="small muted">Pick up to 3 genres so people can find your game.</p>
        <div class="genre-picks">${GENRES.map((gn) => html`<label class="choice"><input type="checkbox" name="genre" value="${gn}" ${(g.genres || []).includes(gn) ? 'checked' : ''}> ${gn}</label>`)}</div>
        <label>Players per server</label><input type="number" name="maxPlayers" min="2" max="30" value="${g.maxPlayers || 12}" style="max-width:100px">
        <label class="choice"><input type="checkbox" name="allowGear" ${g.allowGear ? 'checked' : ''}> Allow gear <span class="muted small">(players bring the gear they equipped from the catalog)</span></label>
        <label class="choice"><input type="checkbox" name="comments" ${g.comments !== false ? 'checked' : ''}> Allow comments <span class="muted small">(players can comment on its page; you can delete any comment)</span></label>
        <label>Private server price <span class="muted small">(Bolts for 30 days; 0 = free. You get 70% of each sale, or your group does if the game is in one. Private servers are always free for you.)</span></label>
        <input type="number" name="privatePrice" min="0" max="10000" value="${g.privatePrice || 0}" style="max-width:100px"></div>
      <div class="box"><h2 class="boxhead">Community</h2>
        <label>Lyreto server <span class="muted small">(optional)</span></label>
        <input type="url" name="lyreto" maxlength="200" placeholder="https://lyreto.com/..." value="${g.lyreto ? g.lyreto.link : ''}">
        <p class="small muted">${LYRETO_FIELD_NOTE}</p></div>
      <div class="box"><h2 class="boxhead">Group</h2>
        <p class="small muted">Put the game in one of your groups: it shows as the group's game, and its game pass and product sales go to the group's Bolts instead of to you.</p>
        <select name="group" data-was="${g.group ? g.group.id : ''}"><option value="">No group (it's just yours)</option>
          ${myGroups.map((x) => html`<option value="${x.id}" ${g.group && g.group.id === x.id ? 'selected' : ''}>${x.name}</option>`)}</select>
        ${myGroups.length ? '' : html`<p class="small muted">You're not in a group where your rank can add games.</p>`}</div>
      <div class="box"><h2 class="boxhead">Who can play</h2>
        ${choice('public', 'Public', 'Everyone can find and play it.')}
        ${choice('friends', 'Friends only', 'Only your friends can see and play it.')}
        ${choice('private', 'Private', 'Only you can see and play it.')}</div>
      <div class="box"><h2 class="boxhead">Pictures</h2>
        <div class="pics">
          <div><label>Thumbnail <span class="muted small">(shown on the game's page and in lists)</span></label>
            <div class="thumb-prev">${gamePic(g, 'pic')}</div>
            <input type="file" name="thumb" accept="image/png,image/jpeg" data-preview="thumb">
            <p class="small muted">Any picture; it's cropped to 16:9.</p></div>
          <div><label>Icon <span class="muted small">(a small square)</span></label>
            <div class="icon-prev">${gameIcon(g, 128)}</div>
            <input type="file" name="icon" accept="image/png,image/jpeg" data-preview="icon">
            <p class="small muted">Any picture; it's cropped to a square.</p></div>
        </div></div>
      <div class="box"><h2 class="boxhead">Update the game</h2>
        <label>New version <span class="muted small">(optional)</span></label>
        <input type="file" name="file" accept=".gbscene">
        <p class="small muted">A .gbscene saved from Studio. Players get it the next time they join.</p></div>
      <p><button class="btn green big">Save</button> <a class="btn" href="#/game/${g.id}">View game</a>
        <button type="button" class="btn red" data-act="deleteAsset" data-id="${g.id}" data-name="${g.name}" style="float:right">Delete game</button>
        <span id="configMsg"></span></p>
    </form>
    <div class="box" id="screenshots"><h2 class="boxhead">Screenshots</h2>
      <p class="small muted">Up to ${SHOT_MOST} pictures of your game, shown after the thumbnail on its page (people can click them to see them bigger).
        .png, .jpg or .webp, up to ${SHOT_MB} MB each. Pictures from your game in the Player or Studio work best.</p>
      ${shots.length ? html`<div class="shot-grid">${shots.map((s, i) => html`<div class="shot-edit" data-shot="${s}">
          <a href="${shotUrl(g, s)}" target="_blank" rel="noopener"><img src="${shotUrl(g, s)}" alt="Screenshot ${i + 1}" loading="lazy"></a>
          <div class="shot-buttons"><span class="small muted">${i + 1}</span>
            <button type="button" class="btn small" data-act="shotMove" data-id="${g.id}" data-shot="${s}" data-dir="-1" ${i ? '' : 'disabled'} aria-label="Move screenshot ${i + 1} earlier">&lsaquo;</button>
            <button type="button" class="btn small" data-act="shotMove" data-id="${g.id}" data-shot="${s}" data-dir="1" ${i < shots.length - 1 ? '' : 'disabled'} aria-label="Move screenshot ${i + 1} later">&rsaquo;</button>
            <button type="button" class="btn small red" data-act="shotDelete" data-id="${g.id}" data-shot="${s}">Delete</button></div></div>`)}</div>`
        : html`<p class="muted">No screenshots yet.</p>`}
      ${shots.length < SHOT_MOST ? html`<form class="form" data-form="shotsAdd"><input type="hidden" name="id" value="${g.id}"><input type="hidden" name="count" value="${shots.length}">
          <label>Add screenshots <span class="muted small">(${SHOT_MOST - shots.length} more)</span></label>
          <input type="file" name="files" accept="image/png,image/jpeg,image/webp" multiple>
          <p><button class="btn green">Upload</button> <span id="shotsMsg"></span></p></form>`
        : html`<p class="small muted">That's the most a game can have. Delete one to add another.</p>`}</div>
    <div class="box"><h2 class="boxhead">Version history</h2>
      <p class="small muted">Every time you publish (from Studio or with "New version" above), the old version is kept.
        Restore puts an old version back: it's published again as the newest version, so nothing is lost.
        The newest ${vr.ok ? vr.most : 50} versions are kept.</p>
      ${!vr.ok ? html`<p class="error">${vr.error}</p>` : html`<table class="versions">
        <tr><th>Version</th><th>Published</th><th>By</th><th>Size</th><th></th></tr>
        ${versions.map((v) => html`<tr><td><b>${v.v}</b>${v.restored ? html` <span class="small muted">(restored from ${v.restored})</span>` : ''}</td>
          <td title="${ago(v.at)}">${dateText(v.at)}</td><td>${v.byName}</td><td>${fileSize(v.size)}</td>
          <td>${v.live ? html`<span class="tag-live">Live now</span>`
            : html`<button class="btn small" data-act="restoreVersion" data-id="${g.id}" data-v="${v.v}">Restore</button>`}</td></tr>`)}</table>`}</div>
    <div class="box"><h2 class="boxhead">Badges</h2>
      <p class="small muted">Make badges players earn in your game. Give one from a script:
        <code>game:GetService("BadgeService"):AwardBadge(player, "badge-id")</code>
        (it only works in online servers of this game).</p>
      ${(g.badges || []).length ? html`<div class="list">${g.badges.map((b) => html`<div>${gameBadgeIcon(b, 40)}
          <span class="grow"><b>${b.name}</b> <span class="small muted">${b.description || ''}</span><br>
            <code class="small">${b.id}</code> · <span class="small muted">won ${b.awarded || 0} times</span></span>
          <button class="btn small" data-act="copyText" data-text="${b.id}">Copy ID</button>
          <button class="btn small red" data-act="deleteBadge" data-game="${g.id}" data-badge="${b.id}" data-name="${b.name}">Delete</button></div>`)}</div>`
        : html`<p class="muted">No badges yet.</p>`}
      <form class="form" data-form="newBadge"><input type="hidden" name="game" value="${g.id}">
        <label>Badge name</label><input type="text" name="name" maxlength="40" required>
        <label>Description <span class="muted small">(how to get it)</span></label><input type="text" name="description" maxlength="300">
        <label>Colour</label><input type="color" name="color" value="#f0b428">
        <p><button class="btn green">Make badge</button></p></form></div>
    <div class="box"><h2 class="boxhead">Passes</h2>
      <p class="small muted">Sell perks for your game (a VIP door, a speed boost, a special tool) for Bolts. You get
        70% of every sale. Check them from a script:
        <code>game:GetService("MarketplaceService"):UserOwnsGamePassAsync(player.UserId, ID)</code>
        or pop up a purchase with <code>:PromptGamePassPurchase(player, ID)</code>.</p>
      ${passes.length ? html`<div class="list">${passes.map((p) => html`<div><span class="pass-mini">${passPic(p)}</span>
          <span class="grow"><b>${p.name}</b> · ${p.price > 0 ? bolts(p.price) : 'free'}${p.offsale ? html` · <span class="muted">off sale</span>` : ''}<br>
            <span class="small muted">ID ${p.num} · sold ${p.sales || 0}</span></span>
          <button class="btn small" data-act="copyText" data-text="${p.num}">Copy ID</button>
          <button class="btn small" data-act="passOffsale" data-id="${p.id}" data-on="${p.offsale ? '' : '1'}">${p.offsale ? 'Put on sale' : 'Take off sale'}</button></div>`)}</div>`
        : html`<p class="muted">No passes yet.</p>`}
      <form class="form" data-form="newPass"><input type="hidden" name="game" value="${g.id}">
        <label>Pass name</label><input type="text" name="name" maxlength="50" required>
        <label>Description <span class="muted small">(what it gives)</span></label><input type="text" name="description" maxlength="300">
        <label>Price in Bolts <span class="muted small">(0 = free; selling needs a Verified account)</span></label><input type="number" name="price" min="0" max="1000000" value="0" style="max-width:140px">
        <label>Picture <span class="muted small">(optional, a square)</span></label><input type="file" name="icon" accept="image/png,image/jpeg">
        <p><button class="btn green">Make pass</button></p></form></div>
    <div class="box"><h2 class="boxhead">Developer products</h2>
      <p class="small muted">Things players can buy again and again inside your game (coins, a revive, a speed boost for a minute).
        You get 70% of every sale. Pop up a purchase with
        <code>game:GetService("MarketplaceService"):PromptProductPurchase(player, ID)</code> and hand out what they bought in
        <code>MarketplaceService.ProcessReceipt</code> (return <code>Enum.ProductPurchaseDecision.PurchaseGranted</code> when done;
        anything else and it's tried again later, so nobody loses what they paid for).</p>
      ${products.length ? html`<div class="list">${products.map((p) => html`<div>
          <span class="grow"><b>${p.name}</b> · ${p.price > 0 ? bolts(p.price) : 'free'}${p.offsale ? html` · <span class="muted">off sale</span>` : ''}<br>
            <span class="small muted">ID ${p.num} · sold ${p.sales || 0}</span></span>
          <button class="btn small" data-act="copyText" data-text="${p.num}">Copy ID</button>
          <button class="btn small" data-act="passOffsale" data-id="${p.id}" data-on="${p.offsale ? '' : '1'}">${p.offsale ? 'Put on sale' : 'Take off sale'}</button></div>`)}</div>`
        : html`<p class="muted">No developer products yet.</p>`}
      <form class="form" data-form="newProduct"><input type="hidden" name="game" value="${g.id}">
        <label>Product name</label><input type="text" name="name" maxlength="50" required>
        <label>Description <span class="muted small">(what it gives)</span></label><input type="text" name="description" maxlength="300">
        <label>Price in Bolts <span class="muted small">(selling needs a Verified account)</span></label><input type="number" name="price" min="0" max="1000000" value="10" style="max-width:140px">
        <p><button class="btn green">Make product</button></p></form></div>`);
  loadThumbs();
  // Show a picked picture straight away.
  view.querySelectorAll('input[data-preview]').forEach((inp) => inp.addEventListener('change', () => {
    const file = inp.files[0];
    if (!file) return;
    const box = view.querySelector(inp.dataset.preview === 'icon' ? '.icon-prev' : '.thumb-prev');
    box.innerHTML = html`<img src="${URL.createObjectURL(file)}" alt="">`.s;
  }));
};

// Trades: offers of your limited copies for someone else's. Two tabs: Open (#/trades) and
// Past trades (#/trades/past/<page>): every finished trade, newest first.
const tradeTabs = (tab) => html`<div class="tabs"><a class="btn ${tab === 'open' ? 'blue' : ''}" href="#/trades">Open trades</a>
  <a class="btn ${tab === 'past' ? 'blue' : ''}" href="#/trades/past">Past trades</a></div>`;
const TRADE_OUTCOMES = { accepted: 'Accepted', declined: 'Declined', cancelled: 'Cancelled', expired: 'Expired', failed: 'Couldn\'t happen' };
const tradeItems = (items) => items.length
  ? items.map((i) => html`<a class="chip" href="#/item/${i.id}">${i.name} <b>#${i.serial}</b></a> `) : html`<span class="muted">Nothing</span>`;
pages.trades = async (tab, pageNo) => {
  if (!signedIn()) { show(html`<h1>Trades</h1>${needSignIn('trade Limited items')}`); return; }
  if (tab === 'past') { await pastTradesPage(Number(pageNo) || 1); return; }
  const r = await pageCall('trade.list', {});
  const list = (items) => items.map((i) => html`<a href="#/item/${i.id}">${i.name} #${i.serial}</a>`).reduce((acc, x, k) => html`${acc}${k ? ', ' : ''}${x}`, html``);
  const row = (tr) => {
    const incoming = tr.to === me.id;
    return html`<div><span class="grow">${incoming ? html`<b><a href="#/user/${tr.from}">${tr.fromName}</a></b> offers ${list(tr.give)} for your ${list(tr.get)}`
        : html`You offered ${list(tr.give)} to <b><a href="#/user/${tr.to}">${tr.toName}</a></b> for their ${list(tr.get)}`}
      <br><span class="small muted">${ago(tr.created)} · ${tr.status}</span></span>
      ${tr.status === 'open' ? (incoming ? html`<button class="btn green small" data-act="tradeAnswer" data-op="trade.accept" data-id="${tr.id}">Accept</button>
          <button class="btn small" data-act="tradeAnswer" data-op="trade.decline" data-id="${tr.id}">Decline</button>`
        : html`<button class="btn small" data-act="tradeAnswer" data-op="trade.cancel" data-id="${tr.id}">Cancel</button>`) : ''}</div>`;
  };
  const open = r.ok ? r.trades.filter((x) => x.status === 'open') : [];
  show(html`<h1>Trades</h1>${tradeTabs('open')}
    <p class="muted">Trade your Limited items with other players. Open someone's profile and press <b>Trade</b> to make an offer.
      A trade nobody answers runs out after 30 days.</p>
    ${r.ok ? '' : html`<p class="error">${r.error}</p>`}
    <h2>Your Limiteds</h2>${r.ok && r.items.length ? html`<div class="row">${r.items.map((i) => html`<a class="chip" href="#/item/${i.id}">${i.name} #${i.serial}</a>`)}</div>`
      : html`<p class="muted">You don't have any Limited items yet.</p>`}
    <h2>Open</h2>${open.length ? html`<div class="list">${open.map(row)}</div>` : html`<p class="muted">No open trades.</p>`}
    <p class="small muted">Finished trades are under <a href="#/trades/past">Past trades</a>.</p>`);
};

// Past trades: who it was with, what each of you put in, when it finished and how it ended.
// Only you and the other person can see a trade here.
async function pastTradesPage(page) {
  const r = await pageCall('trade.history', { page });
  if (!r.ok) { show(html`<h1>Trades</h1>${tradeTabs('past')}<p class="error">${r.error}</p>`); return; }
  const who = (w) => w.userId ? html`<a href="#/user/${w.userId}"><b>${w.name}</b></a>` : html`<b>${w.name}</b>`;
  const card = (tr) => {
    const done = tr.status === 'accepted';
    return html`<div class="past-trade">
      <div class="past-trade-head">
        <span class="outcome outcome-${tr.status}">${TRADE_OUTCOMES[tr.status] || tr.status}</span>
        <span class="grow">${tr.sent ? html`You sent a trade to ${who(tr.with)}` : html`${who(tr.with)} sent you a trade`}</span>
        <span class="small muted" title="${new Date(tr.finished * 1000).toLocaleString()}">${new Date(tr.finished * 1000).toLocaleDateString()}</span></div>
      <div class="past-trade-sides">
        <div><span class="small muted">${done ? 'You gave' : 'You put in'}</span><div>${tradeItems(tr.gave)}</div></div>
        <div><span class="small muted">${done ? html`You got (from ${tr.with.name})` : html`${tr.with.name} put in`}</span><div>${tradeItems(tr.got)}</div></div></div>
      ${done ? '' : html`<p class="small muted">Nothing changed hands.</p>`}</div>`;
  };
  const pager = r.pages > 1 ? html`<div class="pager">
      <a class="btn small ${r.page === 1 ? 'disabled' : ''}" href="#/trades/past/${Math.max(1, r.page - 1)}" aria-label="Newer trades">&lsaquo; Newer</a>
      <span>Page ${r.page} of ${r.pages}</span>
      <a class="btn small ${r.page === r.pages ? 'disabled' : ''}" href="#/trades/past/${Math.min(r.pages, r.page + 1)}" aria-label="Older trades">Older &rsaquo;</a></div>` : '';
  show(html`<h1>Trades</h1>${tradeTabs('past')}
    <p class="muted">Your finished trades, newest first: accepted, declined, cancelled and expired. Only you and the other person can see them.</p>
    ${r.trades.length ? html`<div class="past-trades">${r.trades.map(card)}</div>${pager}`
      : html`<p class="muted">No finished trades yet. Trades you send or get show up here once they're accepted, declined, cancelled or expire.</p>`}`);
}

pages.trade = async (id) => {
  if (!signedIn()) { show(html`<h1>Trade</h1>${needSignIn('trade Limited items')}`); return; }
  const [theirs, mine] = await Promise.all([pageCall('trade.inventory', { user: id }), pageCall('trade.inventory', { user: me.id })]);
  if (!theirs.ok) { show(html`<h1>Trade</h1><p class="error">${theirs.error}</p>`); return; }
  if (theirs.hidden) { show(html`<h1>Trade</h1><p class="muted">${theirs.user.name} keeps their inventory private, so you can't trade with them.</p>`); return; }
  const pickList = (items, side) => items.length ? html`<div class="trade-pick">${items.map((i) => html`<label class="choice">
      <input type="checkbox" name="${side}" value="${i.id}|${i.serial}"> ${i.name} <b>#${i.serial}</b></label>`)}</div>`
    : html`<p class="muted">No Limited items.</p>`;
  show(html`<p><a href="#/user/${theirs.user.userId}">&lt; ${theirs.user.name}</a></p><h1>Trade with ${theirs.user.name}</h1>
    <form class="trade" data-form="tradeSend"><input type="hidden" name="to" value="${theirs.user.id}">
      <div class="trade-cols"><div class="box"><h2 class="boxhead">You give</h2>${pickList(mine.ok ? mine.items : [], 'give')}</div>
        <div class="box"><h2 class="boxhead">You get</h2>${pickList(theirs.items, 'get')}</div></div>
      <p class="small muted">Up to 4 on each side. They can accept or decline; the swap only happens if you both still have everything.</p>
      <p><button class="btn green">Send trade offer</button> <span id="tradeMsg"></span></p></form>`);
};

pages.people = async () => {
  const query = new URLSearchParams(location.hash.split('?')[1] || '').get('q') || '';
  const r = await pageCall('users.search', { query });
  show(html`<h1>People</h1>
    <form class="row" data-form="peopleSearch"><input type="search" name="q" placeholder="Search by username or #number" value="${query}" style="max-width:280px">
      <button class="btn blue">Search</button></form>
    <div class="list">${r.ok ? r.users.map((u) => html`<div><a class="grow" href="#/user/${u.userId}"><b>${u.username}</b></a>${verified(u.verified)}${partner(u)}
      <span class="muted small">#${u.userId}${u.official ? ' · Guts&Bolts staff' : ''}</span></div>`) : html`<p class="error">${r.error}</p>`}</div>`);
};

// Someone's friends, who they follow, or their followers: #/user/5/friends (or /following, /followers).
const PEOPLE_TABS = [['friends', 'Friends'], ['following', 'Following'], ['followers', 'Followers']];
async function peoplePage(id, which, page) {
  const per = 60, at = Math.max(0, (Number(page) || 1) - 1);
  const r = await pageCall('people.list', { user: String(id), which, offset: at * per, limit: per });
  if (!r.ok) { show(html`<h1>Not found</h1><p class="muted">${r.error}</p>`); return; }
  const u = r.user, pages = Math.max(1, Math.ceil(r.total / per));
  const title = { friends: 'Friends', following: 'Following', followers: 'Followers' }[r.which];
  const pager = pages > 1 ? html`<p class="row">${at > 0 ? html`<a class="btn small" href="#/user/${u.userId}/${r.which}/${at}">&lt; Back</a>` : ''}
      <span class="muted small">Page ${at + 1} of ${pages}</span>
      ${at + 1 < pages ? html`<a class="btn small" href="#/user/${u.userId}/${r.which}/${at + 2}">Next &gt;</a>` : ''}</p>` : '';
  show(html`<p><a href="#/user/${u.userId}">&lt; ${u.username}'s profile</a></p>
    <h1><a href="#/user/${u.userId}">${u.username}</a>${verified(u.verified)}: ${title} (${r.total})</h1>
    <div class="tabs">${PEOPLE_TABS.map(([k, label]) => html`<a class="btn ${k === r.which ? 'blue' : ''}" href="#/user/${u.userId}/${k}">${label}</a>`)}</div>
    ${r.people.length ? html`<div class="box"><div class="friends-grid people-grid">${r.people.map((p) => html`<a class="friend" href="#/user/${p.userId}">
        <div class="friend-pic">${avatarSvg(p.avatar, 60)}</div>
        <div class="small"><span class="dot ${p.online ? 'on' : ''}"></span><span class="link">${p.username || p.name}</span>${verified(p.verified)}</div></a>`)}</div></div>${pager}`
      : html`<p class="muted">${{ friends: 'No friends yet.', following: 'Not following anyone yet.', followers: 'No followers yet.' }[r.which]}</p>`}`);
}

// Someone's inventory: #/user/5/inventory (or /inventory/limited/2 for a group and a page).
const INVENTORY_TABS = [['all', 'All'], ['accessories', 'Accessories'], ['clothing', 'Clothing'], ['faces', 'Faces'], ['bundles', 'Bundles'], ['bodyparts', 'Body Parts'], ['gear', 'Gear'], ['emotes', 'Emotes'], ['limited', 'Limiteds']];
function inventoryCard(a) {   // an item card, plus which numbered copies of a Limited they hold
  return html`<div class="inv-item">${itemCard(a)}${(a.serials || []).length ? html`<div class="small serials">${a.serials.map((n) => '#' + n).join(', ')}</div>` : ''}</div>`;
}
function inventoryHidden(u, f) {
  return html`<p class="muted small">${f === 'self' ? 'Only you can see this.' : u.username + ' keeps their inventory private.'}</p>`;
}
async function inventoryPage(id, cat, page) {
  if (!INVENTORY_TABS.some(([k]) => k === cat)) { page = cat; cat = 'all'; }
  const per = 60, at = Math.max(0, (Number(page) || 1) - 1);
  const r = await pageCall('profile.inventory', { id: String(id), cat, offset: at * per, limit: per });
  if (!r.ok) { show(html`<h1>Not found</h1><p class="muted">${r.error}</p>`); return; }
  const u = r.user, pages = Math.max(1, Math.ceil(r.total / per)), base = '#/user/' + u.userId + '/inventory/' + r.cat;
  const pager = pages > 1 ? html`<p class="row">${at > 0 ? html`<a class="btn small" href="${base}/${at}">&lt; Back</a>` : ''}
      <span class="muted small">Page ${at + 1} of ${pages}</span>
      ${at + 1 < pages ? html`<a class="btn small" href="${base}/${at + 2}">Next &gt;</a>` : ''}</p>` : '';
  const mine = signedIn() && me.id === u.id;
  show(html`<p><a href="#/user/${u.userId}">&lt; ${u.username}'s profile</a></p>
    <h1><a href="#/user/${u.userId}">${u.username}</a>${verified(u.verified)}: Inventory</h1>
    ${r.hidden ? html`<div class="box info">${u.username} keeps their inventory private.</div>` : html`
      <div class="tabs">${INVENTORY_TABS.map(([k, label]) => html`<a class="btn ${k === r.cat ? 'blue' : ''}" href="#/user/${u.userId}/inventory/${k}">${label} (${r.counts[k] || 0})</a>`)}</div>
      ${r.items.length ? html`<div class="grid">${r.items.map(inventoryCard)}</div>${pager}`
        : html`<p class="muted">${r.cat === 'all' ? 'Nothing from the catalog yet.' : 'Nothing here.'}</p>`}
      ${mine ? html`<p class="small muted">Who can see this is up to you: <a href="#/settings">Settings</a> &gt; Privacy.</p>`
        : signedIn() && r.counts.limited ? html`<p><a class="btn small" href="#/trade/${u.id}">Trade with ${u.username}</a></p>` : ''}`}`);
}

pages.user = async (id, tab, page, more) => {
  if (tab === 'friends' || tab === 'following' || tab === 'followers') { await peoplePage(id, tab, page); return; }
  if (tab === 'inventory') { await inventoryPage(id, page, more); return; }
  const r = await pageCall('profile', { id });
  if (!r.ok) {
    if (r.code === 'USER_DELETED' || /USER_DELETED/.test(r.error || '')) {
      show(html`<div class="box" role="status"><h1>[ Content Deleted ]</h1><p class="muted">This user has been deleted.</p><p class="small muted">Error Code: USER_DELETED</p></div>`);
    } else show(html`<h1>Not found</h1><p class="muted">${r.error}</p>`);
    return;
  }
  const u = r.user;
  const f = r.friendship;
  // People are known by their user number: show #/user/5, not the long account key.
  if (u.userId > 0 && String(id) !== String(u.userId)) history.replaceState(null, '', '#/user/' + u.userId);
  const friendBtn = f === 'self' ? '' : !signedIn() ? html`<button class="btn green small" data-act="friend" data-op="friends.add" data-user="${u.id}">Add friend</button>`
    : f === 'friends' ? html`<button class="btn small" data-act="friend" data-op="friends.remove" data-user="${u.id}">Unfriend</button>`
      : f === 'sent' ? html`<button class="btn small" data-act="friend" data-op="friends.cancel" data-user="${u.id}">Cancel request</button>`
        : f === 'received' ? html`<button class="btn green small" data-act="friend" data-op="friends.accept" data-user="${u.id}">Accept friend request</button>`
          : html`<button class="btn green small" data-act="friend" data-op="friends.add" data-user="${u.id}">Add friend</button>`;
  // Following: one way, no asking (older servers don't know it: no button then).
  const followBtn = f === 'self' || r.followerCount === undefined ? ''
    : r.isFollowing ? html` <button class="btn small" data-act="follow" data-op="follow.remove" data-user="${u.id}">Unfollow</button>`
      : html` <button class="btn blue small" data-act="follow" data-op="follow.add" data-user="${u.id}">Follow</button>`;
  const messageBtn = f === 'self' ? '' : html` <a class="btn small" href="#/messages/new?to=${u.userId || u.id}">Send Message</a>`;
  const games = r.creations.filter((a) => a.kind === 'game'), items = r.creations.filter((a) => WEARABLE.includes(a.kind));
  // What they wear (older servers don't say: look it up in the catalog).
  let worn = r.wearing;
  if (!worn) {
    const ids = u.avatar && Array.isArray(u.avatar.wearing) ? u.avatar.wearing : [];
    worn = ids.length ? ((await pageCall('list', { kind: 'clothing', limit: 100 })).assets || []).filter((a) => ids.includes(a.id)) : [];
  }
  const friends = r.friends || [];
  const badgeNames = { admin: 'Administrator', verified: 'Verified', staff: 'Staff', tester: 'Tester', bughunter: 'Bug Hunter', featured: 'Featured Creator', partner: 'Partner' };
  const online = r.online === undefined ? null : r.online;
  show(html`<div class="profile-head">
      <h1>${u.username}${verified(u.verified)}${partner(u)}</h1><span class="small muted user-number">#${u.userId}</span>
      ${online === null ? '' : html`<span class="presence ${online ? 'on' : ''}">${online ? '[ Online ]' : '[ Offline ]'}</span>`}
      ${r.playing ? html`<span class="small playing-now">Playing <a href="#/game/${r.playing.game}">${r.playing.title}</a></span>
        ${r.playing.session && f !== 'self' ? html` <button class="btn green small" data-act="joinServer" data-id="${r.playing.game}"
            data-name="${r.playing.title}" data-server="${r.playing.session}"${r.playing.full ? raw(' disabled title="That server is full"') : ''}>Join</button>` : ''}` : ''}
      <span class="grow"></span>${r.blocked ? '' : html`${friendBtn}${followBtn}${messageBtn}`}${f === 'self' ? html` <a class="btn small" href="#/avatar">Edit avatar</a>`
        : signedIn() && !r.blocked ? html` <a class="btn small" href="#/trade/${u.id}">Trade</a>` : ''}
      ${f === 'self' || r.blocked === undefined ? '' : html` <button class="btn small" data-act="block" data-user="${u.id}" data-name="${u.username || u.name}"
          data-on="${r.blocked ? '' : '1'}">${r.blocked ? 'Unblock' : 'Block'}</button> ${reportLink('user', u.id, u.username || u.name)}`}</div>
    ${r.blocked ? html`<div class="box info">You blocked ${u.username || u.name}. You can't message, friend, follow, trade with or join each other.</div>` : ''}
    ${(u.pastNames || []).length ? html`<p class="small muted past-names">Past usernames: ${u.pastNames.join(', ')}</p>` : ''}
    <div class="profile">
      <div class="profile-left">
        <div class="box avatar-box"><div id="profileAvatar">${avatarSvg(u.avatar, 200, worn)}</div>
          <div class="small muted">Drag to turn</div></div>
        <div class="box"><h2 class="boxhead">Currently Wearing</h2>
          ${worn.length ? html`<div class="wearing">${worn.map((it) => html`<a class="card square" href="#/item/${it.id}" title="${it.name}">
              <div class="pic">${itemIcon(it)}</div><div class="name small">${it.name}</div></a>`)}</div>`
            : html`<p class="muted small">Nothing from the catalog${u.avatar && u.avatar.hat ? ' (just a hat)' : ''}.</p>`}</div>
        <div class="box"><h2 class="boxhead">Statistics</h2>
          <table class="stats"><tr><td>Joined</td><td>${new Date(u.created * 1000).toLocaleDateString()} (${ago(u.created)})</td></tr>
            <tr><td>User number</td><td>#${u.userId}</td></tr>
            <tr><td>Friends</td><td><a href="#/user/${u.userId}/friends">${r.friendCount}</a></td></tr>
            ${r.followerCount === undefined ? '' : html`<tr><td>Followers</td><td><a href="#/user/${u.userId}/followers">${r.followerCount}</a></td></tr>
            <tr><td>Following</td><td><a href="#/user/${u.userId}/following">${r.followingCount}</a></td></tr>`}
            ${r.placeVisits !== undefined ? html`<tr><td>Place visits</td><td>${r.placeVisits}</td></tr>` : ''}
            <tr><td>Games made</td><td>${games.length}</td></tr></table>
          ${u.official ? html`<p><b>Guts&amp;Bolts staff</b></p>` : ''}${u.banned ? html`<p class="error">Banned${u.banReason ? ': ' + (BAN_REASONS.find((r) => r[0] === u.banReason) || ['', ''])[1] : ''}</p>` : ''}</div>
        ${(u.badges || []).length ? html`<div class="box"><h2 class="boxhead">Guts&amp;Bolts Badges</h2>
          <p class="small muted">Given by Guts&amp;Bolts staff.</p><div class="row">
          ${u.badges.map((b) => html`<span class="badge-pill">${badgeNames[b] || b}</span>`)}</div></div>` : ''}
        ${r.allPlayerBadges ? html`<div class="box"><h2 class="boxhead">Player Badges (${(r.playerBadges || []).length})</h2>
          <div class="badge-grid">${r.allPlayerBadges.map((b) => {
            const got = (r.playerBadges || []).some((x) => x.key === b.key);
            return html`<span class="game-badge player-badge ${got ? '' : 'locked'}" title="${b.name}: ${got ? 'earned!' : b.need}">${playerBadgeIcon(b.key, 44)}<span>${b.name}</span></span>`;
          })}</div></div>` : ''}
        <div class="box"><h2 class="boxhead">Game Badges (${(r.gameBadges || []).length})</h2>
          ${(r.gameBadges || []).length ? html`<div class="badge-grid">${r.gameBadges.slice(0, 24).map((b) => html`<a class="game-badge" href="#/game/${b.game}"
              title="${b.name}${b.description ? ': ' + b.description : ''} (${b.gameName})">${gameBadgeIcon(b, 48)}<span>${b.name}</span></a>`)}</div>`
            : html`<p class="muted small">No badges from games yet.</p>`}</div>
      </div>
      <div class="profile-right">
        <div class="box about"><h2 class="boxhead">About ${u.username || u.name}</h2>
          ${r.status ? html`<p class="status-line"><b>Right now:</b> <i>"${r.status.text}"</i> <span class="muted small">${ago(r.status.at)}</span></p>` : ''}
          ${r.blurb ? html`<p class="blurb">${r.blurb}</p>` : html`<p class="muted small">${f === 'self' ? 'Tell people about yourself below.' : 'Nothing here yet.'}</p>`}
          ${f === 'self' && r.blurb !== undefined ? html`<details class="edit-about"><summary class="btn small">Edit</summary>
            <form class="form" data-form="statusSet"><label>Right now I'm...</label>
              <div class="row"><input type="text" name="status" maxlength="140" placeholder="building a castle"><button class="btn blue small">Update</button></div></form>
            <form class="form" data-form="blurbSet"><label>About me</label><textarea name="blurb" maxlength="1000" rows="4">${r.blurb}</textarea>
              <p><button class="btn green small">Save</button></p></form></details>` : ''}</div>
        <div class="box"><h2 class="boxhead">Friends (${r.friendCount}) <a class="small" href="#/user/${u.userId}/friends" style="float:right">See all</a></h2>
          ${friends.length ? html`<div class="friends-grid">${friends.map((p) => html`<a class="friend" href="#/user/${p.userId}">
              <div class="friend-pic" data-friend-avatar="${p.id}">${avatarSvg(p.avatar, 60)}</div>
              <div class="small"><span class="dot ${p.online ? 'on' : ''}"></span><span class="link">${p.username || p.name}</span></div></a>`)}</div>`
            : html`<p class="muted small">${f === 'self' ? 'No friends yet. Find people on the People page!' : 'No friends yet.'}</p>`}</div>
        <div class="box"><h2 class="boxhead">Games</h2>
          ${games.length ? html`<div class="grid">${games.map(gameCard)}</div>` : html`<p class="muted small">None yet.</p>`}</div>
        ${items.length ? html`<div class="box"><h2 class="boxhead">Creations</h2><div class="grid">${items.map(itemCard)}</div></div>` : ''}
        ${r.inventory ? html`<div class="box"><h2 class="boxhead">Inventory${r.inventory.hidden ? '' : ` (${r.inventory.total})`}
            ${r.inventory.hidden ? '' : html`<a class="small" href="#/user/${u.userId}/inventory" style="float:right">See all</a>`}</h2>
          ${r.inventory.hidden ? inventoryHidden(u, f) : r.inventory.items.length ? html`<div class="grid">${r.inventory.items.map(inventoryCard)}</div>`
            : html`<p class="muted small">Nothing from the catalog yet.</p>`}</div>` : ''}
        <div class="box"><h2 class="boxhead">Groups</h2>
          ${r.groups.length ? html`<div class="list">${r.groups.map((g) => html`<div><a class="grow" href="#/group/${g.id}">${g.name}</a>
            <span class="muted small">${g.role}</span></div>`)}</div>` : html`<p class="muted small">None yet.</p>`}</div>
      </div>
    </div>`);
  // The 3D avatar (the flat one stays if the browser can't do 3D).
  mountAvatar($('#profileAvatar'), u.avatar, worn, { width: 220 }).catch(() => {});
  for (const p of friends) {
    avatarPicture(p.avatar, p.wearing || [], 60).then((url) => {
      const box = view.querySelector(`[data-friend-avatar="${p.id}"]`);
      if (url && box) box.innerHTML = html`<img src="${url}" alt="" width="60" height="75">`.s;
    }).catch(() => {});
  }
};

pages.friends = async () => {
  const r = signedIn() ? await pageCall('friends.list', {}) : { ok: true, friends: [], incoming: [], outgoing: [] };
  if (!r.ok) { show(html`<h1>Friends</h1><p class="error">${r.error}</p>`); return; }
  const person = (p) => html`<a class="grow" href="#/user/${p.id}"><b>${p.name}</b></a>${verified(p.verified)}`;
  show(html`<h1>Friends</h1>
    ${r.incoming.length ? html`<h2>Friend requests</h2><div class="list">${r.incoming.map((p) => html`<div>${person(p)}
      <button class="btn green small" data-act="friend" data-op="friends.accept" data-user="${p.id}">Accept</button>
      <button class="btn small" data-act="friend" data-op="friends.decline" data-user="${p.id}">Decline</button></div>`)}</div>` : ''}
    <h2>My friends (${r.friends.length})</h2>
    ${r.friends.length ? html`<div class="list">${r.friends.map((p) => html`<div><span class="dot ${p.online ? 'on' : ''}"></span>${person(p)}
      <span class="muted small">${p.playing ? 'Playing ' + p.playing.title : p.online ? 'Online' : 'Offline'}</span>
      <button class="btn small" data-act="friend" data-op="friends.remove" data-user="${p.id}">Unfriend</button></div>`)}</div>`
      : signedIn() ? html`<p class="muted">No friends yet. Find people on the <a href="#/people">People</a> page!</p>`
        : html`<p class="muted">Your friends show up here once you <a href="#/login">log in</a>.
          Meanwhile, look around the <a href="#/people">People</a> page!</p>`}
    ${r.outgoing.length ? html`<h2>Waiting for them</h2><div class="list">${r.outgoing.map((p) => html`<div>${person(p)}
      <button class="btn small" data-act="friend" data-op="friends.cancel" data-user="${p.id}">Cancel</button></div>`)}</div>` : ''}`);
};

pages.groups = async () => {
  const query = new URLSearchParams(location.hash.split('?')[1] || '').get('q') || '';
  const [r, mine] = await Promise.all([pageCall('groups.list', { query }), signedIn() ? pageCall('groups.mine', {}) : { ok: false }]);
  const row = (g) => html`<div><span style="width:14px;height:14px;border-radius:3px;background:#${Number(g.color).toString(16).padStart(6, '0')}"></span>
    <a class="grow" href="#/group/${g.id}"><b>${g.name}</b></a><span class="muted small">${g.members} members${g.open ? '' : ' · invite only'}</span></div>`;
  show(html`<h1>Groups</h1>
    ${mine.ok && mine.groups.length ? html`<h2>My groups</h2><div class="list">${mine.groups.map(row)}</div>` : ''}
    <h2>Find groups</h2>
    <form class="row" data-form="groupSearch"><input type="search" name="q" placeholder="Search groups" value="${query}" style="max-width:280px">
      <button class="btn blue">Search</button></form>
    <div class="list">${r.ok ? r.groups.map(row) : html`<p class="error">${r.error}</p>`}</div>
    <h2>Make a group</h2>
    ${signedIn() ? html`<form class="form" data-form="groupCreate">
      <label>Name</label><input type="text" name="name" maxlength="40" required>
      <label>Description</label><textarea name="description" maxlength="1000"></textarea>
      <label>Colour</label><input type="color" name="color" value="#3a7bd5">
      <label><input type="checkbox" name="open" checked> Anyone can join (untick: people ask to join)</label>
      <p><button class="btn green">Make it${me.verified ? ' (free)' : ' for 50 Bolts'}</button></p></form>` : html`<form class="form" data-form="groupCreate">
      <label>Name</label><input type="text" name="name" maxlength="40">
      <p><button class="btn green">Make a group</button></p></form>`}`);
};

// What each group permission lets a rank do (the server checks the same).
const GROUP_PERMS = [['shout', 'Shout'], ['manage', 'Let people in, remove people, delete wall posts, change settings'],
  ['ranks', 'Change people\'s ranks'], ['games', 'Add their games to the group'], ['funds', 'See the group\'s Bolts and pay people']];

pages.group = async (id) => {
  const r = await pageCall('groups.get', { id });
  if (!r.ok) { show(html`<h1>Group not found</h1><p class="muted">${r.error}</p>`); return; }
  const g = r.group, role = r.myRole, mine = r.myRank, owner = !!mine && mine.id === 'Owner';
  const can = (perm) => !!mine && (owner || mine.perms.includes(perm));
  const myLevel = mine ? mine.level : 0;
  const color = '#' + Number(g.color).toString(16).padStart(6, '0');
  const join = !signedIn() ? html`<button class="btn green" data-act="group" data-op="groups.join" data-id="${g.id}">${g.open ? 'Join' : 'Ask to join'}</button>` : role ? (owner ? '' : html`<button class="btn small" data-act="group" data-op="groups.leave" data-id="${g.id}">Leave</button>`)
    : r.requested ? html`<span class="muted">You asked to join.</span>`
      : html`<button class="btn green" data-act="group" data-op="groups.join" data-id="${g.id}">${g.open ? 'Join' : 'Ask to join'}</button>`;
  // Ranks someone can be given by me: below my level (the owner can give any but Owner).
  const giveable = r.ranks.filter((x) => x.id !== 'Owner' && (owner || x.level < myLevel));
  const memberRow = (u) => {
    const below = u.level < myLevel && u.id !== me.id && u.rank !== 'Owner';
    return html`<div><a class="grow" href="#/user/${u.id}">${u.name}</a>${verified(u.verified)}
      ${can('ranks') && below ? html`<form class="row inline" data-form="groupRank"><input type="hidden" name="id" value="${g.id}">
        <input type="hidden" name="user" value="${u.id}"><select name="rank">${giveable.map((x) => html`<option value="${x.id}" ${x.id === u.rank ? 'selected' : ''}>${x.name}</option>`)}</select>
        <button class="btn small">Set</button></form>` : html`<span class="muted small">${u.role}</span>`}
      ${owner && u.rank !== 'Owner' ? html`<button class="btn small" data-act="group" data-op="groups.member" data-id="${g.id}" data-user="${u.id}" data-action="owner"
        data-confirm="Make ${u.name} the owner of ${g.name}? You can't undo this yourself.">Make owner</button>` : ''}
      ${can('manage') && below ? html`
        <button class="btn small red" data-act="group" data-op="groups.member" data-id="${g.id}" data-user="${u.id}" data-action="kick">Remove</button>` : ''}</div>`;
  };
  const rankForm = (x) => html`<form class="form rank-edit" data-form="groupRankSave"><input type="hidden" name="id" value="${g.id}">
    <input type="hidden" name="rank" value="${x ? x.id : ''}">
    <div class="row"><input type="text" name="name" maxlength="24" placeholder="Rank name" value="${x ? x.name : ''}" required style="max-width:200px">
      ${x && (x.id === 'Owner' || x.id === 'Member') ? html`<span class="muted small">level ${x.level}</span><input type="hidden" name="level" value="${x.level}">`
        : html`<label class="small">Level <input type="number" name="level" min="2" max="254" value="${x ? x.level : ''}" required style="width:70px"></label>`}</div>
    ${x && x.id === 'Owner' ? html`<p class="small muted">The owner can do everything.</p>`
      : html`<div class="perm-picks">${GROUP_PERMS.map(([k, label]) => html`<label class="choice small"><input type="checkbox" name="perm" value="${k}" ${x && x.perms.includes(k) ? 'checked' : ''}> ${label}</label>`)}</div>`}
    <p><button class="btn small blue">${x ? 'Save' : 'Add rank'}</button>
      ${x && x.id !== 'Owner' && x.id !== 'Member' ? html` <button type="button" class="btn small red" data-act="group" data-op="groups.rankDelete" data-id="${g.id}" data-rank="${x.id}"
        data-confirm="Delete the ${x.name} rank? Everyone in it becomes a ${r.ranks.find((y) => y.id === 'Member').name}.">Delete</button>` : ''}</p></form>`;
  show(html`<p><a href="#/groups">&lt; Groups</a></p>
    <div class="banner-color" style="background:${color}"></div>
    <div class="box" style="border-top:0;border-radius:0 0 6px 6px"><h1>${g.name}</h1>
      <p class="muted">Owned by <a href="#/user/${g.owner}">${g.ownerName}</a>${verified(g.ownerVerified)} · ${g.members} members
        ${role ? html` · your rank: <b>${role}</b>` : ''}</p>
      <p style="white-space:pre-wrap">${g.description}</p>${join}${owner ? '' : html` ${reportLink('group', g.id, g.name)}`}</div>
    ${lyretoCard(g.lyreto, g.name)}
    ${r.shoutInfo && r.shoutInfo.text ? html`<div class="box info"><b>${r.shoutInfo.name}:</b> ${r.shoutInfo.text}
      <span class="small muted">· ${ago(r.shoutInfo.time)}</span></div>` : ''}
    ${can('shout') ? html`<form class="row" data-form="groupShout"><input type="hidden" name="id" value="${g.id}">
      <input type="text" name="text" maxlength="200" placeholder="Shout something to the whole group" style="max-width:420px">
      <button class="btn blue">Shout</button></form>` : ''}
    ${r.games.length || can('games') ? html`<h2>Games</h2>
      ${r.games.length ? html`<div class="grid">${r.games.map((x) => html`<div class="group-game">${gameCard(x)}
        ${owner || (can('games') && x.creator === me.id) ? html`<button class="btn small" data-act="group" data-op="groups.removeGame" data-id="${g.id}" data-game="${x.id}"
          data-confirm="Take ${x.name} out of ${g.name}? Its sales go back to its creator.">Take out</button>` : ''}</div>`)}</div>`
        : html`<p class="muted">No games yet.</p>`}
      ${can('games') ? html`<p class="small muted">To add one of your games, open its Configure page and pick this group under "Group". Its game pass and product sales then go to the group's Bolts.</p>` : ''}` : ''}
    ${r.funds !== undefined ? html`<h2>Group Bolts</h2><div class="box">
      <p class="big-bolts">${bolts(r.funds)}</p>
      <p class="small muted">Sales from the group's games go here. You pay members from it.</p>
      ${can('funds') ? html`<form class="row" data-form="groupPayout"><input type="hidden" name="id" value="${g.id}">
        <select name="user">${r.memberList.map((u) => html`<option value="${u.id}">${u.name}</option>`)}</select>
        <input type="number" name="amount" min="1" max="${Math.max(1, r.funds)}" placeholder="Bolts" required style="width:100px">
        <button class="btn green" ${r.funds > 0 ? '' : 'disabled'}>Pay</button></form>` : ''}
      ${r.ledger.length ? html`<table class="ledger">${r.ledger.map((e) => html`<tr><td class="${e.amount < 0 ? 'neg' : 'pos'}">${e.amount > 0 ? '+' : ''}${e.amount}</td>
        <td>${e.reason}</td><td class="muted small">${ago(e.at)}</td></tr>`)}</table>` : html`<p class="muted small">Nothing yet.</p>`}</div>` : ''}
    <h2>Wall</h2>
    ${role ? html`<form class="row" data-form="groupPost"><input type="hidden" name="id" value="${g.id}">
      <input type="text" name="text" maxlength="300" placeholder="Say something" style="max-width:420px" required>
      <button class="btn blue">Post</button></form>` : ''}
    <div class="wall">${r.wall.length ? r.wall.slice().reverse().map((p) => html`<div><a href="#/user/${p.id}"><b>${p.name}</b></a>${verified(p.verified)}
      <span class="small muted">${ago(p.time)}</span>
      ${signedIn() && (p.id === me.id || can('manage')) ? html`<button class="btn small" data-act="group" data-op="groups.deletePost" data-id="${g.id}"
        data-by="${p.id}" data-time="${p.time}">Delete</button>` : ''}<br>${p.text}</div>`) : html`<p class="muted">Nothing on the wall yet.</p>`}</div>
    ${can('manage') && r.requests && r.requests.length ? html`<h2>Waiting to join</h2><div class="list">${r.requests.map((u) => html`<div>
      <a class="grow" href="#/user/${u.id}">${u.name}</a>
      <button class="btn green small" data-act="group" data-op="groups.request" data-id="${g.id}" data-user="${u.id}" data-accept="1">Let in</button>
      <button class="btn small" data-act="group" data-op="groups.request" data-id="${g.id}" data-user="${u.id}">Decline</button></div>`)}</div>` : ''}
    <h2>Members</h2><div class="list">${r.memberList.map(memberRow)}</div>
    ${owner ? html`<h2>Ranks</h2><p class="small muted">Higher levels are more in charge. People can only change the rank of people below them.
      A group can have up to 10 ranks.</p>
      <div class="ranks">${r.ranks.map(rankForm)}${r.ranks.length < 10 ? html`<h3>New rank</h3>${rankForm(null)}` : ''}</div>` : ''}
    ${can('manage') ? html`<h2>Settings</h2><form class="form" data-form="groupEdit"><input type="hidden" name="id" value="${g.id}">
      <label>Description</label><textarea name="description" maxlength="1000">${g.description}</textarea>
      <label>Colour</label><input type="color" name="color" value="${color}">
      <label>Lyreto server</label><input type="url" name="lyreto" maxlength="200" placeholder="https://lyreto.com/..." value="${g.lyreto ? g.lyreto.link : ''}">
      <p class="small muted">${LYRETO_FIELD_NOTE}</p>
      <label><input type="checkbox" name="open" ${g.open ? 'checked' : ''}> Anyone can join</label>
      <p><button class="btn blue">Save</button>
      ${owner ? html` <button type="button" class="btn red" data-act="group" data-op="groups.delete" data-id="${g.id}">Delete group</button>` : ''}</p></form>` : ''}`);
};

let avatarDraft = null;   // the avatar being edited (saved with the Save button)
let avatarOwned = [];     // the clothes we own (on the Avatar page), so Wear knows each one's kind
let avatarTab = 'body';   // which section of the Avatar page is open
// The Avatar page's sections: [key, name]. Each shows some of what you own.
const AVATAR_TABS = [['body', 'Body'], ['clothing', 'Clothing'], ['hats', 'Hats'], ['hair', 'Hair'], ['accessories', 'Accessories'],
  ['gear', 'Gear'], ['emotes', 'Emotes'], ['outfits', 'Outfits']];
// Bundle pieces: each part of a body bundle can be worn on its own, like a single body part.
// Its id is "<bundle id>-<part>" ("137-leftarm"); the server (worker/server.js wearableJson)
// makes the same item, and /wear/<that id> sends just that part's mesh.
const BODY_PART_ORDER = ['Head', 'Torso', 'Left Arm', 'Right Arm', 'Left Leg', 'Right Leg'];
const bundlePartNames = new Map();   // body bundle id -> Promise of the parts it has (from /wear/)
function bundlePieces(b) {
  if (!(b && b.kind === 'bundle' && b.meta && b.meta.body)) return Promise.resolve([]);
  if (!bundlePartNames.has(b.id)) {
    bundlePartNames.set(b.id, fetch('/wear/' + encodeURIComponent(b.id)).then((r) => (r.ok ? r.json() : null))
      .then((j) => BODY_PART_ORDER.filter((p) => j && j.body && j.body[p])).catch(() => []));
  }
  return bundlePartNames.get(b.id).then((parts) => parts.map((part) => Object.assign({}, b, {
    id: b.id + '-' + part.replace(' ', '').toLowerCase(), kind: 'bodypart', name: b.name + ': ' + part, price: 0,
    limited: undefined, auction: undefined, meta: { bodyPart: part, bundle: b.id } })));
}

// Private messages, like the old Roblox inbox: Inbox, Sent, and writing a new one.
pages.messages = async (box = 'inbox') => {
  if (!signedIn()) { show(html`<h1>Messages</h1>${needSignIn('read and send messages')}`); return; }
  const tabs = html`<div class="tabs"><a class="btn ${box === 'inbox' ? 'blue' : ''}" href="#/messages">Inbox${me.unreadMessages ? html` (${me.unreadMessages})` : ''}</a>
    <a class="btn ${box === 'sent' ? 'blue' : ''}" href="#/messages/sent">Sent</a>
    <a class="btn ${box === 'new' ? 'blue' : ''}" href="#/messages/new">New message</a></div>`;
  if (box === 'new') {
    const q = new URLSearchParams(location.hash.split('?')[1] || '');
    const to = q.get('to') || '', re = q.get('re') || '';
    let toName = '';
    if (to) { const p = await pageCall('profile', { id: to }); if (p.ok) toName = p.user.name; }
    show(html`<h1>Messages</h1>${tabs}
      <div class="box"><h2 class="boxhead">New message</h2>
        <form class="form" data-form="sendMessage">
          <label>To <span class="muted small">(their user number, like #5, or their username)</span></label>
          <input type="text" name="to" required maxlength="40" value="${to ? (/^[0-9]+$/.test(to) ? '#' + to : to) : ''}" style="max-width:220px">
          ${toName ? html`<p class="small">Sending to <b>${toName}</b></p>` : ''}
          <label>Subject</label><input type="text" name="subject" maxlength="80" value="${re ? (re.startsWith('Re: ') ? re : 'Re: ' + re) : ''}">
          <label>Message</label><textarea name="body" maxlength="2000" rows="8" required></textarea>
          <p class="small muted">Be nice. Messages follow the same <a href="#/terms">rules</a> as chat, and staff can see reported ones.</p>
          <p><button class="btn green">Send</button> <span id="sendMsg"></span></p></form></div>`);
    return;
  }
  const sent = box === 'sent';
  const r = await pageCall('message.list', { box: sent ? 'sent' : 'inbox' });
  if (r.ok && r.me) setMe(r.me);
  const list = r.ok ? r.messages : [];
  const row = (m) => {
    const other = sent ? m.to : m.from;
    return html`<details class="message ${m.read ? '' : 'unread'}" data-message="${m.id}" data-box="${sent ? 'sent' : 'inbox'}" ${m.read ? '' : raw('data-unread="1"')}>
      <summary><span class="message-who">${sent ? 'To ' : ''}<a href="#/user/${other.userId || other.id}" class="link">${other.name}</a>${verified(other.verified)}</span>
        <span class="message-subject">${m.subject}</span><span class="message-when small muted">${ago(m.at)}</span></summary>
      <div class="message-body">${m.body}</div>
      <p class="message-tools">${sent ? '' : html`<a class="btn small blue" href="#/messages/new?to=${other.userId || other.id}&re=${encodeURIComponent(m.subject)}">Reply</a> `}
        <button class="btn small red" data-act="deleteMessage" data-id="${m.id}" data-box="${sent ? 'sent' : 'inbox'}">Delete</button>
        ${sent ? '' : html` <button class="btn small" data-act="report" data-kind="message" data-id="${m.id}" data-name="${other.name}"
          data-user="${other.id}">Report</button>`}</p></details>`;
  };
  show(html`<h1>Messages</h1>${tabs}
    ${!r.ok ? html`<p class="error">${r.error}</p>` : list.length ? html`<div class="messages">${list.map(row)}</div>`
      : html`<p class="muted">${sent ? 'You haven\'t sent any messages yet.' : 'No messages yet. When someone sends you one, it shows up here.'}</p>`}
    <p class="small muted">Change who can send you messages in <a href="#/settings">Settings</a>.</p>`);
  // Opening an unread message marks it read.
  view.querySelectorAll('details[data-unread]').forEach((d) => d.addEventListener('toggle', async () => {
    if (!d.open || !d.dataset.unread) return;
    delete d.dataset.unread;
    d.classList.remove('unread');
    const res = await call('message.read', { id: d.dataset.message, box: d.dataset.box });
    if (res.ok) setMe(res.me);
  }));
};

pages.avatar = async () => {
  const [r, outfitsR] = signedIn() ? await Promise.all([pageCall('list', { kind: 'clothing', owned: true, limit: 100 }), pageCall('outfit.list', {})])
    : [{ ok: true, assets: [] }, { ok: true, outfits: [] }];
  const ownedAll = (r.ok ? r.assets : []).filter((a) => (me.owned || []).includes(a.id));
  const pieces = (await Promise.all(ownedAll.map(bundlePieces))).flat();
  const owned = ownedAll.filter((a) => !isEmoteItem(a.kind)).concat(pieces);
  avatarOwned = owned;
  const myEmotes = ownedAll.filter((a) => a.kind === 'emote'), myPacks = ownedAll.filter((a) => a.kind === 'animpack');
  const slots = signedIn() ? (me.emotes || []) : [];
  const emoteBox = html`<h2>Emotes</h2>
    <p class="small muted">Your emote slots: press B in a game (or tap the smiley on a phone) to pick one. Slots left empty show the
      built-in emotes (dance, wave, point, cheer, laugh). Moving or jumping stops an emote.</p>
    ${signedIn() ? html`<div class="grid emote-grid">${Array.from({ length: kEmoteSlots }, (_, i) => {
      const it = myEmotes.find((x) => x.id === slots[i]);
      return html`<div class="card square"><div class="pic">${it ? itemIcon(it) : html`<span class="item-render no-picture">Slot ${i + 1}</span>`}</div>
        <div class="name">${it ? it.name : 'Empty'}</div>
        <select data-emote-slot="${i + 1}" aria-label="Emote in slot ${i + 1}"><option value="">(empty)</option>
          ${myEmotes.map((x) => html`<option value="${x.id}" ${x.id === slots[i] ? 'selected' : ''}>${x.name}</option>`)}</select></div>`;
    })}</div>
    ${myEmotes.length ? '' : html`<p class="muted small">You don't have any emotes yet. Get some in the <a href="#/catalog?cat=emotes">Catalog</a>!</p>`}
    <h2>Animation pack</h2>
    ${myPacks.length ? html`<div class="row">
        <button class="btn small ${me.animPack ? '' : 'blue'}" data-act="animpackEquip" data-id="">Normal</button>
        ${myPacks.map((x) => html`<button class="btn small ${me.animPack === x.id ? 'blue' : ''}" data-act="animpackEquip" data-id="${x.id}">${x.name}</button>`)}</div>`
      : html`<p class="muted small">No animation packs yet. They change how you walk, run and jump: see the <a href="#/catalog?cat=animpack">Catalog</a>.</p>`}`
    : html`<p class="muted small">Log in to pick your emotes.</p>`}`;
  if (!avatarDraft) avatarDraft = Object.assign(defaultAvatar(), JSON.parse(JSON.stringify(me.avatar || {})));
  const a = avatarDraft;
  const wornItems = owned.filter((it) => a.wearing.includes(it.id));
  const ownedHats = owned.filter((it) => it.kind === 'hat');
  const wornHatCount = ownedHats.filter((it) => a.wearing.includes(it.id)).length;
  const outfits = outfitsR.ok ? outfitsR.outfits : [];
  const outfitBox = html`<h2>My outfits</h2>
    ${signedIn() ? html`<form class="row" data-form="outfitSave"><input type="text" name="name" maxlength="40" placeholder="Name this outfit" style="max-width:200px">
      <button class="btn green small">Save what I'm wearing as an outfit</button></form>` : html`<p class="muted small">Log in to save outfits.</p>`}
    ${outfits.length ? html`<div class="grid outfits">${outfits.map((o) => html`<div class="card square outfit">
        <div class="pic" data-outfit="${o.id}"><span class="item-render loading"></span></div>
        <div class="name">${o.name}</div>
        <div class="outfit-buttons"><button class="btn small green" data-act="outfit" data-op="outfit.wear" data-id="${o.id}">Wear</button>
          <button class="btn small" data-act="outfit" data-op="outfit.rename" data-id="${o.id}" data-name="${o.name}">Rename</button>
          <button class="btn small red" data-act="outfit" data-op="outfit.delete" data-id="${o.id}" data-name="${o.name}">Delete</button></div></div>`)}</div>`
      : signedIn() ? html`<p class="muted small">No outfits yet. Dress up, then save the look here so you can wear it again in one click.</p>` : ''}`;
  // One item you own, with its Wear button.
  const card = (it, label = 'Wear') => html`<div class="card square">
      <div class="pic" style="${a.wearing.includes(it.id) ? 'outline:3px solid #16a34a' : ''}">${itemIcon(it)}</div>
      <div class="name">${it.name}</div>
      <button class="btn small ${a.wearing.includes(it.id) ? 'green' : ''}" data-act="avatarWear" data-id="${it.id}" data-kind="${it.kind}">
        ${a.wearing.includes(it.id) ? 'Wearing' : label}</button></div>`;
  const grid = (list) => html`<div class="grid">${list.map((it) => card(it))}</div>`;
  const none = (cat) => html`<p class="muted">Nothing here yet. Find some in the <a href="#/catalog${cat ? '?cat=' + cat : ''}">Catalog</a>.</p>`;
  // A group inside a section: its own small heading, or nothing if you own none of them.
  const group = (title, list) => (list.length ? html`<h3>${title}</h3>${grid(list)}` : '');
  const ofKind = (...kinds) => owned.filter((it) => kinds.includes(it.kind));
  const bodyParts = ofKind('bodypart');
  const clothing = ofKind('shirt', 'tshirt', 'pants'), hair = ofKind('hair'), accessories = ofKind('faceacc', 'neck', 'shoulder', 'waist');
  const gear = ofKind('gear'), myGear = signedIn() ? (me.gear || []) : [];
  const sections = {
    body: html`<h2 style="margin-top:0">Colours</h2>
      <div class="row">${PRESETS.map(([name], i) => html`<button class="btn small" data-act="avatarPreset" data-i="${i}">${name}</button>`)}</div>
      <div class="row" style="margin-top:10px">${PARTS.map((p) => html`<label style="margin:0 12px 0 0;font-weight:normal">
        <input type="color" data-avatar-part="${p}" value="${hexOf(a[p])}"> ${PART_NAMES[p]}</label>`)}</div>
      ${a.hat ? html`<p class="row small muted" style="margin-top:14px">You still have an old built-in hat on (from before the catalog).
        <button class="btn small" data-act="avatarHat" data-i="0">Take it off</button></p>` : ''}
      <h2>Bundles</h2>
      ${ofKind('bundle').length ? grid(ofKind('bundle')) : none('bundle')}
      <h2>Body parts</h2>
      <p class="small muted">Wear one head, torso, arm or leg at a time. Each part of a bundle's body is here on its own too, so you can mix parts from different bundles.</p>
      ${bodyParts.length ? BODY_PART_ORDER.map((part) => group(part, bodyParts.filter((it) => it.meta && it.meta.bodyPart === part)))
        : none('bodypart')}
      <h2>Faces</h2>
      ${ofKind('face').length ? grid(ofKind('face')) : none('face')}`,
    clothing: clothing.length ? html`${group('Shirts', ofKind('shirt'))}${group('T-Shirts', ofKind('tshirt'))}${group('Pants', ofKind('pants'))}`
      : none('clothes'),
    hats: html`<p class="small muted">Equip multiple hats at once. Select up to 12 owned hats; equipped hats stay on when you add another.
        <b>(${wornHatCount}/12 equipped)</b></p>
      ${ownedHats.length ? html`<div class="grid">${ownedHats.map((it) => card(it, 'Wear hat'))}</div>` : none('hat')}`,
    hair: hair.length ? grid(hair) : none('hair'),
    accessories: accessories.length ? html`${group('Face', ofKind('faceacc'))}${group('Neck', ofKind('neck'))}${group('Shoulder', ofKind('shoulder'))}${group('Waist', ofKind('waist'))}`
      : none('accessories'),
    gear: html`<p class="small muted">Equipped gear goes in your backpack in games whose creator allows gear.</p>
      ${gear.length ? html`<div class="grid">${gear.map((it) => { const on = myGear.includes(it.id); return html`<div class="card square">
          <div class="pic" style="${on ? 'outline:3px solid #16a34a' : ''}">${itemIcon(it)}</div>
          <div class="name">${it.name}</div>
          <button class="btn small ${on ? 'green' : ''}" data-act="gearEquip" data-id="${it.id}" data-on="${on ? '' : '1'}">${on ? 'Equipped' : 'Equip'}</button></div>`; })}</div>`
        : none('gear')}`,
    emotes: emoteBox,
    outfits: outfitBox,
  };
  if (!sections[avatarTab]) avatarTab = 'body';
  show(html`<h1>Avatar</h1>
    <div class="row top avatar-edit">
      <div class="box" style="text-align:center;margin-right:16px"><div id="avatarPreview">${avatarSvg(a, 180, wornItems)}</div>
        <div class="row avatar-view-toggle" role="group" aria-label="Avatar preview mode" style="justify-content:center;margin-top:8px">
          <button class="btn small" type="button" data-avatar-view="2d" aria-pressed="true">2D View</button>
          <button class="btn small blue" type="button" data-avatar-view="3d" aria-pressed="false">3D View</button>
        </div>
        <div class="small muted" id="avatarPreviewHint">Flat preview</div>
        <p><button class="btn green" data-act="saveAvatar">Save</button>
          <button class="btn" data-act="resetAvatar">Undo changes</button></p>
        <p class="small muted" style="max-width:200px">${signedIn() ? 'Your avatar is the same in the app and on the website.'
          : 'Try things on! Log in to save your avatar.'}</p></div>
      <div class="grow">
        <div class="tabs" role="tablist" aria-label="Avatar sections">${AVATAR_TABS.map(([key, name]) => html`<button type="button"
          class="btn small ${key === avatarTab ? 'blue' : ''}" role="tab" aria-selected="${key === avatarTab ? 'true' : 'false'}" data-avatar-tab="${key}">${name}</button>`)}</div>
        ${AVATAR_TABS.map(([key]) => html`<section class="avatar-section" data-avatar-section="${key}" ${key === avatarTab ? '' : raw('hidden')}>${sections[key]}</section>`)}
      </div></div>`);
  // Switching sections just shows another one (nothing is loaded again).
  view.querySelectorAll('[data-avatar-tab]').forEach((button) => button.addEventListener('click', () => {
    avatarTab = button.dataset.avatarTab;
    view.querySelectorAll('[data-avatar-tab]').forEach((b) => {
      const on = b.dataset.avatarTab === avatarTab;
      b.classList.toggle('blue', on);
      b.setAttribute('aria-selected', String(on));
    });
    view.querySelectorAll('[data-avatar-section]').forEach((el) => { el.hidden = el.dataset.avatarSection !== avatarTab; });
  }));
  view.querySelectorAll('[data-emote-slot]').forEach((sel) => sel.addEventListener('change', () =>
    actions.emoteEquip({ slot: sel.dataset.emoteSlot, id: sel.value })));
  // Each outfit's picture: the character wearing it.
  for (const o of outfits) {
    avatarPicture(o.avatar, o.wearing || [], 110).then((url) => {
      const el = view.querySelector(`[data-outfit="${o.id}"]`);
      if (el && url) el.innerHTML = html`<img class="item3d" src="${url}" alt="${o.name}">`.s;
    }).catch(() => {});
  }
  // Colours change the preview straight away (without redrawing the page, so the colour picker stays open).
  let avatar3d = null, avatarViewMode = '2d', avatarViewRequest = 0;
  const render2dPreview = async (request = ++avatarViewRequest) => {
    const previewEl = $('#avatarPreview');
    if (!previewEl) return;
    try {
      // Roblox-style 2D mode: show a still PNG rendered by the same 3D avatar
      // renderer, including actual clothing textures, faces and Studio accessories.
      const url = await avatarPicture(a, wornItems, 180);
      if (request !== avatarViewRequest || avatarViewMode !== '2d') return;
      previewEl.innerHTML = url
        ? html`<img src="${url}" alt="Avatar preview" width="180" height="225" style="display:block;object-fit:contain">`.s
        : avatarSvg(a, 180, wornItems).s;
    } catch (_) {
      if (request === avatarViewRequest && avatarViewMode === '2d') previewEl.innerHTML = avatarSvg(a, 180, wornItems).s;
    }
  };
  const preview = () => {
    if (avatarViewMode === '3d') {
      if (avatar3d) avatar3d.set(a, wornItems);
      else showAvatarView('3d');
    } else render2dPreview();
  };
  const showAvatarView = async (mode) => {
    avatarViewMode = mode;
    const request = ++avatarViewRequest;
    view.querySelectorAll('[data-avatar-view]').forEach((button) => {
      const active = button.dataset.avatarView === mode;
      button.classList.toggle('blue', active);
      button.setAttribute('aria-pressed', String(active));
    });
    const hint = $('#avatarPreviewHint');
    if (hint) hint.textContent = mode === '3d' ? 'Drag to rotate horizontally and vertically' : '2D image rendered from your 3D avatar';
    const previewEl = $('#avatarPreview');
    if (mode === '2d') {
      await render2dPreview(request);
      return;
    }
    const fallbackTo2d = () => {
      avatarViewMode = '2d';
      view.querySelectorAll('[data-avatar-view]').forEach((button) => {
        const active = button.dataset.avatarView === '2d';
        button.classList.toggle('blue', active);
        button.setAttribute('aria-pressed', String(active));
      });
      if (hint) hint.textContent = '3D preview unavailable; showing a rendered 2D image';
      render2dPreview(++avatarViewRequest);
    };
    try {
      const controller = await mountAvatar(previewEl, a, wornItems, { width: 240 });
      if (request !== avatarViewRequest || avatarViewMode !== '3d') {
        if (avatarViewMode === '2d') render2dPreview();
        else showAvatarView('3d');
        return;
      }
      if (controller) avatar3d = controller;
      else fallbackTo2d();
    } catch (_) {
      if (request === avatarViewRequest && avatarViewMode === '3d') fallbackTo2d();
    }
  };
  view.querySelectorAll('[data-avatar-view]').forEach((button) =>
    button.addEventListener('click', () => showAvatarView(button.dataset.avatarView)));
  showAvatarView('2d');
  view.querySelectorAll('[data-avatar-part]').forEach((inp) => inp.addEventListener('input', () => {
    a[inp.dataset.avatarPart] = fromHex(inp.value); preview();
  }));
  const hc = view.querySelector('[data-avatar-hatcolor]'), hcOn = view.querySelector('[data-avatar-hatcolor-on]');
  if (hc) hc.addEventListener('input', () => { a.hatColor = fromHex(hc.value); if (hcOn) hcOn.checked = true; preview(); });
  if (hcOn) hcOn.addEventListener('change', () => { a.hatColor = hcOn.checked ? fromHex(hc.value) : [-1, -1, -1]; preview(); });
};

// Staff tools (like the Player's Staff page): verify people, give Bolts, ban.
// The reports players sent (newest problems first in line): what was reported, why, and buttons to close them.
function reportsBox(rep, closed) {
  const tabs = html`<a class="btn small ${closed ? '' : 'blue'}" href="#/staff?tab=reports">Open</a> <a class="btn small ${closed ? 'blue' : ''}" href="#/staff?reports=closed">Closed</a>`;
  if (!rep.ok) return html`<div class="box"><h2 class="boxhead">Reports</h2><p class="error">${rep.error}</p></div>`;
  const reasonName = (k) => (REPORT_REASONS.find((x) => x[0] === k) || ['', k])[1];
  const person = (u) => (u ? html`<a href="#/user/${u.userId || u.id}">${u.username || u.name}</a>${verified(u.verified)}` : html`?`);
  const what = (x) => {
    if (x.kind === 'user') return html`the account ${person(x.about)}`;
    if (x.kind === 'message') return html`a message from ${person(x.about)}`;
    if (x.kind === 'game') return html`the game <a href="#/game/${x.target}">${x.name}</a> by ${person(x.about)}`;
    if (x.kind === 'group') return html`the group <a href="#/group/${x.target}">${x.name}</a> (owner ${person(x.about)})`;
    if (x.kind === 'comment') return html`a comment by ${person(x.about)} on <a href="#/game/${x.game}">${x.name}</a>`;
    if (x.kind === 'forum') return html`a forum post by ${person(x.about)} in <a href="#/forum/t/${x.thread}">${x.name}</a>`;
    const href = isCatalogItem(x.assetKind) ? '#/item/' + x.target : '#/library/' + x.target;
    return html`<a href="${href}">${x.name}</a> (${KINDS[x.assetKind] || 'item'}) by ${person(x.about)}`;
  };
  const row = (x) => html`<div class="report">
      <p><b>${reasonName(x.reason)}</b>: ${what(x)}
        ${x.reports > 1 ? html` <span class="badge-pill">${x.reports} reports</span>` : ''}
        <span class="small muted">· from ${person(x.from)} · ${ago(x.at)}</span></p>
      ${x.note ? html`<p class="report-note">"${x.note}"</p>` : ''}
      ${x.copy ? html`<div class="report-copy"><b>${x.copy.subject}</b><div>${x.copy.body}</div></div>` : ''}
      <p>${x.about && x.about.username ? html`<a class="btn small" href="#/staff?q=${encodeURIComponent(x.about.username)}">${me.official ? 'Warn or ban' : 'Look up'} ${x.about.username}</a> ` : ''}
        ${closed ? html`<span class="small muted">${x.outcome === 'dismissed' ? 'Dismissed' : 'Done'} by ${person(x.closedBy)} ${ago(x.closedAt)}</span>`
          : html`<button class="btn small green" data-act="closeReport" data-id="${x.id}" data-outcome="done">Done</button>
            <button class="btn small" data-act="closeReport" data-id="${x.id}" data-outcome="dismissed">Nothing wrong</button>`}</p></div>`;
  return html`<div class="box"><h2 class="boxhead">Reports${closed ? '' : html` (${rep.reports.length})`}</h2>
    <p>${tabs}</p>
    ${rep.reports.length ? html`<div class="reports">${rep.reports.map(row)}</div>`
      : html`<p class="muted small">${closed ? 'No closed reports yet.' : 'Nothing to look at right now.'}</p>`}
    <p class="small muted">"Done" means you did something about it (a warning, a ban, deleting it). Closing one closes every report about the same thing.</p></div>`;
}

// Ban appeals: banned people asking to be unbanned, with the ban (why, the note, when and who) and what they wrote.
function appealsBox(ap, closed) {
  const tabs = html`<a class="btn small ${closed ? '' : 'blue'}" href="#/staff?tab=appeals">Open</a> <a class="btn small ${closed ? 'blue' : ''}" href="#/staff?appeals=closed">Answered</a>`;
  if (!ap.ok) return html`<div class="box"><h2 class="boxhead">Ban appeals</h2><p class="error">${ap.error}</p></div>`;
  const person = (u) => (u ? html`<a href="#/user/${u.userId || u.id}">${u.username || u.name}</a>${verified(u.verified)}` : html`?`);
  const row = (x) => html`<div class="report">
      <p><b>${person(x.user)}</b> <span class="small muted">· appealed ${ago(x.at)}</span></p>
      <p class="small">Banned for <b>${x.ban.title}</b>${x.ban.at ? html` ${ago(x.ban.at)}` : ''}${x.ban.by ? html` by ${person(x.ban.by)}` : ''},
        ${x.ban.until ? html`until ${dateText(x.ban.until)}` : 'for good'}${x.ban.note ? html`. Staff note: "${x.ban.note}"` : ''}</p>
      <p class="report-note">"${x.text}"</p>
      <p>${closed ? html`<span class="small muted">${x.status === 'accepted' ? 'Accepted (unbanned)' : 'Denied'}${x.answeredBy ? html` by ${person(x.answeredBy)}` : ''}
          ${ago(x.answeredAt)}${x.note ? html`: "${x.note}"` : ''}</span>`
        : html`${me.official ? html`<button class="btn small green" data-act="answerAppeal" data-id="${x.id}" data-accept="1" data-name="${x.user.username || x.user.name}">Accept (unban)</button> ` : ''}
          <button class="btn small red" data-act="answerAppeal" data-id="${x.id}" data-accept="" data-name="${x.user.username || x.user.name}">Deny</button>
          <a class="btn small" href="#/staff?log=${x.user.userId || x.user.id}">Log</a>`}</p></div>`;
  return html`<div class="box"><h2 class="boxhead">Ban appeals${closed ? '' : html` (${ap.appeals.length})`}</h2>
    <p>${tabs}</p>
    ${ap.appeals.length ? html`<div class="reports">${ap.appeals.map(row)}</div>`
      : html`<p class="muted small">${closed ? 'No answered appeals yet.' : 'Nobody is appealing a ban right now.'}</p>`}
    <p class="small muted">The person sees your answer (and your note) on their ban screen. ${me.official ? 'Accepting unbans them.' : 'Only the Guts account can accept (unban).'}</p></div>`;
}

// Support tickets: people asking staff for help (like a forgotten password). Answer them,
// send a password reset code, or close them.
function ticketsBox(tk, closed) {
  const tabs = html`<a class="btn small ${closed ? '' : 'blue'}" href="#/staff?tab=tickets">Open</a> <a class="btn small ${closed ? 'blue' : ''}" href="#/staff?tickets=closed">Closed</a>`;
  if (!tk.ok) return html`<div class="box"><h2 class="boxhead">Support tickets</h2><p class="error">${tk.error}</p></div>`;
  const person = (u) => (u ? html`<a href="#/user/${u.userId || u.id}">${u.username || u.name}</a>${verified(u.verified)}` : html`?`);
  const topicName = (k) => (TICKET_TOPICS.find((x) => x[0] === k) || ['', k])[1];
  const row = (x) => html`<div class="report">
      <p><b>${topicName(x.topic)}</b>${x.about ? html`: ${person(x.about)}` : ''}
        <span class="small muted">· from ${x.from ? person(x.from) : 'someone who isn\'t logged in'}${x.contact ? html` (${x.contact})` : ''} · ${ago(x.at)}</span></p>
      <p class="report-note">"${x.message}"</p>
      ${x.replies.map((y) => html`<p class="ticket-reply"><b>${y.by ? y.by.name : 'Staff'}:</b> ${y.text} <span class="small muted">${ago(y.at)}</span></p>`)}
      ${x.resetSent ? html`<p class="small ok">A reset code is waiting for them on their Support page.</p>` : ''}
      ${x.resetUsed ? html`<p class="small ok">They reset their password with the code.</p>` : ''}
      <p>${closed ? html`<span class="small muted">Closed${x.closedBy ? html` by ${person(x.closedBy)}` : ''} ${ago(x.closedAt)}</span>
          <button class="btn small" data-act="replyTicket" data-id="${x.id}">Answer (reopens it)</button>`
        : html`<button class="btn small blue" data-act="replyTicket" data-id="${x.id}">Answer</button>
          ${x.topic === 'password' && x.about ? html`<button class="btn small green" data-act="ticketReset" data-id="${x.id}" data-name="${x.about.username}"
            >${x.resetSent ? 'Send a new reset code' : 'Send a reset code'}</button>` : ''}
          <button class="btn small" data-act="closeTicket" data-id="${x.id}">Close</button>`}</p></div>`;
  return html`<div class="box"><h2 class="boxhead">Support tickets${closed ? '' : html` (${tk.tickets.length})`}</h2>
    <p>${tabs}</p>
    ${tk.tickets.length ? html`<div class="reports">${tk.tickets.map(row)}</div>`
      : html`<p class="muted small">${closed ? 'No closed tickets yet.' : 'Nobody needs help right now.'}</p>`}
    <p class="small muted">Only send a reset code when you're sure it's really them (you know them, or they told you things only the owner
      would know). The code shows up on their Support page, works for 24 hours, and only in the browser that sent the ticket.</p></div>`;
}

// Is email working? Which service, the sender address, how the last email went, and a test button.
function mailBox(ml) {
  if (!ml.ok) return '';
  const m = ml.mail;
  return html`<div class="box"><h2 class="boxhead">Email</h2>
    <p>${m.on ? html`<span class="ok">On</span>: sent with <b>${m.service}</b>${m.from ? html` from <b>${m.from}</b>` : ''}.`
      : html`<span class="error">Off.</span> ${m.problem}`}</p>
    ${m.last ? html`<p class="small">Last email (to ${m.last.to}, ${ago(m.last.at)}): ${m.last.ok ? html`<span class="ok">sent</span>` : html`<span class="error">${m.last.error}</span>`}</p>` : ''}
    ${m.on ? html`<form class="row" data-form="mailTest"><input type="email" name="email" required placeholder="Send a test email to..." style="max-width:280px">
      <button class="btn small blue">Send test</button></form>` : ''}</div>`;
}

// Promo codes (official account only): codes can grant Bolts or an item that cannot be bought in the catalog.
const PROMO_STATUS = { live: ['ok', 'Live'], waiting: ['muted', 'Not started yet'], ended: ['muted', 'Ended'], stopped: ['error', 'Stopped'] };
function promoBox(pr) {
  if (!pr) return '';
  if (!pr.ok) return html`<div class="box"><h2 class="boxhead">Promo codes</h2><p class="error">${pr.error}</p></div>`;
  const until = (p) => p.end ? (p.status === 'ended' ? 'ended ' : 'ends ') + dateText(p.end) : 'never ends';
  return html`<div class="box"><h2 class="boxhead">Promo codes</h2>
    <p class="small muted">Create a code that grants Bolts or an item directly to a player's inventory. Item rewards are hidden from the catalog and cannot be purchased. Each account can redeem a code once, on the <a href="#/redeem">Redeem codes</a> page. For item rewards, enter an existing catalog item's ID.
      Leave the start empty to start now, and the end empty to keep it going until you stop it. Times are in your own time zone.</p>
    <form class="promo-form" data-form="promoMake">
      <div><label class="small">Code</label><input type="text" name="code" required maxlength="30" placeholder="GUTSTOBER2026" autocomplete="off" class="notranslate" style="text-transform:uppercase"></div>
      <div><label class="small">Reward type</label><select name="rewardType"><option value="bolts">Bolts</option><option value="item">Exclusive item</option></select></div>
      <div><label class="small">Bolts (for Bolts rewards)</label><input type="number" name="bolts" min="1" max="1000000" value="1000"></div>
      <div><label class="small">Item ID (for item rewards)</label><input type="text" name="itemId" maxlength="80" placeholder="Item ID"></div>
      <div><label class="small">Starts (optional)</label><input type="datetime-local" name="start"></div>
      <div><label class="small">Ends (optional)</label><input type="datetime-local" name="end"></div>
      <div><button class="btn green">Make code</button></div></form>
    <div class="list">${pr.promos.length ? pr.promos.map((p) => html`<div>
        <span class="grow"><b class="notranslate">${p.code}</b> · ${p.rewardType === 'item' ? 'Exclusive item: ' + (p.itemName || p.itemId) : bolts(p.bolts)}
          <span class="small muted">· ${p.uses} used · ${p.status === 'waiting' ? 'starts ' + dateText(p.start) + ', ' : ''}${until(p)}</span></span>
        <span class="small ${PROMO_STATUS[p.status][0]}">${PROMO_STATUS[p.status][1]}</span>
        <button class="btn small" data-act="copyText" data-text="${location.origin + location.pathname}#/redeem?code=${p.code}" title="Copy a link that fills the code in">Copy link</button>
        ${p.status === 'live' || p.status === 'waiting' ? html`<button class="btn small red" data-act="promoStop" data-code="${p.code}">Stop</button>` : ''}</div>`)
      : html`<p class="muted">No codes yet.</p>`}</div></div>`;
}

// Partner apps that can "Log in with Guts&Bolts" (official account only): make each one's client secret.
function connectBox(ca) {
  if (!ca || !ca.ok) return '';
  return html`<div class="box"><h2 class="boxhead">Connected apps</h2>
    <p class="small muted">Partner apps people can link their account to. Each needs a client secret: make it here, then send it to the
      app's owner privately. It's only shown once, and making a new one stops the old one working.</p>
    <div class="list">${ca.apps.map((a) => html`<div><span class="grow"><b>${a.name}</b>
        <span class="small muted">client id <code>${a.id}</code> · ${a.people} connected · ${a.secretAt ? 'secret made ' + ago(a.secretAt) : 'no secret yet'}</span></span>
        <button class="btn small${a.secretAt ? '' : ' green'}" data-act="connectSecret" data-id="${a.id}" data-name="${a.name}" data-made="${a.secretAt ? '1' : ''}">${a.secretAt ? 'Make a new secret' : 'Make secret'}</button></div>`)}</div></div>`;
}

// New decals, sounds and T-shirts from creators who aren't Verified: look, listen, then OK or turn down.
function uploadsBox(up) {
  if (!up.ok) return html`<div class="box"><h2 class="boxhead">Uploads to check</h2><p class="error">${up.error}</p></div>`;
  const row = (a) => html`<div class="report">
      <div class="row"><div class="review-preview" data-preview="${a.id}" data-kind="${a.kind}"><span class="muted small">Loading...</span></div>
        <div class="grow"><p><b>${a.name}</b> <span class="small muted">(${KINDS[a.kind] || a.kind}) by
          <a href="#/user/${a.creator}">${a.creatorName}</a> · ${ago(a.updated || a.created)}</span></p>
          ${a.description ? html`<p class="small">${a.description}</p>` : ''}
          <p><button class="btn small green" data-act="reviewUpload" data-id="${a.id}" data-ok="1">OK</button>
            <button class="btn small red" data-act="reviewUpload" data-id="${a.id}" data-ok="">Turn down</button></p></div></div></div>`;
  return html`<div class="box"><h2 class="boxhead">Uploads to check (${up.uploads.length})</h2>
    ${up.uploads.length ? html`<div class="reports">${up.uploads.map(row)}</div>`
      : html`<p class="muted small">Nothing waiting. New decals, sounds and T-shirts from people who aren't Verified show up here.</p>`}
    <p class="small muted">Nobody but the creator can see or hear these until you press OK. Turn down anything that breaks the rules.</p></div>`;
}
function loadReviewPreviews() {
  for (const el of document.querySelectorAll('[data-preview]')) {
    assetUrl(el.dataset.preview).then((f) => {
      if (!f.url) { el.innerHTML = html`<span class="small error">${f.error}</span>`.s; return; }
      el.innerHTML = (el.dataset.kind === 'audio' ? html`<audio controls preload="none" src="${f.url}"></audio>`
        : html`<a href="${f.url}" target="_blank" rel="noopener"><img src="${f.url}" alt=""></a>`).s;
    });
  }
}

// The Staff page, in sections so each kind of work has its own place. The tabs show how many
// things are waiting in each; Overview has them all at a glance.
// The badges the official account can hand out (Verified, Partner and Staff also have their own buttons).
const GIVE_BADGES = [['tester', 'Tester'], ['bughunter', 'Bug Hunter'], ['featured', 'Featured Creator'], ['verified', 'Verified'],
  ['partner', 'Partner'], ['staff', 'Staff']];
const OTHER_BADGES = { tester: 'Tester', bughunter: 'Bug Hunter', featured: 'Featured Creator' };
const STAFF_TABS = [['home', 'Overview'], ['tickets', 'Tickets'], ['uploads', 'Uploads'], ['reports', 'Reports'], ['appeals', 'Appeals'],
  ['ideas', 'Ideas'], ['people', 'People'], ['log', 'Staff log'], ['tools', 'Tools']];
pages.staff = async () => {
  if (!signedIn() || !me.staff) { show(html`<h1>Staff</h1><p class="muted">Only Guts&amp;Bolts staff can see this page.</p>`); return; }
  const params = new URLSearchParams(location.hash.split('?')[1] || '');
  const query = params.get('q') || '';
  const closed = params.get('reports') === 'closed';
  const logFor = params.get('log') || '';
  const ticketsClosed = params.get('tickets') === 'closed';
  const appealsClosed = params.get('appeals') === 'closed';
  // (older links name what they show, not the tab)
  const tab = params.get('tab') || (params.has('q') ? 'people' : params.has('log') ? 'log' : params.has('reports') ? 'reports'
    : params.has('tickets') ? 'tickets' : params.has('appeals') ? 'appeals' : 'home');
  // What's waiting, for the counts on the tabs (and the lists themselves).
  const [tk, up, rep, ap, rm] = await Promise.all([pageCall('admin.tickets', { status: ticketsClosed ? 'closed' : 'open' }), pageCall('admin.uploads', {}),
    pageCall('admin.reports', { status: closed ? 'closed' : 'open' }), pageCall('admin.appeals', { status: appealsClosed ? 'closed' : 'open' }),
    pageCall('roadmap.get', {})]);
  const waiting = {
    tickets: tk.ok && !ticketsClosed ? tk.tickets.length : null, uploads: up.ok ? up.uploads.length : null,
    reports: rep.ok && !closed ? rep.reports.length : null, appeals: ap.ok && !appealsClosed ? ap.appeals.length : null,
    ideas: rm.ok ? rm.openIdeas : null };
  const tabs = html`<div class="tabs staff-tabs">${STAFF_TABS.map(([k, l]) => html`<a class="btn ${tab === k ? 'blue' : ''}" href="#/staff?tab=${k}">${l}${
    waiting[k] ? html` <span class="count">${waiting[k]}</span>` : ''}</a>`)}</div>`;
  let body;
  if (tab === 'tickets') body = ticketsBox(tk, ticketsClosed);
  else if (tab === 'uploads') body = uploadsBox(up);
  else if (tab === 'reports') body = reportsBox(rep, closed);
  else if (tab === 'appeals') body = appealsBox(ap, appealsClosed);
  else if (tab === 'ideas') body = staffIdeasBox(rm);
  else if (tab === 'people') {
    const r = await pageCall('admin.find', { query });
    // Give a badge by the user ID on their profile (#5). Staff can only give Verified and Partner.
    const giveable = GIVE_BADGES.filter(([k]) => me.official || k === 'verified' || k === 'partner');
    body = html`<div class="box"><h2 class="boxhead">Give a badge</h2>
    <p class="muted">Type their user ID (the number on their profile, like 5) and pick a badge.</p>
    <form class="row" data-form="giveBadge"><input name="who" placeholder="User ID" style="max-width:160px" required>
      <select name="key">${giveable.map(([k, t]) => html`<option value="${k}">${t}</option>`)}</select>
      <button class="btn green">Give badge</button></form></div>
    <div class="box"><h2 class="boxhead">People</h2>
    <p class="muted">${me.official ? 'You\'re the official Guts account: you can verify people, make partners, make staff, give Bolts and ban.'
      : 'Staff can verify people, make them a Partner, take those badges away, and reset usernames that aren\'t allowed.'}</p>
    <form class="row" data-form="staffSearch"><input type="search" name="q" placeholder="Search by name or user number (#5)" value="${query}" style="max-width:320px">
      <button class="btn blue">Search</button></form>
    <div class="list">${r.ok ? r.users.map((u) => html`<div>
      <a class="grow" href="#/user/${u.id}"><b>${u.username || u.name}</b></a>${verified(u.verified)}${partner(u)}
      <span class="small muted">${u.userId ? '#' + u.userId : 'not signed up'}${u.staff ? ' · staff' : ''}${u.banned ? ' · banned' : ''}</span>
      ${(u.badges || []).filter((b) => OTHER_BADGES[b]).map((b) => html`<span class="badge-pill">${OTHER_BADGES[b]}${me.official
        ? html` <button class="btn small" data-act="staff" data-op="revoke" data-key="${b}" data-id="${u.id}" title="Take the ${OTHER_BADGES[b]} badge away">x</button>` : ''}</span>`)}
      ${u.official ? '' : html`
        ${u.verified ? html`<button class="btn small" data-act="staff" data-op="revoke" data-key="verified" data-id="${u.id}">Unverify</button>`
          : html`<button class="btn small green" data-act="staff" data-op="grant" data-key="verified" data-id="${u.id}">Verify</button>`}
        ${u.partner ? html`<button class="btn small" data-act="staff" data-op="revoke" data-key="partner" data-id="${u.id}">Remove partner</button>`
          : html`<button class="btn small" data-act="staff" data-op="grant" data-key="partner" data-id="${u.id}" title="Give them the Partner badge (for friends of Guts&amp;Bolts, like Lyreto)">Make partner</button>`}
        ${u.userId && (me.official || !u.staff) ? html`<button class="btn small" data-act="staff" data-op="resetName" data-id="${u.id}" data-name="${u.username || u.name}" title="For usernames that aren't allowed: it becomes User ${u.userId}">Reset username</button>` : ''}
        ${me.official ? html`
          ${u.staff ? html`<button class="btn small" data-act="staff" data-op="revoke" data-key="staff" data-id="${u.id}">Remove staff</button>`
            : html`<button class="btn small" data-act="staff" data-op="grant" data-key="staff" data-id="${u.id}">Make staff</button>`}
          <button class="btn small" data-act="staff" data-op="bolts" data-id="${u.id}" data-name="${u.username || u.name}">Give Bolts</button>
          <button class="btn small" data-act="staff" data-op="warn" data-id="${u.id}">Warn</button>
          <button class="btn small red" data-act="staff" data-op="ban" data-on="${u.banned ? '' : '1'}" data-id="${u.id}">${u.banned ? 'Unban' : 'Ban'}</button>` : ''}`}
        <a class="btn small" href="#/staff?log=${u.userId || u.id}" title="What staff did to or by them">Log</a>
      </div>`) : html`<p class="error">${r.error}</p>`}</div>
    </div>`;
  } else if (tab === 'log') body = staffLogBox(await pageCall('admin.log', logFor ? { user: logFor } : {}), logFor);
  else if (tab === 'tools') {
    const [ml, ca, pr] = await Promise.all([pageCall('admin.mailStatus', {}), me.official ? pageCall('admin.connectApps', {}) : null,
      me.official ? pageCall('admin.promos', {}) : null]);
    body = html`<div class="box"><h2 class="boxhead">Roadmap</h2><p class="small">Add, move and remove things on the public roadmap on
        <a href="#/create/roadmap">Create &gt; Roadmap</a> (staff see Edit buttons there).</p></div>
      ${promoBox(pr)}${mailBox(ml)}${connectBox(ca)}`;
  } else {
    const lg = await pageCall('admin.log', {});
    const card = (k, label, help) => html`<a class="staff-card ${waiting[k] ? 'busy' : ''}" href="#/staff?tab=${k}">
        <b>${waiting[k] === null ? '?' : waiting[k]}</b><span>${label}</span><span class="small muted">${help}</span></a>`;
    body = html`<h2>Waiting for you</h2>
      <div class="staff-cards">
        ${card('tickets', 'Support tickets', 'People asking for help')}
        ${card('uploads', 'Uploads to check', 'Decals, sounds and T-shirts')}
        ${card('reports', 'Reports', 'Things players reported')}
        ${card('appeals', 'Ban appeals', 'Banned people asking again')}
        ${card('ideas', 'Ideas', 'Roadmap suggestions to answer')}</div>
      <form class="row" data-form="staffSearch"><input type="search" name="q" placeholder="Find a person by name or user number (#5)" style="max-width:320px">
        <button class="btn blue">Search</button></form>
      ${lg.ok ? staffLogBox({ ok: true, log: lg.log.slice(0, 8) }, '') : ''}`;
  }
  show(html`<h1>Staff</h1>${tabs}${body}`);
  if (tab === 'uploads') loadReviewPreviews();
};

// The ideas people suggested for the roadmap (most votes first), for staff to answer.
function staffIdeasBox(rm) {
  if (!rm.ok) return html`<div class="box"><h2 class="boxhead">Ideas</h2><p class="error">${rm.error}</p></div>`;
  return html`<div class="box"><h2 class="boxhead">Ideas (${rm.openIdeas})</h2>
    <p class="small muted">What developers want added, most votes first. Put one on the roadmap or say no (with a note): the person who
      suggested it gets told either way. Everyone can see these on <a href="#/create/roadmap">Create &gt; Roadmap</a>.</p>
    ${rm.ideas.length ? html`<div class="ideas">${rm.ideas.map((x) => ideaRow(x, true))}</div>` : html`<p class="muted small">No ideas waiting.</p>`}
    ${rm.answered.length ? html`<h3>Answered lately</h3><div class="ideas">${rm.answered.map((x) => ideaRow(x, true))}</div>` : ''}</div>`;
}

// The staff action log: who banned, warned, verified, checked or deleted what (newest first).
const STAFF_ACTION = { ban: '🔨', unban: '🕊️', warn: '⚠️', bolts: '💰', badge: '🏅', unbadge: '🏅', review: '🖼️', report: '🚩', ticket: '🛟', delete: '🗑️', comment: '💬', feature: '⭐', appeal: '📨', account: '👤', connect: '🔑', rename: '✏️', roadmap: '🗺️' };
function staffLogBox(lg, logFor) {
  const person = (u) => (u ? html`<a href="#/user/${u.userId || u.id}">${u.name}</a>` : '');
  const body = !lg.ok ? html`<p class="error">${lg.error}</p>`
    : lg.log.length ? html`<div class="list stafflog">${lg.log.map((x) => html`<div>
        <span aria-hidden="true">${STAFF_ACTION[x.action] || '•'}</span>
        <span class="grow"><b>${person(x.by)}</b>: ${x.text}</span><span class="small muted">${ago(x.at)}</span></div>`)}</div>`
    : html`<p class="muted">Nothing yet.</p>`;
  return html`<div class="box"><h2 class="boxhead">Staff action log</h2>
    <p class="small muted">${logFor ? html`Just what was done by or to this person. <a href="#/staff?tab=log">Show everything</a>`
      : 'Everything staff did lately: bans, warnings, ban appeals, badges, Bolts, upload checks, closed reports and deletes (and deleted accounts).'}</p>${body}</div>`;
}

// The bell: friend requests, sales, trades, uploads checked by staff... (newest first).
// Opening the page marks them all read.
function noteLink(n) {
  if (n.kind === 'friendRequest') return '#/friends';
  if (n.kind === 'friend' || n.kind === 'follow') return '#/user/' + n.about;
  if (n.kind === 'trade') return '#/trades';
  if (n.kind === 'group') return '#/group/' + n.about;
  if (n.kind === 'comment' || n.kind === 'featured') return '#/game/' + n.about;
  if (n.kind === 'forum') return '#/forum/t/' + n.about;
  if (n.kind === 'sale' || n.kind === 'upload') return '#/library/' + n.about;
  if (n.kind === 'gift' || n.kind === 'auction') return '#/item/' + n.about;
  if (n.kind === 'support') return '#/support';
  if (n.kind === 'ticket') return '#/staff?tab=tickets';
  if (n.kind === 'roadmap') return '#/create/roadmap';
  if (n.kind === 'rename') return '#/settings';
  return '';
}
pages.notifications = async () => {
  if (!signedIn()) { show(html`<h1>Notifications</h1>${needSignIn('see your notifications')}`); return; }
  const r = await pageCall('notes.list', {});
  if (!r.ok) { show(html`<h1>Notifications</h1><p class="error">${r.error}</p>`); return; }
  const icon = { friendRequest: '👋', friend: '🤝', follow: '⭐', trade: '🔁', sale: '💰', upload: '🖼️', group: '👥', comment: '💬', featured: '⭐', support: '🛟', ticket: '🛟', gift: '🎁', roadmap: '🗺️' };
  show(html`<h1>Notifications</h1>
    <div class="list notes">${r.notes.length ? r.notes.map((n) => {
      const link = noteLink(n);
      const body = html`<span class="note-icon" aria-hidden="true">${icon[n.kind] || '🔔'}</span><span class="grow">${n.text}</span>
        <span class="small muted">${ago(n.at)}</span>`;
      return html`<div class="${n.read ? '' : 'unread'}">${link ? html`<a class="row grow" href="${link}">${body}</a>` : body}</div>`;
    }) : html`<p class="muted">Nothing yet. Friend requests, sales, trades and more show up here.</p>`}</div>`);
  if (r.notes.some((n) => !n.read)) {
    const d = await call('notes.read', {});
    if (d.ok && d.me) setMe(d.me);
  }
};

pages.bolts = async () => {
  const r = signedIn() ? await pageCall('bolts.history', {}) : { ok: true, history: [] };
  const club = signedIn() ? await pageCall('boltworks.status', {}) : { ok: true, registered: false };
  show(html`<h1>Bolts</h1>
    <section class="bolts-hero">
      <div class="bolts-hero-copy"><span class="bolts-eyebrow">YOUR GUTS &amp; BOLTS WALLET</span><h2>Make every Bolt count.</h2><p>Earn Bolts by playing, create things worth buying, and spend them across the platform.</p></div>
      <div class="bolts-balance"><span>YOUR BALANCE</span><strong>${bolts(signedIn() ? me.bolts : 0)}</strong>
        ${!signedIn() || me.canDaily ? html`<button class="btn green" data-act="daily">Claim today's 25 Bolts</button>` : html`<span class="muted">Daily reward claimed. Come back tomorrow.</span>`}</div>
    </section>
    <div class="bolts-earn-grid"><div class="bolts-earn-card"><span class="bolts-earn-icon">⚡</span><strong>Show up daily</strong><p>Claim 25 Bolts every day.</p></div><div class="bolts-earn-card"><span class="bolts-earn-icon">🎮</span><strong>Play games</strong><p>Earn 5 Bolts every 5 minutes, up to 50 a day.</p></div><div class="bolts-earn-card"><span class="bolts-earn-icon">🛠️</span><strong>Make stuff</strong><p>Earn Bolts when people buy your creations.</p></div></div>
    <section class="boltworks-teaser">
      <div class="boltworks-topline"><span class="boltworks-stamp">COMING LATER</span><span class="boltworks-mini">G&amp;B MEMBERSHIP</span></div>
      <div class="boltworks-content"><div class="boltworks-emblem" aria-hidden="true"><span>✦</span><b>B</b><i></i></div><div class="boltworks-copy">
        <p class="boltworks-kicker">THE NEXT LEVEL OF GUTS &amp; BOLTS</p><h2>Boltworks Club</h2><p class="boltworks-lede">For the builders, collectors, and absolute agents of chaos.</p>
        <p>Boltworks Club is a future paid membership with multiple tiers, exclusive perks, and a little extra status around the platform. The tiers, benefits, and prices will be revealed closer to launch.</p>
        <div class="boltworks-tier-preview" aria-label="Membership tiers coming soon"><div><span>01</span><strong>FOUNDATION</strong><small>First details soon</small></div><div><span>02</span><strong>WORKSHOP</strong><small>First details soon</small></div><div><span>03</span><strong>MASTERWORK</strong><small>First details soon</small></div></div>
        <div class="boltworks-cta">${!signedIn() ? html`<a class="btn gold big" href="#/signup">Sign up for early access</a>` : club.registered ? html`<span class="boltworks-registered"><b>✓</b> You're on the early list</span>` : html`<button class="btn gold big" data-act="boltworksRegister">Register for the launch discount</button>`}
          <span class="boltworks-note">${signedIn() ? 'Register now to qualify for a discount when memberships launch. No payment today.' : 'Create a free account to reserve your early-registration discount. No payment today.'}</span></div>
      </div></div><div class="boltworks-footer"><span>NOT AVAILABLE YET</span><span>EARLY REGISTRATION = FUTURE DISCOUNT ELIGIBILITY</span></div>
    </section>
    <h2>Redeem a code</h2><form class="row" data-form="redeem"><input type="text" name="code" placeholder="Type your code here" style="max-width:360px"><button class="btn blue">Redeem</button></form><p class="small"><a href="#/redeem">Got a promo code? Redeem it here</a></p>
    <h2>Transaction history</h2><div class="list bolts-history">${r.ok && r.history.length ? r.history.slice().reverse().map((h) => html`<div>${h.gift ? html`<span class="note-icon" aria-hidden="true">🎁</span>` : ''}<span class="grow">${h.reason}</span>${h.gift === 'got' && !h.amount ? html`<span class="ok">Gift</span>` : html`<span class="${h.amount >= 0 ? 'ok' : 'error'}">${h.amount >= 0 ? '+' : ''}${h.amount}</span>`}<span class="small muted">${ago(h.time)}</span></div>`) : html`<p class="muted">${signedIn() ? 'Nothing yet.' : 'Log in to see your Bolts. New accounts start with 100!'}</p>`}</div>`);
};

// Promo codes: type one (like GUTSTOBER2026) to get Bolts. #/redeem?code=... fills the box in.
pages.redeem = async () => {
  const code = new URLSearchParams(location.hash.split('?')[1] || '').get('code') || '';
  show(html`<div class="form" style="margin:0 auto">
    <h1>Redeem a code</h1>
    <p class="muted">Got a promo code from Guts&amp;Bolts? Type it here to get free Bolts. Each code works once per account, and some only work for a limited time.</p>
    ${signedIn() ? '' : html`<p class="error">Log in first to redeem codes.</p>`}
    <form data-form="redeem"><label>Code</label>
      <input type="text" name="code" required maxlength="80" autocomplete="off" spellcheck="false" placeholder="Type your code here" value="${code}" class="notranslate" style="text-transform:uppercase">
      <p><button class="btn green big">Redeem</button></p></form>
    <p id="redeemMsg" class="muted" role="status"></p>
    <p class="small muted">Look out for new codes on the Updates page and the forum.</p></div>`);
};

// Forgot password: a code goes to the account's email, then pick a new password.
pages.forgot = async () => {
  if (signedIn()) { show(html`<h1>Forgot password</h1><p>You're logged in as ${me.username}. You can change your password in
    <a href="#/settings">Account Settings</a>.</p>`); return; }
  show(html`<div class="form" style="margin:0 auto">
    <h1>Forgot your password?</h1>
    <p class="muted">If your account has an email, we'll send it a code.</p>
    <form data-form="forgot"><label>Username</label><input type="text" name="username" autocomplete="username" required maxlength="20">
      <p><button class="btn blue">Email me a code</button></p><p id="forgotMsg"></p></form>
    <form data-form="reset" id="resetForm" hidden><input type="hidden" name="username">
      <label>Code from your email</label><input type="text" name="code" inputmode="numeric" autocomplete="one-time-code" maxlength="6" required>
      <label>New password</label><input type="password" name="password" autocomplete="new-password" required>
      <label>New password again</label><input type="password" name="password2" autocomplete="new-password" required>
      <p><button class="btn green big" style="width:100%">Set new password</button></p><p class="error" id="resetMsg"></p>
      <p class="small muted">This logs your other devices out. Log in on them again with the new password.</p></form>
    <div class="box"><h2 class="boxhead">No email on your account, or the code never came?</h2>
      <p>Ask Guts&amp;Bolts staff for help: <a class="btn small blue" href="#/support?topic=password">Open a support ticket</a></p>
      <p class="small muted">Or log in on a device where you're still logged in (like the Guts&amp;Bolts app)
        and add an email in Account Settings, so this works next time.</p></div></div>`);
};

// Support: ask staff for help (even when you can't log in). Tickets belong to this browser:
// staff's answers show up here, and so does a password reset code if staff send one.
const TICKET_TOPICS = [['password', 'I forgot my password'], ['account', 'Something else about my account'], ['other', 'Something else']];
pages.support = async () => {
  const topic = new URLSearchParams(location.hash.split('?')[1] || '').get('topic') || (signedIn() ? 'account' : 'password');
  const r = await pageCall('support.mine', {});
  const tickets = r.ok ? r.tickets : [];
  const withCode = tickets.find((x) => x.resetCode);
  const topicName = (k) => (TICKET_TOPICS.find((x) => x[0] === k) || ['', k])[1];
  const ticket = (x) => html`<div class="report">
      <p><b>${topicName(x.topic)}</b>${x.about ? html` (${x.about.username || x.about.name})` : ''}
        <span class="small muted">· ${ago(x.at)} · ${x.status === 'open' ? 'waiting for staff' : 'closed'}</span></p>
      <p class="report-note">"${x.message}"</p>
      ${x.replies.map((y) => html`<p class="ticket-reply"><b>Staff:</b> ${y.text} <span class="small muted">${ago(y.at)}</span></p>`)}
      ${x.resetCode ? html`<p class="ok">Staff sent a reset code for <b>${x.resetUsername}</b>: use the form at the top of this page.</p>` : ''}
      ${x.resetUsed ? html`<p class="ok">Your password was reset with the code staff sent.</p>` : ''}</div>`;
  show(html`<div class="form" style="margin:0 auto;max-width:640px">
    <h1>Support</h1>
    ${withCode && !signedIn() ? html`<div class="box info"><h2 class="boxhead">Pick a new password for ${withCode.resetUsername}</h2>
      <form data-form="reset"><input type="hidden" name="username" value="${withCode.resetUsername}">
        <input type="hidden" name="code" value="${withCode.resetCode}">
        <label>New password</label><input type="password" name="password" autocomplete="new-password" required>
        <label>New password again</label><input type="password" name="password2" autocomplete="new-password" required>
        <p><button class="btn green big" style="width:100%">Set new password</button></p><p class="error" id="resetMsg"></p>
        <p class="small muted">The code staff sent works until ${new Date(withCode.resetUntil * 1000).toLocaleString()}, only in this browser.
          This logs your other devices out: log in on them again with the new password.</p></form></div>` : ''}
    ${withCode && signedIn() ? html`<p class="box gold small">Staff sent a reset code for ${withCode.resetUsername}. Log out first, then come back here to use it.</p>` : ''}
    <p class="muted">Stuck? Ask Guts&amp;Bolts staff. They'll answer here, so come back to this page in this browser.
      ${signedIn() ? '' : 'You don\'t need to be logged in.'}</p>
    <form data-form="supportSend">
      <label>What do you need help with?</label>
      <select name="topic">${TICKET_TOPICS.map(([k, t]) => html`<option value="${k}" ${k === topic ? 'selected' : ''}>${t}</option>`)}</select>
      <label>Username of the account <span class="muted small">${signedIn() ? '(leave empty for yours)' : ''}</span></label>
      <input type="text" name="username" maxlength="20" autocomplete="username" placeholder="${signedIn() ? me.username : 'your username'}">
      <label>Tell staff what happened</label>
      <textarea name="message" rows="5" maxlength="1000" required placeholder="For a forgotten password: tell staff something that shows it's really you, like when you made the account, a game you made, or who your friends are on it."></textarea>
      <label>Your email <span class="muted small">(optional: we'll email you when staff answer)</span></label>
      <input type="email" name="contact" maxlength="254" autocomplete="email" placeholder="you@example.com">
      <p><button class="btn blue">Send to staff</button> <span id="supportMsg"></span></p></form>
    <h2>Your tickets</h2>
    ${!r.ok ? html`<p class="error">${r.error}</p>` : tickets.length ? html`<div class="reports">${tickets.map(ticket)}</div>`
      : html`<p class="muted small">Nothing yet. Tickets you send from this browser show up here.</p>`}</div>`);
};

// Account Settings: email, two-step verification, password.
// The language for the website (the Player and Studio have their own, in their Settings).
const langBox = () => html`<div class="box"><h2 class="boxhead">Language</h2>
    <p>The language for the website's menus and buttons. Things people wrote stay as they are.</p>
    <p><span class="lang-slot"></span> <span class="small muted">(only for this browser)</span></p></div>`;
function themeBox() {
  const on = gutstoberWanted();
  return html`<div class="box"><h2 class="boxhead">Site theme</h2>
    <p><b>Gutstober</b>: the Halloween theme (orange and purple, pumpkins and bats) for all of October.
      ${isGutstober() ? html`It's <b class="${on ? 'ok' : 'muted'}">${on ? 'on' : 'off'}</b> right now.` : html`<span class="muted">It comes back next October.</span>`}</p>
    <p><button class="btn ${on ? '' : 'blue'}" data-act="gutstober" data-on="${on ? '' : '1'}">${on ? 'Turn Gutstober off' : 'Turn Gutstober on'}</button>
      <span class="small muted">(only for this browser)</span></p></div>`;
}
pages.settings = async () => {
  if (!signedIn()) { show(html`<h1>Account Settings</h1>${langBox()}${themeBox()}${needSignIn('change your account settings')}`); return; }
  const noPw = !me.hasPassword;
  show(html`<h1>Account Settings</h1>
    <div class="settings">${langBox()}${themeBox()}
      <div class="box"><h2 class="boxhead">Account</h2>
        <table class="stats"><tr><td>Username</td><td><b>${me.username}</b></td></tr><tr><td>User number</td><td>#${me.userId}</td></tr>
          <tr><td>Password</td><td>${noPw ? html`<span class="error">Not set yet</span> (set one in the app: Avatar &gt; Your account)` : 'Set'}</td></tr>
          ${(me.pastNames || []).length ? html`<tr><td>Past usernames</td><td>${me.pastNames.join(', ')}</td></tr>` : ''}
          <tr><td>Joined</td><td>${me.created ? new Date(me.created * 1000).toLocaleDateString() : '?'}</td></tr></table>
        ${me.official ? html`<form class="form" data-form="joinDate"><label>Join date <span class="muted small">(Guts only)</span></label>
          <input type="date" name="date" required value="${me.created ? new Date(me.created * 1000).toISOString().slice(0, 10) : ''}" style="max-width:180px">
          <p><button class="btn blue">Save join date</button> <span id="joinMsg"></span></p></form>` : ''}
        <form class="form" data-form="rename"><label>Change your username <span class="muted small">(1,000 Bolts)</span></label>
          <input type="text" name="username" maxlength="20" required placeholder="3-20 letters or numbers" autocomplete="off">
          <p class="small muted">Your old username stays on your profile under "Past usernames", and nobody else can ever take it.
            You can switch back to it later (for the same price).</p>
          <p><button class="btn blue">Change username</button> <span id="renameMsg"></span></p></form></div>
      <div class="box"><h2 class="boxhead">Email</h2>
        ${me.canMail ? '' : html`<p class="box gold small">Email isn't switched on for this server yet, so codes can't be sent.</p>`}
        <p>${me.email ? html`Your email: <b>${me.email}</b> <span class="ok">(confirmed)</span>` : html`<span class="muted">No email yet.</span>
          Add one so you can reset your password and turn on two-step verification.`}</p>
        <form class="form" data-form="setEmail"><label>${me.email ? 'Change email' : 'Add an email'}</label>
          <input type="email" name="email" autocomplete="email" required maxlength="254" placeholder="you@example.com">
          ${me.twoStep ? html`<label>Password</label><input type="password" name="password" autocomplete="current-password" required>` : ''}
          <p><button class="btn blue">Send a code</button> <span id="emailMsg"></span></p></form>
        <form class="form" data-form="verifyEmail" ${me.emailPending ? '' : 'hidden'} id="verifyForm">
          <label>Code we sent to <span id="pendingTo">${me.emailPending}</span></label>
          <input type="text" name="code" inputmode="numeric" autocomplete="one-time-code" maxlength="6" required>
          <p><button class="btn green">Confirm email</button> <span id="verifyMsg"></span></p></form>
        ${me.email ? html`<form class="form" data-form="removeEmail"><label>Remove your email</label>
          ${noPw ? '' : html`<input type="password" name="password" placeholder="Your password" autocomplete="current-password" required>`}
          <p><button class="btn red small">Remove email</button></p></form>` : ''}</div>
      <div class="box"><h2 class="boxhead">Two-step verification</h2>
        <p>${me.twoStep ? html`<b class="ok">On.</b> Logging in on a new device needs your password <i>and</i> a code we email you.`
          : html`<b>Off.</b> Turn it on so a stolen password isn't enough to get into your account: logging in also needs a code
            from your email.`}</p>
        ${noPw ? html`<p class="muted small">Set a password first.</p>` : !me.email && !me.twoStep ? html`<p class="muted small">Add and confirm an email first.</p>`
          : html`<form class="form" data-form="twoStep"><input type="hidden" name="on" value="${me.twoStep ? '' : '1'}">
            <input type="password" name="password" placeholder="Your password" autocomplete="current-password" required>
            <p><button class="btn ${me.twoStep ? '' : 'green'}">${me.twoStep ? 'Turn off' : 'Turn on'}</button> <span id="twoStepMsg"></span></p></form>`}</div>
      <div class="box"><h2 class="boxhead">Privacy</h2>
        <form class="form" data-form="privacy">
          <label>Who can see when I'm online and what I'm playing</label>
          <select name="status">${['everyone', 'friends', 'nobody'].map((v) => html`<option value="${v}"${(me.privacy || {}).status === v ? raw(' selected') : ''}>${{ everyone: 'Everyone', friends: 'Friends only', nobody: 'No one (appear offline)' }[v]}</option>`)}</select>
          <label>Who can join me in games</label>
          <select name="join">${['everyone', 'friends', 'nobody'].map((v) => html`<option value="${v}"${(me.privacy || {}).join === v ? raw(' selected') : ''}>${{ everyone: 'Everyone', friends: 'Friends only', nobody: 'No one' }[v]}</option>`)}</select>
          <label>Who can send me messages</label>
          <select name="messages">${['everyone', 'friends', 'nobody'].map((v) => html`<option value="${v}"${((me.privacy || {}).messages || 'everyone') === v ? raw(' selected') : ''}>${{ everyone: 'Everyone', friends: 'Friends only', nobody: 'No one' }[v]}</option>`)}</select>
          <label>Who can see my inventory (and trade with me)</label>
          <select name="inventory">${['everyone', 'friends', 'nobody'].map((v) => html`<option value="${v}"${((me.privacy || {}).inventory || 'everyone') === v ? raw(' selected') : ''}>${{ everyone: 'Everyone', friends: 'Friends only', nobody: 'No one' }[v]}</option>`)}</select>
          <p><button class="btn green">Save</button></p></form></div>
      <div class="box"><h2 class="boxhead">Blocked people</h2>
        <p class="small muted">People you block can't message, friend, follow, trade with or join you, and you won't see each other online.
          Block someone from their profile.</p>
        <div class="list" id="blockedList"><p class="muted small">Loading...</p></div></div>
      <div class="box"><h2 class="boxhead">Connected apps</h2>
        <p class="small muted">Apps you linked your account to with "Log in with Guts&amp;Bolts" (like Lyreto). They can see your
          username, user number and a picture of your avatar. If you allowed it, they can also use Studio's AI tools while Studio is open.</p>
        <div class="list" id="appsList"><p class="muted small">Loading...</p></div></div>
      <div class="box"><h2 class="boxhead">Authenticator app</h2>
        ${me.authApp ? html`<p><b class="ok">On.</b> Logging in on a new device needs your password <i>and</i> the 6-digit code
            from your authenticator app (Google Authenticator, Authy, 2FAS, Aegis, Microsoft Authenticator...).</p>
          <form class="form" data-form="authAppOff">
            ${noPw ? '' : html`<input type="password" name="password" placeholder="Your password" autocomplete="current-password" required>`}
            <input type="text" name="code" inputmode="numeric" autocomplete="one-time-code" maxlength="6" placeholder="Code from the app" required>
            <p><button class="btn">Turn off</button> <span id="authAppMsg"></span></p></form>`
          : authSetup ? html`<p>1. In your authenticator app, add an account and type this key (or tap the link on your phone):</p>
            <p class="secret-key"><code>${authSetup.secret.replace(/(.{4})/g, '$1 ').trim()}</code></p>
            <p class="small"><a href="${authSetup.uri}">Open in my authenticator app</a></p>
            <p>2. Type the 6-digit code it shows:</p>
            <form class="form" data-form="authAppOn">
              <input type="text" name="code" inputmode="numeric" autocomplete="one-time-code" maxlength="6" placeholder="6 digits" required>
              <p><button class="btn green">Turn on</button> <span id="authAppMsg"></span></p></form>`
          : html`<p><b>Off.</b> The safest way to protect your account: a code from an app on your phone that changes every 30
              seconds. A stolen password alone won't get anyone in. (No email needed.)</p>
            <form class="form" data-form="authAppSetup">
              ${noPw ? '' : html`<input type="password" name="password" placeholder="Your password" autocomplete="current-password" required>`}
              <p><button class="btn green">Set up</button> <span id="authAppMsg"></span></p></form>`}</div>
      ${noPw ? '' : html`<div class="box"><h2 class="boxhead">Change password</h2>
        <form class="form" data-form="changePassword">
          <label>Current password</label><input type="password" name="current" autocomplete="current-password" required>
          <label>New password</label><input type="password" name="password" autocomplete="new-password" required>
          <label>New password again</label><input type="password" name="password2" autocomplete="new-password" required>
          <p><button class="btn blue">Change password</button> <span id="pwMsg"></span></p></form></div>`}
      ${me.official ? '' : html`<div class="box danger-box"><h2 class="boxhead">Delete your account</h2>
        <p>This deletes your account for good: your password, email, two-step settings, avatar, About me, posts, outfits,
          messages, friends, follows, favourites and notifications. It can't be undone.</p>
        <p class="small muted">Games and items you made stay up (so nothing people bought breaks), shown as made by "Deleted user".
          Your username and user number are never given to anyone else. Limited copies you own stay with the deleted account and can't be traded.</p>
        ${noPw ? html`<p class="muted small">Set a password first (in the app: Avatar &gt; Your account).</p>`
          : html`<form class="form" data-form="deleteAccount">
            <label>Type your username (<b>${me.username}</b>) to say you're sure</label>
            <input type="text" name="confirm" autocomplete="off" required>
            <label>Your password</label><input type="password" name="password" autocomplete="current-password" required>
            <p><button class="btn red">Delete my account</button> <span id="deleteMsg"></span></p></form>`}</div>`}
    </div>`);
  const [blocked, apps] = await Promise.all([pageCall('block.list', {}), pageCall('connect.list', {})]);
  const box = $('#blockedList');
  if (box) box.innerHTML = (!blocked.ok ? html`<p class="error">${blocked.error}</p>`
    : blocked.people.length ? html`${blocked.people.map((u) => html`<div><a class="grow" href="#/user/${u.userId}">${u.username || u.name}</a>
        <button class="btn small" data-act="block" data-user="${u.id}" data-name="${u.username || u.name}" data-on="">Unblock</button></div>`)}`
      : html`<p class="muted small">You haven't blocked anyone.</p>`).s;
  const appBox = $('#appsList');
  if (appBox) appBox.innerHTML = (!apps.ok ? html`<p class="error">${apps.error}</p>`
    : apps.apps.length ? html`${apps.apps.map((a) => html`<div><span class="grow"><a href="${a.site}" target="_blank" rel="noopener"><b>${a.name}</b></a>
        <span class="small muted">connected ${ago(a.at)}${(a.scopes || []).includes('studio.write') ? ' · can change your Studio game'
          : (a.scopes || []).includes('studio.read') ? ' · can look at your Studio game' : ''}</span></span>
        <button class="btn small" data-act="connectRemove" data-id="${a.id}" data-name="${a.name}">Disconnect</button></div>`)}`
      : html`<p class="muted small">You haven't connected any apps.</p>`).s;
};

// --- "Log in with Guts&Bolts" ---------------------------------------------------------
// A partner app (like Lyreto) sent you here to link your account (worker/index.js
// /api/oauth/authorize). Approve sends you back to the app with a one-time code, which it
// swaps for your user number, username and avatar picture. Nothing else.
// What each permission an app asks for lets it do (the server's CONNECT_SCOPES says the same).
const SCOPE_TEXT = {
  identify: 'See your username, user number and a picture of your avatar',
  'studio.read': 'Look at the game open in your Studio: its objects, scripts, output, errors and screenshots',
  'studio.write': 'Change the game open in your Studio: add, change and delete things, write scripts, run playtests, undo',
};
pages.connect = async () => {
  const q = hashParams();
  if (!signedIn()) {
    try { sessionStorage.setItem('gb.back', location.hash); } catch { /* (then they come back by hand) */ }
    show(html`<div class="form" style="margin:0 auto"><h1>Connect your account</h1>
      <p>An app wants to link your Guts&amp;Bolts account. Log in first, and you'll come straight back here.</p>
      <p class="popup-buttons"><a class="btn blue big" href="#/login">Log In</a> <a class="btn green big" href="#/signup">Sign Up</a></p></div>`);
    return;
  }
  const r = await pageCall('connect.app', { client_id: q.get('client_id') || '', redirect_uri: q.get('redirect_uri') || '', scope: q.get('scope') || '' });
  if (!r.ok) {
    show(html`<div class="form" style="margin:0 auto"><h1>Can't connect</h1><p class="error">${r.error}</p>
      <p><a class="btn" href="#/">Home</a></p></div>`);
    return;
  }
  const host = r.app.site.replace(/^https?:\/\//, '');
  const studio = (r.scopes || []).some((s) => s.id.startsWith('studio.'));
  const note = studio ? html`<p class="small">Studio only lets it in while Studio is open and you have ticked "Allow connected apps to use AI tools"
      in the Assistant tab. It can never publish your games.</p>
    <p class="small muted">It can't see your password, email, Bolts or messages. You can disconnect it any time in Settings.</p>`
    : html`<p class="small muted">It can't see your password, email, Bolts or messages, and it can't do anything as you.
      You can disconnect it any time in Settings.</p>`;
  show(html`<div class="form" style="margin:0 auto">
    <h1>Connect to ${r.app.name}?</h1>
    <p><b>${r.app.name}</b> (<a href="${r.app.site}" target="_blank" rel="noopener">${host}</a>) wants to link your
      Guts&amp;Bolts account, <b>${me.username}</b>.${r.connected ? ' (It\'s already linked: approving again just refreshes it.)' : ''}</p>
    <p>It will be able to:</p>
    <ul>${(r.scopes || []).map((s) => html`<li>${SCOPE_TEXT[s.id] || s.text}</li>`)}</ul>
    ${note}
    <p class="popup-buttons"><button class="btn green big" data-act="connectApprove">Approve</button>
      <a class="btn big" href="${connectReturn({ error: 'access_denied' })}">Cancel</a></p>
    <p class="small" id="connectMsg"></p></div>`);
};
// Where to send them back to: the app's return address (the server checked it) with the answer and its state.
function connectReturn(params) {
  const q = hashParams(), u = new URL(q.get('redirect_uri') || '');
  for (const [k, v] of Object.entries(params)) u.searchParams.set(k, v);
  if (q.get('state')) u.searchParams.set('state', q.get('state'));
  return u.toString();
}
// The square avatar picture connected apps show: drawn here from your avatar and what you wear
// (the server can't draw), as PNG base64; '' if this browser can't draw it.
async function connectPicture() {
  try {
    const p = await call('profile', { id: String(me.userId) });
    const url = p.ok ? await avatarPicture(p.user.avatar, p.wearing || [], 160) : null;
    if (!url) return '';
    const img = new Image();
    img.src = url;
    await img.decode();
    const side = Math.min(256, img.width), c = document.createElement('canvas');
    c.width = c.height = side;
    c.getContext('2d').drawImage(img, 0, 0, img.width, img.width, 0, 0, side, side);   // the top square: head and body
    return c.toDataURL('image/png').split(',')[1];
  } catch {
    return '';
  }
}
// After logging in or signing up: back to the Connect page, if that's where they came from.
function afterLogin() {
  let back = '';
  try { back = sessionStorage.getItem('gb.back') || ''; sessionStorage.removeItem('gb.back'); } catch { back = ''; }
  location.hash = back.startsWith('#/connect?') ? back : '#/';
}

pages.login = async () => loginPage(false);
pages.signup = async () => loginPage(true);
let authSetup = null;   // Settings: an authenticator app being set up ({secret, uri})

// The Terms of Service. Plain words on purpose. (A short version is in the Player app: PlayerLogin.cpp.)
pages.terms = async () => {
  show(html`<div class="terms">
    <h1>Guts&amp;Bolts Terms of Service</h1>
    <p class="muted small">Last updated October 2026. By making an account, playing or using Studio, you agree to these
      terms. If you don't agree, please don't use Guts&amp;Bolts.</p>
    <p>"Guts&amp;Bolts", "we" and "us" mean the people who make and run Guts&amp;Bolts. "You" means you, the person
      using it. These terms cover the website, Studio, the Player and our game servers.</p>

    <h2>1. What Guts&amp;Bolts is</h2>
    <p>Guts&amp;Bolts is a free, independent game engine and platform: Studio to build games, the Player to play them,
      and this website. It's a hobby project made for fun. There are no paid subscriptions, and Bolts (the money used
      inside Guts&amp;Bolts) can't be bought with or turned into real money (see section 4).</p>
    <p>Guts&amp;Bolts is <b>not affiliated with, endorsed by, or connected to Roblox Corporation</b>. "Roblox" is
      their trademark. We don't use their name or logos for our own branding, and we don't support or speak for them.
      Guts&amp;Bolts exists because its creator got fed up with update after update on that platform and decided to
      build their own.</p>

    <h2>2. Adults only (18+)</h2>
    <p>This is the R-rated, non-kid version of the genre. Games and chat can contain <b>strong language, crude
      humour, cartoon violence and gore, and mature themes</b>, and swearing isn't filtered. You must be <b>18 or
      older</b> to make an account or play. If you're under 18, please don't use Guts&amp;Bolts.</p>
    <ul>
      <li><b>No kids, and especially no one under 13.</b> Guts&amp;Bolts isn't made for children, and we don't knowingly
        collect personal information from anyone under 13 (or anyone under 18).</li>
      <li>If we find out an account belongs to someone under 18, we'll close it and delete what's personal in it.</li>
      <li>If you think someone is under 18, report them, or tell staff on the <a href="#/support">Support</a> page.
        Parents can ask us there to close their child's account.</li>
      <li>Ticking the "I'm 18 or older" box when you sign up is a promise. Lying about your age breaks these terms.</li>
    </ul>

    <h2>3. Your account</h2>
    <ul>
      <li>Keep your password (and your authenticator, if you turn one on) to yourself. You're responsible for
        everything that happens on your account.</li>
      <li>Your password never leaves your device in a readable form, so if you lose it and have no recovery email,
        staff may not be able to get your account back.</li>
      <li>One person, one account is the normal way to play. Don't make accounts to dodge a ban, to farm Bolts or to
        vote on your own games.</li>
      <li>Don't sell, buy, rent or share accounts. An account belongs to the person who made it, and can't be
        handed over.</li>
      <li>Usernames have to follow the rules (section 7). A username that breaks them can get the account warned or
        banned.</li>
      <li>You can delete your account any time in Settings. That removes what's personal in it (like your password,
        email, avatar, posts, messages and friends) and can't be undone. Games and items you made stay up as made by
        "Deleted user", so people who bought them keep them, and your username is never given to anyone else. Bolts
        and items on a deleted account are gone for good.</li>
    </ul>

    <h2>4. Bolts and items are not real money</h2>
    <p>This is the important bit about the in-game money and stuff, so please read it.</p>
    <ul>
      <li><b>Bolts have no real-money value.</b> You can't buy them with real money, and you can't cash them out,
        sell them, or swap them for real money or anything outside Guts&amp;Bolts.</li>
      <li>You get Bolts by playing: 100 when you sign up, a daily reward, some for playing games, from selling things
        you made, from groups, and sometimes as gifts or codes from staff.</li>
      <li>Bolts and items (hats, shirts, Limiteds, game passes and the rest) aren't your property. They're a
        <b>permission to use</b> them inside Guts&amp;Bolts, which we can change or take away (for example if you're
        banned, or Guts&amp;Bolts shuts down).</li>
      <li><b>Everything is final and non-refundable.</b> Bolts spent on items, game passes, private servers, upload
        fees, username changes, auctions and the rest don't come back, unless staff decide to fix a mistake.</li>
      <li>We can change prices, rewards, fees and how Bolts work at any time.</li>
      <li>If you got Bolts or items through a bug, an exploit, a scam or by breaking the rules, staff can take them
        back, and may take back what you bought with them.</li>
      <li><b>No real-money trading.</b> Selling or buying Bolts, items or accounts for real money (or gift cards,
        crypto, or anything else outside Guts&amp;Bolts) isn't allowed, and can get every account involved banned.</li>
    </ul>

    <h2>5. Selling, trading, resale and auctions</h2>
    <ul>
      <li>When someone buys something you made, you get 70% of the price in Bolts (when someone resells a Limited,
        the seller gets 70%). The rest is gone: it isn't paid to anyone.</li>
      <li>Trades are between players. Look closely before you accept one: <b>accepted trades, resale purchases and
        auction wins are final</b>, and staff usually won't undo them.</li>
      <li>Staff may undo a trade or sale that came from a scam, a hacked account or a bug, but don't have to.</li>
      <li>Don't scam: no fake trades, tricks, lying about what an item is, or "give me your items first" deals.</li>
      <li>Limiteds held by a deleted account are out of circulation for good.</li>
    </ul>

    <h2>6. What you make</h2>
    <ul>
      <li>Games, models, clothing, pictures, sounds and anything else you make <b>stay yours</b>.</li>
      <li>By uploading something, you let Guts&amp;Bolts store it, copy it onto our servers, make pictures of it
        (thumbnails), show it to other players, and run it in games, for as long as it's on Guts&amp;Bolts. If you make
        a model or other item public in the Library, you also let other players use it in their own games.</li>
      <li>You promise you have the right to upload it: you made it yourself, or the person who did said you could.</li>
      <li>Uploads can be checked by staff before anyone else sees them, and staff can turn down or remove anything that
        breaks the rules.</li>
      <li>You can delete what you upload. Copies other people already bought or used in their games may stay.</li>
      <li>Think something on Guts&amp;Bolts is your work, uploaded without asking? Tell staff on the
        <a href="#/support">Support</a> page with what it is and why it's yours, and we'll look at it and take it down
        if it's copied. People who keep uploading other people's work get banned.</li>
    </ul>

    <h2>7. Rules</h2>
    <p>Mature content is allowed; being a menace isn't. Don't:</p>
    <ul>
      <li>post anything sexual involving minors (this gets you banned for good and may be reported to the police), or
        anything illegal where you live;</li>
      <li>post hate speech or slurs about who people are (race, religion, sexuality, gender, disability and so on);</li>
      <li>threaten, stalk, dox (share someone's personal info) or seriously harass anyone;</li>
      <li>share your own or anyone else's personal info, like real names, addresses, phone numbers or emails;</li>
      <li>cheat, exploit bugs to hurt others or gain Bolts and items, or attack, overload or break into the servers;</li>
      <li>scam people out of their items, Bolts or accounts, or try to get their passwords;</li>
      <li>spam, or advertise things outside Guts&amp;Bolts;</li>
      <li>upload viruses, or games or plugins made to harm people's computers or steal from them;</li>
      <li>upload things you don't have the right to share (other people's art, music, or another platform's assets
        passed off as yours);</li>
      <li>pretend to be staff or another player.</li>
    </ul>

    <h2>8. Chat, safety and moderation</h2>
    <ul>
      <li>Swearing is allowed. The text filter still hides slurs, links to other sites and personal info (like phone
        numbers and email addresses) as "[ Content Deleted ]".</li>
      <li>You can <b>block</b> anyone and <b>report</b> people, games, items, messages, groups, comments and forum posts.
        Staff look at reports and can see the messages that were reported.</li>
      <li>Staff can't watch everything, and we aren't responsible for what other players say, make or do. If something
        is dangerous, report it, and contact the police if someone is in danger.</li>
      <li>What you post can be seen by others (depending on your Privacy settings), so don't post anything you'd want
        kept secret.</li>
    </ul>

    <h2>9. Warnings, bans and appeals</h2>
    <ul>
      <li>If you break these terms, staff can remove your content, take back Bolts or items, warn you, or ban you
        for a time or for good. Staff decide, at their own judgement. Bad enough things can mean a ban without a
        warning first.</li>
      <li>While you're banned, you can't use your account, Bolts or items, and you won't get them back if the ban is
        for good.</li>
      <li>A banned account can send staff one short appeal per ban from the ban screen. Staff read it and decide; their
        answer is final.</li>
      <li>Don't dodge a ban with another account. That account gets banned too, and the first ban can get longer.</li>
    </ul>

    <h2>10. Other people's games, servers and apps</h2>
    <ul>
      <li>Games are made by players, not by us. A game can save things about you (like your progress) and sell its own
        passes and products for Bolts. Its creator is responsible for it.</li>
      <li>Games hosted by a player (like LAN games) or on a server someone else runs don't go through our servers, and
        we don't control them.</li>
      <li>Apps you connect to your account (like Lyreto) are run by other people, with their own rules. You can
        disconnect them in Settings.</li>
    </ul>

    <h2>11. Privacy, in short</h2>
    <p>We keep what's needed to run your account: your username, user number, avatar, friends, what you own and make,
      your Bolts history, what you post and send, and an email address if you add one (only used for account emails,
      like codes and resetting your password). We don't sell anything to anyone. Online play goes through our servers,
      not straight to other players. Staff can see what's needed to keep Guts&amp;Bolts safe, like reports.</p>

    <h2>12. No guarantees</h2>
    <p>Guts&amp;Bolts is provided <b>"as is" and "as available"</b>, with no warranty of any kind. It's a hobby
      project: things may break, change, lose data, or shut down at any time, with or without warning. We don't promise
      it will always work, be safe from bugs, or keep your games, items or Bolts forever. Keep your own copies of
      things you make.</p>

    <h2>13. Limits on what we're responsible for</h2>
    <p>As far as the law allows, the people who make and run Guts&amp;Bolts <b>aren't liable</b> for any loss or damage
      from using it (or not being able to), including lost items, Bolts, games, data or accounts, or anything other
      players do. Guts&amp;Bolts is free, so if we're ever found liable anyway, the most we'd owe is nothing more than
      what you paid us, which is $0. Some places don't allow some of these limits, so they may not all apply to
      you.</p>
    <p>If someone makes a claim against us because of something you uploaded or did on Guts&amp;Bolts, you're
      responsible for it, not us.</p>

    <h2>14. Ending things</h2>
    <p>You can stop using Guts&amp;Bolts and delete your account at any time. We can suspend or close accounts that
      break these terms, and we can change, pause or shut down Guts&amp;Bolts (or any part of it) at any time. If it shuts
      down, Bolts and items end with it. Sections 4, 6, 12, 13 and 15 still count after your account is gone.</p>

    <h2>15. Changes and the law</h2>
    <p>These terms can change as Guts&amp;Bolts grows. If they change in a big way, it'll be on the
      <a href="#/updates">Updates</a> page, and the date at the top of this page will change. Keeping on playing
      after that means you accept the new version. If paid features are ever added, they'll come with their own terms
      first.</p>
    <p>These terms follow the laws of the State of Florida, USA. If one part of them can't be enforced, the rest still
      counts.</p>

    <h2>16. Contact</h2>
    <p>Questions about these terms, a copyright problem, or anything else? Ask staff on the
      <a href="#/support">Support</a> page. You don't need to be logged in.</p>
    <p class="small muted">This page is written in plain words for a hobby project. It isn't legal advice.</p>
  </div>`);
};

function loginPage(signup) {
  if (signedIn()) { show(html`<h1>You're signed in as ${me.username}.</h1><p><a class="btn" href="#/">Home</a></p>`); return; }
  show(html`<div class="form" style="margin:0 auto">
    <h1>${signup ? 'Sign Up' : 'Log In'}</h1>
    <p class="muted">${signup ? 'Make your Guts&Bolts account. It\'s free, and you get 100 Bolts!' : 'Welcome back!'}</p>
    <div class="tabs"><a class="btn ${signup ? 'blue' : ''}" href="#/signup">Sign Up</a><a class="btn ${signup ? '' : 'blue'}" href="#/login">Log In</a></div>
    <form data-form="${signup ? 'signup' : 'login'}">
      <label>Username</label><input type="text" name="username" autocomplete="username" required maxlength="20"
        ${signup ? raw('placeholder="3-20 letters or numbers"') : ''}>
      ${signup ? html`<div class="small" id="nameCheck"></div>` : ''}
      <label>Password</label><input type="password" name="password" autocomplete="${signup ? 'new-password' : 'current-password'}" required>
      ${signup ? html`<label>Password again</label><input type="password" name="password2" autocomplete="new-password" required>`
        : html`<div id="codeBox" hidden><label>Code from your email</label>
          <input type="text" name="code" inputmode="numeric" autocomplete="one-time-code" maxlength="6" placeholder="6 digits"></div>
          <p class="small"><a href="#/forgot">Forgot your password?</a> · <a href="#/support">Get help from staff</a></p>`}
      ${signup ? html`<div class="mature-note"><b>Heads up: Guts&amp;Bolts is for adults (18+).</b> It's the
          uncensored, R-rated cousin of the blocky-game genre: games can have strong language, crude humour,
          cartoon gore and violence, and swearing in chat isn't filtered. It's a free hobby project, not
          Roblox and not connected to Roblox.</div>
        <label class="check"><input type="checkbox" name="agree" required> <span>I'm 18 or older and I agree to the
          <a href="#/terms" target="_blank">Terms of Service</a>.</span></label>` : ''}
      <p><button class="btn green big" style="width:100%">${signup ? 'Sign Up' : 'Log In'}</button></p>
      <p class="error" id="loginMsg"></p></form>
    <p class="small muted">${signup ? 'Usernames can\'t be changed. Your password never leaves this page: if you forget it, nobody can get it back, so write it down somewhere safe.'
      : 'Use the same username and password as in the Guts&Bolts app.'}</p></div>`);
  if (signup) {
    const input = view.querySelector('input[name=username]'), out = $('#nameCheck');
    let timer;
    input.addEventListener('input', () => {
      clearTimeout(timer);
      const name = input.value;
      timer = setTimeout(async () => {
        if (!name) { out.textContent = ''; return; }
        const r = await gb.call('account.check', { username: name });
        if (input.value !== name || !r.ok) return;
        out.className = 'small ' + (r.available ? 'ok' : 'error');
        out.textContent = r.available ? name + ' is available!' : r.problem;
      }, 400);
    });
  }
}

// --- clicks and forms ----------------------------------------------------------

const actions = {
  async boltworksRegister() {
    const r = await call('boltworks.register', {});
    if (!r.ok) { toast(r.error || 'Could not register right now.'); return; }
    toast('You’re on the Boltworks Club early list. Your launch discount eligibility is saved.');
    render();
  },
  async favorite(d) {
    const r = await call('game.favorite', { id: d.id, on: !!d.on });
    if (!r.ok) { toast(r.error); return; }
    toast(d.on ? 'Added to your favourites.' : 'Taken out of your favourites.');
    render();
  },
  // The heart on an item's page: your wishlist (Catalog > My Favorites) and its Most Favorited count.
  async itemFavorite(d) {
    const r = await call('item.favorite', { id: d.id, on: !!d.on });
    if (!r.ok) { toast(r.error); return; }
    toast(d.on ? 'Added to your favourites.' : 'Taken out of your favourites.');
    render();
  },
  async outfit(d) {
    const args = { id: d.id };
    if (d.op === 'outfit.rename') {
      const name = prompt('New name for this outfit:', d.name);
      if (!name) return;
      args.name = name;
    }
    if (d.op === 'outfit.delete' && !confirm('Delete the outfit "' + d.name + '"?')) return;
    const r = await call(d.op, args);
    if (!r.ok) { toast(r.error); return; }
    if (d.op === 'outfit.wear') { setMe(r.me); avatarDraft = null; toast('You\'re wearing it now.'); }
    render();
  },
  async deleteMessage(d) {
    if (!confirm('Delete this message?')) return;
    const r = await call('message.delete', { id: d.id, box: d.box });
    if (!r.ok) { toast(r.error); return; }
    setMe(r.me);
    render();
  },
  gutstober(d) {
    try { localStorage.setItem('gb.gutstober', d.on ? 'on' : 'off'); } catch { /* private window */ }
    applyTheme();
    render();
  },
  async deleteUpdate(d) {
    if (!confirm('Delete this update?')) return;
    const r = await call('updates.delete', { id: d.id });
    toast(r.ok ? 'Deleted.' : r.error);
    if (r.ok) render();
  },
  async logout() {
    if (!confirm('Log out? You can log back in with your username and password.')) return;
    await gb.forgetKey();
    await hello();
    location.hash = '#/';
    render();
  },
  async daily() {
    const r = await call('bolts.daily', {});
    toast(r.ok ? 'You got 25 Bolts!' : r.error);
    render();
  },
  // Play: games run in the Guts&Bolts app, so the website opens it with a
  // gutsandbolts:// link. Visitors pick a guest character first, like the old sites.
  play(d) {
    if (!signedIn()) { guestPicker(d.id, d.name); return; }
    launchGame(d.id, d.name, '');
  },
  pickGuest(d) { launchGame(d.id, d.name, d.guest); },
  // Edit in Studio: the app (Player) downloads the game and opens Studio on it.
  editInStudio(d) {
    const url = 'gutsandbolts://edit/' + encodeURIComponent(d.id);
    popup(html`<h1 class="popup-title">Opening Studio...</h1>
      <div class="launch-spin" aria-hidden="true"></div>
      <p>Opening <b>${d.name}</b> in Guts&amp;Bolts Studio. The Guts&amp;Bolts app gets the game, then starts Studio.</p>
      <p class="small muted">If your browser asks, choose <b>Open Guts&amp;Bolts</b>. Studio runs on Windows, Mac and Linux (not phones).</p>
      <p class="popup-buttons"><a class="btn green" href="${url}">Try again</a> <a class="btn" href="../#download">Download the app</a></p>`);
    location.href = url;
  },
  joinServer(d) {
    if (!signedIn()) { guestPicker(d.id, d.name); return; }
    launchGame(d.id, d.name, '', d.server);
  },
  async shareServer(d) {
    const link = location.origin + location.pathname + '#/game/' + encodeURIComponent(d.id) + '?server=' + encodeURIComponent(d.server);
    try { await navigator.clipboard.writeText(link); toast('Link copied! Friends who open it can join this server.'); }
    catch { prompt('Copy this link:', link); }
  },
  serverPage(d) { serverPage = Number(d.to) || 1; render(); },
  toggle(d) { const el = $(d.target); if (el) el.hidden = !el.hidden; },
  async modelAccess(d) {
    const r = await call('model.access', { id: d.id, access: d.access });
    if (r.ok && r.me) setMe(r.me);
    toast(r.ok ? (d.access === 'public' ? 'It\'s public now.' : 'It\'s private now.') : r.error);
    render();
  },
  auctionBid() { loginPopup('bid on items'); },
  async auctionCancel(d) {
    if (!confirm('Cancel this auction? Everyone gets their Bolts back and nobody gets the item.')) return;
    const r = await call('auction.cancel', { id: d.id });
    toast(r.ok ? 'Auction cancelled. Everyone has their Bolts back.' : r.error);
    render();
  },
  async resaleBuy(d) {
    if (!signedIn()) { loginPopup('buy items'); return; }
    if (!confirm('Buy copy #' + d.serial + ' for ' + d.price + ' Bolts?')) return;
    const r = await call('resale.buy', { id: d.id, serial: Number(d.serial) });
    if (r.ok) setMe(r.me);
    toast(r.ok ? 'It\'s yours!' : r.error);
    render();
  },
  // The pictures on a game's page (gameGallery).
  galleryStep(d) { galleryGo(gallery.i + Number(d.step)); },
  galleryShow(d) { galleryGo(Number(d.i)); },
  galleryOpen() { galleryLightbox(); },
  galleryClose() { document.querySelectorAll('.lightbox').forEach((m) => m.remove()); },
  // Screenshots on the Configure page: move one left or right, or delete it.
  async shotMove(d) {
    const order = [...view.querySelectorAll('.shot-edit[data-shot]')].map((x) => x.dataset.shot);
    const i = order.indexOf(d.shot), j = i + Number(d.dir);
    if (i < 0 || j < 0 || j >= order.length) return;
    [order[i], order[j]] = [order[j], order[i]];
    const r = await call('shots.order', { id: d.id, order });
    if (!r.ok) { toast(r.error); return; }
    render();
  },
  async shotDelete(d) {
    if (!confirm('Delete this screenshot? It comes off the game\'s page straight away.')) return;
    const r = await call('shots.delete', { id: d.id, shot: d.shot });
    toast(r.ok ? 'Screenshot deleted.' : r.error);
    render();
  },
  async tradeAnswer(d) {
    const r = await call(d.op, { id: d.id });
    if (r.ok && r.me) setMe(r.me);
    toast(r.ok ? ({ 'trade.accept': 'Trade done!', 'trade.decline': 'Declined.', 'trade.cancel': 'Cancelled.' }[d.op]) : r.error);
    render();
  },
  async forumDelete(d) {
    if (!confirm(d.first ? 'Delete this whole thread?' : 'Delete this post?')) return;
    const r = await call('forum.delete', { thread: d.thread, post: d.post, page: Number(d.page) || 0 });
    if (!r.ok) { toast(r.error); return; }
    if (r.gone) { location.hash = '#/forum/' + r.board; return; }
    render();
  },
  async forumMod(d) {
    const r = await call('forum.mod', { thread: d.thread, pinned: !!d.pinned, locked: !!d.locked, page: Number(d.page) || 0 });
    if (!r.ok) toast(r.error);
    render();
  },
  async featureGame(d) {
    const r = await call('admin.feature', { id: d.id, on: !!d.on });
    toast(r.ok ? (d.on ? 'Featured! It shows on the home page now.' : 'Not featured any more.') : r.error);
    if (r.ok) render();
  },
  async deleteComment(d) {
    if (!confirm('Delete this comment?')) return;
    const r = await call('comments.delete', { game: d.game, id: d.id });
    if (!r.ok) { toast(r.error); return; }
    const el = view.querySelector('[data-comment="' + d.id + '"]');
    if (el) el.remove();
  },
  async moreComments(d, el) {
    const r = await call('comments.list', { game: d.game, before: d.before });
    if (!r.ok) { toast(r.error); return; }
    const g = { id: d.game };
    view.querySelector('.comments').insertAdjacentHTML('beforeend', r.comments.map((c) => commentRow(g, c).s).join(''));
    if (r.more && r.comments.length) el.dataset.before = r.comments[r.comments.length - 1].id;
    else el.remove();
  },
  async modelVote(d) {
    if (!signedIn()) { loginPopup('rate models'); return; }
    const r = await call('model.vote', { id: d.id, vote: Number(d.vote) });
    if (!r.ok) toast(r.error);
    else toast(Number(d.vote) === 0 ? 'Vote taken back.' : 'Thanks for rating it!');
    render();
  },
  // Buy as a gift: pick a friend, add a note if you like, check, send.
  async gift(d) {
    const r = await call('friends.list', {});
    if (!r.ok) { toast(r.error); return; }
    const friends = r.friends.slice().sort((x, y) => lower(x.username || x.name).localeCompare(lower(y.username || y.name)));
    const price = Number(d.price);
    popup(html`<h1 class="popup-title">&#127873; Buy as a gift</h1>
      <p>Send <b>${d.name}</b> to a friend. You pay ${bolts(price)}, and it goes straight into their inventory.</p>
      ${friends.length ? html`<form class="form gift-form" data-form="gift"><input type="hidden" name="id" value="${d.id}">
          <input type="hidden" name="name" value="${d.name}"><input type="hidden" name="price" value="${price}">
          <label>Who's it for?</label>
          <select name="to" required><option value="">Pick a friend...</option>
            ${friends.map((f) => html`<option value="${f.userId}">${f.username || f.name}</option>`)}</select>
          <label>A note <span class="muted small">(optional, up to 100 letters)</span></label>
          <input type="text" name="message" maxlength="100" placeholder="Happy birthday!">
          <p class="small muted">They get a notification with your note. You have ${bolts(me.bolts)}.</p>
          <p class="popup-buttons"><button class="btn green big">Next</button> <a href="#" class="btn" data-act="closeModal">Cancel</a></p></form>`
        : html`<p class="muted">You can only send gifts to friends, and you don't have any yet. <a href="#/people">Find some!</a></p>
          <p class="popup-buttons"><a href="#" class="btn" data-act="closeModal">Close</a></p>`}`);
  },
  // Buy as a gift, step 3: pay and send (the server checks everything again).
  async giftSend(d, el) {
    const r = await call('gift.buy', { id: d.id, to: Number(d.to), message: d.message || '' });
    if (!r.ok) { toast(r.error, 6000); return; }
    el.closest('.modal').remove();
    toast('Sent! ' + d.who + ' got ' + d.name + ' and a notification.', 5000);
    render();
  },
  async vote(d) {
    if (!signedIn()) { loginPopup('vote on games'); return; }
    const r = await call('game.vote', { id: d.id, vote: Number(d.vote) });
    if (!r.ok) toast(r.error);
    render();
  },
  async connectApprove() {
    const msg = $('#connectMsg');
    msg.className = 'muted'; msg.textContent = 'Connecting...';
    const q = hashParams();
    const r = await call('connect.approve', { client_id: q.get('client_id') || '', redirect_uri: q.get('redirect_uri') || '', scope: q.get('scope') || '',
      picture: await connectPicture() });
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    msg.textContent = 'Taking you back...';
    location.href = connectReturn({ code: r.code });
  },
  async connectRemove(d) {
    if (!confirm('Disconnect ' + d.name + '? It won\'t be able to see your Guts&Bolts account any more.')) return;
    const r = await call('connect.remove', { id: d.id });
    toast(r.ok ? 'Disconnected ' + d.name + '.' : r.error);
    if (r.ok) render();
  },
  async connectSecret(d) {
    if (d.made && !confirm('Make a new secret for ' + d.name + '? The old one stops working straight away, so ' + d.name + ' will need the new one.')) return;
    const r = await call('admin.connectSecret', { id: d.id });
    if (!r.ok) { toast(r.error); return; }
    popup(html`<h1 class="popup-title">${d.name}'s client secret</h1>
      <p>Copy it now and send it to ${d.name}'s owner privately (a direct message, not a public chat). It's only shown this once.</p>
      <p class="secret-key"><code>${r.secret}</code></p>
      <p class="popup-buttons"><button class="btn blue" data-act="copySecret" data-text="${r.secret}">Copy</button>
        <a href="#" class="btn" data-act="closeModal">Done</a></p>`);
  },
  async ideaVote(d) {
    if (!signedIn()) { toast('Sign up or log in to vote.'); return; }
    const r = await call('roadmap.vote', { id: d.id, on: !!d.on });
    if (!r.ok) { toast(r.error); return; }
    render();
  },
  async ideaAnswer(d) {
    let note = '';
    if (d.answer !== 'open') {
      note = prompt(d.answer === 'planned' ? 'A note for everyone (optional), like when it might come:'
        : 'Why not? (Everyone sees this, and the person who suggested it gets told.)', '');
      if (note === null) return;
    }
    const r = await call('admin.ideaAnswer', { id: d.id, answer: d.answer, note });
    if (!r.ok) { toast(r.error); return; }
    toast(d.answer === 'planned' ? 'Added to the roadmap.' : d.answer === 'declined' ? 'Answered.' : 'Reopened.');
    render();
  },
  async ideaRemove(d) {
    if (!confirm('Delete this idea? (For spam and the like: to say no, use Say no.)')) return;
    const r = await call('admin.ideaRemove', { id: d.id });
    if (!r.ok) { toast(r.error); return; }
    render();
  },
  async roadRemove(d) {
    if (!confirm('Take this off the roadmap?')) return;
    const r = await call('admin.roadmapRemove', { id: d.id });
    if (!r.ok) { toast(r.error); return; }
    render();
  },
  async promoStop(d) {
    if (!confirm('Stop this code? Nobody will be able to use it after this.')) return;
    const r = await call('admin.promoStop', { code: d.code });
    if (!r.ok) { toast(r.error); return; }
    render();
  },
  async copySecret(d) {
    try { await navigator.clipboard.writeText(d.text); toast('Copied!'); } catch { prompt('Copy this:', d.text); }
  },
  async copyText(d) {
    try { await navigator.clipboard.writeText(d.text); toast('Copied ' + d.text); } catch { prompt('Copy this:', d.text); }
  },
  async restoreVersion(d) {
    if (!confirm('Restore version ' + d.v + '? It becomes the live version (as a new version), and players get it the next time they join.')) return;
    const r = await call('game.restore', { id: d.id, version: Number(d.v) });
    toast(r.ok ? 'Version ' + d.v + ' is live again (as version ' + r.version + ').' : r.error, 5000);
    render();
  },
  async deleteBadge(d) {
    if (!confirm('Delete the badge "' + d.name + '"? Players who earned it lose it from their profile.')) return;
    const r = await call('gamebadge.delete', { game: d.game, badge: d.badge });
    toast(r.ok ? 'Deleted.' : r.error);
    render();
  },
  closeModal(d, el) { el.closest('.modal').remove(); },
  // Report something to staff: pick what's wrong, add a note, and (for people and messages) block them too.
  report(d) {
    const what = { user: d.name, game: 'the game "' + d.name + '"', item: '"' + d.name + '"', message: 'this message from ' + d.name,
      group: 'the group "' + d.name + '"', comment: 'this comment by ' + d.name }[d.kind] || d.name;
    const blockId = d.kind === 'user' ? d.id : d.kind === 'message' ? d.user : '';
    popup(html`<h1 class="popup-title">Report ${what}</h1>
      <p>Guts&amp;Bolts staff will look at it. The person you report isn't told who sent it.</p>
      <p><label for="reportReason">What's wrong?</label><br>
        <select id="reportReason">${REPORT_REASONS.map(([k, t]) => html`<option value="${k}">${t}</option>`)}</select></p>
      <p><label for="reportNote">Tell staff what happened (optional)</label><br>
        <textarea id="reportNote" maxlength="500" rows="4" style="width:100%"></textarea></p>
      ${blockId ? html`<p><label><input type="checkbox" id="reportBlock"> Block them too</label></p>` : ''}
      <p><button class="btn red" data-act="sendReport" data-kind="${d.kind}" data-id="${d.id}" data-block="${blockId}">Send report</button>
         <button class="btn" data-act="closeModal">Cancel</button> <span id="reportMsg"></span></p>`);
  },
  async sendReport(d, el) {
    const r = await call('report.send', { kind: d.kind, id: d.id, reason: $('#reportReason').value, note: $('#reportNote').value });
    if (!r.ok) { const m = $('#reportMsg'); m.className = 'error'; m.textContent = r.error; return; }
    const alsoBlock = d.block && $('#reportBlock') && $('#reportBlock').checked;
    if (alsoBlock) await call('block.add', { user: d.block });
    el.closest('.modal').remove();
    toast(alsoBlock ? 'Thanks. We got your report, and you blocked them.' : 'Thanks. We got your report and staff will look at it.');
    if (alsoBlock) render();
  },
  async block(d) {
    if (d.on && !confirm('Block ' + d.name + '? You won\'t be friends any more, and you can\'t message, follow, trade with or join each other.')) return;
    const r = await call(d.on ? 'block.add' : 'block.remove', { user: d.user });
    if (!r.ok) { toast(r.error); return; }
    toast(d.on ? 'You blocked ' + d.name + '.' : 'You unblocked ' + d.name + '.');
    render();
  },
  async reviewUpload(d) {
    let note = '';
    if (!d.ok) {
      note = prompt('Why is it turned down? (The creator sees this. You can leave it empty.)', '');
      if (note === null) return;
    }
    const r = await call('admin.review', { id: d.id, ok: !!d.ok, note });
    if (!r.ok) { toast(r.error); return; }
    toast(d.ok ? 'OK: everyone can see it now.' : 'Turned down.');
    render();
  },
  async replyTicket(d) {
    const text = prompt('Your answer (they see it on their Support page):', '');
    if (!text || !text.trim()) return;
    const r = await call('admin.replyTicket', { id: d.id, text });
    toast(r.ok ? 'Answer sent.' : r.error);
    if (r.ok) render();
  },
  async ticketReset(d) {
    if (!confirm('Send a password reset code for ' + d.name + '?\n\nOnly do this if you\'re sure this is really them. Whoever sent the ticket can take over the account with it.')) return;
    const r = await call('admin.ticketReset', { id: d.id });
    toast(r.ok ? 'Sent: it shows up on their Support page.' : r.error);
    if (r.ok) render();
  },
  async closeTicket(d) {
    const r = await call('admin.closeTicket', { id: d.id });
    toast(r.ok ? 'Closed.' : r.error);
    if (r.ok) render();
  },
  async answerAppeal(d) {
    const accept = !!d.accept;
    const note = prompt((accept ? 'Unban ' + d.name + '?' : 'Keep ' + d.name + ' banned?') + '\n\nA short note for them (optional, they see it on their ban screen):', '');
    if (note === null) return;
    const r = await call('admin.answerAppeal', { id: d.id, accept, note });
    toast(r.ok ? (accept ? d.name + ' is unbanned.' : 'Appeal denied.') : r.error);
    if (r.ok) render();
  },
  async closeReport(d) {
    const r = await call('admin.closeReport', { id: d.id, outcome: d.outcome });
    if (!r.ok) { toast(r.error); return; }
    toast(d.outcome === 'dismissed' ? 'Dismissed.' : 'Marked as done.');
    render();
  },
  async ackWarning(d, el) {
    const r = await call('account.ackWarning', { id: d.id });
    el.closest('.modal').remove();
    if (r.ok && r.me) setMe(r.me);
  },
  async buyPrivate(d) {
    if (!confirm('Buy 30 days of your own private server of "' + d.name + '" for ' + Number(d.price).toLocaleString() + ' Bolts?')) return;
    const r = await call('servers.buyPrivate', { game: d.id });
    if (r.ok && r.me) me = r.me;
    toast(r.ok ? 'It\'s yours until ' + new Date(r.until * 1000).toLocaleDateString() + '! Start it from the Servers button in the app.' : r.error);
    render();
  },
  async buyPass(d) {
    if (Number(d.price) > 0 && !confirm('Buy "' + d.name + '" for ' + Number(d.price).toLocaleString() + ' Bolts?')) return;
    const r = await call('buy', { id: d.id });
    if (r.ok && r.me) me = r.me;
    toast(r.ok ? 'It\'s yours! You\'ll have it next time you join.' : r.error);
    render();
  },
  async passOffsale(d) {
    const r = await call('pass.edit', { id: d.id, offsale: !!d.on });
    toast(r.ok ? (d.on ? 'Taken off sale.' : 'On sale again.') : r.error);
    render();
  },
  async buy(d) {
    const r = await call('buy', { id: d.id });
    toast(r.ok ? 'It\'s yours! Wear (or equip) it from the Avatar page.' : r.error);
    render();
  },
  async emoteEquip(d) {
    const r = await call('emotes.equip', { slot: Number(d.slot), id: d.id || '' });
    if (r.ok) me = r.me;
    toast(r.ok ? (d.id ? 'In slot ' + d.slot + '! Press B in a game to use it.' : 'Slot ' + d.slot + ' is empty now.') : r.error);
    render();
  },
  async animpackEquip(d) {
    const r = await call('animpack.equip', { id: d.id || '' });
    if (r.ok) me = r.me;
    toast(r.ok ? (d.id ? 'You\'ll move like this in every game.' : 'Back to the normal animations.') : r.error);
    render();
  },
  async gearEquip(d) {
    const r = await call('gear.equip', { id: d.id, on: !!d.on });
    if (r.ok) me = r.me;
    toast(r.ok ? (d.on ? 'Equipped! You\'ll have it in games that allow gear.' : 'Unequipped.') : r.error);
    render();
  },
  async assetListen(d) {
    const box = $('#assetAudio');
    if (box) box.innerHTML = html`<span class="muted">Loading...</span>`.s;
    const f = await assetUrl(d.id);
    if (!box || !box.isConnected) return;
    box.innerHTML = f.url ? html`<audio controls autoplay src="${f.url}"></audio>`.s : html`<p class="error">${f.error}</p>`.s;
  },
  async copyId(d) {
    try { await navigator.clipboard.writeText(d.id); toast('Copied ' + d.id + '. Paste it into a Decal\'s Texture or a Sound\'s SoundId in Studio.'); }
    catch { prompt('Copy this ID:', d.id); }
  },
  async deleteAsset(d) {
    if (!confirm('Delete "' + d.name + '" for good?')) return;
    const r = await call('delete', { id: d.id });
    toast(r.ok ? 'Deleted.' : r.error);
    render();
  },
  async renameGame(d) {
    const name = prompt('New name for the game:', d.name);
    if (!name || name === d.name) return;
    toast('Renaming...');
    const got = await gb.call('get', { id: d.id });
    if (!got.ok) { toast(got.error); return; }
    let game;
    try { game = JSON.parse(new TextDecoder().decode(gb.base64Bytes(got.data))); } catch { toast('That game file couldn\'t be read.'); return; }
    game.info = Object.assign({}, game.info, { title: name });
    const bytes = new TextEncoder().encode(JSON.stringify(game, null, 2));
    const r = await call('update', { id: d.id, name, data: await gb.fileBase64(new Blob([bytes])) });
    toast(r.ok ? 'Renamed to "' + name + '".' : r.error);
    render();
  },
  resetAvatar() { avatarDraft = null; render(); },
  avatarPreset(d) { PRESETS[Number(d.i)][1].forEach((c, i) => { avatarDraft[PARTS[i]] = c.slice(); }); render(); },
  avatarHat(d) { avatarDraft.hat = Number(d.i); avatarDraft.hatColor = [-1, -1, -1]; render(); },
  avatarWear(d) {
    const w = avatarDraft.wearing;
    const item = (id) => avatarOwned.find((x) => x.id === id);
    const kindOf = (id) => (item(id) ? item(id).kind : id.slice(0, id.lastIndexOf('-')));   // (old item ids are "<kind>-<hex>")
    const slotOf = (id) => { const a = item(id); return a && a.kind === 'bodypart' ? 'bodypart:' + String(a.meta && a.meta.bodyPart || '') : kindOf(id); };
    const putOn = (id) => {
      const kind = kindOf(id);
      // Hats are stackable: keep every selected hat, up to the 12-item avatar limit.
      // Other wearable categories retain their existing one-of-each-kind behavior.
      if (kind !== 'hat') {
        const slot = slotOf(id);
        for (let i = w.length - 1; i >= 0; --i) if (slotOf(w[i]) === slot) w.splice(i, 1);
      }
      if (w.length >= 12) { toast('You can wear up to 12 items at once. Remove an item first.'); return false; }
      if (!w.includes(id)) w.push(id);
      return true;
    };
    if (w.includes(d.id)) w.splice(w.indexOf(d.id), 1);
    else {
      if (!putOn(d.id)) { render(); return; }
      // A bundle puts on everything in it too (the things in it can be taken off one by one).
      const b = item(d.id);
      if (b && b.kind === 'bundle') for (const id of (b.meta && b.meta.items) || []) if (item(id) && !w.includes(id)) putOn(id);
    }
    render();
  },
  async saveAvatar() {
    const a = avatarDraft, avatar = { hat: a.hat, hatColor: a.hatColor.map((x) => x | 0), wearing: a.wearing };
    PARTS.forEach((p) => { avatar[p] = a[p].map((x) => x | 0); });
    const r = await call('avatar.set', { avatar });
    toast(r.ok ? 'Saved! You\'ll look like this in every game.' : r.error);
    if (r.ok) { avatarDraft = null; render(); connectPicture().then((picture) => picture && call('connect.picture', { picture })); }   // (for connected apps)
  },
  async staff(d) {
    let r;
    if (d.op === 'grant') {
      // A badge is a signature: the official account signs it, or a staff member
      // signs it and adds their own Staff badge (only for Verified and Partner).
      const message = 'gb-badge:' + d.key + ':' + d.id;
      let sig = await gb.sign(message);
      if (!me.official) {
        const mine = (me.grants || []).find((g) => g[0] === 'staff');
        if (!mine) { toast('You need the Staff badge for that.'); return; }
        sig = 's:' + me.id + ':' + mine[1] + ':' + sig;
      }
      r = await call('admin.grant', { to: d.id, key: d.key, sig });
    } else if (d.op === 'revoke') {
      r = await call('admin.revoke', { to: d.id, key: d.key });
    } else if (d.op === 'bolts') {
      const amount = parseInt(prompt('How many Bolts to give ' + d.name + '? (A minus number takes them away.)', '100') || '0', 10);
      if (!amount) return;
      r = await call('admin.giveBolts', { to: d.id, amount, reason: '' });
    } else if (d.op === 'ban') {
      if (d.on) {   // pick why first (the banned player sees it)
        popup(html`<h1 class="popup-title">Ban this account</h1>
          <p>Why are they being banned? They'll see this reason.</p>
          <p><select id="banReason">${BAN_REASONS.map(([k, t]) => html`<option value="${k}">${t}</option>`)}</select></p>
          <p><input id="banNote" maxlength="200" placeholder="Note for them (optional)"></p>
          <p>For how long? <select id="banDays"><option value="1">1 day</option><option value="3">3 days</option>
            <option value="7">7 days</option><option value="30">30 days</option><option value="0" selected>Forever</option></select></p>
          <p><button class="btn red" data-act="doBan" data-id="${d.id}">Ban</button>
             <button class="btn" data-act="closeModal">Cancel</button></p>`);
        return;
      }
      r = await call('admin.ban', { to: d.id, on: false });
    } else if (d.op === 'resetName') {   // a username that isn't allowed becomes "User <number>"
      if (!confirm('Reset ' + d.name + '\'s username? It becomes "User" and their user number, and nobody can use "' + d.name + '" again.')) return;
      r = await call('admin.resetName', { to: d.id });
    } else if (d.op === 'warn') {   // a warning they see next time they come (and must say they understand)
      popup(html`<h1 class="popup-title">Warn this account</h1>
        <p>What did they do? They'll see this.</p>
        <p><select id="warnReason">${BAN_REASONS.map(([k, t]) => html`<option value="${k}">${t}</option>`)}</select></p>
        <p><input id="warnNote" maxlength="200" placeholder="Note for them (optional)"></p>
        <p><button class="btn blue" data-act="doWarn" data-id="${d.id}">Send warning</button>
           <button class="btn" data-act="closeModal">Cancel</button></p>`);
      return;
    }
    toast(r && r.ok ? 'Done.' : (r && r.error) || 'That didn\'t work.');
    render();
  },
  async doBan(d) {
    const reason = document.getElementById('banReason').value;
    const note = document.getElementById('banNote').value;
    const days = parseInt(document.getElementById('banDays').value, 10) || 0;
    const r = await call('admin.ban', { to: d.id, on: true, reason, note, days });
    document.querySelectorAll('.modal').forEach((m) => m.remove());
    toast(r && r.ok ? 'Banned.' : (r && r.error) || 'That didn\'t work.');
    render();
  },
  async doWarn(d) {
    const reason = document.getElementById('warnReason').value;
    const note = document.getElementById('warnNote').value;
    const r = await call('admin.warn', { to: d.id, reason, note });
    document.querySelectorAll('.modal').forEach((m) => m.remove());
    toast(r && r.ok ? 'Warning sent. They\'ll see it next time they come.' : (r && r.error) || 'That didn\'t work.');
  },
  async friend(d) {
    const r = await call(d.op, { user: d.user });
    toast(r.ok ? ({ friends: 'You\'re friends now!', sent: 'Friend request sent.', none: 'Done.' }[r.status] || 'Done.') : r.error);
    render();
  },
  async follow(d) {
    const r = await call(d.op, { user: d.user });
    toast(r.ok ? (r.following ? 'You\'re following them now.' : 'Unfollowed.') : r.error);
    render();
  },
  async group(d) {
    if (d.op === 'groups.delete' && !confirm('Delete this group for good? Its games stay, but its Bolts are gone.')) return;
    if (d.confirm && !confirm(d.confirm)) return;
    if (d.op === 'groups.leave' && !confirm('Leave this group?')) return;
    const args = { id: d.id };
    if (d.user) args.user = d.user;
    if (d.action) args.action = d.action;
    if (d.rank) args.rank = d.rank;
    if (d.game) args.game = d.game;
    if (d.op === 'groups.request') args.accept = d.accept === '1';
    if (d.op === 'groups.deletePost') { args.by = d.by; args.time = Number(d.time); }
    const r = await call(d.op, args);
    if (!r.ok) toast(r.error);
    else if (r.requested) toast('Asked to join. An admin will let you in.');
    if (d.op === 'groups.delete' && r.ok) { location.hash = '#/groups'; return; }
    render();
  },
};

const forms = {
  forumSearch(f) {
    const q = f.q.value.trim();
    if (!q) return;
    location.hash = '#/forumsearch/' + encodeURIComponent(q) + '/' + encodeURIComponent(f.board.value || 'all') + '/1';
  },
  async forumPost(f) {
    const r = await call('forum.post', { board: f.board.value, title: f.title.value, text: f.text.value });
    if (!r.ok) { toast(r.error); return; }
    location.hash = '#/forum/t/' + r.thread.id;
  },
  async forumReply(f) {
    const r = await call('forum.reply', { thread: f.thread.value, text: f.text.value });
    if (!r.ok) { toast(r.error); return; }
    const want = '#/forum/t/' + r.thread.id + '/' + (r.page + 1);
    if (location.hash === want) render(); else location.hash = want;
  },
  async comment(f) {
    const r = await call('comments.post', { game: f.game.value, text: f.text.value });
    if (!r.ok) { toast(r.error); return; }
    render();
  },
  async postUpdate(f) {
    const r = await call('updates.post', { name: f.name.value, version: f.version.value, tag: f.tag.value,
      summary: f.summary.value, items: f.items.value.split('\n').map((x) => x.trim()).filter(Boolean) });
    toast(r.ok ? 'Posted "' + r.update.name + '"!' : r.error);
    if (r.ok) render();
  },
  async supportSend(f) {
    const msg = $('#supportMsg');
    msg.className = 'muted'; msg.textContent = 'Sending...';
    const r = await call('support.send', { topic: f.topic.value, username: f.username.value.trim(), message: f.message.value, contact: f.contact.value.trim() });
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    toast('Sent! Staff will answer on this page.', 5000);
    render();
  },
  async mailTest(f) {
    const r = await call('admin.mailTest', { email: f.email.value.trim() });
    toast(r.ok ? 'Sending... reload the Staff page in a few seconds to see how it went.' : r.error, 5000);
  },
  async forgot(f) {
    const msg = $('#forgotMsg');
    msg.className = 'muted'; msg.textContent = 'Sending...';
    const r = await call('account.forgot', { username: f.username.value.trim() });
    msg.className = r.ok ? 'ok' : 'error';
    msg.textContent = r.ok ? r.message : r.error;
    if (r.ok) { const rf = $('#resetForm'); rf.hidden = false; rf.username.value = f.username.value.trim(); rf.code.focus(); }
  },
  async reset(f) {
    const msg = $('#resetMsg');
    if (f.password.value.length < 8) { msg.textContent = 'Your password needs at least 8 characters.'; return; }
    if (f.password.value !== f.password2.value) { msg.textContent = 'The two passwords don\'t match.'; return; }
    if (f.agree && !f.agree.checked) { msg.textContent = 'Tick the box to say you\'re 18+ and agree to the Terms.'; return; }
    msg.className = 'muted'; msg.textContent = 'Setting your new password...';
    const r = await gb.resetPassword(f.username.value, f.code.value.trim(), f.password.value);
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    await hello();
    toast('Your password is changed, ' + me.username + '. You\'re logged in.', 6000);
    location.hash = '#/';
  },
  async setEmail(f) {
    const msg = $('#emailMsg');
    msg.className = 'muted'; msg.textContent = 'Sending...';
    const args = { email: f.email.value.trim() };
    if (f.password) args.auth = await gb.passwordProof(me.username, f.password.value);
    const r = await call('account.email', args);
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    msg.className = 'ok'; msg.textContent = 'Sent! Check your inbox (and spam).';
    $('#pendingTo').textContent = r.sentTo;
    $('#verifyForm').hidden = false;
    $('#verifyForm').code.focus();
  },
  async verifyEmail(f) {
    const r = await call('account.emailVerify', { code: f.code.value.trim() });
    if (!r.ok) { const m = $('#verifyMsg'); m.className = 'error'; m.textContent = r.error; return; }
    me = r.me; toast('Email confirmed!'); render();
  },
  async removeEmail(f) {
    if (!confirm('Remove your email? You won\'t be able to reset your password, and two-step verification turns off.')) return;
    const r = await call('account.emailRemove', f.password ? { auth: await gb.passwordProof(me.username, f.password.value) } : {});
    if (!r.ok) { toast(r.error); return; }
    me = r.me; toast('Email removed.'); render();
  },
  async statusSet(f) {
    if (!f.status.value.trim()) return;
    const r = await call('profile.set', { status: f.status.value });
    if (!r.ok) { toast(r.error); return; }
    toast('Status updated!');
    render();
  },
  async blurbSet(f) {
    const r = await call('profile.set', { blurb: f.blurb.value });
    if (!r.ok) { toast(r.error); return; }
    toast('Saved!');
    render();
  },
  async outfitSave(f) {
    // Save the look on screen first (it may have unsaved changes), then keep it as an outfit.
    if (avatarDraft) {
      const a = avatarDraft, avatar = { hat: a.hat, hatColor: a.hatColor.map((x) => x | 0), wearing: a.wearing };
      PARTS.forEach((p) => { avatar[p] = a[p].map((x) => x | 0); });
      const s = await call('avatar.set', { avatar });
      if (!s.ok) { toast(s.error); return; }
    }
    const r = await call('outfit.save', { name: f.name.value });
    if (!r.ok) { toast(r.error); return; }
    setMe(r.me); avatarDraft = null;
    toast('Outfit saved!');
    render();
  },
  async sendMessage(f) {
    const r = await call('message.send', { to: f.to.value.trim(), subject: f.subject.value, body: f.body.value });
    if (!r.ok) { const m = $('#sendMsg'); m.className = 'error'; m.textContent = ' ' + r.error; return; }
    setMe(r.me);
    toast('Message sent!');
    location.hash = '#/messages/sent';
  },
  async privacy(f) {
    const r = await call('account.privacy', { status: f.status.value, join: f.join.value, messages: f.messages.value, inventory: f.inventory.value });
    if (!r.ok) { toast(r.error); return; }
    me = r.me; toast('Privacy saved.');
  },
  async authAppSetup(f) {
    const r = await call('account.authAppSetup', f.password ? { auth: await gb.passwordProof(me.username, f.password.value) } : {});
    if (!r.ok) { const m = $('#authAppMsg'); m.className = 'error'; m.textContent = r.error; return; }
    authSetup = { secret: r.secret, uri: r.uri };
    render();
  },
  async authAppOn(f) {
    const r = await call('account.authAppEnable', { code: f.code.value.trim() });
    if (!r.ok) { const m = $('#authAppMsg'); m.className = 'error'; m.textContent = r.error; return; }
    authSetup = null; me = r.me; toast('Authenticator app is on. Keep that phone safe!', 5000); render();
  },
  async authAppOff(f) {
    const args = { code: f.code.value.trim() };
    if (f.password) args.auth = await gb.passwordProof(me.username, f.password.value);
    const r = await call('account.authAppDisable', args);
    if (!r.ok) { const m = $('#authAppMsg'); m.className = 'error'; m.textContent = r.error; return; }
    me = r.me; toast('Authenticator app is off.'); render();
  },
  async twoStep(f) {
    const r = await call('account.twoStep', { on: !!f.on.value, auth: await gb.passwordProof(me.username, f.password.value) });
    if (!r.ok) { const m = $('#twoStepMsg'); m.className = 'error'; m.textContent = r.error; return; }
    me = r.me; toast(me.twoStep ? 'Two-step verification is on.' : 'Two-step verification is off.'); render();
  },
  async appeal(f) {
    const msg = $('#appealMsg');
    msg.className = 'muted'; msg.textContent = 'Sending...';
    const r = await call('appeal.send', { text: f.text.value });
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    toast('Appeal sent. Staff will answer it here.');
    render();
  },
  async deleteAccount(f) {
    const msg = $('#deleteMsg');
    if (f.confirm.value.trim() !== me.username) { msg.className = 'error'; msg.textContent = 'Type your username to say you\'re sure.'; return; }
    if (!confirm('Delete ' + me.username + ' for good? This can\'t be undone.')) return;
    msg.className = 'muted'; msg.textContent = 'Deleting...';
    const auth = await gb.passwordProof(me.username, f.password.value);
    const r = await call('account.delete', { auth });
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    await gb.forgetKey();
    await hello();
    toast('Your account is deleted. Goodbye, and thanks for playing!', 6000);
    location.hash = '#/';
    render();
  },
  async changePassword(f) {
    const msg = $('#pwMsg');
    if (f.password.value.length < 8) { msg.className = 'error'; msg.textContent = 'Your new password needs at least 8 characters.'; return; }
    if (f.password.value !== f.password2.value) { msg.className = 'error'; msg.textContent = 'The two new passwords don\'t match.'; return; }
    msg.className = 'muted'; msg.textContent = 'Changing...';
    const r = await gb.changePassword(me.username, f.current.value, f.password.value);
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    toast('Password changed.'); render();
  },
  async newPass(f) {
    const args = { game: f.game.value, name: f.name.value, description: f.description.value, price: Number(f.price.value) || 0 };
    if (f.icon.files[0]) args.icon = await pictureBase64(f.icon.files[0], 256, 256);
    const r = await call('pass.create', args);
    toast(r.ok ? 'Pass made! Its ID is ' + r.asset.num + '.' : r.error);
    if (r.ok) render();
  },
  async newProduct(f) {
    const r = await call('pass.create', { game: f.game.value, product: true, name: f.name.value, description: f.description.value,
      price: Number(f.price.value) || 0 });
    toast(r.ok ? 'Product made! Its ID is ' + r.asset.num + '.' : r.error);
    if (r.ok) render();
  },
  async newBadge(f) {
    const hex = f.color.value.replace('#', '');
    const color = [0, 2, 4].map((i) => parseInt(hex.substr(i, 2), 16));
    const r = await call('gamebadge.create', { game: f.game.value, name: f.name.value, description: f.description.value, color });
    toast(r.ok ? 'Badge made! Copy its ID into your script.' : r.error);
    if (r.ok) render();
  },
  // Add screenshots to a game (Configure page): each one is checked here first so the
  // message is quick, and the server checks again.
  async shotsAdd(f) {
    const msg = $('#shotsMsg'), say = (t, cls = 'muted') => { msg.className = cls; msg.textContent = ' ' + t; };
    const files = [...f.files.files], room = SHOT_MOST - Number(f.count.value || 0);
    if (!files.length) { say('Pick one or more pictures first.', 'error'); return; }
    if (files.length > room) { say(room ? `You can add ${room} more (a game has at most ${SHOT_MOST}).` : `A game can have ${SHOT_MOST} screenshots. Delete one first.`, 'error'); return; }
    for (const file of files) {
      const problem = shotProblem(file);
      if (problem) { say(problem, 'error'); return; }
    }
    let added = 0;
    for (const file of files) {
      say(`Uploading ${added + 1} of ${files.length}...`);
      const r = await call('shots.add', { id: f.id.value, data: await gb.fileBase64(file) });
      if (!r.ok) {
        if (!added) { say(`"${file.name}": ${r.error}`, 'error'); return; }
        toast(`Added ${added}, but "${file.name}": ${r.error}`, 6000);
        render();
        return;
      }
      added++;
    }
    toast(added === 1 ? 'Screenshot added!' : added + ' screenshots added!');
    render();
  },
  async configure(f) {
    const msg = $('#configMsg'), say = (t, cls = 'muted') => { msg.className = cls; msg.textContent = ' ' + t; };
    const id = f.id.value;
    say('Saving...');
    try {
      const genres = [...f.querySelectorAll('input[name=genre]:checked')].map((x) => x.value);
      if (genres.length > 3) { say('Pick up to 3 genres.', 'error'); return; }
      let r = await call('game.settings', { id, name: f.name.value, description: f.description.value, access: f.access.value,
        genres, maxPlayers: Number(f.maxPlayers.value) || 12, allowGear: f.allowGear.checked, comments: f.comments.checked,
        privatePrice: Math.max(0, Math.trunc(Number(f.privatePrice.value) || 0)), lyreto: f.lyreto.value,
        ...(f.group.value !== f.group.dataset.was ? { group: f.group.value } : {}) });
      if (!r.ok) { say(r.error, 'error'); return; }
      if (f.thumb.files[0]) {
        r = await call('thumb.set', { id, data: await pictureBase64(f.thumb.files[0], 768, 432) });
        if (!r.ok) { say('Thumbnail: ' + r.error, 'error'); return; }
      }
      if (f.icon.files[0]) {
        r = await call('icon.set', { id, data: await pictureBase64(f.icon.files[0], 256, 256) });
        if (!r.ok) { say('Icon: ' + r.error, 'error'); return; }
      }
      if (f.file.files[0]) {
        say('Uploading the new version...');
        r = await call('update', { id, data: await gb.fileBase64(f.file.files[0]) });
        if (!r.ok) { say('New version: ' + r.error, 'error'); return; }
      }
    } catch (err) { say(String(err.message || err), 'error'); return; }
    toast('Saved!');
    render();
  },
  topSearch(f) { location.hash = '#/games?' + new URLSearchParams({ q: f.q.value }); },
  assetGo(f) { const id = f.id.value.trim().replace(/^gb:/, ''); if (id) location.hash = '#/library/' + encodeURIComponent(id); },
  browseSearch(f) { location.hash = browseLink({ q: f.q.value.trim(), cat: f.cat.value }); },
  browseCreator(f) { location.hash = browseLink({ creator: f.name.value.trim() }); },
  gameSearch(f) { location.hash = '#/games?' + new URLSearchParams({ q: f.q.value, sort: f.sort.value, genre: f.genre.value }); },
  peopleSearch(f) { location.hash = '#/people?' + new URLSearchParams({ q: f.q.value }); },
  staffSearch(f) { location.hash = '#/staff?' + new URLSearchParams({ q: f.q.value }); },
  // Give a badge by user ID: find the account it belongs to, then sign the badge for that account.
  async giveBadge(f) {
    const want = f.who.value.trim().replace(/^#/, '');
    if (!want) return;
    const r = await call('admin.find', { query: want });
    if (!r.ok) { toast(r.error); return; }
    const low = want.toLowerCase();
    const u = r.users.find((x) => String(x.userId) === want) || r.users.find((x) => x.id === low)
      || r.users.find((x) => (x.username || x.name || '').toLowerCase() === low);
    if (!u) { toast(/^[0-9]+$/.test(want) ? 'Nobody has the user ID ' + want + '.' : 'There\'s nobody called "' + want + '".'); return; }
    if ((u.badges || []).includes(f.key.value)) { toast((u.username || u.name) + ' already has that badge.'); return; }
    await actions.staff({ op: 'grant', key: f.key.value, id: u.id });
  },
  groupSearch(f) { location.hash = '#/groups?' + new URLSearchParams({ q: f.q.value }); },
  // Buy as a gift, step 2: check who it's for and what it costs before anything is paid.
  gift(f) {
    if (!f.to.value) { toast('Pick a friend first.'); return; }
    const who = f.to.selectedOptions[0].textContent, price = Number(f.price.value), note = f.message.value.trim();
    const short = (me.bolts || 0) < price;
    popup(html`<h1 class="popup-title">Send this gift?</h1>
      <table class="stats gift-check">
        <tr><td>Gift</td><td><b>${f.name.value}</b></td></tr>
        <tr><td>For</td><td><b>${who}</b></td></tr>
        <tr><td>Note</td><td>${note ? html`"${note}"` : html`<span class="muted">No note</span>`}</td></tr>
        <tr><td>You pay</td><td>${bolts(price)}</td></tr>
        <tr><td>You'll have</td><td>${bolts(Math.max(0, (me.bolts || 0) - price))}</td></tr></table>
      ${short ? html`<p class="error">You need ${(price - (me.bolts || 0)).toLocaleString()} more Bolts for this. <a href="#/bolts">Get Bolts</a></p>` : ''}
      <p class="popup-buttons"><button class="btn green big" data-act="giftSend" data-id="${f.id.value}" data-to="${f.to.value}"
          data-who="${who}" data-name="${f.name.value}" data-message="${note}" ${short ? 'disabled' : ''}>Send gift</button>
        <a href="#" class="btn" data-act="closeModal">Cancel</a></p>`);
  },
  async redeem(f) {
    const r = await call('bolts.redeem', { code: f.code.value });
    const msg = $('#redeemMsg');
    const rewardMessage = r.rewardType === 'item' ? 'You got ' + (r.itemName || 'an exclusive item') + '!' : 'You got ' + r.got + ' Bolts!';
    if (msg) { msg.className = r.ok ? 'ok' : 'error'; msg.textContent = r.ok ? rewardMessage : r.error; }
    toast(r.ok ? rewardMessage : r.error);
    if (r.ok) { if (r.me) setMe(r.me); f.code.value = ''; if (!msg) render(); }
  },
  async roadSave(f) {
    const r = await call('admin.roadmapSave', { id: f.id.value, title: f.title.value, text: f.text.value, status: f.status.value, version: f.version.value });
    if (!r.ok) { toast(r.error); return; }
    toast('Saved.');
    render();
  },
  async ideaSuggest(f) {
    const r = await call('roadmap.suggest', { title: f.title.value, text: f.text.value });
    if (!r.ok) { toast(r.error); return; }
    toast('Thanks! Your idea is up for votes.');
    render();
  },
  async promoMake(f) {
    const when = (box) => (box.value ? Math.floor(new Date(box.value).getTime() / 1000) : 0);
    const r = await call('admin.promoMake', { code: f.code.value, rewardType: f.rewardType.value, bolts: Number(f.bolts.value), itemId: f.itemId.value, start: when(f.start), end: when(f.end) });
    if (!r.ok) { toast(r.error); return; }
    toast('Code made! People can redeem it on the Redeem codes page.');
    render();
  },
  async login(f) {
    setTimeout(checkRequests, 1500);
    const msg = $('#loginMsg');
    msg.className = 'muted'; msg.textContent = 'Logging in...';
    const r = await gb.logIn(f.username.value.trim(), f.password.value, f.code ? f.code.value.trim() : '');
    if (r.needCode) {   // two-step verification: the code from the email, or from the authenticator app
      $('#codeBox').hidden = false;
      $('#codeBox label').textContent = r.app ? 'Code from your authenticator app' : 'Code from your email';
      f.code.focus();
    }
    if (!r.ok) { msg.className = r.needCode ? 'muted' : 'error'; msg.textContent = r.error; return; }
    await hello();
    toast('Welcome back, ' + me.username + '!');
    afterLogin();
  },
  async signup(f) {
    const msg = $('#loginMsg');
    if (f.password.value.length < 8) { msg.textContent = 'Your password needs at least 8 characters.'; return; }
    if (f.password.value !== f.password2.value) { msg.textContent = 'The two passwords don\'t match.'; return; }
    msg.className = 'muted'; msg.textContent = 'Signing up...';
    const r = await gb.signUp(f.username.value.trim(), f.password.value);
    if (!r.ok) { msg.className = 'error'; msg.textContent = r.error; return; }
    await hello();
    toast('Welcome to Guts&Bolts, ' + me.username + '! You\'re user #' + me.userId + '.', 6000);
    afterLogin();
  },
  async upload(f) {
    const kind = f.kind.value, msg = $('#uploadMsg');
    const args = { kind, name: f.name.value, description: f.description.value, price: f.price ? Number(f.price.value) || 0 : 0 };
    if (kind === 'bundle') {
      args.data = '';
      args.meta = { items: f.items.value.split(/[\s,]+/).filter(Boolean), headless: f.headless.checked };
    } else if (kind === 'shirt' || kind === 'pants') {
      const pic = f.picture && f.picture.files[0];
      if (!pic) { msg.textContent = 'Pick your picture made from the ' + kind + ' template first.'; return; }
      args.data = await gb.fileBase64(pic);
    } else {
      const file = f.file.files[0];
      if (!file) { msg.textContent = 'Pick a file first.'; return; }
      args.data = await gb.fileBase64(file);
      if (kind === 'audio') args.meta = { ext: (file.name.split('.').pop() || '').toLowerCase() };
    }
    msg.className = 'muted'; msg.textContent = ' Uploading...';
    f.querySelector('button').disabled = true;
    const r = await call('upload', args);
    f.querySelector('button').disabled = false;
    if (!r.ok) { msg.className = 'error'; msg.textContent = ' ' + r.error; return; }
    toast('Uploaded "' + r.asset.name + '"!' + (r.fee ? ' (' + r.fee + ' Bolts)' : '')
      + (r.asset.review === 'pending' ? ' Staff check it before anyone else can see or hear it.' : ''));
    render();
  },
  async itemEdit(f) {
    const args = { id: f.id.value, name: f.name.value, description: f.description.value, price: Number(f.price.value) || 0 };
    // (Ask the form's own list: a plain f.style is the form's CSS style when there's no Shape box,
    // which sent NaN and broke the whole request.)
    const field = (n) => f.elements.namedItem(n);
    if (field('style')) args.style = Number(field('style').value) || 2;
    const m = $('#itemEditMsg');
    if (field('offsaleAt')) {
      const when = field('offsaleAt').value ? Math.floor(new Date(field('offsaleAt').value).getTime() / 1000) : 0;
      if (Number.isNaN(when)) { m.className = 'error'; m.textContent = ' Pick a whole date and time for it to go off sale (or clear the box).'; return; }
      args.offsaleAt = when;
    }
    if (field('exclusive')) args.exclusive = field('exclusive').checked;
    if (field('giftItem')) args.giftItem = field('giftItem').value.trim();
    if (field('picture') && field('picture').files[0]) args.data = await gb.fileBase64(field('picture').files[0]);
    const r = await call('item.edit', args);
    if (!r.ok) { m.className = 'error'; m.textContent = ' ' + r.error; return; }
    toast('Saved!');
    render();
  },
  async joinDate(f) {
    const r = await call('account.joinDate', { date: f.date.value });
    if (!r.ok) { const m = $('#joinMsg'); m.className = 'error'; m.textContent = ' ' + r.error; return; }
    toast('Join date saved!');
    await hello();
    render();
  },
  async itemGive(f) {
    const m = $('#itemGiveMsg');
    const r = await call('item.give', { id: f.id.value, to: f.to.value.trim() });
    if (!r.ok) { m.className = 'error'; m.textContent = ' ' + r.error; return; }
    toast('Gave it to ' + r.to + '!');
    render();
  },
  async auctionStart(f) {
    const r = await call('auction.start', { id: f.id.value, copies: Number(f.copies.value) || 1, minBid: Number(f.minBid.value) || 1, hours: Number(f.hours.value) || 0 });
    if (!r.ok) { const m = $('#auctionMsg'); m.className = 'error'; m.textContent = ' ' + r.error; return; }
    toast('The auction is on!');
    render();
  },
  async auctionBid(f) {
    const amount = Number(f.amount.value) || 0;
    if (!confirm('Bid ' + amount + ' Bolts? They\'re held while you\'re winning, and come back if someone outbids you.')) return;
    const r = await call('auction.bid', { id: f.id.value, amount });
    if (r.ok) setMe(r.me);
    toast(r.ok ? 'Bid placed! You\'re one of the top bidders.' : r.error);
    render();
  },
  async itemLimited(f) {
    const r = await call('item.limited', { id: f.id.value, stock: Number(f.stock.value) || 0 });
    toast(r.ok ? 'It\'s Limited now!' : r.error);
    render();
  },
  async resaleList(f) {
    const r = await call('resale.list', { id: f.id.value, serial: Number(f.serial.value), price: Number(f.price.value) || 0 });
    toast(r.ok ? (Number(f.price.value) > 0 ? 'On sale!' : 'Taken off sale.') : r.error);
    render();
  },
  async tradeSend(f) {
    const pick = (side) => [...f.querySelectorAll('input[name=' + side + ']:checked')].map((x) => { const [id, serial] = x.value.split('|'); return { id, serial: Number(serial) }; });
    const r = await call('trade.send', { to: f.to.value, give: pick('give'), get: pick('get') });
    if (!r.ok) { const m = $('#tradeMsg'); m.className = 'error'; m.textContent = ' ' + r.error; return; }
    toast('Trade offer sent!');
    location.hash = '#/trades';
  },
  async rename(f) {
    const want = f.username.value.trim();
    if (!confirm('Change your username to "' + want + '" for 1,000 Bolts?')) return;
    const r = await call('account.rename', { username: want });
    const msg = $('#renameMsg');
    if (!r.ok) { msg.className = 'error'; msg.textContent = ' ' + r.error; return; }
    setMe(r.me);
    toast('You\'re now ' + r.me.username + '!');
    render();
  },
  async groupCreate(f) {
    const r = await call('groups.create', { name: f.name.value, description: f.description.value,
      color: parseInt(f.color.value.slice(1), 16), open: f.open.checked });
    if (!r.ok) { toast(r.error); return; }
    location.hash = '#/group/' + r.group.id;
  },
  async groupPost(f) {
    const r = await call('groups.post', { id: f.id.value, text: f.text.value });
    if (!r.ok) toast(r.error); else render();
  },
  async groupShout(f) {
    const r = await call('groups.shout', { id: f.id.value, text: f.text.value });
    if (!r.ok) toast(r.error); else render();
  },
  async groupRank(f) {
    const r = await call('groups.member', { id: f.id.value, user: f.user.value, action: 'rank', rank: f.rank.value });
    toast(r.ok ? 'Rank changed.' : r.error);
    if (r.ok) render();
  },
  async groupRankSave(f) {
    const perms = [...f.querySelectorAll('input[name=perm]:checked')].map((x) => x.value);
    const r = await call('groups.rank', { id: f.id.value, rank: { id: f.rank.value, name: f.name.value, level: Number(f.level.value), perms } });
    toast(r.ok ? 'Saved.' : r.error);
    if (r.ok) render();
  },
  async groupPayout(f) {
    const amount = Number(f.amount.value), who = f.user.selectedOptions[0].textContent;
    if (!confirm('Pay ' + who + ' ' + amount + ' Bolts from the group?')) return;
    const r = await call('groups.payout', { id: f.id.value, user: f.user.value, amount });
    toast(r.ok ? 'Paid ' + who + '.' : r.error);
    if (r.ok) render();
  },
  async groupEdit(f) {
    const r = await call('groups.edit', { id: f.id.value, description: f.description.value,
      color: parseInt(f.color.value.slice(1), 16), open: f.open.checked, lyreto: f.lyreto.value });
    toast(r.ok ? 'Saved.' : r.error);
    if (r.ok) render();
  },
};

// Drop-downs that go somewhere when you pick (like the catalog's "Sort by").
document.addEventListener('change', (e) => { if (e.target.matches && e.target.matches('select[data-go]')) location.hash = e.target.value; });

document.addEventListener('click', async (e) => {
  const el = e.target.closest('[data-act]');
  if (!el || !actions[el.dataset.act]) return;
  e.preventDefault();
  if (!signedIn() && NEEDS_ACCOUNT[el.dataset.act]) { loginPopup(NEEDS_ACCOUNT[el.dataset.act]); return; }
  if (el.disabled) return;
  el.disabled = true;
  try { await actions[el.dataset.act](el.dataset, el); } finally { el.disabled = false; }
});

document.addEventListener('submit', async (e) => {
  const f = e.target;
  if (!f.dataset.form || !forms[f.dataset.form]) return;
  e.preventDefault();
  if (!signedIn() && NEEDS_ACCOUNT[f.dataset.form]) { loginPopup(NEEDS_ACCOUNT[f.dataset.form]); return; }
  const btn = f.querySelector('button:not([type=button])');
  if (btn) btn.disabled = true;
  try { await forms[f.dataset.form](f); } finally { if (btn) btn.disabled = false; }
});

// --- the page picker ------------------------------------------------------------------

async function render() {
  const token = ++pageToken;
  const path = (location.hash.replace(/^#\/?/, '').split('?')[0] || 'home').split('/');
  route = path;
  const name = pages[path[0]] ? path[0] : 'home';
  document.querySelectorAll('#nav a[data-page]').forEach((a) => {
    const p = a.dataset.page;
    a.classList.toggle('on', p === name || (p === 'games' && name === 'game') || (p === 'catalog' && name === 'item') || (p === 'create' && name === 'library') ||
      (p === 'people' && name === 'user') || (p === 'groups' && name === 'group') || (p === 'forum' && name === 'forum'));
  });
  if (name !== 'avatar') avatarDraft = null;   // leaving the avatar page drops unsaved changes
  if (!me) {
    show(html`<h1>Can't reach the Guts&amp;Bolts server</h1>
      <p class="muted">It might be switched off right now. Try again in a bit.</p>
      <p><button class="btn blue" data-act="retry">Try again</button></p>`);
    return;
  }
  if (banned()) { banPage(); return; }
  try {
    await pages[name](...path.slice(1).map(decodeURIComponent));
  } catch (err) {
    if (err instanceof Stale || token !== pageToken) return;   // the visitor moved on: the new page draws itself
    show(html`<h1>Something went wrong</h1><p class="error">${String(err && err.message || err)}</p>`);
  }
}
actions.retry = async () => { await hello(); render(); };

window.addEventListener('hashchange', () => {
  document.querySelectorAll('.modal').forEach((m) => m.remove());
  window.scrollTo(0, 0);
  render();
});
const langReady = startTranslating();   // (loads while we say hello)
document.querySelector('.foot').append(languagePicker());
(async () => {
  await hello();
  await langReady;
  render();
  checkRequests();
  checkUpdates();
  setInterval(() => { if (document.visibilityState === 'visible') { checkRequests(); checkUpdates(); keepFresh(); } }, 60000);
})();
