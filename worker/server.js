// The Guts&Bolts server, running on Cloudflare (a Durable Object), so it stays
// online without anyone's computer. It is a JavaScript copy of the C++ server
// in src/server (same requests, same answers, same rules), with its data in the
// Durable Object's own SQLite database instead of JSON files.
//
// Two ways in:
//   POST /api   one signed request -> one answer (the website, and the apps' requests)
//   GET  /ws    a WebSocket, used like the C++ server's TCP connections: the
//               multiplayer relay (relay.host / relay.join / accept, then a pipe
//               that passes game messages straight through).
//
// If you change a rule here, change it in src/server too (and the other way round).
import { DurableObject } from 'cloudflare:workers';
import wasmModule from '../website/app/gbcrypto.wasm';
import { BUILT_IN_UPDATES } from './updates.js';
import { filterText, nameHasHateWord } from './textfilter.js';
// The example games that come with Guts&Bolts: always on the server (as Guts's
// games), so the website and the apps have something to play from day one.
import demolitionYard from '../games/Demolition Yard.gbscene';
import megaWaterSlide from '../games/Mega Water Slide.gbscene';
import nightPlaza from '../games/Night Plaza.gbscene';
import obbyOfDoom from '../games/Obby of Doom.gbscene';
const EXAMPLE_GAMES = [
  { id: 'game-demolition-yard', text: demolitionYard, genres: ['Destruction', 'Sandbox'] },
  { id: 'game-mega-water-slide', text: megaWaterSlide, genres: ['Adventure', 'Showcase'] },
  { id: 'game-night-plaza', text: nightPlaza, genres: ['Showcase', 'Town and City'] },
  { id: 'game-obby-of-doom', text: obbyOfDoom, genres: ['Obby'] },
];

// --- rules (src/online/Protocol.h) ---------------------------------------------
const kMaxClockSkew = 600;
const kDailyUploadsUnverified = 5;
// Pictures and sounds from creators who aren't Verified wait for a staff check before anyone
// else can see or hear them (the creator and staff can). a.review: 'pending' or 'rejected'.
const REVIEWED_KINDS = ['decal', 'audio', 'tshirt'];
const kCreatorSharePercent = 70;
const kMaxNotes = 50;   // notifications kept per account (the bell)
const KINDS = ['hat', 'shirt', 'pants', 'audio', 'plugin', 'game', 'decal', 'model', 'hair', 'faceacc', 'neck', 'shoulder', 'waist', 'face', 'tshirt', 'gear', 'animation'];
const FEE = { hat: 10, shirt: 10, pants: 10, audio: 20, plugin: 20, game: 0, decal: 5, model: 0,
  hair: 10, faceacc: 10, neck: 10, shoulder: 10, waist: 10, face: 0, tshirt: 10, gear: 0, animation: 0 };
// Accessories: things worn on the body, made (and placed on a mannequin) in Studio's
// Accessory window. Verified creators only. Faces are pictures, and only Guts makes them.
const ACCESSORIES = ['hat', 'hair', 'faceacc', 'neck', 'shoulder', 'waist'];
const isAccessory = (k) => ACCESSORIES.includes(k);
// Only Guts' own accessories and faces can become Limited.
const canBeLimited = (k) => isAccessory(k) || k === 'face';
// Models (objects published from Studio to the Library) are public or private. Verified
// creators can make as many public as they like; everyone else 5 a week.
const kPublicModelsPerWeek = 5;
const kMostPasses = 50;   // game passes per game
// Animations (published from Studio's Animation Editor) are public or private too, so other
// creators can use them, or not. They're free and have no weekly limit.
// Things that are public or private: games, models and animations.
const hasAccess = (k) => k === 'game' || k === 'model' || k === 'animation';
const weekOf = (t) => Math.floor(t / (7 * 86400));
const MAX_SIZE = { audio: 6 << 20, game: 24 << 20, plugin: 512 << 10, decal: 4 << 20, shirt: 1 << 20, pants: 1 << 20, model: 4 << 20,
  hat: 1 << 20, hair: 1 << 20, faceacc: 1 << 20, neck: 1 << 20, shoulder: 1 << 20, waist: 1 << 20, face: 1 << 20, tshirt: 1 << 20, gear: 4 << 20, animation: 1 << 20 };
// Gear: a Tool (made in Studio) sold in the catalog, like Roblox's old gear. Only staff
// make gear. You can have up to kMostGear equipped, and you get them in games whose
// creator ticked "Allow gear". (src/server has the same rules.)
const kMostGear = 4;
const isCatalogItem = (k) => isClothing(k) || k === 'gear';   // sold in the catalog
// Inventory on profiles: what someone owns from the catalog, in these groups. (src/server has the same.)
const INVENTORY_CATS = ['all', 'accessories', 'clothing', 'faces', 'gear', 'limited'];
function inventoryCat(cat, a) {
  if (cat === 'accessories') return isAccessory(a.kind);
  if (cat === 'clothing') return a.kind === 'shirt' || a.kind === 'pants' || a.kind === 'tshirt';
  if (cat === 'faces') return a.kind === 'face';
  if (cat === 'gear') return a.kind === 'gear';
  if (cat === 'limited') return !!a.limited;
  return true;
}
function gearProblem(data) {
  let m = null;
  try { m = JSON.parse(new TextDecoder().decode(data)); } catch { m = null; }
  if (!m || m.format !== 'gbmodel' || !Array.isArray(m.nodes) || m.nodes.length !== 1) return 'Gear must be one Tool published from Studio.';
  if (!m.nodes[0] || m.nodes[0].kind !== 'Tool') return 'Gear must be a Tool (with a Handle part inside).';
  return '';
}
// Shirts and pants can have a picture: a PNG laid out like the clothing template.
const kTemplateW = 585, kTemplateH = 559;
const pngSize = (d) => (d.length > 24 && [0x89, 0x50, 0x4e, 0x47].every((v, i) => d[i] === v)
  ? [(d[16] << 24 | d[17] << 16 | d[18] << 8 | d[19]) >>> 0, (d[20] << 24 | d[21] << 16 | d[22] << 8 | d[23]) >>> 0] : null);
const maxSize = (k) => MAX_SIZE[k] || 64 * 1024;
const isClothing = (k) => k === 'shirt' || k === 'pants' || k === 'tshirt' || k === 'face' || isAccessory(k);   // anything you wear
// T-shirts: any picture, worn flat on the front of the torso (over a shirt, if there is one).
const kTShirtMaxSide = 1024;
const jpgFile = (d) => d.length > 3 && d[0] === 0xff && d[1] === 0xd8 && d[2] === 0xff;
function tshirtProblem(data) {
  const size = pngSize(data);
  if (!size && !jpgFile(data)) return 'T-shirts must be .png or .jpg pictures.';
  if (size && (size[0] > kTShirtMaxSide || size[1] > kTShirtMaxSide)) return 'T-shirt pictures can be at most ' + kTShirtMaxSide + ' x ' + kTShirtMaxSide + '.';
  return '';
}
// Decals and audio are free-use assets: anyone can put them in their games.
const alwaysFree = (k) => k === 'decal' || k === 'audio' || k === 'animation';
// Why an account was banned (staff pick one). Shown to the banned player.
const BAN_REASONS = {
  sexual: 'Sexual content',
  extremism: 'Violent extremism',
  harassment: 'Harassment or bullying',
  hate: 'Hate speech or discrimination',
  threats: 'Threats of violence',
  selfharm: 'Promoting self-harm',
  scam: 'Scamming or phishing',
  personal: 'Sharing personal information',
  exploit: 'Cheating or exploiting',
  spam: 'Spam',
  impersonation: 'Impersonation',
  inappropriate: 'Inappropriate content',
  underage: 'Underage safety violation',
  other: 'Breaking the rules',
};
const banMessage = (u) => 'This account has been banned' + (u.banReason && BAN_REASONS[u.banReason]
  ? ' for: ' + BAN_REASONS[u.banReason] + '.' : '.') + (u.banNote ? ' Note from staff: ' + u.banNote : '');
const kMaxWarnings = 30;   // kept per account (oldest dropped)
const kOnlineFor = 150;                       // ServerFriends.cpp
const kMaxFriends = 200, kMaxRequests = 100;
const kMaxFollowing = 2000;
// Saved outfits, favourite games, "continue playing" and private messages (ServerSocial in
// src/server/Server.cpp has the same limits).
const kMaxOutfits = 30, kMaxFavorites = 200, kMaxRecent = 30;
const kMaxInbox = 100, kMaxSent = 50, kMessagesPerDay = 40;
// Classic profiles: an "About me" blurb, a "Right now I'm..." status (the last few
// make up your friends' My Feed), and player badges earned by doing things.
const kMaxBlurb = 1000, kMaxStatus = 140, kMaxPosts = 10, kStatusesPerDay = 30;
// Blocking and reports (ServerSafety.cpp has the same).
const kMaxBlocked = 200, kReportsPerDay = 20, kMaxReports = 3000;
// The staff action log: what staff did (bans, checks, badges...), newest kept (ServerSafety.cpp has the same).
const kMaxStaffLog = 3000;
// Support tickets (ServerSupport.cpp has the same): anyone, even someone who can't log in,
// can ask staff for help. A few open at once per device, and a few a day.
const kMaxTickets = 2000, kOpenTickets = 3, kTicketsPerDay = 5, kTicketLength = 1000, kResetHours = 24;
const TICKET_TOPICS = ['password', 'account', 'other'];
const KIND_TITLE = (a) => ({ tshirt: 'T-shirt', devproduct: 'product', gamepass: 'pass' })[a.kind] || a.kind;
const REPORT_KINDS = ['user', 'game', 'item', 'message', 'group', 'comment', 'forum'];
// Comments under games (ServerSocial.cpp has the same): the newest kMaxComments are kept.
const kMaxComments = 500, kCommentLength = 200, kCommentCooldown = 15, kCommentsPage = 20;
// The forum (ServerForum.cpp has the same boards and limits). Boards are fixed; anyone can
// read, signed-up players post, staff pin, lock and delete. Each thread keeps its posts, oldest first.
const FORUM_BOARDS = [
  { id: 'news', name: 'News & Announcements', about: 'Updates from the Guts&Bolts team.', staffOnly: true },
  { id: 'help', name: 'Help', about: 'Stuck? Ask how to do something in Guts&Bolts.' },
  { id: 'scripting', name: 'Scripting Helpers', about: 'Lua questions, scripts that won\'t work, and cool code.' },
  { id: 'building', name: 'Building & Studio', about: 'Tips, tricks and things you built in Studio.' },
  { id: 'games', name: 'Game Ads', about: 'Show off a game you made and get people playing it.' },
  { id: 'trading', name: 'Trading', about: 'Find people to trade Limiteds with.' },
  { id: 'offtopic', name: 'Off Topic', about: 'Anything else. Keep it friendly.' },
];
const kForumTitle = 80, kForumText = 3000, kForumPage = 20, kMaxForumPosts = 500, kMaxBoardThreads = 1000;
const kThreadCooldown = 60, kReplyCooldown = 15;
// Player badges (ServerSocial.cpp has the same list): earned automatically, checked
// whenever a profile is looked at. `need` says how to get one.
const PLAYER_BADGES = [
  { key: 'creator', name: 'Creator', need: 'Publish a game.' },
  { key: 'builder', name: 'Builder', need: 'Get 100 visits on your games.' },
  { key: 'architect', name: 'Architect', need: 'Get 1,000 visits on your games.' },
  { key: 'friendly', name: 'Friendly', need: 'Have 20 friends.' },
  { key: 'collector', name: 'Collector', need: 'Own 10 things from the catalog.' },
  { key: 'oldtimer', name: 'Old Timer', need: 'Be a member for a year.' },
];
const PRIVACY = ['everyone', 'friends', 'nobody'];                   // ServerFriends.cpp
const kGroupFee = 50, kMaxOwned = 5, kMaxJoined = 50, kWallSize = 200, kPostCooldown = 10;
// Group ranks (ServerGroups.cpp has the same). Each rank has a level (higher = more in
// charge) and permissions: shout, manage (let people in, remove people, delete wall posts,
// change the group's about), ranks (change people's ranks), games (put your games in the
// group) and funds (see the group's Bolts and pay people). The Owner can do everything.
const GROUP_PERMS = ['shout', 'manage', 'ranks', 'games', 'funds'];
const kMaxRanks = 10, kGroupLedger = 500;
const defaultRanks = () => [
  { id: 'Owner', name: 'Owner', level: 255, perms: GROUP_PERMS.slice() },
  { id: 'Admin', name: 'Admin', level: 200, perms: ['shout', 'manage', 'games'] },
  { id: 'Member', name: 'Member', level: 1, perms: [] },
];
const kMaxWrongPasswords = 5, kLockoutSeconds = 600;
const kRenameCost = 1000;   // Bolts to change your username
const kDefaultMax = 12, kMostPlayers = 30, kHostedEach = 3, kJoinWait = 15, kHostSilence = 90, kPipeSilence = 120;
// Paid private servers (ServerRelay.cpp has the same): a game's creator can charge Bolts for a
// private server. Buying one lasts 30 days (buy again to add 30 more); friends join free with its code.
const kPrivateDays = 30, kMaxPrivatePrice = 10000;
// When a host leaves, the players left behind get kMoveWait seconds to move to a new server
// (one of them hosts it), and each would-be new host gets kHeirWait seconds to start it.
const kMoveWait = 90, kHeirWait = 12;
// Game server machines (GutsAndBoltsGameServer): how many games each runs at once, and how long
// a player waits for one to start a game before hosting it themselves.
const kPoolMost = 50, kPoolStartWait = 25;
// DataStores: names and keys up to 100 letters, values up to 256 KB of JSON, 100,000 keys a game.
const kDataName = 100, kDataValue = 256 * 1024, kDataKeys = 100000;
const kStaffName = 'Guts';
const LOOK_ONLY = new Set(['forum.boards', 'forum.list', 'forum.thread', 'pass.list', 'pass.owned', 'list', 'asset.info', 'item.copies', 'profile', 'profile.inventory', 'people.list', 'users.search', 'groups.list', 'groups.get', 'servers.list', 'stats', 'thumb.get', 'icon.get', 'updates.list', 'comments.list']);
const UPDATE_TAGS = ['Engine', 'Studio', 'Website', 'Player', 'Server', 'Fix'];
// Email codes (adding an email, forgot password, two-step login).
const kCodeMinutes = 15, kCodeTries = 5, kMailGap = 60, kMailsPerDay = 8;
const isEmail = (e) => typeof e === 'string' && e.length <= 254 && /^[^\s@<>"]{1,64}@[^\s@<>"]{1,190}\.[^\s@<>"]{2,}$/.test(e);
function maskEmail(e) {
  if (!e) return '';
  const [name, host] = e.split('@');
  return (name.length <= 2 ? name[0] + '*' : name[0] + '*'.repeat(Math.min(6, name.length - 2)) + name[name.length - 1]) + '@' + host;
}
function sixDigits() { return String(crypto.getRandomValues(new Uint32Array(1))[0] % 1000000).padStart(6, '0'); }

// Who can play a game: everyone, the creator's friends, or only the creator.
const ACCESS = ['public', 'friends', 'private'];
// Genres a game can pick (up to 3), so people can find the kind of game they like.
const GENRES = ['Adventure', 'Obby', 'Fighting', 'Horror', 'Roleplay', 'Simulator', 'Tycoon', 'Racing', 'Sports',
  'Shooter', 'Puzzle', 'Survival', 'Comedy', 'Building', 'Sandbox', 'Showcase', 'Town and City', 'Destruction'];
// How well liked a game is, for sorting: likes out of votes, pulled towards 50% while there are few votes.
const ratingOf = (a) => ((a.likes || 0) + 1) / ((a.likes || 0) + (a.dislikes || 0) + 2);
// Guests (no account) can also play: download games, find and join servers (not chat, that's in the game).
const GUEST_OK = new Set([...LOOK_ONLY, 'get', 'support.send', 'support.mine', 'servers.play', 'relay.host', 'relay.join', 'product.pending', 'product.grant']);   // (product.*: guests can host games too)

// Assets have plain numbers counting up, like Roblox's asset IDs (1, 2, 3...). New ones
// use the number as their ID; older ones ("decal-1a2b3c4d5e") got a number too and
// answer to both, so games that already use the old IDs keep working.
// (src/server has the same.)
class AssetMap extends Map {
  constructor() { super(); this.byNum = new Map(); }
  set(id, a) { if (a && a.num) this.byNum.set(String(a.num), id); return super.set(id, a); }
  resolve(id) { id = String(id ?? ''); return super.has(id) ? id : (this.byNum.get(id) || id); }
  get(id) { return super.get(this.resolve(id)); }
  has(id) { return super.has(this.resolve(id)); }
  delete(id) { return super.delete(this.resolve(id)); }
}

// --- helpers ---------------------------------------------------------------------
const now = () => Math.floor(Date.now() / 1000);
const utcDay = (t) => new Date(t * 1000).toISOString().slice(0, 10);
const lower = (s) => String(s).replace(/[A-Z]/g, (c) => c.toLowerCase());
const upper = (s) => String(s).replace(/[a-z]/g, (c) => c.toUpperCase());
const isHex = (s, min, max) => typeof s === 'string' && s.length >= min && s.length <= max && /^[0-9a-fA-F]*$/.test(s);
const fail = (error) => ({ ok: false, error });
const okay = (extra) => Object.assign({ ok: true }, extra || {});
const enc = new TextEncoder();
const hexOf = (bytes) => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
const randomHex = (n) => hexOf(crypto.getRandomValues(new Uint8Array(n)));
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const num = (args, k) => (typeof args[k] === 'number' && isFinite(args[k]) ? Math.trunc(args[k]) : 0);
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

// Online::cleanText: drop control characters, cap the length (in UTF-8 bytes), trim.
function cleanText(s, maxLen, allowNewlines = false) {
  let out = '', bytes = 0;
  for (const ch of String(s || '')) {
    const c = ch.codePointAt(0);
    let keep = '';
    if (ch === '\n' && allowNewlines) keep = '\n';
    else if (c >= 32 && c !== 127) keep = ch;
    if (!keep) continue;
    const b = enc.encode(keep).length;
    if (bytes + b > maxLen) break;
    out += keep; bytes += b;
    if (bytes >= maxLen) break;
  }
  return out.replace(/^[ \n]+|[ \n]+$/g, '');
}

function usernameProblem(name, official = false) {
  if (name.length < 3) return 'Usernames need at least 3 characters.';
  if (name.length > 20) return 'Usernames can be at most 20 characters.';
  if (!/^[A-Za-z0-9_]*$/.test(name)) return 'Usernames can only have letters, numbers and _.';
  if ((name.match(/_/g) || []).length > 1) return 'Usernames can have only one _.';
  if (name[0] === '_' || name[name.length - 1] === '_') return 'Usernames can\'t start or end with _.';
  if (/^[0-9]+$/.test(name)) return 'Usernames can\'t be only numbers.';
  const l = lower(name.replace(/_/g, ''));
  if (l === 'guts') return official ? '' : 'That username belongs to Guts&Bolts staff.';
  if (['admin', 'administrator', 'staff', 'moderator', 'mod', 'gutsandbolts', 'gutsbolts', 'official', 'system',
    'server', 'roblox', 'support', 'help'].includes(l)) return 'That username is reserved.';
  if (nameHasHateWord(name)) return 'That username isn\'t allowed.';
  return '';
}
// What people write for others to read goes through the text filter (textfilter.js).
const say = (s, maxLen, allowNewlines = false) => filterText(cleanText(s, maxLen, allowNewlines));

// Lyreto (lyreto.com) is the chat app Guts&Bolts partners with: a game or a group can
// link its Lyreto server, and its page shows the link as a card (like Roblox does with Guilded).
// A link cleaned up to https://lyreto.com/..., '' for no link, or null when it isn't a Lyreto link.
export function lyretoLink(raw) {
  let s = String(raw || '').trim();
  if (!s) return '';
  if (s.length > 200) return null;
  if (!/^[a-z]+:\/\//i.test(s)) s = 'https://' + s;
  let u;
  try { u = new URL(s); } catch { return null; }
  if (u.protocol !== 'https:' && u.protocol !== 'http:') return null;
  if (u.username || u.password || u.port || !/^(www\.)?lyreto\.com$/i.test(u.hostname)) return null;
  const path = u.pathname.replace(/\/+$/, '');
  if (!/^(\/[A-Za-z0-9._~-]+)+$/.test(path)) return null;   // just lyreto.com isn't anyone's server
  if (u.search && !/^\?[A-Za-z0-9._~=&-]{1,100}$/.test(u.search)) return null;
  return 'https://lyreto.com' + path + u.search;
}
// Lyreto's invite pages say what server they're for in their preview tags (og:title and so on,
// the same ones chat apps use for link previews). This reads them into the card shown on the page.
export function lyretoCardFrom(page) {
  const tag = (prop) => {
    for (const m of String(page).matchAll(/<meta\s[^>]*>/gi)) {
      const t = m[0], name = /(?:property|name)\s*=\s*["']([^"']+)["']/i.exec(t), content = /content\s*=\s*["']([^"']*)["']/i.exec(t);
      if (name && content && name[1].toLowerCase() === prop) return content[1];
    }
    return '';
  };
  const unescape = (x) => x.replace(/&(amp|lt|gt|quot|#39|apos);/g, (_, e) => ({ amp: '&', lt: '<', gt: '>', quot: '"', '#39': "'", apos: "'" })[e]);
  const title = say(unescape(tag('og:title')), 100), description = say(unescape(tag('og:description')), 300);
  let image = unescape(tag('og:image'));
  try { const u = new URL(image); image = u.protocol === 'https:' && /(^|\.)lyreto\.com$/i.test(u.hostname) ? u.href : ''; } catch { image = ''; }
  return title ? { title, description, image } : null;
}
const nameIsReserved = (name) => lower(name).replace(/[ _.]/g, '') === lower(kStaffName);

function makeCode() {
  const letters = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  return Array.from(crypto.getRandomValues(new Uint8Array(6)), (b) => letters[b % 32]).join('');
}

// --- crypto (the same Monocypher as the apps, as WebAssembly) ----------------------
let wasm = null;
function crypt() {
  if (!wasm) wasm = new WebAssembly.Instance(wasmModule, {}).exports;
  return wasm;
}
const unhex = (s) => {
  const out = new Uint8Array(s.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(s.substr(i * 2, 2), 16);
  return out;
};
function verify(idHex, message, sigHex) {
  if (!isHex(idHex, 64, 64) || !isHex(sigHex, 128, 128)) return false;
  const w = crypt(), msg = enc.encode(message);
  const m = new Uint8Array(w.memory.buffer, w.buf(), w.bufSize());
  if (msg.length > m.length - 128) return false;
  m.set(unhex(idHex), 0); m.set(unhex(sigHex), 32); m.set(msg, 128);
  return w.check(msg.length) === 0;
}
function hashHex(text) {
  const w = crypt(), data = enc.encode(text);
  const m = new Uint8Array(w.memory.buffer, w.buf(), w.bufSize());
  m.set(data, 128);
  w.hash(data.length);
  return hexOf(new Uint8Array(w.memory.buffer, w.buf() + 64, 32));
}

// --- Authenticator apps (TOTP, RFC 6238): a 6-digit code that changes every 30 seconds,
// made from a secret shared once with the app (as a QR code / text). src/core/Totp.h
// does exactly the same for the C++ server.
function sha1(bytes) {
  const ml = bytes.length, words = ((ml + 8) >> 6) + 1, w = new Uint32Array(words * 16);
  for (let i = 0; i < ml; i++) w[i >> 2] |= bytes[i] << (24 - (i % 4) * 8);
  w[ml >> 2] |= 0x80 << (24 - (ml % 4) * 8);
  w[words * 16 - 1] = ml * 8;
  let h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  const x = new Uint32Array(80);
  for (let b = 0; b < words * 16; b += 16) {
    for (let i = 0; i < 16; i++) x[i] = w[b + i];
    for (let i = 16; i < 80; i++) { const v = x[i - 3] ^ x[i - 8] ^ x[i - 14] ^ x[i - 16]; x[i] = (v << 1) | (v >>> 31); }
    let a = h0, bb = h1, c = h2, d = h3, e = h4;
    for (let i = 0; i < 80; i++) {
      const f = i < 20 ? (bb & c) | (~bb & d) : i < 40 ? bb ^ c ^ d : i < 60 ? (bb & c) | (bb & d) | (c & d) : bb ^ c ^ d;
      const k = i < 20 ? 0x5A827999 : i < 40 ? 0x6ED9EBA1 : i < 60 ? 0x8F1BBCDC : 0xCA62C1D6;
      const t = (((a << 5) | (a >>> 27)) + f + e + k + x[i]) >>> 0;
      e = d; d = c; c = (bb << 30) | (bb >>> 2); bb = a; a = t;
    }
    h0 = (h0 + a) >>> 0; h1 = (h1 + bb) >>> 0; h2 = (h2 + c) >>> 0; h3 = (h3 + d) >>> 0; h4 = (h4 + e) >>> 0;
  }
  const out = new Uint8Array(20);
  [h0, h1, h2, h3, h4].forEach((v, i) => { out[i * 4] = v >>> 24; out[i * 4 + 1] = (v >>> 16) & 255; out[i * 4 + 2] = (v >>> 8) & 255; out[i * 4 + 3] = v & 255; });
  return out;
}
function hmacSha1(key, msg) {
  if (key.length > 64) key = sha1(key);
  const k = new Uint8Array(64); k.set(key);
  const inner = new Uint8Array(64 + msg.length), outer = new Uint8Array(84);
  for (let i = 0; i < 64; i++) { inner[i] = k[i] ^ 0x36; outer[i] = k[i] ^ 0x5c; }
  inner.set(msg, 64);
  outer.set(sha1(inner), 64);
  return sha1(outer);
}
const B32 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
function base32(bytes) {
  let bits = 0, val = 0, out = '';
  for (const b of bytes) { val = (val << 8) | b; bits += 8; while (bits >= 5) { out += B32[(val >>> (bits - 5)) & 31]; bits -= 5; } }
  if (bits > 0) out += B32[(val << (5 - bits)) & 31];
  return out;
}
function unbase32(text) {
  let bits = 0, val = 0; const out = [];
  for (const ch of String(text).toUpperCase().replace(/[^A-Z2-7]/g, '')) {
    val = (val << 5) | B32.indexOf(ch); bits += 5;
    if (bits >= 8) { out.push((val >>> (bits - 8)) & 255); bits -= 8; }
  }
  return new Uint8Array(out);
}
function totpCode(secret, step) {
  const msg = new Uint8Array(8);
  let s = step;
  for (let i = 7; i >= 0; i--) { msg[i] = s & 255; s = Math.floor(s / 256); }
  const h = hmacSha1(unbase32(secret), msg), o = h[19] & 15;
  const bin = ((h[o] & 127) << 24) | (h[o + 1] << 16) | (h[o + 2] << 8) | h[o + 3];
  return String(bin % 1000000).padStart(6, '0');
}
// The 30-second step the code belongs to (one step early or late is fine: clocks drift), or -1.
function totpCheck(secret, code, t, lastStep = -1) {
  code = String(code || '').replace(/\s/g, '');
  if (!/^[0-9]{6}$/.test(code)) return -1;
  const step = Math.floor(t / 30);
  for (const d of [0, -1, 1]) if (step + d > lastStep && totpCode(secret, step + d) === code) return step + d;
  return -1;
}

// The Verified Hat: everyone who confirms their email gets one (an original
// Guts&Bolts cap with our blue check, not a copy of anything). Server.cpp makes the same one.
const VERIFIED_HAT_ID = 'item-verified-hat';
// Gutstober: Guts&Bolts' Halloween month (all of October). Things made for it, like
// the Pumpkin hat, are timed items: they go off sale when it ends (midnight UTC,
// November 1st).
function gutstoberEnd(t) {
  const d = new Date(t * 1000);
  return Date.UTC(d.getUTCFullYear(), 10, 1) / 1000;
}
const VERIFIED_HAT = {
  format: 'gbaccessory', version: 1, kind: 'hat',
  node: { id: 1, name: 'VerifiedHat', kind: 'Model', pos: [0, 2.66, 0], rot: [0, 0, 0], size: [1, 1, 1], children: [
    { id: 2, name: 'Crown', kind: 'Part', shape: 'Sphere', pos: [0, 0.02, -0.02], rot: [0, 0, 0], size: [0.8, 0.5, 0.8], color: [0.1, 0.13, 0.2], material: 'Fabric', anchored: true, canCollide: false },
    { id: 3, name: 'Brim', kind: 'Part', shape: 'Cube', pos: [0, -0.06, 0.42], rot: [-8, 0, 0], size: [0.62, 0.04, 0.36], color: [0.1, 0.13, 0.2], material: 'Fabric', anchored: true, canCollide: false },
    { id: 4, name: 'Badge', kind: 'Part', shape: 'Cylinder', pos: [0, 0.08, 0.36], rot: [72, 0, 0], size: [0.24, 0.04, 0.24], color: [0.16, 0.55, 1.0], material: 'SmoothPlastic', anchored: true, canCollide: false },
    { id: 5, name: 'CheckShort', kind: 'Part', shape: 'Cube', pos: [-0.035, 0.065, 0.385], rot: [72, 0, 45], size: [0.035, 0.08, 0.02], color: [1, 1, 1], material: 'SmoothPlastic', anchored: true, canCollide: false },
    { id: 6, name: 'CheckLong', kind: 'Part', shape: 'Cube', pos: [0.03, 0.085, 0.38], rot: [72, 0, -40], size: [0.035, 0.15, 0.02], color: [1, 1, 1], material: 'SmoothPlastic', anchored: true, canCollide: false },
  ] },
};

// nlohmann::json::dump(): keys sorted, no spaces (what signatures are made over).
function canon(v) {
  if (Array.isArray(v)) return '[' + v.map(canon).join(',') + ']';
  if (v && typeof v === 'object') return '{' + Object.keys(v).sort().map((k) => JSON.stringify(k) + ':' + canon(v[k])).join(',') + '}';
  if (typeof v === 'number') return Number.isInteger(v) ? String(v) : JSON.stringify(v);
  return JSON.stringify(v === undefined ? null : v);
}
const requestText = (op, account, time, nonce, args) => 'gb-req:' + op + '\n' + account + '\n' + time + '\n' + nonce + '\n' + canon(args);
const grantMessage = (key, accountId) => 'gb-badge:' + key + ':' + accountId;

function grantValid(official, key, accountId, sig) {
  if (!official || typeof sig !== 'string') return false;
  if (!sig.startsWith('s:')) return verify(official, grantMessage(key, accountId), sig);
  if (key !== 'verified') return false;   // the only badge Staff members can give
  const a = sig.indexOf(':', 2), b = a < 0 ? -1 : sig.indexOf(':', a + 1);
  if (b < 0) return false;
  const staffId = sig.slice(2, a), staffSig = sig.slice(a + 1, b), grantSig = sig.slice(b + 1);
  return verify(official, grantMessage('staff', staffId), staffSig) && verify(staffId, grantMessage(key, accountId), grantSig);
}

// base64 <-> bytes (uploads)
function b64ToBytes(s) {
  const bin = atob(String(s).replace(/[\r\n ]/g, ''));
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
function bytesToB64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s);
}
const CHUNK = 1 << 20;   // files are stored in 1 MB pieces (SQLite rows are at most 2 MB)

// --- the server -------------------------------------------------------------------
export class GbServerObject extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.env = env;
    this.sql = ctx.storage.sql;
    this.sql.exec(`CREATE TABLE IF NOT EXISTS users (id TEXT PRIMARY KEY, data TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS assets (id TEXT PRIMARY KEY, data TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS groups (id TEXT PRIMARY KEY, data TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS forum (id TEXT PRIMARY KEY, data TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
      CREATE TABLE IF NOT EXISTS files (id TEXT NOT NULL, part INTEGER NOT NULL, data BLOB NOT NULL, PRIMARY KEY (id, part));
      CREATE TABLE IF NOT EXISTS nonces (key TEXT PRIMARY KEY, time INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS gamedata (game TEXT NOT NULL, store TEXT NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL,
        updated INTEGER NOT NULL, PRIMARY KEY (game, store, key));`);
    this.official = lower(env.OFFICIAL || '');
    this.name = env.SERVER_NAME || 'Guts&Bolts';
    this.users = new Map(); this.assets = new AssetMap(); this.groups = new Map(); this.forum = new Map();
    // What changed and needs writing (set up first: loading can already change things).
    this.dirty = { users: new Set(), assets: new Set(), groups: new Set(), forum: new Set(), ids: false, trades: false, updates: false, reports: false, staffLog: false, tickets: false };
    for (const r of this.sql.exec('SELECT id, data FROM users')) this.users.set(r.id, JSON.parse(r.data));
    for (const r of this.sql.exec('SELECT id, data FROM assets')) {
      const a = JSON.parse(r.data);
      if (alwaysFree(a.kind)) a.price = 0;   // decals and audio are always free now
      this.assets.set(r.id, a);
    }
    for (const r of this.sql.exec('SELECT id, data FROM groups')) this.groups.set(r.id, JSON.parse(r.data));
    for (const r of this.sql.exec('SELECT id, data FROM forum')) this.forum.set(r.id, JSON.parse(r.data));
    this.trades = this.getMeta('trades', []);   // trade offers between players (limited items)
    this.posted = this.getMeta('updates', []);  // updates staff posted on the website (the rest are in updates.js)
    this.reports = this.getMeta('reports', []);  // what players reported, for staff to look at (newest last)
    this.staffLog = this.getMeta('stafflog', []);   // what staff did, newest last
    this.tickets = this.getMeta('tickets', []);   // support tickets (like "I forgot my password"), newest last
    const ids = this.getMeta('ids', { next: 2, taken: [] });
    this.nextUserId = Math.max(2, ids.next || 2);
    this.nextAssetNum = Math.max(1, ids.asset || 1);
    this.takenNames = new Set(ids.taken || []);
    for (const u of this.users.values()) {
      this.claimOfficial(u);
      if (u.userId > 0) { this.nextUserId = Math.max(this.nextUserId, u.userId + 1); this.takenNames.add(lower(u.username)); }
    }
    this.takenNames.add('guts');
    // Guts follows everyone (like Builderman did on old Roblox).
    for (const u of this.users.values()) this.gutsFollows(u);
    this.addExampleGames();
    this.addVerifiedHat();
    this.timeGutstoberItems();
    this.numberAssets();
    for (const u of this.users.values()) if (u.emailVerified && !u.owned.includes(VERIFIED_HAT_ID)) { this.giveVerifiedHat(u); this.saveUser(u); }
    // Keys made by "forgot password": they sign for the account they reset.
    this.aliases = new Map();
    for (const u of this.users.values()) for (const k of u.keys || []) this.aliases.set(k, u.id);
    // Emails to send once the request is done (see sendMail).
    this.outbox = [];
    // Lyreto links whose preview card still needs fetching: [kind, id] (see fetchLyretoCards).
    this.lyretoQueue = [];
    this.canMail = !!(((env.BREVO_API_KEY || env.RESEND_API_KEY) && env.MAIL_FROM) || env.MAIL_DEBUG);
    this.mailLast = null;   // how the last email went: { at, to, ok, error } (for the Staff page)
    this.failedLogins = new Map();
    this.lastPost = new Map();
    // The relay (in memory: a restart closes every game, like the C++ server).
    this.conns = new Map();      // conn id -> { ws, mode, account, session, ticket, peer, since, lastActive, closing }
    this.sessions = new Map();   // session id -> { id, game, title, host, code, priv, max, created, control, players:Set, dedicated }
    // Game server machines: control connection id -> { account, slots, starting: Map(game -> when asked) }
    this.pools = new Map();
    // Servers whose host left: old session id -> { game, title, priv, code, max, members:[accounts], heir, heirSince, newId, until }
    this.moved = new Map();
    this.sweepTimer = null;
  }

  getMeta(key, fallback) {
    for (const r of this.sql.exec('SELECT value FROM meta WHERE key = ?', key)) return JSON.parse(r.value);
    return fallback;
  }

  // Save what a request changed (called once at the end of each request).
  flush() {
    for (const id of this.dirty.users) {
      const u = this.users.get(id);
      if (u) this.sql.exec('INSERT OR REPLACE INTO users (id, data) VALUES (?, ?)', id, JSON.stringify(u));
    }
    for (const id of this.dirty.assets) {
      const a = this.assets.get(id);
      if (a) this.sql.exec('INSERT OR REPLACE INTO assets (id, data) VALUES (?, ?)', id, JSON.stringify(a));
      else this.sql.exec('DELETE FROM assets WHERE id = ?', id);
    }
    for (const id of this.dirty.groups) {
      const g = this.groups.get(id);
      if (g) this.sql.exec('INSERT OR REPLACE INTO groups (id, data) VALUES (?, ?)', id, JSON.stringify(g));
      else this.sql.exec('DELETE FROM groups WHERE id = ?', id);
    }
    for (const id of this.dirty.forum) {
      const th = this.forum.get(id);
      if (th) this.sql.exec('INSERT OR REPLACE INTO forum (id, data) VALUES (?, ?)', id, JSON.stringify(th));
      else this.sql.exec('DELETE FROM forum WHERE id = ?', id);
    }
    if (this.dirty.trades) {
      this.trades = this.trades.filter((x) => x.status === 'open' || now() - x.updated < 30 * 86400).slice(-3000);
      this.sql.exec('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)', 'trades', JSON.stringify(this.trades));
    }
    if (this.dirty.ids) {
      this.sql.exec('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)', 'ids',
        JSON.stringify({ next: this.nextUserId, taken: [...this.takenNames], asset: this.nextAssetNum }));
    }
    if (this.dirty.updates)
      this.sql.exec('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)', 'updates', JSON.stringify(this.posted));
    if (this.dirty.reports) {
      // Keep every open report, and only the newest closed ones.
      if (this.reports.length > kMaxReports) {
        const extra = this.reports.length - kMaxReports;
        let dropped = 0;
        this.reports = this.reports.filter((x) => x.status === 'open' || dropped++ >= extra);
      }
      this.sql.exec('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)', 'reports', JSON.stringify(this.reports));
    }
    if (this.dirty.tickets) {
      // Keep every open ticket, and only the newest closed ones.
      if (this.tickets.length > kMaxTickets) {
        const extra = this.tickets.length - kMaxTickets;
        let dropped = 0;
        this.tickets = this.tickets.filter((x) => x.status === 'open' || dropped++ >= extra);
      }
      this.sql.exec('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)', 'tickets', JSON.stringify(this.tickets));
    }
    if (this.dirty.staffLog) {
      if (this.staffLog.length > kMaxStaffLog) this.staffLog.splice(0, this.staffLog.length - kMaxStaffLog);
      this.sql.exec('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)', 'stafflog', JSON.stringify(this.staffLog));
    }
    this.dirty = { users: new Set(), assets: new Set(), groups: new Set(), forum: new Set(), ids: false, trades: false, updates: false, reports: false, staffLog: false, tickets: false };
  }
  // --- Limited items: numbered copies (a.copies = [{ serial, owner, price }]) ---
  copiesOf(u) {   // every limited copy this account holds
    const out = [];
    for (const a of this.assets.values())
      if (a.limited) for (const c of a.copies || []) if (c.owner === u.id) out.push({ id: a.id, name: a.name, kind: a.kind, serial: c.serial, price: c.price || 0 });
    return out;
  }
  // After copies change hands: `owned` (what you can wear) follows the copies you hold.
  syncOwned(u, a) {
    const has = (a.copies || []).some((c) => c.owner === u.id);
    if (has && !u.owned.includes(a.id)) u.owned.push(a.id);
    if (!has && a.creator !== u.id) {
      u.owned = u.owned.filter((x) => x !== a.id);
      if (u.avatar && Array.isArray(u.avatar.wearing)) u.avatar.wearing = u.avatar.wearing.filter((x) => x !== a.id);
    }
    this.saveUser(u);
  }
  limitedJson(a) {   // what the catalog shows about a limited
    if (!a.limited) return undefined;
    const listed = (a.copies || []).filter((c) => c.price > 0);
    return { stock: a.stock, sold: a.sales, left: Math.max(0, a.stock - a.sales),
      resellers: listed.length, lowest: listed.length ? Math.min(...listed.map((c) => c.price)) : 0 };
  }
  saveUser(...us) { for (const u of us) if (u) this.dirty.users.add(u.id); }
  // The bell: tell someone something happened (a friend request, a sale, an upload checked...).
  // kind says what (the apps pick an icon and a page from it), id is what it's about.
  notify(u, kind, text, id = '') {
    if (!u || u.userId === 0) return;
    u.notes = [{ id: randomHex(5), kind, text, about: id, at: now(), read: false }, ...(u.notes || [])].slice(0, kMaxNotes);
    this.saveUser(u);
  }
  saveAsset(a) { this.dirty.assets.add(a.id); }
  // Creator stats: count something on a day (plays, sales, bolts earned), kept for 60 days.
  // Passes and products also count on their game, so a game's page adds them up.
  tally(a, field, n = 1) {
    if (!a || !n) return;
    const day = utcDay(now());
    a.days = a.days || {};
    const d = a.days[day] || (a.days[day] = {});
    d[field] = (d[field] || 0) + n;
    const keys = Object.keys(a.days).sort();
    while (keys.length > 60) delete a.days[keys.shift()];
    this.saveAsset(a);
    if ((a.kind === 'gamepass' || a.kind === 'devproduct') && field !== 'plays') this.tally(this.assets.get((a.meta || {}).game), field, n);
  }
  // Give every asset without one its number (oldest first), and never hand a number out twice.
  numberAssets() {
    for (const a of this.assets.values()) if (a.num) this.nextAssetNum = Math.max(this.nextAssetNum, a.num + 1);
    const todo = [...this.assets.values()].filter((a) => !a.num).sort((x, y) => (x.created || 0) - (y.created || 0) || (x.id < y.id ? -1 : 1));
    for (const a of todo) { a.num = this.nextAssetNum++; this.assets.set(a.id, a); this.saveAsset(a); }
    if (todo.length) this.dirty.ids = true;
  }
  saveGroup(g) { this.dirty.groups.add(g.id); }

  writeFile(id, bytes) {
    this.sql.exec('DELETE FROM files WHERE id = ?', id);
    for (let i = 0, part = 0; i < bytes.length || part === 0; i += CHUNK, part++)
      this.sql.exec('INSERT INTO files (id, part, data) VALUES (?, ?, ?)', id, part, bytes.subarray(i, i + CHUNK));
    return true;
  }
  readFile(id) {
    const parts = [...this.sql.exec('SELECT data FROM files WHERE id = ? ORDER BY part', id)].map((r) => new Uint8Array(r.data));
    if (!parts.length) return null;
    const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
    let at = 0;
    for (const p of parts) { out.set(p, at); at += p.length; }
    return out;
  }

  // --- accounts ---
  isOfficial(u) { return !!this.official && u.id === this.official; }
  isStaff(u) { return this.isOfficial(u) || grantValid(this.official, 'staff', u.id, (u.grants || {}).staff); }
  isVerified(u) { return this.isOfficial(u) || grantValid(this.official, 'verified', u.id, (u.grants || {}).verified); }
  claimOfficial(u) {
    if (!this.isOfficial(u) || u.userId === 1) return;
    u.userId = 1; u.username = kStaffName;
    this.takenNames.add(lower(kStaffName));
    this.saveUser(u);
  }
  user(id) {
    let u = this.users.get(id);
    if (u) return u;
    u = { id, name: 'Player', created: now(), lastSeen: 0, ledger: [], grants: {}, owned: [], uploadDay: '', uploadsToday: 0,
      playDay: '', playEarned: 0, lastPlay: 0, banned: false, friends: [], friendIn: [], friendOut: [], following: [], followers: [],
      username: '', userId: 0, pwSalt: '', pwHash: '', keyBlob: '' };
    this.users.set(id, u);
    this.claimOfficial(u);
    this.saveUser(u);
    return u;
  }
  findUser(id) { return this.users.get(lower(id || '')) || null; }
  findUsername(name) {
    const want = lower(name);
    for (const u of this.users.values()) if (u.userId > 0 && lower(u.username) === want) return u;
    return null;
  }
  // The Guts account (#1) follows every signed-up player. (ServerAccounts.cpp gutsFollows)
  gutsFollows(u) {
    const g = this.findUserId(1);
    if (!g || u.userId <= 1 || g.id === u.id) return;
    const following = g.following || (g.following = []), followers = u.followers || (u.followers = []);
    if (following.includes(u.id) && followers.includes(g.id)) return;
    if (!following.includes(u.id)) following.push(u.id);
    if (!followers.includes(g.id)) followers.push(g.id);
    this.saveUser(g, u);
  }
  findUserId(n) { if (n <= 0) return null; for (const u of this.users.values()) if (u.userId === n) return u; return null; }
  // An account key, or "@name" / a name as shown in games (the in-game player list).
  // The user number (5 or "#5") is the main way to name someone; long account keys,
  // "@username" and names work too. (ServerAccounts.cpp findPerson)
  findPerson(s) {
    if (typeof s === 'number') return this.findUserId(Math.trunc(s));
    s = String(s || '').trim();
    if (/^#?[0-9]{1,11}$/.test(s)) return this.findUserId(Number(s.replace('#', '')));
    const u = this.findUser(s) || this.findUsername(s.replace(/^@/, ''));
    if (u) return u;
    const want = lower(s);
    for (const v of this.users.values()) if (v.userId > 0 && lower(v.name) === want) return v;
    return null;
  }
  balance(u) { return u.ledger.reduce((s, e) => s + e[0], 0); }
  hasRef(u, ref) { return u.ledger.some((e) => e[3] === ref); }
  add(u, amount, reason, ref) {
    u.ledger.push([amount, reason, now(), ref]);
    if (u.ledger.length > 2000) {
      const cut = u.ledger.length - 1500;
      const old = u.ledger.slice(0, cut).reduce((s, e) => s + e[0], 0);
      u.ledger = [[old, 'Earlier history', 0, 'carry'], ...u.ledger.slice(cut)];
    }
    this.saveUser(u);
  }
  isOnline(u) { return now() - u.lastSeen <= kOnlineFor || !!this.sessionOf(u.id); }
  // Privacy, like Roblox's: who sees you online / what you're playing, and who can
  // join you there: 'everyone', 'friends' or 'nobody'. (Server.cpp has the same rules.)
  privacyOf(u) {
    const p = u.privacy || {};
    return { status: PRIVACY.includes(p.status) ? p.status : 'everyone', join: PRIVACY.includes(p.join) ? p.join : 'everyone',
      messages: PRIVACY.includes(p.messages) ? p.messages : 'everyone',
      inventory: PRIVACY.includes(p.inventory) ? p.inventory : 'everyone' };   // who sees your inventory
  }
  // Can `viewer` see what `u` owns? (Staff always can, to sort out trades and scams.)
  seesInventory(viewer, u) {
    return (!!viewer && this.isStaff(viewer)) || (this.allows(this.privacyOf(u).inventory, viewer, u) && !this.blocks(viewer, u));
  }
  // Someone's inventory: the catalog items they own, newest first, with the numbers of the Limited copies they hold.
  inventoryOf(u, cat = 'all') {
    const items = [];
    for (const id of [...(u.owned || [])].reverse()) {
      const a = this.assets.get(id);
      if (!a || !isCatalogItem(a.kind) || !inventoryCat(cat, a)) continue;
      const serials = a.limited ? (a.copies || []).filter((c) => c.owner === u.id).map((c) => c.serial).sort((x, y) => x - y) : [];
      items.push(Object.assign(this.publicAsset(a), { serials }));
    }
    return items;
  }
  allows(setting, viewer, u) {
    if (viewer && viewer.id === u.id) return true;
    if (setting === 'everyone') return true;
    return setting === 'friends' && !!viewer && u.friends.includes(viewer.id);
  }
  // Has either of them blocked the other? (Then they can't message, friend, follow, trade or join each other.)
  // The staff action log. action: ban, unban, warn, bolts, badge, unbadge, review, report, delete, comment.
  // about: who it was done to (an account id), so the log can link to them.
  staffDid(me, action, text, about = '') {
    this.staffLog.push({ at: now(), by: me.id, action, text, about });
    this.dirty.staffLog = true;
  }
  staffLogJson(who) {
    const name = (id) => { const u = this.users.get(id); return u ? { id: u.id, userId: u.userId, name: u.name } : { id, userId: 0, name: '?' }; };
    const list = who ? this.staffLog.filter((x) => x.by === who || x.about === who) : this.staffLog;
    return list.slice(-200).reverse().map((x) => ({ at: x.at, by: name(x.by), action: x.action, text: x.text, about: x.about ? name(x.about) : null }));
  }
  blocks(a, b) {
    return !!a && !!b && a.id !== b.id && ((a.blocked || []).includes(b.id) || (b.blocked || []).includes(a.id));
  }
  // What `viewer` may know about where `u` is: { online, playing: {game, title, session (null = can't join)} }.
  presence(viewer, u) {
    const pv = this.privacyOf(u);
    if (!this.allows(pv.status, viewer, u) || this.blocks(viewer, u)) return { online: false, playing: null };
    const s = this.sessionOf(u.id);
    const g = s && this.assets.get(s.game);
    const playing = s ? { game: s.game, title: s.title || (g ? g.name : 'a game'), private: s.priv,
      full: this.headcount(s) >= s.max, session: this.allows(pv.join, viewer, u) ? s.id : null } : null;
    return { online: this.isOnline(u), playing };
  }
  // The player badges `u` has earned (PLAYER_BADGES), each { key, name, need }.
  playerBadgesOf(u) {
    const games = [...this.assets.values()].filter((a) => a.kind === 'game' && a.creator === u.id);
    const visits = games.reduce((n, a) => n + (a.plays || 0), 0);
    const items = (u.owned || []).filter((id) => { const a = this.assets.get(id); return a && isCatalogItem(a.kind); }).length;
    const has = { creator: games.length > 0, builder: visits >= 100, architect: visits >= 1000, friendly: u.friends.length >= 20,
      collector: items >= 10, oldtimer: now() - (u.created || now()) >= 365 * 86400 };
    return PLAYER_BADGES.filter((b) => has[b.key]);
  }
  badgesOf(u) {
    const b = [];
    if (this.isOfficial(u)) b.push('admin');
    for (const [key, sig] of Object.entries(u.grants || {})) if (grantValid(this.official, key, u.id, sig)) b.push(key);
    return b;
  }

  publicUser(u) {
    return { id: u.id, name: u.name, username: u.username, userId: u.userId, verified: this.isVerified(u),
      staff: this.isStaff(u), official: this.isOfficial(u), created: u.created, banned: u.banned,
      pastNames: u.pastNames || [],
      banReason: u.banned ? (u.banReason || '') : undefined };
  }
  meJson(u) {
    const today = utcDay(now());
    return Object.assign(this.publicUser(u), {
      bolts: this.balance(u), hasPassword: !!u.keyBlob, grants: Object.entries(u.grants || {}),
      canDaily: !this.hasRef(u, 'daily:' + today),
      playEarnedToday: u.playDay === today ? u.playEarned : 0,
      uploadsLeft: this.isVerified(u) ? -1 : kDailyUploadsUnverified - (u.uploadDay === today ? u.uploadsToday : 0),
      publicModelsLeft: this.publicModelsLeft(u),
      owned: [...u.owned].sort(),
      avatar: u.avatar || null,
      email: maskEmail(u.emailVerified ? u.email : ''), emailPending: maskEmail(u.pendingEmail || ''),
      twoStep: !!(u.twoStep && u.emailVerified), canMail: this.canMail, authApp: !!u.totpSecret, privacy: this.privacyOf(u),
      gear: (u.gear || []).filter((g) => u.owned.includes(g)),
      // Banned: what for and until when (the app and the site show a ban screen).
      ban: u.banned ? { reason: u.banReason || '', title: BAN_REASONS[u.banReason] || 'Breaking the rules',
        note: u.banNote || '', at: u.bannedAt || 0, until: u.bannedUntil || 0 } : null,
      // Staff warnings not seen yet (shown once, until "I understand").
      warnings: (u.warnings || []).filter((w) => !w.seen)
        .map((w) => ({ id: w.id, reason: w.reason, title: BAN_REASONS[w.reason] || 'Breaking the rules', note: w.note, at: w.at })),
      warningCount: (u.warnings || []).length,
      unreadMessages: (u.inbox || []).filter((m) => !m.read).length,
      unreadNotes: (u.notes || []).filter((n) => !n.read).length,
    });
  }

  // --- email codes ---
  // Make a code for `purpose` and put an email with it in the outbox. Returns an error text, or ''.
  mailCode(u, purpose, to, subject, what) {
    if (!this.canMail) return 'Email isn\'t set up on this server yet, so it can\'t send codes.';
    const t = now();
    u.mailTimes = (u.mailTimes || []).filter((x) => t - x < 86400);
    const last = u.codes && u.codes[purpose];   // one of each kind of email a minute
    if (last && last.sent && t - last.sent < kMailGap) return 'We just sent a code. Wait a minute and try again.';
    if (u.mailTimes.length >= kMailsPerDay) return 'That\'s enough emails for today. Try again tomorrow.';
    const code = sixDigits();
    u.codes = u.codes || {};
    u.codes[purpose] = { hash: hashHex(code + ':' + u.id + ':' + purpose), exp: t + kCodeMinutes * 60, tries: 0, sent: t };
    u.mailTimes.push(t);
    this.saveUser(u);
    this.outbox.push({ to, subject, text: 'Hi ' + (u.username || u.name) + ',\n\n' + what + '\n\n    ' + code + '\n\n'
      + 'The code works for ' + kCodeMinutes + ' minutes. If you didn\'t ask for this, you can ignore this email.\n\n- Guts&Bolts',
      code });
    return '';
  }
  // Check a code: '' if right (and it's used up), else what went wrong.
  checkCode(u, purpose, code) {
    const c = u.codes && u.codes[purpose];
    if (!c || now() > c.exp) return 'That code has expired. Ask for a new one.';
    if (c.tries >= kCodeTries) return 'Too many wrong codes. Ask for a new one.';
    if (hashHex(String(code || '').trim() + ':' + u.id + ':' + purpose) !== c.hash) {
      c.tries++;
      this.saveUser(u);
      return 'That code isn\'t right.';
    }
    delete u.codes[purpose];
    this.saveUser(u);
    return '';
  }
  // Which email service is set up, and what's missing (for the Staff page).
  mailStatus() {
    const env = this.env;
    const service = env.BREVO_API_KEY ? 'Brevo' : env.RESEND_API_KEY ? 'Resend' : env.MAIL_DEBUG ? 'test mode (codes go in the log)' : '';
    let problem = '';
    if (!service) problem = 'Email isn\'t set up yet: add a BREVO_API_KEY secret and a MAIL_FROM variable in Cloudflare (see the README).';
    else if (!env.MAIL_DEBUG && !env.MAIL_FROM) problem = 'There\'s an API key, but no MAIL_FROM variable (the address emails come from). Add it in Cloudflare.';
    return { service, from: env.MAIL_FROM || '', on: this.canMail, problem, last: this.mailLast };
  }
  // Fetch the preview cards for Lyreto links that were just set (after the request, like emails).
  async fetchLyretoCards() {
    for (const [kind, id] of this.lyretoQueue.splice(0)) {
      const o = kind === 'group' ? this.groups.get(id) : this.assets.get(id);
      if (!o || !o.lyreto) continue;
      const link = o.lyreto;
      let card = null;
      try {
        const res = await fetch(link, { headers: { accept: 'text/html' }, signal: AbortSignal.timeout(5000) });
        if (res.ok && lyretoLink(res.url) !== null) card = lyretoCardFrom((await res.text()).slice(0, 300000));
      } catch { /* Lyreto didn't answer: the page shows the plain link */ }
      if (o.lyreto !== link) continue;   // it was changed while we waited
      if (card) o.lyretoCard = card; else delete o.lyretoCard;
      if (kind === 'group') this.saveGroup(o); else this.saveAsset(o);
    }
    this.flush();
  }
  // Set a game's or group's Lyreto link (false when it isn't one).
  setLyreto(kind, o, raw) {
    const link = lyretoLink(raw);
    if (link === null) return false;
    if (link !== (o.lyreto || '')) {
      delete o.lyretoCard;
      if (link) { o.lyreto = link; this.lyretoQueue.push([kind, o.id]); } else delete o.lyreto;
    }
    return true;
  }
  lyretoJson(o) { return o.lyreto ? Object.assign({ link: o.lyreto }, o.lyretoCard || {}) : undefined; }
  // Send what's in the outbox (after the request, so the answer isn't held up by much).
  async sendMail() {
    const mails = this.outbox.splice(0);
    for (const m of mails) {
      const from = this.env.MAIL_FROM || '', fromName = this.env.MAIL_NAME || 'Guts&Bolts';
      let error = '';
      try {
        let res = null;
        if (this.env.BREVO_API_KEY) {
          res = await fetch('https://api.brevo.com/v3/smtp/email', { method: 'POST',
            headers: { 'api-key': this.env.BREVO_API_KEY, 'content-type': 'application/json', accept: 'application/json' },
            body: JSON.stringify({ sender: { name: fromName, email: from }, to: [{ email: m.to }], subject: m.subject, textContent: m.text }) });
        } else if (this.env.RESEND_API_KEY) {
          res = await fetch('https://api.resend.com/emails', { method: 'POST',
            headers: { authorization: 'Bearer ' + this.env.RESEND_API_KEY, 'content-type': 'application/json' },
            body: JSON.stringify({ from: fromName + ' <' + from + '>', to: [m.to], subject: m.subject, text: m.text }) });
        } else {
          console.log('MAIL (MAIL_DEBUG) to ' + m.to + ': ' + m.subject + (m.code ? ' code ' + m.code : '') + '\n' + m.text);
        }
        // The email service says what went wrong (a bad key, a sender it doesn't know...) in its answer.
        if (res && !res.ok) error = (this.env.BREVO_API_KEY ? 'Brevo' : 'Resend') + ' said ' + res.status + ': ' + (await res.text()).slice(0, 300);
      } catch (e) {
        error = 'Couldn\'t reach the email service: ' + (e && e.message || e);
      }
      if (error) console.log('Couldn\'t send an email: ' + error);
      this.mailLast = { at: now(), to: maskEmail(m.to), ok: !error, error };
    }
  }
  // The example games (see EXAMPLE_GAMES): added once, kept up to date with the
  // copies that come with the server, and owned by the staff account (Guts).
  // The Verified Hat (see VERIFIED_HAT): an award, not for sale.
  addVerifiedHat() {
    if (!this.official) return;
    const text = JSON.stringify(VERIFIED_HAT), data = new TextEncoder().encode(text);
    let a = this.assets.get(VERIFIED_HAT_ID);
    if (!a) {
      a = { id: VERIFIED_HAT_ID, kind: 'hat', name: 'Verified Hat', creator: this.official, price: 0, created: now(), sales: 0,
        plays: 0, size: 0, meta: { award: 'email', model: true }, builtin: true, likes: 0, dislikes: 0 };
      this.assets.set(a.id, a);
    }
    a.meta = Object.assign(a.meta || {}, { award: 'email', model: true });   // a 3D hat (older copies lacked "model")
    a.description = 'Given to everyone who confirms their email address. Can\'t be bought: add and confirm an email in Settings to get it.';
    if (a.size !== data.length) { a.size = data.length; a.updated = now(); this.writeFile(a.id, data); }
    this.saveAsset(a);
  }

  // Pumpkin items in the catalog are Gutstober items: they go off sale when it ends.
  // Done once per item (staff can change the date afterwards on the item's page).
  timeGutstoberItems() {
    const t = now();
    for (const a of this.assets.values()) {
      if (!isCatalogItem(a.kind) || a.timedFor || !/pumpkin/i.test(a.name || '')) continue;
      a.timedFor = 'gutstober';
      a.offsaleAt = gutstoberEnd(a.created || t);
      this.saveAsset(a);
    }
  }
  isOffsale(a, t = now()) { return !!a.offsaleAt && t >= a.offsaleAt; }

  giveVerifiedHat(u) {
    const a = this.assets.get(VERIFIED_HAT_ID);
    if (!a || !u.emailVerified || u.owned.includes(a.id)) return;
    u.owned.push(a.id);
    a.sales++;
    this.saveAsset(a);
  }

  addExampleGames() {
    if (!this.official) return;
    for (const ex of EXAMPLE_GAMES) {
      let info = {};
      try { info = JSON.parse(ex.text).info || {}; } catch { continue; }
      const data = new TextEncoder().encode(ex.text);
      let a = this.assets.get(ex.id);
      if (!a) {
        a = { id: ex.id, kind: 'game', name: cleanText(info.title || 'Game', 50), creator: this.official, price: 0,
          created: now(), sales: 0, plays: 0, size: 0, meta: {}, access: 'public', genres: ex.genres, maxPlayers: 12,
          builtin: true, likes: 0, dislikes: 0 };
        this.assets.set(a.id, a);
      }
      a.description = cleanText(info.description || '', 1000, true);
      if (a.size !== data.length) { a.size = data.length; a.updated = now(); this.writeFile(a.id, data); }
      this.saveAsset(a);
    }
  }

  // What someone is wearing (their avatar's items), as the catalog shows them.
  wornItems(u) {
    const ids = u && u.avatar && Array.isArray(u.avatar.wearing) ? u.avatar.wearing : [];
    return ids.map((id) => this.assets.get(id)).filter(Boolean).map((a) => this.publicAsset(a));
  }

  publicAsset(a, me = null) {
    const c = this.users.get(a.creator);
    return { id: a.id, num: a.num || 0, kind: a.kind, name: a.name, description: a.description, creator: a.creator, price: a.price,
      created: a.created, sales: a.sales, plays: a.plays, size: a.size, meta: a.meta || {},
      creatorName: c ? c.name : (a.builtin ? 'Guts' : '?'), creatorVerified: !!c && this.isVerified(c), creatorStaff: !!c && this.isStaff(c), thumb: a.thumb || 0,
      icon: a.icon || 0, access: hasAccess(a.kind) ? (a.access || 'public') : undefined,
      badges: a.kind === 'game' ? (a.badges || []) : undefined,
      genres: a.kind === 'game' ? (a.genres || []) : undefined, allowGear: a.kind === 'game' ? !!a.allowGear : undefined, maxPlayers: a.kind === 'game' ? (a.maxPlayers || kDefaultMax) : undefined,
      likes: a.kind === 'game' ? (a.likes || 0) : undefined, dislikes: a.kind === 'game' ? (a.dislikes || 0) : undefined,
      updated: a.updated || a.created, playing: a.kind === 'game' ? this.playingIn(a.id) : undefined,
      myVote: me && a.votes ? (a.votes[me.id] || 0) : undefined, limited: this.limitedJson(a),
      favorites: a.kind === 'game' ? (a.favorites || 0) : undefined,
      featured: a.kind === 'game' && a.featured > 0 ? true : undefined,
      comments: a.kind === 'game' ? !a.commentsOff : undefined, commentCount: a.kind === 'game' ? (a.comments || []).length : undefined,
      myFavorite: a.kind === 'game' && me ? (me.favorites || []).includes(a.id) : undefined,
      offsaleAt: a.offsaleAt || 0, offsale: this.isOffsale(a), review: a.review || undefined, reviewNote: a.reviewNote || undefined,
      privatePrice: a.kind === 'game' ? (a.privatePrice || 0) : undefined,
      lyreto: a.kind === 'game' ? this.lyretoJson(a) : undefined,
      group: a.kind === 'game' ? this.groupJson(a.group) : undefined };
  }
  groupJson(id) { const g = id ? this.groups.get(id) : null; return g ? { id: g.id, name: g.name, color: g.color } : undefined; }
  publicModelsLeft(u) {   // -1 = no limit
    if (this.isVerified(u)) return -1;
    const used = u.publicWeek === weekOf(now()) ? (u.publicThisWeek || 0) : 0;
    return Math.max(0, kPublicModelsPerWeek - used);
  }
  publicModelOk(u) { return this.publicModelsLeft(u) !== 0; }
  publicModelLimitText() {
    return 'You\'ve made ' + kPublicModelsPerWeek + ' models public this week. Publish it as private for now, or get Verified for no limit.';
  }
  countPublicModel(u) {
    if (this.isVerified(u)) return;
    const w = weekOf(now());
    if (u.publicWeek !== w) { u.publicWeek = w; u.publicThisWeek = 0; }
    u.publicThisWeek++;
    this.saveUser(u);
  }
  // When your private server of a game runs out (0 = you don't have one).
  privateUntil(u, gameId) { const t = (u.privateServers || {})[gameId] || 0; return t > now() ? t : 0; }
  // Can `u` start a private server of this game? Free unless its creator set a price.
  privateBlocked(u, gameId) {
    const a = this.assets.get(gameId);
    if (!a || a.kind !== 'game' || !(a.privatePrice > 0) || a.creator === u.id || this.isStaff(u) || this.privateUntil(u, gameId)) return '';
    return 'Private servers of ' + a.name + ' cost ' + a.privatePrice + ' Bolts for ' + kPrivateDays + ' days. Buy one on the game\'s page first.';
  }
  playingIn(gameId) {   // people in the game's servers right now
    let n = 0;
    for (const s of this.sessions.values()) if (s.game === gameId) n += this.headcount(s);
    return n;
  }
  // New decals, sounds and T-shirts waiting for a staff check (oldest first).
  uploadsToReview() {
    return [...this.assets.values()].filter((a) => a.review === 'pending').sort((x, y) => (x.updated || x.created) - (y.updated || y.created))
      .slice(0, 50).map((a) => this.publicAsset(a));
  }
  // Can `me` see and play this game? (Other kinds of things are always visible, unless they're
  // waiting for a staff check.)
  canPlay(a, me) {
    if (a.review && a.creator !== me.id && !this.isStaff(me)) return false;   // waiting for (or failed) a staff check
    if (!hasAccess(a.kind) || !a.access || a.access === 'public') return true;
    if (a.creator === me.id || this.isStaff(me)) return true;
    if (a.access === 'friends') return me.friends.includes(a.creator);
    return false;
  }
  noPlay(a) {
    const c = this.users.get(a.creator);
    if (a.review === 'pending') return 'This is waiting for a staff check.';
    if (a.review === 'rejected') return 'This didn\'t pass the staff check.';
    if (a.kind === 'model') return 'This model is private.';
    if (a.kind === 'animation') return 'This animation is private.';
    return a.access === 'friends' ? 'Only ' + (c ? c.name : 'the creator') + '\'s friends can play this game.' : 'This game is private.';
  }
  publicGroup(g) {
    const o = this.users.get(g.owner);
    return { id: g.id, name: g.name, description: g.description, owner: g.owner, ownerName: o ? o.name : '?',
      ownerVerified: !!o && this.isVerified(o), members: Object.keys(g.members).length, color: g.color, open: g.open,
      created: g.created, shout: (g.shout && g.shout.text) || '', lyreto: this.lyretoJson(g) };
  }
  groupsOf(id) { return [...this.groups.values()].filter((g) => g.members[id]); }
  ranksOf(g) { return Array.isArray(g.ranks) && g.ranks.length ? g.ranks : defaultRanks(); }
  // Someone's rank in a group (null = not in it). A rank that was deleted counts as Member.
  rankIn(g, userId) {
    const id = g.members[userId];
    if (!id) return null;
    const ranks = this.ranksOf(g);
    return ranks.find((r) => r.id === id) || ranks.find((r) => r.id === 'Member') || defaultRanks()[2];
  }
  groupCan(g, u, perm) { const r = this.rankIn(g, u.id); return !!r && (r.id === 'Owner' || r.perms.includes(perm)); }
  groupFunds(g) { return (g.ledger || []).reduce((n, e) => n + e[0], 0); }
  groupAdd(g, amount, reason, ref) {
    g.ledger = g.ledger || [];
    g.ledger.push([amount, reason, now(), ref]);
    if (g.ledger.length > kGroupLedger + 100) {
      const cut = g.ledger.length - kGroupLedger;
      const old = g.ledger.slice(0, cut).reduce((n, e) => n + e[0], 0);
      g.ledger = [[old, 'Earlier history', 0, 'carry'], ...g.ledger.slice(cut)];
    }
    this.saveGroup(g);
  }
  // The group a game (or one of its passes or products) belongs to, if any.
  groupFor(a) {
    const gameId = a.kind === 'game' ? a.id : (a.meta || {}).game;
    const game = gameId ? this.assets.get(gameId) : null;
    return game && game.group ? this.groups.get(game.group) || null : null;
  }
  // Pay the seller's share of a sale: to the group when it's a group's game, else to the creator.
  paySeller(a, buyer, share, ref) {
    const seller = this.findUser(a.creator);
    if (!seller || seller === buyer) return;
    const g = this.groupFor(a);
    if (g) {
      if (share > 0) this.groupAdd(g, share, buyer.name + ' bought ' + a.name, ref);
      this.tally(a, 'bolts', share);
      this.notify(seller, 'sale', buyer.name + ' bought ' + a.name + '. ' + share + ' Bolts went to ' + g.name + '.', a.id);
      return;
    }
    if (share > 0) this.add(seller, share, 'Sold ' + a.name, ref);
    this.tally(a, 'bolts', share);
    this.notify(seller, 'sale', buyer.name + ' bought ' + a.name + '. You got ' + share + ' Bolts.', a.id);
  }

  // --- checking a request (GbServer::checkRequest) ---
  checkRequest(req) {
    const opName = typeof req.op === 'string' ? req.op : '';
    const account = lower(typeof req.account === 'string' ? req.account : '');
    const nonce = typeof req.nonce === 'string' ? req.nonce : '';
    const time = typeof req.time === 'number' ? req.time : 0;
    const args = req.args && typeof req.args === 'object' && !Array.isArray(req.args) ? req.args : {};
    if (!opName || opName.length > 40) return { bad: fail('Unknown request.') };
    if (!isHex(account, 64, 64)) return { bad: fail('Missing account.') };
    if (!isHex(nonce, 8, 64)) return { bad: fail('Missing nonce.') };
    const t = now();
    if (Math.abs(t - time) > kMaxClockSkew)
      return { bad: fail('Your computer\'s clock is too far off from the server\'s. Fix the date and time and try again.') };
    if (!verify(account, requestText(opName, account, time, nonce, args), typeof req.sig === 'string' ? req.sig : ''))
      return { bad: fail('That request wasn\'t signed by its account.') };
    const key = account + nonce;
    try {
      this.sql.exec('INSERT INTO nonces (key, time) VALUES (?, ?)', key, t);
    } catch {
      return { bad: fail('That request was already sent once.') };
    }
    if (Math.random() < 0.01) this.sql.exec('DELETE FROM nonces WHERE time < ?', t - 2 * kMaxClockSkew);
    const owner = this.aliases.get(account);
    const me = owner ? this.users.get(owner) : this.user(account);
    if (!me) return { bad: fail('That account is gone.') };
    if (!owner && me.keyRevoked)
      return { bad: fail('This device was logged out because the account\'s password was reset. Log in again with the new password.') };
    me.lastSeen = t;
    this.saveUser(me);
    if (me.banned && me.bannedUntil && t >= me.bannedUntil) {   // a timed ban is over
      me.banned = false;
      delete me.banReason; delete me.banNote; delete me.bannedAt; delete me.bannedUntil;
      this.saveUser(me);
    }
    if (me.banned && opName !== 'hello') return { bad: fail(banMessage(me)) };
    if (me.userId === 0 && opName !== 'hello' && opName !== 'ping' && !opName.startsWith('account.') && !GUEST_OK.has(opName))
      return { bad: fail('Sign up or log in first.') };
    return { me, args, opName };
  }

  handle(req) {
    try {
      const c = this.checkRequest(req);
      if (c.bad) return c.bad;
      return this.op(c.opName, c.me, c.args);
    } catch (e) {
      return fail('The server hit a problem: ' + (e && e.message || e));
    } finally {
      this.flush();
    }
  }

  // --- the requests (GbServer::op) ---
  op(name, me, args) {
    const t = now(), today = utcDay(t);

    if (name === 'hello') {
      let n = cleanText(str(args, 'name'), 20);
      if (me.userId > 0) n = me.username;
      if (n) {
        if ((nameIsReserved(n) && !this.isOfficial(me)) || filterText(n) !== n) n = 'Player';
        me.name = n;
      }
      if (Array.isArray(args.grants))
        for (const g of args.grants)
          if (Array.isArray(g) && g.length === 2 && typeof g[0] === 'string' && typeof g[1] === 'string' &&
              grantValid(this.official, g[0], me.id, g[1])) me.grants[g[0]] = g[1];
      this.saveUser(me);
      return okay({ me: this.meJson(me), server: { name: this.name, protocol: 1, official: this.official } });
    }
    if (name === 'profile') {
      const want = str(args, 'id');
      const u = want && want.length < 12 && /^[0-9]+$/.test(want) ? this.findUserId(Number(want)) : this.findPerson(want);
      if (!u || u.userId === 0) return fail('There\'s no account with that ID on this server.');
      const user = Object.assign(this.publicUser(u), { badges: this.badgesOf(u), avatar: u.avatar || null });
      const creations = [...this.assets.values()].filter((a) => a.creator === u.id && this.canPlay(a, me)).map((a) => this.publicAsset(a));
      const groups = this.groupsOf(u.id).map((g) => Object.assign(this.publicGroup(g), { role: this.rankIn(g, u.id).name }));
      const friendship = u.id === me.id ? 'self' : me.friends.includes(u.id) ? 'friends'
        : me.friendOut.includes(u.id) ? 'sent' : me.friendIn.includes(u.id) ? 'received' : 'none';
      // Like a Roblox profile: what they're wearing, some friends, visits to their games.
      const wearing = this.wornItems(u);
      const friends = u.friends.slice(0, 9).map((id) => this.users.get(id)).filter(Boolean)
        .map((f) => Object.assign(this.publicUser(f), { avatar: f.avatar || null, online: this.presence(me, f).online, wearing: this.wornItems(f) }));
      const placeVisits = creations.filter((a) => a.kind === 'game').reduce((n, a) => n + (a.plays || 0), 0);
      // Game badges (made by game creators, earned by playing) - separate from the
      // Guts&Bolts badges above, which only staff give out.
      const gameBadges = (u.gameBadges || []).map(([bid, gid, when]) => {
        const g = this.assets.get(gid), b = g && (g.badges || []).find((x) => x.id === bid);
        return b ? { id: b.id, name: b.name, description: b.description, color: b.color, game: gid, gameName: g.name, earned: when } : null;
      }).filter(Boolean).reverse();
      // Their inventory (the first few; profile.inventory has the rest), unless they hid it.
      const canSee = this.seesInventory(me, u), owns = canSee ? this.inventoryOf(u) : [];
      const inventory = { hidden: !canSee, total: owns.length, items: owns.slice(0, 12) };
      return okay({ user, creations, groups, friendCount: u.friends.length, friendship, wearing, friends, inventory,
        followerCount: (u.followers || []).length, followingCount: (u.following || []).length,
        isFollowing: (me.following || []).includes(u.id), blocked: (me.blocked || []).includes(u.id),
        online: this.presence(me, u).online, playing: this.presence(me, u).playing, placeVisits, gameBadges,
        blurb: u.blurb || '', status: (u.posts || [])[0] || null, playerBadges: this.playerBadgesOf(u), allPlayerBadges: PLAYER_BADGES });
    }
    if (name === 'profile.inventory') {
      // Everything someone owns from the catalog, a page at a time, in one group (INVENTORY_CATS).
      const want = str(args, 'id');
      const u = want && want.length < 12 && /^[0-9]+$/.test(want) ? this.findUserId(Number(want)) : this.findPerson(want);
      if (!u || u.userId === 0) return fail('There\'s no account with that ID on this server.');
      if (!this.seesInventory(me, u)) return okay({ user: this.publicUser(u), hidden: true, cat: 'all', total: 0, counts: {}, items: [] });
      const cat = INVENTORY_CATS.includes(str(args, 'cat')) ? str(args, 'cat') : 'all';
      const all = this.inventoryOf(u);
      const counts = Object.fromEntries(INVENTORY_CATS.map((c) => [c, all.filter((a) => inventoryCat(c, a)).length]));
      const list = cat === 'all' ? all : all.filter((a) => inventoryCat(cat, a));
      const offset = Math.max(0, num(args, 'offset'));
      const limit = 'limit' in args ? clamp(num(args, 'limit'), 1, 100) : 60;
      return okay({ user: this.publicUser(u), hidden: false, cat, total: list.length, counts, items: list.slice(offset, offset + limit) });
    }
    if (name === 'people.list') {
      // Someone's friends, who they follow, or their followers (anyone can look, like a
      // Roblox profile). A page at a time: Guts follows everybody. (Server.cpp has the same.)
      const u = this.findPerson(str(args, 'user'));
      if (!u || u.userId === 0) return fail('There\'s no account with that ID on this server.');
      const which = ['friends', 'following', 'followers'].includes(str(args, 'which')) ? str(args, 'which') : 'friends';
      const ids = (u[which] || []).filter((id) => this.users.has(id));
      const all = ids.map((id) => this.users.get(id)).filter((p) => p.userId > 0).sort((a, b) => a.userId - b.userId);
      const offset = Math.max(0, num(args, 'offset'));
      const limit = 'limit' in args ? clamp(num(args, 'limit'), 1, 100) : 60;
      const people = all.slice(offset, offset + limit).map((p) => Object.assign(this.publicUser(p),
        { avatar: p.avatar || null, online: this.presence(me, p).online }));
      return okay({ user: this.publicUser(u), which, total: all.length, people });
    }
    if (name === 'users.search') {
      let q = lower(cleanText(str(args, 'query'), 64));
      if (q[0] === '#') q = q.slice(1);
      if (q[0] === '@') q = q.slice(1);
      const wantId = q && q.length < 12 && /^[0-9]+$/.test(q) ? Number(q) : -1;
      const found = [...this.users.values()].filter((u) => !u.banned && u.userId > 0 &&
        (!q || u.userId === wantId || lower(u.name).includes(q) || lower(u.username).includes(q)));
      found.sort((a, b) => {
        const ea = lower(a.username) === q || a.userId === wantId, eb = lower(b.username) === q || b.userId === wantId;
        if (ea !== eb) return ea ? -1 : 1;
        const va = this.isVerified(a), vb = this.isVerified(b);
        if (va !== vb) return va ? -1 : 1;
        return b.lastSeen - a.lastSeen;
      });
      return okay({ users: found.slice(0, 50).map((u) => this.publicUser(u)) });
    }
    if (name.startsWith('account.')) return this.accountOp(name, me, args);
    if (name.startsWith('groups.')) return this.groupOp(name, me, args);
    if (name.startsWith('forum.')) return this.forumOp(name, me, args);
    if (name.startsWith('friends.') || name.startsWith('follow.')) return this.friendOp(name, me, args);
    if (name.startsWith('servers.')) return this.serverOp(name, me, args);
    if (name.startsWith('data.')) return this.dataOp(name, me, args);
    if (name.startsWith('updates.')) return this.updateOp(name, me, args);
    if (name.startsWith('block.') || name.startsWith('report.')) return this.safetyOp(name, me, args);
    if (name.startsWith('support.')) return this.supportOp(name, me, args);
    // "I'm still here" (for friends' online dots); the answer keeps your account fresh
    // (a new warning, or Bolts someone sent you, shows up within a minute).
    if (name === 'ping') return okay({ me: this.meJson(me) });

    if (name === 'avatar.set') {
      // Your look, shared by the website and the apps. Colours are 0-255 whole numbers.
      const a = args.avatar && typeof args.avatar === 'object' ? args.avatar : null;
      if (!a) return fail('That avatar looks wrong.');
      const rgb = (v, allowNone) => {
        if (!Array.isArray(v) || v.length !== 3 || !v.every((x) => Number.isInteger(x))) return null;
        if (allowNone && v.every((x) => x < 0)) return [-1, -1, -1];
        return v.map((x) => clamp(x, 0, 255));
      };
      const colors = {};
      for (const part of ['head', 'torso', 'leftArm', 'rightArm', 'leftLeg', 'rightLeg']) {
        colors[part] = rgb(a[part]);
        if (!colors[part]) return fail('That avatar looks wrong.');
      }
      const hat = Number.isInteger(a.hat) ? clamp(a.hat, 0, 4) : 0;
      const hatColor = rgb(a.hatColor, true) || [-1, -1, -1];
      const wearing = Array.isArray(a.wearing) ? a.wearing.filter((w) => typeof w === 'string' && me.owned.includes(w)).slice(0, 12) : [];
      me.avatar = Object.assign(colors, { hat, hatColor, wearing, updated: t });
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'gear.equip') {   // put gear you own in (or take it out of) your backpack for games
      const on = args.on !== false;
      const a = this.assets.get(str(args, 'id'));
      const id = a ? a.id : str(args, 'id');
      me.gear = (me.gear || []).filter((g) => g !== id && me.owned.includes(g));
      if (on) {
        if (!a || a.kind !== 'gear') return fail('That gear doesn\'t exist (any more).');
        if (!me.owned.includes(id)) return fail('Get it from the catalog first.');
        if (me.gear.length >= kMostGear) return fail('You can have ' + kMostGear + ' gear equipped at once. Take one off first.');
        me.gear.push(id);
      }
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'thumb.set') {
      // A picture of a game (Studio sends one when publishing): a PNG or JPG, up to 400 KB.
      const a = this.assets.get(str(args, 'id'));
      if (!a) return fail('That doesn\'t exist (any more).');
      if (a.creator !== me.id && !this.isStaff(me)) return fail('You can only change pictures of your own things.');
      // Catalog items aren't given pictures: everyone sees the item itself, drawn from its
      // real shape (so a picture can never be swapped for something it isn't).
      if (isCatalogItem(a.kind)) return fail('Catalog items don\'t take pictures: they\'re shown as the item itself.');
      let data;
      try { data = b64ToBytes(str(args, 'data')); } catch { return fail('The picture got scrambled. Try again.'); }
      const png = data.length > 8 && [0x89, 0x50, 0x4e, 0x47].every((v, i) => data[i] === v);
      const jpg = data.length > 3 && data[0] === 0xff && data[1] === 0xd8;
      if (!png && !jpg) return fail('Pictures must be .png or .jpg.');
      if (data.length > 400 * 1024) return fail('That picture is too big (the most is 400 KB).');
      this.writeFile('thumb:' + a.id, data);
      a.thumb = t;
      this.saveAsset(a);
      return okay({ asset: this.publicAsset(a) });
    }
    if (name === 'icon.set') {
      // A game's icon: a small square picture (PNG or JPG, up to 200 KB).
      const a = this.assets.get(str(args, 'id'));
      if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
      if (a.creator !== me.id && !this.isStaff(me)) return fail('You can only change your own games.');
      let data;
      try { data = b64ToBytes(str(args, 'data')); } catch { return fail('The picture got scrambled. Try again.'); }
      const png = data.length > 8 && [0x89, 0x50, 0x4e, 0x47].every((v, i) => data[i] === v);
      const jpg = data.length > 3 && data[0] === 0xff && data[1] === 0xd8;
      if (!png && !jpg) return fail('Icons must be .png or .jpg.');
      if (data.length > 200 * 1024) return fail('That icon is too big (the most is 200 KB).');
      this.writeFile('icon:' + a.id, data);
      a.icon = t;
      this.saveAsset(a);
      return okay({ asset: this.publicAsset(a) });
    }
    if (name === 'game.settings') {
      // Configure a game from the website: name, description and who can play it.
      const a = this.assets.get(str(args, 'id'));
      if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
      if (a.creator !== me.id && !this.isStaff(me)) return fail('You can only change your own games.');
      if ('name' in args) {
        const title = say(str(args, 'name'), 50);
        if (!title) return fail('Give it a name.');
        if (nameIsReserved(title) && !this.isOfficial(me)) return fail('That name belongs to Guts&Bolts.');
        a.name = title;
      }
      if ('description' in args) a.description = say(str(args, 'description'), 1000, true);
      if ('access' in args) {
        const access = str(args, 'access');
        if (!ACCESS.includes(access)) return fail('Pick public, friends or private.');
        a.access = access;
      }
      if ('genres' in args) {
        const picked = Array.isArray(args.genres) ? [...new Set(args.genres.filter((x) => GENRES.includes(x)))] : [];
        if (picked.length > 3) return fail('Pick up to 3 genres.');
        a.genres = picked;
      }
      if ('maxPlayers' in args) a.maxPlayers = clamp(num(args, 'maxPlayers'), 2, kMostPlayers);
      if ('allowGear' in args) a.allowGear = args.allowGear === true;
      if ('comments' in args) a.commentsOff = args.comments === false;
      if ('privatePrice' in args) a.privatePrice = clamp(num(args, 'privatePrice'), 0, kMaxPrivatePrice);
      if ('lyreto' in args && !this.setLyreto('game', a, str(args, 'lyreto'))) return fail('That isn\'t a Lyreto link. Copy your server\'s invite link in Lyreto: it starts with https://lyreto.com/');
      if ('group' in args) {   // put the game in one of your groups (its sales go to the group), or '' to take it out
        const gid = str(args, 'group');
        if (gid) {
          const g = this.groups.get(gid);
          if (!g) return fail('That group doesn\'t exist (any more).');
          if (!this.groupCan(g, me, 'games') && !this.isStaff(me)) return fail('You need the "Add games" permission in ' + g.name + '.');
        }
        a.group = gid;
      }
      a.updated = t;
      this.saveAsset(a);
      return okay({ asset: this.publicAsset(a, me) });
    }
    // --- Classic profile: "About me" and "Right now I'm..." ---
    if (name === 'profile.set') {
      if (me.userId === 0) return fail('Sign up first.');
      if ('blurb' in args) me.blurb = say(str(args, 'blurb'), kMaxBlurb, true);
      if ('status' in args) {
        const text = say(str(args, 'status'), kMaxStatus);
        if (text) {
          if (me.statusDay !== today) { me.statusDay = today; me.statusesToday = 0; }
          if (me.statusesToday >= kStatusesPerDay) return fail('That\'s enough status updates for today.');
          me.statusesToday++;
          me.posts = [{ text, at: t }, ...(me.posts || [])].slice(0, kMaxPosts);
        }
      }
      this.saveUser(me);
      return okay({ me: this.meJson(me), blurb: me.blurb || '', status: (me.posts || [])[0] || null });
    }
    // My Feed: what your friends and the people you follow said lately (newest first).
    if (name === 'feed.list') {
      if (me.userId === 0) return okay({ feed: [] });
      const ids = new Set([me.id, ...me.friends, ...(me.following || [])]);
      const feed = [];
      for (const id of ids) {
        const u = this.users.get(id);
        if (!u || u.banned || this.blocks(me, u)) continue;
        for (const post of (u.posts || []).slice(0, 5))
          feed.push({ text: post.text, at: post.at, user: { id: u.id, userId: u.userId, name: u.name, verified: this.isVerified(u), avatar: u.avatar || null } });
      }
      feed.sort((x, y) => y.at - x.at);
      return okay({ feed: feed.slice(0, 40) });
    }
    // --- Saved outfits: your whole look (colours, hat, what you wear), kept to put back on later ---
    if (name.startsWith('outfit.')) {
      if (me.userId === 0) return fail('Sign up first.');
      const outfits = me.outfits || (me.outfits = []);
      const find = () => outfits.find((o) => o.id === str(args, 'id'));
      const list = () => okay({ outfits: me.outfits.map((o) => ({ id: o.id, name: o.name, avatar: o.avatar, created: o.created,
        wearing: (o.avatar.wearing || []).map((id) => this.assets.get(id)).filter(Boolean).map((a) => this.publicAsset(a)) })), me: this.meJson(me) });
      if (name === 'outfit.list') return list();
      if (name === 'outfit.save') {
        if (!me.avatar) return fail('Change your look first, then save it as an outfit.');
        const title = say(str(args, 'name'), 40) || 'Outfit ' + (outfits.length + 1);
        const old = find();
        if (old) { old.avatar = JSON.parse(JSON.stringify(me.avatar)); old.name = title; }
        else {
          if (outfits.length >= kMaxOutfits) return fail('You have ' + kMaxOutfits + ' outfits. Delete one first.');
          outfits.unshift({ id: randomHex(6), name: title, avatar: JSON.parse(JSON.stringify(me.avatar)), created: t });
        }
        this.saveUser(me);
        return list();
      }
      const o = find();
      if (!o) return fail('That outfit isn\'t there any more.');
      if (name === 'outfit.wear') {
        // Things you've sold or traded away since are left off.
        me.avatar = Object.assign(JSON.parse(JSON.stringify(o.avatar)), { updated: t });
        me.avatar.wearing = (me.avatar.wearing || []).filter((id) => me.owned.includes(id));
        this.saveUser(me);
        return list();
      }
      if (name === 'outfit.rename') {
        const title = say(str(args, 'name'), 40);
        if (!title) return fail('Give it a name.');
        o.name = title;
        this.saveUser(me);
        return list();
      }
      if (name === 'outfit.delete') {
        me.outfits = outfits.filter((x) => x !== o);
        this.saveUser(me);
        return list();
      }
      return fail('Unknown request.');
    }
    // --- Favourite games, and the ones you played last ("Continue playing") ---
    if (name === 'game.favorite') {
      if (me.userId === 0) return fail('Sign up first.');
      const a = this.assets.get(str(args, 'id'));
      if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
      const favs = me.favorites || (me.favorites = []);
      const had = favs.includes(a.id), want = args.on !== false;
      if (want && !had) {
        if (favs.length >= kMaxFavorites) return fail('You have ' + kMaxFavorites + ' favourites. Take one off first.');
        favs.unshift(a.id); a.favorites = (a.favorites || 0) + 1;
      } else if (!want && had) {
        me.favorites = favs.filter((x) => x !== a.id); a.favorites = Math.max(0, (a.favorites || 0) - 1);
      }
      this.saveUser(me); this.saveAsset(a);
      return okay({ asset: this.publicAsset(a, me) });
    }
    if (name === 'games.mine') {   // which: "recent" (Continue playing) or "favorites"
      const ids = str(args, 'which') === 'favorites' ? (me.favorites || []) : (me.recent || []);
      const games = ids.map((id) => this.assets.get(id)).filter((a) => a && a.kind === 'game' && this.canPlay(a, me));
      return okay({ assets: games.slice(0, clamp(num(args, 'limit') || 30, 1, 200)).map((a) => this.publicAsset(a, me)) });
    }
    // --- Private messages (an inbox, like old Roblox). Guests can't send or get them. ---
    if (name === 'message.send') {
      if (me.userId === 0) return fail('Sign up first.');
      const want = str(args, 'to');
      const them = /^#?[0-9]+$/.test(want) ? this.findUserId(Number(want.replace('#', ''))) : this.findPerson(want);
      if (!them || them.userId === 0) return fail('There\'s no account with that ID on this server.');
      if (them.id === me.id) return fail('You can\'t send a message to yourself.');
      if (them.banned || this.blocks(me, them)) return fail('You can\'t send messages to that account.');
      if (!this.allows(this.privacyOf(them).messages, me, them)) {
        return fail(this.privacyOf(them).messages === 'friends' ? them.name + ' only gets messages from friends.' : them.name + ' doesn\'t get messages.');
      }
      const subject = say(str(args, 'subject'), 80) || '(no subject)';
      const body = say(str(args, 'body'), 2000, true);
      if (!body) return fail('Write something first.');
      if (me.messageDay !== today) { me.messageDay = today; me.messagesToday = 0; }
      if (me.messagesToday >= kMessagesPerDay) return fail('That\'s ' + kMessagesPerDay + ' messages today. Try again tomorrow.');
      me.messagesToday++;
      const id = randomHex(8);
      them.inbox = [{ id, from: me.id, subject, body, at: t, read: false }, ...(them.inbox || [])].slice(0, kMaxInbox);
      me.sent = [{ id, to: them.id, subject, body, at: t }, ...(me.sent || [])].slice(0, kMaxSent);
      this.saveUser(me, them);
      return okay({ me: this.meJson(me) });
    }
    // The bell: newest first, and mark them all read.
    if (name === 'notes.list') return okay({ notes: me.notes || [], me: this.meJson(me) });
    if (name === 'notes.read') {
      for (const n of me.notes || []) n.read = true;
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'message.list') {   // box: "inbox" or "sent"
      if (me.userId === 0) return okay({ messages: [], me: this.meJson(me) });
      const sent = str(args, 'box') === 'sent';
      const who = (id) => { const u = this.users.get(id); return u ? { id: u.id, userId: u.userId, name: u.name, verified: this.isVerified(u) } : { id, userId: 0, name: '?' }; };
      const messages = (sent ? (me.sent || []) : (me.inbox || [])).map((m) => ({ id: m.id, subject: m.subject, body: m.body, at: m.at,
        read: sent ? true : !!m.read, from: sent ? who(me.id) : who(m.from), to: sent ? who(m.to) : who(me.id) }));
      return okay({ messages, me: this.meJson(me) });
    }
    if (name === 'message.read' || name === 'message.delete') {
      const box = str(args, 'box') === 'sent' ? 'sent' : 'inbox';
      const list = me[box] || [];
      const m = list.find((x) => x.id === str(args, 'id'));
      if (!m) return fail('That message isn\'t there any more.');
      if (name === 'message.read') m.read = true;
      else me[box] = list.filter((x) => x !== m);
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'game.vote') {
      // Thumbs up or down, like Roblox; you have to have played it first.
      const a = this.assets.get(str(args, 'id'));
      if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
      if (!(me.played || []).includes(a.id) && a.creator !== me.id) return fail('Play the game first, then you can vote.');
      const v = Math.sign(num(args, 'vote'));
      a.votes = a.votes || {};
      const old = a.votes[me.id] || 0;
      if (old === 1) a.likes--; else if (old === -1) a.dislikes--;
      if (v === 0) delete a.votes[me.id]; else a.votes[me.id] = v;
      a.likes = (a.likes || 0) + (v === 1 ? 1 : 0);
      a.dislikes = (a.dislikes || 0) + (v === -1 ? 1 : 0);
      this.saveAsset(a);
      return okay({ asset: this.publicAsset(a, me) });
    }
    // --- Comments under a game, newest first. Anyone can read; signed-up players can write. ---
    if (name === 'comments.list' || name === 'comments.post' || name === 'comments.delete') {
      const a = this.assets.get(str(args, 'game'));
      if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
      if (!this.canPlay(a, me)) return fail(this.noPlay(a));
      const list = a.comments || (a.comments = []);
      const mod = a.creator === me.id || this.isStaff(me);
      if (name === 'comments.post') {
        if (me.userId === 0) return fail('Sign up to comment.');
        if (a.commentsOff) return fail('Comments are turned off for this game.');
        const owner = this.users.get(a.creator);
        if (owner && this.blocks(me, owner)) return fail('You can\'t comment on this game.');
        const text = say(str(args, 'text'), kCommentLength);
        if (!text) return fail('Write something first.');
        const key = 'c:' + me.id;
        if (t - (this.lastPost.get(key) || 0) < kCommentCooldown) return fail('Slow down a little - wait a few seconds between comments.');
        this.lastPost.set(key, t);
        list.unshift({ id: randomHex(6), by: me.id, text, at: t });
        if (list.length > kMaxComments) list.length = kMaxComments;
        this.saveAsset(a);
        if (owner && owner.id !== me.id) this.notify(owner, 'comment', me.name + ' commented on ' + a.name + ': "' + text.slice(0, 60) + '"', a.id);
      }
      if (name === 'comments.delete') {
        const i = list.findIndex((c) => c.id === str(args, 'id'));
        if (i < 0) return fail('That comment is already gone.');
        if (list[i].by !== me.id && !mod) return fail('You can only delete your own comments.');
        if (list[i].by !== me.id && a.creator !== me.id)
          this.staffDid(me, 'comment', 'Deleted a comment on "' + a.name + '": "' + list[i].text.slice(0, 80) + '"', list[i].by);
        list.splice(i, 1);
        this.saveAsset(a);
      }
      // A page of comments: "before" is the id of the last one you already have.
      const before = str(args, 'before'), from = before ? list.findIndex((c) => c.id === before) + 1 : 0;
      const shown = list.slice(from, from + kCommentsPage).filter((c) => !this.blocks(me, this.users.get(c.by)));
      const who = (id) => { const u = this.users.get(id); return u ? { id: u.id, userId: u.userId, name: u.name, verified: this.isVerified(u), staff: this.isStaff(u) } : { id, userId: 0, name: '?' }; };
      return okay({ comments: shown.map((c) => ({ id: c.id, text: c.text, at: c.at, by: who(c.by), creator: c.by === a.creator,
        canDelete: me.userId !== 0 && (c.by === me.id || mod) })), more: from + kCommentsPage < list.length, off: !!a.commentsOff, count: list.length });
    }
    if (name === 'icon.get') {
      // A game's icon (the app shows it while you connect).
      const a = this.assets.get(str(args, 'id'));
      if (a && !this.canPlay(a, me)) return fail(this.noPlay(a));
      if (!a) return fail('That game doesn\'t exist (any more).');
      const c = this.users.get(a.creator);
      const data = a.icon ? this.readFile('icon:' + a.id) : null;
      return okay({ data: data ? bytesToB64(data) : '', icon: data ? a.icon : 0, name: a.name, creatorName: c ? c.name : '' });
    }
    if (name === 'thumb.get') {
      const a = this.assets.get(str(args, 'id'));
      const data = a && a.thumb ? this.readFile('thumb:' + a.id) : null;
      return data ? okay({ data: bytesToB64(data) }) : fail('No picture.');
    }
    if (name === 'bolts.history') {
      const h = me.ledger.slice(-100).map((e) => ({ amount: e[0], reason: e[1], time: e[2] }));
      return okay({ history: h, me: this.meJson(me) });
    }
    if (name === 'bolts.daily') {
      if (this.hasRef(me, 'daily:' + today)) return fail('You already got today\'s Bolts. Come back tomorrow!');
      this.add(me, 25, 'Daily reward', 'daily:' + today);
      return okay({ me: this.meJson(me), got: 25 });
    }
    if (name === 'bolts.play') {
      if (t - me.lastPlay < 280) return fail('Keep playing!');
      if (me.playDay !== today) { me.playDay = today; me.playEarned = 0; }
      me.lastPlay = t;
      this.saveUser(me);
      if (me.playEarned >= 50) return fail('That\'s all the Bolts from playing for today.');
      me.playEarned += 5;
      this.add(me, 5, 'Playing games', 'play:' + today);
      return okay({ me: this.meJson(me), got: 5 });
    }
    if (name === 'bolts.redeem') {
      const code = str(args, 'code').replace(/\s/g, '');
      const a = code.indexOf('-'), b = a < 0 ? -1 : code.indexOf('-', a + 1), c = b < 0 ? -1 : code.indexOf('-', b + 1);
      if (!code.startsWith('BOLTS-') || c < 0) return fail('That doesn\'t look like a Bolts code.');
      const amount = parseInt(code.slice(a + 1, b), 10) || 0;
      const cn = code.slice(b + 1, c);
      if (amount <= 0 || !verify(this.official, 'gb-bolts:' + amount + ':' + me.id + ':' + cn, code.slice(c + 1)))
        return fail('That code isn\'t valid for your account.');
      if (this.hasRef(me, 'code:' + cn)) return fail('You already used that code.');
      this.add(me, amount, 'Bolts from Guts&Bolts staff', 'code:' + cn);
      return okay({ me: this.meJson(me), got: amount });
    }

    if (name.startsWith('admin.')) {
      if (!this.isStaff(me)) return fail('Only staff can do that.');
      let to = this.findPerson(args.to);
      if (!to && name !== 'admin.find' && isHex(lower(str(args, 'to')), 64, 64)) to = this.user(lower(str(args, 'to')));
      if (name === 'admin.find') {
        const q = lower(cleanText(str(args, 'query'), 64));
        const list = [];
        for (const u of this.users.values()) {
          if (list.length >= 40) break;
          const num = /^#?[0-9]+$/.test(q) ? Number(q.replace('#', '')) : -1;
          if (!q || u.userId === num || lower(u.name).includes(q) || u.id.startsWith(q)) list.push(this.publicUser(u));
        }
        return okay({ users: list });
      }
      // Upload review: new decals, sounds and T-shirts from creators who aren't Verified, oldest first.
      if (name === 'admin.uploads') return okay({ uploads: this.uploadsToReview() });
      if (name === 'admin.review') {
        const a = this.assets.get(str(args, 'id'));
        if (!a || !a.review) return fail('That isn\'t waiting for a check any more.');
        if (args.ok === true) { delete a.review; delete a.reviewNote; }
        else { a.review = 'rejected'; a.reviewNote = cleanText(str(args, 'note'), 200); }
        this.notify(this.users.get(a.creator), 'upload', args.ok === true ? a.name + ' passed the staff check. Everyone can see it now.'
          : a.name + ' didn\'t pass the staff check.' + (a.reviewNote ? ' Staff said: "' + a.reviewNote + '"' : ''), a.id);
        a.reviewedBy = me.id;
        this.staffDid(me, 'review', (args.ok === true ? 'Passed ' : 'Rejected ') + KIND_TITLE(a) + ' "' + a.name + '"' + (a.reviewNote ? ' ("' + a.reviewNote + '")' : ''), a.creator);
        this.saveAsset(a);
        return okay({ uploads: this.uploadsToReview() });
      }
      // Featured games: staff pick games for the Featured row on the home page and the Games page.
      if (name === 'admin.feature') {
        const a = this.assets.get(str(args, 'id'));
        if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
        const on = args.on !== false;
        if (on && (a.access || 'public') !== 'public') return fail('Only public games can be featured.');
        if (on && !a.featured) this.notify(this.users.get(a.creator), 'featured', 'Your game ' + a.name + ' is featured! Everyone sees it on the home page now.', a.id);
        if (on) a.featured = a.featured || t; else delete a.featured;
        this.staffDid(me, 'feature', (on ? 'Featured' : 'Unfeatured') + ' the game "' + a.name + '"', a.creator);
        this.saveAsset(a);
        return okay({ asset: this.publicAsset(a, me) });
      }
      if (name === 'admin.reports') return okay({ reports: this.reportsJson(str(args, 'status') === 'closed' ? 'closed' : 'open') });
      if (name === 'admin.tickets') return okay({ tickets: this.ticketsJson(str(args, 'status') === 'closed' ? 'closed' : 'open') });
      if (name === 'admin.replyTicket' || name === 'admin.ticketReset' || name === 'admin.closeTicket') return this.ticketAdminOp(name, me, args);
      if (name === 'admin.mailStatus') return okay({ mail: this.mailStatus() });
      if (name === 'admin.mailTest') {
        // Send a test email, to check the email settings work (the Staff page shows how it went).
        const to = lower(cleanText(str(args, 'email'), 254));
        if (!isEmail(to)) return fail('That doesn\'t look like an email address.');
        if (!this.canMail) return fail(this.mailStatus().problem);
        this.outbox.push({ to, subject: 'Guts&Bolts test email', text: 'Hi,\n\nIf you can read this, email works on your Guts&Bolts server.\n\n- Guts&Bolts', code: '' });
        return okay({ mail: this.mailStatus() });
      }
      if (name === 'admin.log') {   // the newest 200, or just what one person did or had done to them
        const who = str(args, 'user') ? this.findPerson(str(args, 'user')) : null;
        if (str(args, 'user') && !who) return fail('There\'s no account with that ID on this server.');
        return okay({ log: this.staffLogJson(who ? who.id : '') });
      }
      if (name === 'admin.closeReport') {
        // Staff looked at it: "done" (they did something, like a ban or a warning) or "dismissed" (nothing wrong).
        const outcome = str(args, 'outcome') === 'dismissed' ? 'dismissed' : 'done';
        const r = this.reports.find((x) => x.id === str(args, 'id'));
        if (!r) return fail('That report isn\'t there any more.');
        // Close the other open reports about the same thing too.
        for (const x of this.reports)
          if (x.status === 'open' && x.kind === r.kind && x.target === r.target) {
            x.status = 'closed'; x.outcome = outcome; x.closedBy = me.id; x.closedAt = now();
          }
        this.staffDid(me, 'report', (outcome === 'dismissed' ? 'Dismissed' : 'Closed') + ' the reports about a ' + r.kind + ' (' + (BAN_REASONS[r.reason] || r.reason) + ')', r.about);
        this.dirty.reports = true;
        return okay({ reports: this.reportsJson('open') });
      }
      if (!to) return fail('There\'s no account with that ID on this server.');
      if (name === 'admin.grant') {
        const key = str(args, 'key'), sig = str(args, 'sig');
        if (!grantValid(this.official, key, to.id, sig)) return fail('That badge signature isn\'t valid.');
        to.grants[key] = sig;
        this.staffDid(me, 'badge', 'Gave ' + to.name + ' the ' + key + ' badge', to.id);
        this.saveUser(to);
        return okay({ user: this.publicUser(to) });
      }
      if (name === 'admin.revoke') {
        const key = str(args, 'key');
        if (key !== 'verified' && !this.isOfficial(me)) return fail('Only the official account can take that badge away.');
        delete to.grants[key];
        this.staffDid(me, 'unbadge', 'Took the ' + key + ' badge from ' + to.name, to.id);
        this.saveUser(to);
        return okay({ user: this.publicUser(to) });
      }
      if (!this.isOfficial(me)) return fail('Only the official Guts account can do that.');
      if (name === 'admin.giveBolts') {
        const amount = num(args, 'amount');
        if (amount === 0 || Math.abs(amount) > 10000000) return fail('Pick an amount between 1 and 10,000,000.');
        const why = cleanText(str(args, 'reason'), 80);
        this.add(to, amount, why || (amount > 0 ? 'Bolts from Guts&Bolts staff' : 'Taken by staff'), 'gift:' + randomHex(6));
        this.staffDid(me, 'bolts', (amount > 0 ? 'Gave ' + to.name + ' ' + amount : 'Took ' + -amount + ' from ' + to.name) + ' Bolts' + (why ? ' ("' + why + '")' : ''), to.id);
        return okay({ user: this.publicUser(to), bolts: this.balance(to) });
      }
      if (name === 'admin.ban') {
        if (this.isOfficial(to)) return fail('You can\'t ban yourself.');
        const on = args.on === undefined ? true : !!args.on;
        if (on) {
          const reason = str(args, 'reason');
          if (!BAN_REASONS[reason]) return fail('Pick a reason for the ban.');
          to.banReason = reason;
          to.banNote = cleanText(str(args, 'note'), 200);
          to.bannedAt = now();
          const days = clamp(num(args, 'days'), 0, 3650);   // 0 = for good
          if (days > 0) to.bannedUntil = to.bannedAt + days * 86400; else delete to.bannedUntil;
        } else {
          delete to.banReason; delete to.banNote; delete to.bannedAt; delete to.bannedUntil;
        }
        to.banned = on;
        this.staffDid(me, on ? 'ban' : 'unban', on ? 'Banned ' + to.name + ' (' + (BAN_REASONS[to.banReason] || to.banReason) + ', '
          + (to.bannedUntil ? clamp(num(args, 'days'), 0, 3650) + ' days' : 'for good') + ')' + (to.banNote ? ': "' + to.banNote + '"' : '') : 'Unbanned ' + to.name, to.id);
        this.saveUser(to);
        return okay({ user: this.publicUser(to) });
      }
      if (name === 'admin.warn') {
        // A warning: they see it (with the reason) next time they open the app or the site.
        const reason = str(args, 'reason');
        if (!BAN_REASONS[reason]) return fail('Pick a reason for the warning.');
        to.warnings = to.warnings || [];
        to.warnings.push({ id: randomHex(4), reason, note: cleanText(str(args, 'note'), 200), at: now(), seen: false });
        if (to.warnings.length > kMaxWarnings) to.warnings.splice(0, to.warnings.length - kMaxWarnings);
        this.staffDid(me, 'warn', 'Warned ' + to.name + ' (' + (BAN_REASONS[reason] || reason) + ')', to.id);
        this.saveUser(to);
        return okay({ user: this.publicUser(to) });
      }
      return fail('Unknown staff request.');
    }

    if (name === 'upload') {
      const kind = str(args, 'kind');
      if (!KINDS.includes(kind)) return fail('You can\'t upload that kind of thing.');
      const title = say(str(args, 'name'), 50);
      if (!title) return fail('Give it a name.');
      const desc = say(str(args, 'description'), 1000, true);
      const verified = this.isVerified(me);
      if (isAccessory(kind) && !verified && !this.isStaff(me)) return fail('Only Verified creators can make hats and accessories. Shirts and pants are open to everyone!');
      if (kind === 'face' && !this.isOfficial(me)) return fail('Only Guts can make faces.');
      if (kind === 'gear' && !this.isStaff(me)) return fail('Only Guts&Bolts staff can make gear.');
      let price = clamp(num(args, 'price'), 0, 1000000);
      if (kind === 'game' || alwaysFree(kind)) price = 0;
      if (price > 0 && !verified) return fail('Only Verified creators can sell things. Upload it for free, or get Verified!');
      if (!verified) {
        if (me.uploadDay !== today) { me.uploadDay = today; me.uploadsToday = 0; }
        if (me.uploadsToday >= kDailyUploadsUnverified)
          return fail('You\'ve uploaded ' + kDailyUploadsUnverified + ' things today. Come back tomorrow (Verified creators have no limit).');
      }
      let data;
      try { data = b64ToBytes(str(args, 'data')); } catch { return fail('The upload got scrambled. Try again.'); }
      if (data.length > maxSize(kind)) return fail('That\'s too big (the most is ' + Math.floor(maxSize(kind) / 1024) + ' KB).');
      const meta = args.meta && typeof args.meta === 'object' && !Array.isArray(args.meta) ? args.meta : {};
      if (JSON.stringify(meta).length > 4096) return fail('Too much extra information.');
      if (kind === 'audio') {
        const ext = lower(typeof meta.ext === 'string' ? meta.ext : '');
        if (!['mp3', 'wav', 'ogg', 'flac'].includes(ext)) return fail('Audio must be .mp3, .wav, .ogg or .flac.');
        if (!data.length) return fail('That audio file is empty.');
      }
      if (kind === 'decal') {
        const png = data.length > 8 && [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a].every((v, i) => data[i] === v);
        const jpg = data.length > 3 && data[0] === 0xff && data[1] === 0xd8 && data[2] === 0xff;
        if (!png && !jpg) return fail('Decals must be .png or .jpg pictures.');
        meta.ext = png ? 'png' : 'jpg';
      }
      if (kind === 'game' && !isJson(data)) return fail('That isn\'t a Guts&Bolts game file.');
      if (kind === 'plugin' && !data.length) return fail('That plugin is empty.');
      if (kind === 'gear') { const problem = gearProblem(data); if (problem) return fail(problem); }
      let access;
      if (kind === 'model') {
        if (!isJson(data)) return fail('That isn\'t a Guts&Bolts model.');
        access = str(args, 'access') === 'private' ? 'private' : 'public';
        if (access === 'public' && !this.publicModelOk(me)) return fail(this.publicModelLimitText());
      }
      if (kind === 'animation') {
        let anim = null;
        try { anim = JSON.parse(new TextDecoder().decode(data)); } catch { anim = null; }
        if (!anim || anim.format !== 'gbanim' || !anim.clip || typeof anim.clip !== 'object') return fail('That isn\'t a Guts&Bolts animation.');
        access = str(args, 'access') === 'private' ? 'private' : 'public';
      }
      delete meta.image; delete meta.model;
      delete meta.color; delete meta.style;   // items don't have a colour: hats are Studio models, clothes are template pictures
      if (isAccessory(kind)) {
        // Made in Studio: the model and where it sits on the body.
        if (!data.length) return fail('Make hats and accessories in Studio\'s Accessory window, then upload them from there.');
        let acc = null;
        try { acc = JSON.parse(new TextDecoder().decode(data)); } catch { acc = null; }
        if (!acc || acc.format !== 'gbaccessory' || !acc.node) return fail('That isn\'t a Guts&Bolts accessory. Make it in Studio\'s Accessory window.');
        meta.model = true;
      }
      if (kind === 'face') {
        if (!pngSize(data)) return fail('Faces must be .png pictures (see-through around the face).');
        meta.image = true;
        meta.ext = 'png';
      }
      if (kind === 'tshirt') {
        const problem = tshirtProblem(data);
        if (problem) return fail(problem);
        meta.image = true;
        meta.ext = pngSize(data) ? 'png' : 'jpg';
      }
      if (kind === 'shirt' || kind === 'pants') {
        if (!data.length) return fail('Pick a picture made from the ' + kind + ' template.');
        const size = pngSize(data);
        if (!size) return fail('Clothing pictures must be .png files made from the template.');
        if (size[0] !== kTemplateW || size[1] !== kTemplateH)
          return fail('Clothing pictures must be ' + kTemplateW + ' x ' + kTemplateH + ' (the template\'s size).');
        meta.image = true;
        meta.ext = 'png';
      }
      const fee = verified ? 0 : FEE[kind];
      if (fee > 0 && this.balance(me) < fee)
        return fail('Uploading costs ' + fee + ' Bolts, and you have ' + this.balance(me) + '. (It\'s free for Verified creators.)');
      const assetNo = this.nextAssetNum++;
      this.dirty.ids = true;
      const a = { id: String(assetNo), num: assetNo, kind, name: title, description: desc, creator: me.id, price, created: t,
        sales: 0, plays: 0, size: data.length, meta };
      if (access) { a.access = access; if (access === 'public' && kind === 'model') this.countPublicModel(me); }
      if (REVIEWED_KINDS.includes(kind) && !verified && !this.isStaff(me)) a.review = 'pending';
      this.writeFile(a.id, data);
      if ((kind === 'face' || kind === 'tshirt') && data.length <= 400 * 1024) { this.writeFile('thumb:' + a.id, data); a.thumb = t; }   // its own picture
      this.assets.set(a.id, a);
      this.saveAsset(a);
      if (!me.owned.includes(a.id)) me.owned.push(a.id);
      if (!verified) me.uploadsToday++;
      if (fee > 0) this.add(me, -fee, 'Upload fee: ' + title, 'upload:' + a.id);
      this.saveUser(me);
      return okay({ asset: this.publicAsset(a), me: this.meJson(me), fee });
    }
    // --- Game passes: perks a game's creator sells for Bolts (like Roblox's). Each pass is its
    // own asset (kind 'gamepass', meta.game = the game), so 'buy' and owning work like any
    // item; game scripts ask 'pass.owned' who has which. (src/server has the same.)
    if (name === 'pass.create' || name === 'pass.edit') {
      const creating = name === 'pass.create';
      const a = creating ? null : this.assets.get(str(args, 'id'));
      // Developer products (kind 'devproduct'): like passes, but bought again and again inside the game
      // (MarketplaceService:PromptProductPurchase), each purchase a receipt the game's scripts hand out.
      const product = creating ? args.product === true : !!a && a.kind === 'devproduct';
      const kindName = product ? 'devproduct' : 'gamepass', what = product ? 'product' : 'pass';
      if (!creating && (!a || (a.kind !== 'gamepass' && a.kind !== 'devproduct'))) return fail('That pass doesn\'t exist (any more).');
      const g = this.assets.get(creating ? str(args, 'game') : (a.meta || {}).game);
      if (!g || g.kind !== 'game') return fail('That game doesn\'t exist (any more).');
      if (g.creator !== me.id && !this.isStaff(me)) return fail('Only the game\'s creator can make or change its passes.');
      if (creating && [...this.assets.values()].filter((x) => x.kind === kindName && (x.meta || {}).game === g.id).length >= kMostPasses)
        return fail('A game can have at most ' + kMostPasses + ' ' + what + (product ? 's.' : 'es.'));
      const title = 'name' in args || creating ? say(str(args, 'name'), 50) : a.name;
      if (!title) return fail('Give the ' + what + ' a name.');
      const price = 'price' in args || creating ? clamp(num(args, 'price'), 0, 1000000) : a.price;
      if (price > 0 && !this.isVerified(me) && !this.isStaff(me)) return fail('Only Verified creators can sell ' + what + (product ? 's' : 'es') + '. Make it free for now, or get Verified!');
      let icon = null;
      if (str(args, 'icon')) {
        try { icon = b64ToBytes(str(args, 'icon')); } catch { return fail('The picture got scrambled. Try again.'); }
        if (!pngSize(icon) && !jpgFile(icon)) return fail('Pass pictures must be .png or .jpg.');
        if (icon.length > 400 * 1024) return fail('That picture is too big (400 KB at most).');
      }
      const t = now();
      let pass = a;
      if (creating) {
        const assetNo = this.nextAssetNum++;
        this.dirty.ids = true;
        pass = { id: String(assetNo), num: assetNo, kind: kindName, name: title, description: '', creator: g.creator, price, created: t,
          sales: 0, plays: 0, size: 0, meta: { game: g.id } };
        this.writeFile(pass.id, new Uint8Array(0));
        this.assets.set(pass.id, pass);
        const owner = this.findUser(g.creator);
        if (!product && owner && !owner.owned.includes(pass.id)) { owner.owned.push(pass.id); this.saveUser(owner); }   // creators have their own passes
      }
      pass.name = title;
      pass.price = price;
      if ('description' in args) pass.description = say(str(args, 'description'), 1000, true);
      if ('offsale' in args) pass.offsaleAt = args.offsale === true ? 1 : 0;   // (1 = off sale since the start of time)
      if (icon) { this.writeFile('thumb:' + pass.id, icon); pass.thumb = t; }
      pass.updated = t;
      this.saveAsset(pass);
      return okay({ asset: this.publicAsset(pass, me) });
    }
    if (name === 'pass.list') {   // (products: true lists the game's developer products instead)
      const gameId = str(args, 'game'), kindName = args.products === true ? 'devproduct' : 'gamepass';
      const list = [...this.assets.values()].filter((x) => x.kind === kindName && (x.meta || {}).game === gameId)
        .sort((x, y) => x.created - y.created)
        .map((x) => ({ ...this.publicAsset(x, me), owned: me.owned.includes(x.id) }));
      return okay({ passes: list });
    }
    if (name === 'pass.owned') {
      // Which of a game's passes someone owns (game scripts: UserOwnsGamePassAsync). Takes a user
      // number or an account id.
      const gameId = str(args, 'game');
      const who = typeof args.user === 'number' ? this.findUserId(args.user) : this.findUser(str(args, 'user'));
      if (!who) return okay({ passes: [] });
      const passes = [...this.assets.values()].filter((x) => x.kind === 'gamepass' && (x.meta || {}).game === gameId && who.owned.includes(x.id))
        .map((x) => ({ id: x.id, num: x.num || 0 }));
      return okay({ passes });
    }
    // Developer products: buying one makes a receipt; the game's scripts (ProcessReceipt, on whoever
    // runs the server) ask for the buyer's waiting receipts and say when each one was handed out.
    if (name === 'product.buy') {
      if (me.userId === 0) return fail('Sign up to buy things.');
      const a = this.assets.get(str(args, 'id'));
      if (!a || a.kind !== 'devproduct') return fail('That product doesn\'t exist (any more).');
      if (this.isOffsale(a)) return fail('That isn\'t for sale right now.');
      if (this.balance(me) < a.price) return fail('You need ' + (a.price - this.balance(me)) + ' more Bolts for that.');
      const receipt = { id: 'r-' + randomHex(6), product: a.id, game: (a.meta || {}).game, price: a.price, at: t, granted: false };
      if (a.price > 0) {
        this.add(me, -a.price, 'Bought ' + a.name, 'product:' + receipt.id);
        this.paySeller(a, me, Math.floor(a.price * kCreatorSharePercent / 100), 'productsale:' + receipt.id);
      }
      me.receipts = [receipt, ...(me.receipts || [])].slice(0, 200);
      a.sales++;
      this.tally(a, 'sales');
      this.saveAsset(a); this.saveUser(me);
      return okay({ receipt: receipt.id, me: this.meJson(me) });
    }
    if (name === 'product.pending' || name === 'product.grant') {
      // Only the buyer, or whoever runs an online server of that game, may look and say "handed out".
      const gameId = str(args, 'game');
      const who = typeof args.user === 'number' ? this.findUserId(args.user) : this.findUser(str(args, 'user'));
      if (!who) return okay({ receipts: [] });
      const hosts = [...this.sessions.values()].some((x) => x.host === me.id && x.game === gameId);
      if (who.id !== me.id && !hosts) return fail('Only a server of that game can see its receipts.');
      const mine = (who.receipts || []).filter((r) => r.game === gameId);
      if (name === 'product.grant') {
        const r = mine.find((x) => x.id === str(args, 'receipt'));
        if (!r) return fail('There\'s no receipt like that.');
        r.granted = true;
        this.saveUser(who);
        return okay();
      }
      const receipts = mine.filter((r) => !r.granted).reverse().map((r) => {   // oldest first
        const a = this.assets.get(r.product);
        return { id: r.id, product: r.product, num: a ? a.num || 0 : 0, price: r.price, at: r.at };
      });
      return okay({ receipts });
    }
    // --- Game badges: creators make them on their game's page, game scripts award them.
    if (name === 'gamebadge.create' || name === 'gamebadge.delete') {
      const g = this.assets.get(str(args, 'game'));
      if (!g || g.kind !== 'game') return fail('That game doesn\'t exist (any more).');
      if (g.creator !== me.id) return fail('Only the game\'s creator can change its badges.');
      g.badges = g.badges || [];
      if (name === 'gamebadge.delete') {
        g.badges = g.badges.filter((b) => b.id !== str(args, 'badge'));
        this.saveAsset(g);
        return okay({ badges: g.badges });
      }
      if (g.badges.length >= 30) return fail('A game can have up to 30 badges.');
      const title = say(str(args, 'name'), 40);
      if (!title) return fail('Give the badge a name.');
      const col = Array.isArray(args.color) && args.color.length === 3 ? args.color.map((v) => clamp(Number(v) | 0, 0, 255)) : [240, 180, 40];
      const b = { id: 'badge-' + randomHex(5), name: title, description: say(str(args, 'description'), 300, true),
        color: col, created: t, awarded: 0 };
      g.badges.push(b);
      this.saveAsset(g);
      return okay({ badge: b, badges: g.badges });
    }
    if (name === 'gamebadge.award') {
      // Only the host of a live server of the badge's game can award it, and only
      // to someone in that server (BadgeService:AwardBadge in the game's scripts).
      const bid = str(args, 'badge');
      let g = null, b = null;
      for (const a of this.assets.values()) {
        if (a.kind !== 'game' || !a.badges) continue;
        const found = a.badges.find((x) => x.id === bid);
        if (found) { g = a; b = found; break; }
      }
      if (!b) return fail('There\'s no badge with that ID.');
      let session = null;
      for (const s of this.sessions.values()) if (s.host === me.id && s.game === g.id) session = s;
      if (!session) return fail('Badges can only be given in an online server of ' + g.name + '.');
      const who = lower(str(args, 'to'));
      let to = lower(me.name) === who ? me : null;
      for (const p of session.players) {
        const c = this.conns.get(p), u = c && this.users.get(c.account);
        if (!to && u && lower(u.name) === who) to = u;
      }
      if (!to) return fail('That player isn\'t in this server.');
      if (to.userId === 0) return fail('Guests can\'t earn badges. Sign up to collect them!');
      to.gameBadges = to.gameBadges || [];
      if (to.gameBadges.some((x) => x[0] === b.id)) return okay({ already: true, name: b.name });
      to.gameBadges.push([b.id, g.id, t]);
      b.awarded = (b.awarded || 0) + 1;
      this.saveUser(to);
      this.saveAsset(g);
      return okay({ awarded: true, name: b.name, player: to.name });
    }
    if (name === 'update') {
      const a = this.assets.get(str(args, 'id'));
      if (!a) return fail('That doesn\'t exist (any more).');
      if (a.creator !== me.id) return fail('You can only update your own things.');
      let data;
      try { data = b64ToBytes(str(args, 'data')); } catch { return fail('The upload got scrambled. Try again.'); }
      if (data.length > maxSize(a.kind)) return fail('That\'s too big.');
      if (a.kind === 'game' && !isJson(data)) return fail('That isn\'t a Guts&Bolts game file.');
      if (a.kind === 'gear') { const problem = gearProblem(data); if (problem) return fail(problem); }
      // A 3D accessory made in Studio: a new version of its model (e.g. with its pictures uploaded).
      const model = isAccessory(a.kind) && a.meta && a.meta.model && data.length;
      if (model) {
        let acc = null;
        try { acc = JSON.parse(new TextDecoder().decode(data)); } catch { acc = null; }
        if (!acc || acc.format !== 'gbaccessory' || !acc.node) return fail('That isn\'t a Guts&Bolts accessory.');
      }
      if (!isClothing(a.kind) || model) this.writeFile(a.id, data);
      // A new picture or sound gets checked again.
      if (REVIEWED_KINDS.includes(a.kind) && !isClothing(a.kind) && !this.isVerified(me) && !this.isStaff(me)) { a.review = 'pending'; delete a.reviewNote; }
      const title = say(str(args, 'name'), 50);
      if (title) a.name = title;
      if ('description' in args) a.description = say(str(args, 'description'), 1000, true);
      if ('price' in args && a.kind !== 'game' && !alwaysFree(a.kind)) {
        const price = clamp(num(args, 'price'), 0, 1000000);
        if (price > 0 && !this.isVerified(me)) return fail('Only Verified creators can sell things.');
        a.price = price;
      }
      if (!isClothing(a.kind)) a.size = data.length;
      a.updated = t;
      this.saveAsset(a);
      return okay({ asset: this.publicAsset(a) });
    }
    if (name === 'list') {
      const kind = str(args, 'kind'), q = lower(cleanText(str(args, 'query'), 64)), creator = lower(str(args, 'creator'));
      const sort = str(args, 'sort'), genre = str(args, 'genre');
      const ownedOnly = args.owned === true;   // your inventory (the Avatar page)
      const found = [...this.assets.values()].filter((a) =>
        (sort !== 'featured' || a.featured > 0) &&
        (!kind || a.kind === kind || (kind === 'clothing' && isCatalogItem(a.kind))) &&
        (!ownedOnly || me.owned.includes(a.id)) &&
        (!creator || a.creator === creator) && this.canPlay(a, me) &&
        (!genre || (a.genres || []).includes(genre)) &&
        (!q || lower(a.name).includes(q) || lower(a.description || '').includes(q) || (a.genres || []).some((g) => lower(g) === q)));
      const playing = sort === 'playing' ? new Map(found.map((a) => [a.id, this.playingIn(a.id)])) : null;
      const by = {
        popular: (x, y) => (y.plays + y.sales) - (x.plays + x.sales),
        playing: (x, y) => playing.get(y.id) - playing.get(x.id) || y.plays - x.plays,
        rated: (x, y) => ratingOf(y) - ratingOf(x) || y.plays - x.plays,
        updated: (x, y) => (y.updated || y.created) - (x.updated || x.created),
        featured: (x, y) => y.featured - x.featured,   // picked by staff, newest pick first
      }[sort] || ((x, y) => y.created - x.created);
      found.sort(by);
      const offset = Math.max(0, num(args, 'offset'));
      const limit = 'limit' in args ? clamp(num(args, 'limit'), 1, 100) : 60;
      return okay({ assets: found.slice(offset, offset + limit).map((a) => this.publicAsset(a, me)), total: found.length, genres: GENRES });
    }
    // One asset's page (the Library's "asset ID" pages): what it is, without downloading it.
    // Takes the ID people paste into games too ("gb:decal-..."). (src/server has the same.)
    if (name === 'asset.info') {
      const a = this.assets.get(str(args, 'id').trim().replace(/^gb:/, ''));
      if (!a || !this.canPlay(a, me)) return fail('There\'s nothing with that ID (or it\'s private).');
      return okay({ asset: this.publicAsset(a, me), owned: me.owned.includes(a.id) });
    }
    if (name === 'get') {
      const a = this.assets.get(str(args, 'id'));
      if (!a) return fail('That doesn\'t exist (any more).');
      const mine = me.owned.includes(a.id) || a.creator === me.id;
      if (a.price > 0 && !mine && !this.isStaff(me) && (a.kind === 'plugin' || a.kind === 'audio' || a.kind === 'gear')) return fail('Buy it first.');
      if (!this.canPlay(a, me)) return fail(this.noPlay(a));
      const data = this.readFile(a.id);
      if (!data) return fail('The server lost that file.');
      if (a.kind === 'game' && me.userId > 0) {   // "Continue playing" (newest first)
        me.recent = [a.id, ...(me.recent || []).filter((x) => x !== a.id)].slice(0, kMaxRecent);
        this.saveUser(me);
      }
      if (a.kind === 'game' && a.creator !== me.id) {
        a.plays++;
        this.tally(a, 'plays');
        me.played = me.played || [];
        if (!me.played.includes(a.id)) { me.played.push(a.id); if (me.played.length > 500) me.played.shift(); this.saveUser(me); }
      }
      return okay({ asset: this.publicAsset(a), data: bytesToB64(data) });
    }
    if (name === 'buy') {
      const a = this.assets.get(str(args, 'id'));
      if (!a) return fail('That doesn\'t exist (any more).');
      if (me.owned.includes(a.id)) return okay({ me: this.meJson(me), already: true });
      if (a.review && a.creator !== me.id) return fail(this.noPlay(a));
      if (a.kind === 'devproduct') return fail('Developer products are bought inside their game.');
      if (a.meta && a.meta.award === 'email') return fail('This hat can\'t be bought: confirm an email in Settings and it\'s yours.');
      if (this.isOffsale(a)) return fail('This item is off sale: it was only for sale for a limited time.' + (a.limited ? ' Buy one from a reseller on the item\'s page.' : ''));
      if (a.limited && a.sales >= a.stock) return fail('Sold out! Buy one from a reseller on the item\'s page.');
      if (a.price > 0) {
        if (this.balance(me) < a.price) return fail('You need ' + (a.price - this.balance(me)) + ' more Bolts for that.');
        this.add(me, -a.price, 'Bought ' + a.name, 'buy:' + a.id);
        this.paySeller(a, me, Math.floor(a.price * kCreatorSharePercent / 100), 'sale:' + a.id + ':' + randomHex(4));
      }
      a.sales++;
      if (a.creator !== me.id) this.tally(a, 'sales');
      if (a.limited) { a.copies = a.copies || []; a.copies.push({ serial: a.sales, owner: me.id, price: 0 }); }
      me.owned.push(a.id);
      this.saveAsset(a); this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'model.access') {
      // Make one of your Library models public or private.
      const a = this.assets.get(str(args, 'id'));
      if (!a || (a.kind !== 'model' && a.kind !== 'animation')) return fail('That model doesn\'t exist (any more).');
      if (a.creator !== me.id && !this.isStaff(me)) return fail('You can only change your own models.');
      const access = str(args, 'access') === 'private' ? 'private' : 'public';
      if (access === 'public' && a.access !== 'public' && a.kind === 'model') {
        if (!this.publicModelOk(me)) return fail(this.publicModelLimitText());
        this.countPublicModel(me);
      }
      a.access = access;
      this.saveAsset(a);
      return okay({ asset: this.publicAsset(a, me), me: this.meJson(me) });
    }
    // --- Editing items (their creator or staff) ---
    if (name === 'item.edit') {
      const a = this.assets.get(str(args, 'id'));
      if (!a || !isCatalogItem(a.kind)) return fail('That item doesn\'t exist (any more).');
      if (a.creator !== me.id && !this.isStaff(me)) return fail('Only the item\'s creator or staff can change it.');
      if ('name' in args) {
        const title = say(str(args, 'name'), 50);
        if (!title) return fail('Give it a name.');
        a.name = title;
      }
      if ('description' in args) a.description = say(str(args, 'description'), 1000, true);
      if ('price' in args) {
        const price = clamp(num(args, 'price'), 0, 1000000);
        if (price > 0 && !this.isVerified(this.users.get(a.creator) || me) && !this.isStaff(me)) return fail('Only Verified creators can sell things.');
        a.price = price;
      }
      if ('offsaleAt' in args) {   // a timed item: off sale from then on (0 = for sale for good)
        a.offsaleAt = Math.max(0, num(args, 'offsaleAt'));
        a.timedFor = a.timedFor || 'set';
      }
      a.meta = a.meta || {};
      if (a.kind === 'hat' && 'style' in args) a.meta.style = clamp(num(args, 'style'), 1, 3);   // the hat's shape
      const picture = a.kind === 'shirt' || a.kind === 'pants' || a.kind === 'face' || a.kind === 'tshirt';
      if (picture && typeof args.data === 'string' && args.data) {   // a new clothing / face picture
        let data;
        try { data = b64ToBytes(args.data); } catch { return fail('The upload got scrambled. Try again.'); }
        const size = pngSize(data);
        if (a.kind === 'face') {
          if (!size) return fail('Faces must be .png pictures.');
        } else if (a.kind === 'tshirt') {
          const problem = tshirtProblem(data);
          if (problem) return fail(problem);
        } else if (!size || size[0] !== kTemplateW || size[1] !== kTemplateH) return fail('Clothing pictures must be ' + kTemplateW + ' x ' + kTemplateH + ' .png files.');
        if (data.length > maxSize(a.kind)) return fail('That\'s too big.');
        this.writeFile(a.id, data);
        if ((a.kind === 'face' || a.kind === 'tshirt') && data.length <= 400 * 1024) { this.writeFile('thumb:' + a.id, data); a.thumb = t; }
        a.meta.image = true; a.meta.ext = a.kind === 'tshirt' && !size ? 'jpg' : 'png'; a.size = data.length;
      }
      a.updated = t;
      this.saveAsset(a);
      return okay({ asset: this.publicAsset(a, me) });
    }
    if (name === 'item.limited') {
      // Only Guts makes items Limited: a fixed number of numbered copies. People who
      // already have it get the first serial numbers.
      if (!this.isOfficial(me)) return fail('Only Guts can make items Limited.');
      const a = this.assets.get(str(args, 'id'));
      if (!a || !isClothing(a.kind)) return fail('That item doesn\'t exist (any more).');
      if (!canBeLimited(a.kind)) return fail('Only accessories and faces can be Limited (not shirts, pants, audio, decals or models).');
      if (a.creator !== me.id) return fail('Only accessories and faces Guts made can be Limited.');
      const stock = clamp(num(args, 'stock'), 1, 1000000);
      if (!a.limited) {
        a.copies = [];
        let serial = 0;
        for (const u of this.users.values())
          if (u.owned.includes(a.id) && u.id !== a.creator) a.copies.push({ serial: ++serial, owner: u.id, price: 0 });
        a.sales = serial;
        a.limited = true;
      }
      if (stock < a.sales) return fail(a.sales + ' copies are already out there: the stock can\'t be less than that.');
      a.stock = stock;
      this.saveAsset(a);
      return okay({ asset: this.publicAsset(a, me) });
    }
    if (name === 'item.copies') {
      // A limited item's copies: who has which number, and which are for sale.
      const a = this.assets.get(str(args, 'id'));
      if (!a || !a.limited) return okay({ copies: [] });
      // (Who holds a copy that isn't for sale stays hidden when they hid their inventory from you.)
      const copies = (a.copies || []).map((c) => {
        const u = this.users.get(c.owner), shown = c.price > 0 || !u || this.seesInventory(me, u);
        return { serial: c.serial, price: c.price || 0, owner: shown ? c.owner : '', ownerName: !shown ? 'Hidden' : u ? u.name : '?', mine: c.owner === me.id };
      });
      return okay({ copies });
    }
    if (name === 'resale.list') {
      // Put your copy of a limited up for sale (price 0 = take it off sale).
      const a = this.assets.get(str(args, 'id'));
      const c = a && a.limited && (a.copies || []).find((x) => x.serial === num(args, 'serial'));
      if (!c || c.owner !== me.id) return fail('You don\'t have that copy.');
      c.price = clamp(num(args, 'price'), 0, 10000000);
      this.saveAsset(a);
      return okay({ asset: this.publicAsset(a, me) });
    }
    if (name === 'resale.buy') {
      const a = this.assets.get(str(args, 'id'));
      const c = a && a.limited && (a.copies || []).find((x) => x.serial === num(args, 'serial'));
      if (!c || !(c.price > 0)) return fail('That copy isn\'t for sale (any more).');
      if (c.owner === me.id) return fail('That\'s your own copy.');
      if (this.balance(me) < c.price) return fail('You need ' + (c.price - this.balance(me)) + ' more Bolts for that.');
      const seller = this.users.get(c.owner);
      this.add(me, -c.price, 'Bought ' + a.name + ' #' + c.serial, 'resale:' + a.id + ':' + c.serial + ':' + randomHex(4));
      if (seller) {
        const share = Math.floor(c.price * kCreatorSharePercent / 100);
        this.add(seller, share, 'Sold ' + a.name + ' #' + c.serial, 'resold:' + a.id + ':' + c.serial + ':' + randomHex(4));
        this.notify(seller, 'sale', me.name + ' bought your ' + a.name + ' #' + c.serial + '. You got ' + share + ' Bolts.', a.id);
      }
      c.owner = me.id;
      c.price = 0;
      this.saveAsset(a);
      this.syncOwned(me, a);
      if (seller) this.syncOwned(seller, a);
      return okay({ me: this.meJson(me) });
    }
    // --- Trading limited copies ---
    if (name === 'trade.inventory') {
      const u = this.findPerson(args.user);
      if (!u) return fail('There\'s no account with that ID on this server.');
      // Someone who hid their inventory from you: you see nothing (and can't trade with them).
      const canSee = this.seesInventory(me, u);
      return okay({ user: this.publicUser(u), items: canSee ? this.copiesOf(u) : [], hidden: !canSee });
    }
    if (name === 'trade.send') {
      const to = this.findPerson(args.to);
      if (!to || to.userId === 0) return fail('There\'s no account with that ID on this server.');
      if (to.id === me.id) return fail('You can\'t trade with yourself.');
      if (this.blocks(me, to)) return fail('You can\'t trade with that account.');
      if (!this.seesInventory(me, to)) return fail(to.name + ' keeps their inventory private, so you can\'t trade with them.');
      const pick = (list) => (Array.isArray(list) ? list : []).slice(0, 4).map((x) => ({ id: String(x && x.id || ''), serial: Number(x && x.serial) | 0 }));
      const give = pick(args.give), get = pick(args.get);
      if (!give.length || !get.length) return fail('Pick at least one of your limiteds and one of theirs.');
      const holds = (u, list) => list.every((x) => { const a = this.assets.get(x.id); return a && a.limited && (a.copies || []).some((c) => c.serial === x.serial && c.owner === u.id); });
      if (!holds(me, give)) return fail('You don\'t have all of those.');
      if (!holds(to, get)) return fail('They don\'t have all of those.');
      if (this.trades.filter((x) => x.from === me.id && x.status === 'open').length >= 20) return fail('You have 20 trades waiting already.');
      const trade = { id: 'tr-' + randomHex(6), from: me.id, to: to.id, give, get, status: 'open', created: t, updated: t };
      this.trades.push(trade);
      this.dirty.trades = true;
      this.notify(to, 'trade', me.name + ' sent you a trade.', trade.id);
      return okay({ trade });
    }
    if (name === 'trade.list') {
      const named = (x) => {
        const f = this.users.get(x.from), to = this.users.get(x.to);
        const label = (l) => l.map((i) => { const a = this.assets.get(i.id); return Object.assign({ name: a ? a.name : '(gone)', kind: a ? a.kind : '' }, i); });
        return Object.assign({}, x, { fromName: f ? f.name : '?', toName: to ? to.name : '?', give: label(x.give), get: label(x.get) });
      };
      const mine = this.trades.filter((x) => x.from === me.id || x.to === me.id).slice(-100).reverse().map(named);
      return okay({ trades: mine, items: this.copiesOf(me) });
    }
    if (name === 'trade.accept' || name === 'trade.decline' || name === 'trade.cancel') {
      const tr = this.trades.find((x) => x.id === str(args, 'id'));
      if (!tr || tr.status !== 'open') return fail('That trade isn\'t open (any more).');
      if (name === 'trade.cancel' ? tr.from !== me.id : tr.to !== me.id) return fail('That isn\'t your trade to answer.');
      tr.updated = t;
      this.dirty.trades = true;
      if (name !== 'trade.accept') {
        tr.status = name === 'trade.cancel' ? 'cancelled' : 'declined';
        if (name === 'trade.decline') this.notify(this.users.get(tr.from), 'trade', me.name + ' turned down your trade.', tr.id);
        return okay({ trade: tr });
      }
      const from = this.users.get(tr.from);
      const copy = (x) => { const a = this.assets.get(x.id); return a && a.limited ? (a.copies || []).find((c) => c.serial === x.serial) : null; };
      const ok = from && tr.give.every((x) => { const c = copy(x); return c && c.owner === from.id; }) &&
        tr.get.every((x) => { const c = copy(x); return c && c.owner === me.id; });
      if (!ok) { tr.status = 'failed'; return fail('Someone doesn\'t have those items any more, so the trade can\'t happen.'); }
      const touched = new Set();
      for (const x of tr.give) { const c = copy(x); c.owner = me.id; c.price = 0; touched.add(x.id); }
      for (const x of tr.get) { const c = copy(x); c.owner = from.id; c.price = 0; touched.add(x.id); }
      for (const id of touched) { const a = this.assets.get(id); this.saveAsset(a); this.syncOwned(me, a); this.syncOwned(from, a); }
      tr.status = 'accepted';
      this.notify(from, 'trade', me.name + ' accepted your trade!', tr.id);
      return okay({ trade: tr, me: this.meJson(me) });
    }
    if (name === 'delete') {
      const a = this.assets.get(str(args, 'id'));
      if (!a) return fail('That doesn\'t exist (any more).');
      if (a.creator !== me.id && !this.isStaff(me)) return fail('You can only delete your own things.');
      if (a.limited && (a.copies || []).length && !this.isOfficial(me)) return fail('People own copies of this Limited, so it can\'t be deleted.');
      if (a.creator !== me.id) this.staffDid(me, 'delete', 'Deleted ' + KIND_TITLE(a) + ' "' + a.name + '"', a.creator);
      this.sql.exec('DELETE FROM files WHERE id = ? OR id = ? OR id = ?', a.id, 'thumb:' + a.id, 'icon:' + a.id);
      this.assets.delete(a.id);
      this.dirty.assets.add(a.id);
      return okay();
    }
    // Creator stats: everything you made, with totals and the last 30 days (plays, sales, Bolts earned).
    if (name === 'creator.stats') {
      if (me.userId === 0) return fail('Sign up first.');
      const t0 = now(), days = [];
      for (let i = 29; i >= 0; i--) days.push(utcDay(t0 - i * 86400));
      const mine = [...this.assets.values()].filter((a) => a.creator === me.id);
      const items = mine.map((a) => {
        const d = a.days || {};
        const series = (f) => days.map((k) => (d[k] && d[k][f]) || 0);
        const sum = (f) => Object.values(d).reduce((n, x) => n + (x[f] || 0), 0);
        return { id: a.id, num: a.num || 0, kind: a.kind, name: a.name, plays: a.plays || 0, sales: a.sales || 0, price: a.price || 0,
          favorites: a.favorites || 0, likes: a.likes || 0, dislikes: a.dislikes || 0, playing: a.kind === 'game' ? this.playingIn(a.id) : 0,
          plays30: series('plays'), sales30: series('sales'), bolts30: series('bolts'), bolts60: sum('bolts') };
      }).sort((x, y) => (y.plays + y.sales) - (x.plays + x.sales));
      return okay({ days, items });
    }
    if (name === 'stats') return okay({ users: this.users.size, assets: this.assets.size, name: this.name });
    return fail('The server doesn\'t know how to do "' + name + '". It might need updating.');
  }

  accountOp(name, me, args) {
    const username = cleanText(str(args, 'username'), 30);
    if (name === 'account.ackWarning') {   // "I understand" on a staff warning
      const w = (me.warnings || []).find((x) => x.id === str(args, 'id'));
      if (!w) return fail('That warning is gone.');
      w.seen = true;
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'account.check') {
      let problem = usernameProblem(username, this.isOfficial(me));
      if (!problem && this.takenNames.has(lower(username)) && !(this.isOfficial(me) && lower(username) === 'guts'))
        problem = 'That username is taken.';
      return okay({ available: !problem, problem });
    }
    if (name === 'account.signup') {
      const salt = lower(str(args, 'salt')), auth = lower(str(args, 'auth')), blob = lower(str(args, 'key'));
      if (!isHex(salt, 32, 32) || !isHex(auth, 64, 64) || !isHex(blob, 208, 208))
        return fail('The sign-up form was missing something. Update the app and try again.');
      const official = this.isOfficial(me);
      if (me.userId > 0 && !(official && !me.keyBlob)) return fail('This device is already signed up as ' + me.username + '.');
      if (official) {
        if (lower(username) !== 'guts') return fail('The staff account\'s username is Guts.');
        me.name = me.username;
      } else {
        const problem = usernameProblem(username);
        if (problem) return fail(problem);
        if (this.takenNames.has(lower(username))) return fail('That username is taken. Try another one.');
        me.userId = this.nextUserId++;
        me.username = username;
        this.takenNames.add(lower(username));
        me.name = username;
        this.gutsFollows(me);
      }
      me.pwSalt = salt; me.pwHash = hashHex(auth); me.keyBlob = blob;
      this.dirty.ids = true;
      if (!this.hasRef(me, 'welcome')) this.add(me, 100, 'Welcome to Guts&Bolts!', 'welcome');
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    // --- your email, password and two-step verification (signed in) ---
    const provePassword = () => me.keyBlob && isHex(str(args, 'auth'), 64, 64) && hashHex(lower(str(args, 'auth'))) === me.pwHash;
    if (name === 'account.email') {
      if (me.userId === 0) return fail('Sign up first.');
      const email = lower(cleanText(str(args, 'email'), 254));
      if (!isEmail(email)) return fail('That doesn\'t look like an email address.');
      if (me.twoStep && me.emailVerified && !provePassword()) return fail('Type your password to change your email.');
      const problem = this.mailCode(me, 'email', email, 'Your Guts&Bolts email code',
        'Type this code on the Guts&Bolts website to add this email to your account (' + me.username + '):');
      if (problem) return fail(problem);
      me.pendingEmail = email;
      this.saveUser(me);
      return okay({ me: this.meJson(me), sentTo: maskEmail(email) });
    }
    if (name === 'account.emailVerify') {
      if (me.userId === 0 || !me.pendingEmail) return fail('Add an email first.');
      const problem = this.checkCode(me, 'email', str(args, 'code'));
      if (problem) return fail(problem);
      me.email = me.pendingEmail; me.emailVerified = true; me.pendingEmail = '';
      this.giveVerifiedHat(me);
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'account.emailRemove') {
      if (me.userId === 0) return fail('Sign up first.');
      if (me.keyBlob && !provePassword()) return fail('Wrong password.');
      me.email = ''; me.emailVerified = false; me.pendingEmail = ''; me.twoStep = false;
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'account.twoStep') {
      if (me.userId === 0) return fail('Sign up first.');
      if (!provePassword()) return fail('Wrong password.');
      const on = !!args.on;
      if (on && !me.emailVerified) return fail('Add and confirm an email first: the codes go there.');
      me.twoStep = on;
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    // Authenticator app: setup gives a secret (and an otpauth:// link for the QR code);
    // a code from the app turns it on. Then logging in needs a code from the app.
    if (name === 'account.privacy') {
      if (me.userId === 0) return fail('Sign up first.');
      const p = this.privacyOf(me);
      if (PRIVACY.includes(args.status)) p.status = args.status;
      if (PRIVACY.includes(args.join)) p.join = args.join;
      if (PRIVACY.includes(args.messages)) p.messages = args.messages;   // who can send you messages
      if (PRIVACY.includes(args.inventory)) p.inventory = args.inventory;   // who can see your inventory
      me.privacy = p;
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'account.authAppSetup') {
      if (me.userId === 0) return fail('Sign up first.');
      if (me.keyBlob && !provePassword()) return fail('Wrong password.');
      me.totpPending = base32(crypto.getRandomValues(new Uint8Array(20)));
      this.saveUser(me);
      const label = encodeURIComponent('Guts&Bolts:' + me.username);
      return okay({ secret: me.totpPending,
        uri: 'otpauth://totp/' + label + '?secret=' + me.totpPending + '&issuer=' + encodeURIComponent('Guts&Bolts') + '&digits=6&period=30' });
    }
    if (name === 'account.authAppEnable') {
      if (!me.totpPending) return fail('Start setting it up first.');
      const step = totpCheck(me.totpPending, str(args, 'code'), now());
      if (step < 0) return fail('That code isn\'t right. Check your phone\'s clock, and type the newest code.');
      me.totpSecret = me.totpPending; me.totpPending = ''; me.totpLast = step;
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'account.authAppDisable') {
      if (!me.totpSecret) return okay({ me: this.meJson(me) });
      if (me.keyBlob && !provePassword()) return fail('Wrong password.');
      if (totpCheck(me.totpSecret, str(args, 'code'), now()) < 0) return fail('That code isn\'t right.');
      me.totpSecret = ''; me.totpLast = -1;
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'account.joinDate') {
      // Guts only: set the account's join date (earlier than it really was, e.g. when
      // the project started). Everyone sees it on the profile.
      if (!this.isOfficial(me)) return fail('Only Guts can change a join date.');
      const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(str(args, 'date'));
      const when = m ? Math.floor(Date.UTC(+m[1], +m[2] - 1, +m[3], 12) / 1000) : NaN;
      if (!Number.isFinite(when) || when < Date.UTC(2000, 0, 1) / 1000 || when > now()) return fail('Pick a date in the past (like 2025-09-15).');
      me.created = when;
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'account.rename') {
      // Change your username for 1000 Bolts. Old usernames stay reserved for you: nobody
      // else can take them, and you can switch back to one (for the same price).
      if (me.userId === 0) return fail('Sign up first.');
      if (this.isOfficial(me)) return fail('The staff account is always Guts.');
      const username = cleanText(str(args, 'username'), 20);
      const problem = usernameProblem(username);
      if (problem) return fail(problem);
      const want = lower(username);
      if (want === lower(me.username)) {
        if (username === me.username) return fail('That\'s already your username.');
      } else {
        const mine = (me.pastNames || []).some((p) => lower(p) === want);
        if (!mine && this.takenNames.has(want)) return fail('That username is taken (usernames are never reused).');
      }
      if (this.balance(me) < kRenameCost) return fail('Changing your username costs ' + kRenameCost + ' Bolts. You have ' + this.balance(me) + '.');
      const old = me.username;
      this.add(me, -kRenameCost, 'Username change: ' + old + ' to ' + username, 'rename:' + randomHex(6));
      me.pastNames = (me.pastNames || []).filter((p) => lower(p) !== want);
      if (lower(old) !== want && !me.pastNames.some((p) => lower(p) === lower(old))) me.pastNames.push(old);
      me.username = username;
      me.name = username;
      this.takenNames.add(want);
      this.dirty.ids = true;
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }
    if (name === 'account.password') {
      // Change your password: the new locked key comes from this device (which has the key).
      if (me.userId === 0 || !me.keyBlob) return fail('Set a password in the app first.');
      if (!provePassword()) return fail('Your current password is wrong.');
      const salt = lower(str(args, 'salt')), auth = lower(str(args, 'newAuth')), blob = lower(str(args, 'key'));
      if (!isHex(salt, 32, 32) || !isHex(auth, 64, 64) || !isHex(blob, 208, 208)) return fail('Something was missing. Try again.');
      me.pwSalt = salt; me.pwHash = hashHex(auth); me.keyBlob = blob;
      this.saveUser(me);
      return okay({ me: this.meJson(me) });
    }

    const u = this.findUsername(username);
    // --- forgot password: a code by email, then a new password (and a new key) ---
    if (name === 'account.forgot') {
      const sent = okay({ message: 'If that account has an email, we sent it a code. Check your inbox (and spam).' });
      if (!u || !u.emailVerified || !u.email) return sent;
      if (this.isOfficial(u)) return fail('The Guts account can\'t be reset by email.');
      const problem = this.mailCode(u, 'reset', u.email, 'Reset your Guts&Bolts password',
        'Someone (hopefully you) asked to reset the password for ' + u.username + '. Type this code on the website:');
      return problem && problem.startsWith('Email isn') ? fail(problem) : sent;
    }
    if (name === 'account.reset') {
      // The code came by email, or staff made it for a support ticket (then it only works on the device that sent it).
      const c = u && u.codes && u.codes.reset;
      if (!u || !c || (!u.emailVerified && !c.ticket)) return fail('That code isn\'t right.');
      if (this.isOfficial(u)) return fail('The Guts account can\'t be reset by email.');
      if (me.userId > 0) return fail('Log out first: this device is signed in as ' + me.username + '.');
      if (c.device && c.device !== me.id) return fail('That code only works on the device that sent the support ticket.');
      const ticket = c.ticket;
      const problem = this.checkCode(u, 'reset', str(args, 'code'));
      if (problem) return fail(problem);
      const salt = lower(str(args, 'salt')), auth = lower(str(args, 'auth')), blob = lower(str(args, 'key'));
      if (!isHex(salt, 32, 32) || !isHex(auth, 64, 64) || !isHex(blob, 208, 208)) return fail('Something was missing. Try again.');
      // This device's key takes over the account; every older key (and device) is logged out.
      for (const k of u.keys || []) this.aliases.delete(k);
      u.keys = [me.id];
      this.aliases.set(me.id, u.id);
      u.keyRevoked = true;
      u.pwSalt = salt; u.pwHash = hashHex(auth); u.keyBlob = blob;
      this.failedLogins.delete(lower(u.username));
      this.saveUser(u);
      const tk = ticket && this.tickets.find((x) => x.id === ticket);
      if (tk) { tk.resetUsed = true; delete tk.code; tk.status = 'closed'; tk.closedAt = now(); this.dirty.tickets = true; }
      return okay({ me: this.meJson(u) });
    }
    // An account made on a device that never set a password can't be logged into anywhere else yet.
    const noLogin = (acc) => (!acc ? 'There\'s no account with that username.'
      : 'That account hasn\'t set a password yet, so it only works on the device it was made on. On that device, open the '
        + 'Guts&Bolts Player, go to Avatar > Your account and press "Set a password". Then you can log in here.');
    if (name === 'account.salt') {
      if (!u || !u.keyBlob) return fail(noLogin(u));
      return okay({ salt: u.pwSalt });
    }
    if (name === 'account.login') {
      if (!u || !u.keyBlob) return fail(noLogin(u));
      if (u.banned) return fail(banMessage(u));
      const t = now(), key = lower(username);
      const fails = (this.failedLogins.get(key) || []).filter((x) => t - x <= kLockoutSeconds);
      this.failedLogins.set(key, fails);
      if (fails.length >= kMaxWrongPasswords) return fail('Too many wrong passwords. Wait 10 minutes and try again.');
      const auth = lower(str(args, 'auth'));
      if (!isHex(auth, 64, 64) || hashHex(auth) !== u.pwHash) { fails.push(t); return fail('Wrong password.'); }
      if (u.totpSecret) {   // authenticator app (checked first; it doesn't need email)
        const code = str(args, 'code');
        if (!code) return Object.assign(fail('Type the 6-digit code from your authenticator app.'), { needCode: true, app: true });
        const step = totpCheck(u.totpSecret, code, now(), u.totpLast ?? -1);
        if (step < 0) { fails.push(t); return Object.assign(fail('That code isn\'t right (or was already used). Try the newest one.'), { needCode: true, app: true }); }
        u.totpLast = step;
        this.saveUser(u);
      } else if (u.twoStep && u.emailVerified) {
        const code = str(args, 'code');
        if (!code) {
          const problem = this.mailCode(u, 'login', u.email, 'Your Guts&Bolts login code',
            'Someone (hopefully you) is logging in to ' + u.username + '. Type this code to finish:');
          if (problem && !problem.startsWith('We just sent')) return fail(problem);
          return Object.assign(fail('We emailed a code to ' + maskEmail(u.email) + '. Type it in to finish logging in.'), { needCode: true });
        }
        const problem = this.checkCode(u, 'login', code);
        if (problem) return Object.assign(fail(problem), { needCode: true });
      }
      this.failedLogins.delete(key);
      // The locked key is for the account's newest key (after a password reset, not the account ID itself).
      const keyAccount = u.keys && u.keys.length ? u.keys[0] : u.id;
      return okay({ account: u.id, keyAccount, key: u.keyBlob, username: u.username, name: u.name });
    }
    return fail('Unknown request.');
  }

  friendOp(name, me, args) {
    const person = (u) => ({ id: u.id, userId: u.userId, username: u.username, name: u.name, verified: this.isVerified(u) });
    if (name === 'friends.list') {
      const friends = [];
      for (const id of me.friends) {
        const u = this.findUser(id);
        if (!u) continue;
        const pr = this.presence(me, u);
        const f = Object.assign(person(u), { online: pr.online });
        if (pr.playing) f.playing = pr.playing;
        friends.push(f);
      }
      return okay({ friends, incoming: me.friendIn.map((id) => this.findUser(id)).filter(Boolean).map(person),
        outgoing: me.friendOut.map((id) => this.findUser(id)).filter(Boolean).map(person) });
    }
    const them = this.findPerson(args.user);
    if (!them || them.userId === 0) return fail('There\'s no account with that ID on this server.');
    const following = me.following || (me.following = []), followers = them.followers || (them.followers = []);
    // Where you stand with someone (the in-game player list asks before showing its menu).
    if (name === 'friends.relation') {
      const friendship = them.id === me.id ? 'self' : me.friends.includes(them.id) ? 'friends'
        : me.friendOut.includes(them.id) ? 'sent' : me.friendIn.includes(them.id) ? 'received' : 'none';
      return okay({ id: them.id, name: them.name, friendship, following: following.includes(them.id),
        followers: followers.length });
    }
    // Following: one way, no asking (like Roblox). You see what they're up to.
    if (name === 'follow.add' || name === 'follow.remove') {
      if (them.id === me.id) return fail('You can\'t follow yourself.');
      const i = following.indexOf(them.id), j = followers.indexOf(me.id);
      if (name === 'follow.add') {
        if (them.banned || this.blocks(me, them)) return fail('You can\'t follow that account.');
        if (i < 0 && following.length >= kMaxFollowing) return fail('You already follow ' + kMaxFollowing + ' people.');
        if (i < 0) following.push(them.id);
        if (j < 0) { followers.push(me.id); this.notify(them, 'follow', me.name + ' follows you now.', me.id); }
      } else {
        if (i >= 0) following.splice(i, 1);
        if (j >= 0) followers.splice(j, 1);
      }
      this.saveUser(me, them);
      return okay({ following: name === 'follow.add', followers: followers.length });
    }
    if (them.id === me.id) return fail('You can\'t be friends with yourself (but we like you).');
    const drop = (list, id) => { const i = list.indexOf(id); if (i >= 0) list.splice(i, 1); };
    const addTo = (list, id) => { if (!list.includes(id)) list.push(id); };
    const clearRequests = () => { drop(me.friendIn, them.id); drop(me.friendOut, them.id); drop(them.friendIn, me.id); drop(them.friendOut, me.id); };
    const becomeFriends = () => {
      if (me.friends.length >= kMaxFriends || them.friends.length >= kMaxFriends)
        return fail('One of you already has ' + kMaxFriends + ' friends.');
      clearRequests();
      addTo(me.friends, them.id); addTo(them.friends, me.id);
      this.saveUser(me, them);
      this.notify(them, 'friend', me.name + ' is your friend now.', me.id);
      return okay({ status: 'friends' });
    };
    if (name === 'friends.add') {
      if (me.friends.includes(them.id)) return okay({ status: 'friends' });
      if (me.friendIn.includes(them.id)) return becomeFriends();
      if (them.banned || this.blocks(me, them)) return fail('You can\'t add that account.');
      if (me.friendOut.length >= kMaxRequests) return fail('You have too many friend requests waiting. Cancel some first.');
      if (them.friendIn.length >= kMaxRequests) return fail(them.name + ' has too many friend requests waiting.');
      addTo(me.friendOut, them.id); addTo(them.friendIn, me.id);
      this.saveUser(me, them);
      this.notify(them, 'friendRequest', me.name + ' sent you a friend request.', me.id);
      return okay({ status: 'sent' });
    }
    if (name === 'friends.accept') {
      if (!me.friendIn.includes(them.id)) return fail('That friend request isn\'t there any more.');
      return becomeFriends();
    }
    if (name === 'friends.decline' || name === 'friends.cancel' || name === 'friends.remove') {
      clearRequests();
      if (name === 'friends.remove') { drop(me.friends, them.id); drop(them.friends, me.id); }
      this.saveUser(me, them);
      return okay({ status: 'none' });
    }
    return fail('Unknown request.');
  }

  // --- Blocking and reports (ServerSafety.cpp has the same) ---
  safetyOp(name, me, args) {
    if (me.userId === 0) return fail('Sign up first.');
    const today = utcDay(now());
    const blocked = me.blocked || (me.blocked = []);
    if (name === 'block.list') {
      return okay({ people: blocked.map((id) => this.users.get(id)).filter(Boolean).map((u) => this.publicUser(u)) });
    }
    if (name === 'block.add' || name === 'block.remove') {
      const them = this.findPerson(args.user);
      if (!them || them.userId === 0) return fail('There\'s no account with that ID on this server.');
      if (them.id === me.id) return fail('You can\'t block yourself.');
      if (name === 'block.remove') {
        me.blocked = blocked.filter((id) => id !== them.id);
        this.saveUser(me);
        return okay({ blocked: false });
      }
      if (this.isStaff(them)) return fail('You can\'t block Guts&Bolts staff. Report them instead if something\'s wrong.');
      if (!blocked.includes(them.id)) {
        if (blocked.length >= kMaxBlocked) return fail('You\'ve blocked ' + kMaxBlocked + ' people already. Unblock some first.');
        blocked.push(them.id);
      }
      // Blocking ends everything between you: friends, requests, follows, open trades, and their messages to you.
      const drop = (list, id) => { const i = (list || []).indexOf(id); if (i >= 0) list.splice(i, 1); };
      for (const [a, b] of [[me, them], [them, me]]) {
        drop(a.friends, b.id); drop(a.friendIn, b.id); drop(a.friendOut, b.id); drop(a.following, b.id); drop(a.followers, b.id);
      }
      me.inbox = (me.inbox || []).filter((m) => m.from !== them.id);
      for (const x of this.trades)
        if (x.status === 'open' && ((x.from === me.id && x.to === them.id) || (x.from === them.id && x.to === me.id))) {
          x.status = 'cancelled'; x.updated = now(); this.dirty.trades = true;
        }
      this.saveUser(me, them);
      return okay({ blocked: true, me: this.meJson(me) });
    }
    if (name === 'report.send') {
      // kind: user, game, item, message (one in your inbox) or group; id: which one.
      const kind = str(args, 'kind'), id = str(args, 'id');
      if (!REPORT_KINDS.includes(kind)) return fail('You can\'t report that.');
      const reason = str(args, 'reason');
      if (!BAN_REASONS[reason]) return fail('Pick what\'s wrong.');
      const note = cleanText(str(args, 'note'), 500, true);
      let target = '', about = '', copy = null;
      if (kind === 'user') {
        const u = this.findPerson(id);
        if (!u || u.userId === 0) return fail('There\'s no account with that ID on this server.');
        if (u.id === me.id) return fail('You can\'t report yourself.');
        target = u.id; about = u.id;
      } else if (kind === 'game' || kind === 'item') {
        const a = this.assets.get(id);
        if (!a || (kind === 'game') !== (a.kind === 'game')) return fail('That isn\'t there any more.');
        target = a.id; about = a.creator;
      } else if (kind === 'message') {
        const m = (me.inbox || []).find((x) => x.id === id);
        if (!m) return fail('That message isn\'t in your inbox any more.');
        // Staff see a copy: the sender can't delete it from here.
        target = m.id; about = m.from; copy = { subject: m.subject, body: m.body, at: m.at };
      } else if (kind === 'comment') {
        // id: "game:comment". Staff see a copy, in case it's deleted.
        const [gid, cid] = id.split(':');
        const a = this.assets.get(gid), c = a && (a.comments || []).find((x) => x.id === cid);
        if (!c) return fail('That comment isn\'t there any more.');
        if (c.by === me.id) return fail('You can\'t report yourself.');
        target = id; about = c.by; copy = { subject: 'Comment on ' + a.name, body: c.text, at: c.at };
      } else if (kind === 'forum') {
        // id: "thread:post". Staff see a copy, in case it's deleted.
        const [tid, pid] = id.split(':');
        const th = this.forum.get(tid), p = th && th.posts.find((x) => x.id === pid);
        if (!p) return fail('That post isn\'t there any more.');
        if (p.by === me.id) return fail('You can\'t report yourself.');
        target = id; about = p.by; copy = { subject: 'Forum: ' + th.title, body: p.text, at: p.at };
      } else {
        const g = this.groups.get(id);
        if (!g) return fail('That group isn\'t there any more.');
        target = g.id; about = g.owner;
      }
      if (me.reportDay !== today) { me.reportDay = today; me.reportsToday = 0; }
      const again = this.reports.find((x) => x.status === 'open' && x.from === me.id && x.kind === kind && x.target === target);
      if (again) {   // reporting the same thing twice just updates it
        again.reason = reason; again.note = note || again.note; again.at = now();
      } else {
        if (me.reportsToday >= kReportsPerDay) return fail('That\'s ' + kReportsPerDay + ' reports today. Staff will look at them; try again tomorrow.');
        me.reportsToday = (me.reportsToday || 0) + 1;
        this.reports.push({ id: 'rp-' + randomHex(6), from: me.id, kind, target, about, reason, note, copy, at: now(), status: 'open' });
        this.saveUser(me);
      }
      this.dirty.reports = true;
      return okay();
    }
    return fail('Unknown request.');
  }
  // --- Support tickets (ServerSupport.cpp has the same) ---
  // Anyone can ask staff for help, even without logging in (like "I forgot my password and
  // my account has no email"). A ticket belongs to the device that sent it: only that device
  // sees staff's answers, and a reset code staff make only works there.
  supportOp(name, me, args) {
    const t = now();
    if (name === 'support.send') {
      const topic = TICKET_TOPICS.includes(str(args, 'topic')) ? str(args, 'topic') : 'other';
      const message = cleanText(str(args, 'message'), kTicketLength, true).trim();
      const contact = lower(cleanText(str(args, 'contact'), 254)).trim();
      const username = cleanText(str(args, 'username'), 32).trim().replace(/^@/, '');
      if (message.length < 10) return fail('Tell staff a bit more (at least 10 letters), so they know how to help.');
      if (contact && !isEmail(contact)) return fail('That doesn\'t look like an email address. You can leave it empty.');
      let about = me.userId > 0 ? me : null;
      if (username || (topic === 'password' && !about)) {
        about = this.findUsername(username);
        if (!about || about.userId === 0) return fail('There\'s no account called "' + username + '". Check the spelling.');
        if (this.isOfficial(about)) return fail('The Guts account can\'t get help this way.');
      }
      const mine = this.tickets.filter((x) => x.from === me.id && x.status === 'open');
      if (mine.length >= kOpenTickets) return fail('You already have ' + kOpenTickets + ' open tickets. Wait for staff to answer those first.');
      if (this.tickets.filter((x) => x.from === me.id && t - x.at < 86400).length >= kTicketsPerDay)
        return fail('That\'s enough tickets for today. Try again tomorrow.');
      const tk = { id: 'tk-' + randomHex(6), from: me.id, topic, about: about ? about.id : '', contact, message, at: t, status: 'open', replies: [] };
      this.tickets.push(tk);
      this.dirty.tickets = true;
      this.notify(this.users.get(this.official), 'ticket', 'New support ticket' + (about ? ' about ' + about.name : '') + ': "' + message.slice(0, 80) + '"', tk.id);
      return okay({ ticket: this.ticketJson(tk, false) });
    }
    if (name === 'support.mine') {
      const list = this.tickets.filter((x) => x.from === me.id).slice(-20).reverse();
      return okay({ tickets: list.map((x) => this.ticketJson(x, false)) });
    }
    return fail('Unknown request.');
  }
  // A ticket as JSON. Staff see who sent it and their contact; the sender sees staff's
  // answers and, while it works, the reset code staff made for them.
  ticketJson(x, forStaff) {
    const who = (id) => { const u = id && this.users.get(id); return u ? this.publicUser(u) : null; };
    const r = { id: x.id, topic: x.topic, message: x.message, at: x.at, status: x.status, about: who(x.about),
      replies: (x.replies || []).map((y) => ({ text: y.text, at: y.at, by: forStaff ? who(y.by) : null })) };
    if (x.status === 'closed') r.closedAt = x.closedAt;
    const u = x.about && this.users.get(x.about);
    const c = u && u.codes && u.codes.reset;
    const codeWorks = !!(x.code && c && c.ticket === x.id && now() <= c.exp && c.tries < kCodeTries);
    if (forStaff) {
      const from = this.users.get(x.from);
      r.from = from && from.userId > 0 ? this.publicUser(from) : null;   // null: a guest (not logged in)
      r.contact = x.contact || '';
      r.resetSent = codeWorks;
      r.resetUsed = !!x.resetUsed;
      r.hasEmail = !!(u && u.emailVerified);
      if (x.status === 'closed') r.closedBy = who(x.closedBy);
    } else if (codeWorks) {
      r.resetCode = x.code;
      r.resetUsername = u.username;
      r.resetUntil = c.exp;
    }
    if (!forStaff && x.resetUsed) r.resetUsed = true;
    return r;
  }
  // Open tickets oldest first (first come, first served); closed ones newest first.
  ticketsJson(status) {
    const list = this.tickets.filter((x) => x.status === status);
    return (status === 'open' ? list.slice(0, 100) : list.slice(-100).reverse()).map((x) => this.ticketJson(x, true));
  }
  // Staff answer a ticket: a reply (the sender sees it on the Support page, and gets an
  // email too if they left one), a reset code, or closing it.
  ticketAdminOp(name, me, args) {
    const tk = this.tickets.find((x) => x.id === str(args, 'id'));
    if (!tk) return fail('That ticket isn\'t there any more.');
    const about = tk.about && this.users.get(tk.about);
    const tellSender = (text) => {
      this.notify(this.users.get(tk.from), 'support', text, tk.id);
      if (tk.contact && this.canMail)
        this.outbox.push({ to: tk.contact, subject: 'Guts&Bolts staff answered your ticket',
          text: 'Hi,\n\n' + text + '\n\nOpen the Support page on the Guts&Bolts website (on the same device you sent the ticket from) '
            + 'to see it and answer.\n\n- Guts&Bolts', code: '' });
    };
    if (name === 'admin.replyTicket') {
      const text = cleanText(str(args, 'text'), kTicketLength, true).trim();
      if (!text) return fail('Type an answer first.');
      tk.replies = (tk.replies || []).concat([{ by: me.id, text, at: now() }]);
      if (tk.status === 'closed') { tk.status = 'open'; delete tk.closedBy; delete tk.closedAt; }
      tellSender('Staff answered your support ticket: "' + text + '"');
      this.staffDid(me, 'ticket', 'Answered a support ticket' + (about ? ' about ' + about.name : ''), about ? about.id : '');
    } else if (name === 'admin.ticketReset') {
      // A password reset code for the account the ticket is about. Only press this when you're
      // sure the person really owns it (you know them, they can tell you things only they'd know).
      if (tk.topic !== 'password' || !about) return fail('Only "I forgot my password" tickets can get a reset code.');
      if (this.isOfficial(about)) return fail('The Guts account can\'t be reset this way.');
      if (tk.status === 'closed') return fail('That ticket is closed.');
      const code = sixDigits();
      about.codes = about.codes || {};
      about.codes.reset = { hash: hashHex(code + ':' + about.id + ':reset'), exp: now() + kResetHours * 3600, tries: 0, sent: now(), device: tk.from, ticket: tk.id };
      tk.code = code;
      delete tk.resetUsed;
      this.saveUser(about);
      tellSender('Staff made a password reset code for ' + about.username + '. It works for ' + kResetHours + ' hours.');
      this.staffDid(me, 'ticket', 'Made a password reset code for ' + about.name + ' (support ticket)', about.id);
    } else if (name === 'admin.closeTicket') {
      tk.status = 'closed'; tk.closedBy = me.id; tk.closedAt = now();
      this.staffDid(me, 'ticket', 'Closed a support ticket' + (about ? ' about ' + about.name : ''), about ? about.id : '');
    } else return fail('Unknown request.');
    this.dirty.tickets = true;
    return okay({ tickets: this.ticketsJson('open') });
  }

  // Reports for the Staff page, with names filled in. Open: oldest first (first come, first served).
  reportsJson(status) {
    const who = (id) => { const u = this.users.get(id); return u ? this.publicUser(u) : { id, name: '?', userId: 0 }; };
    const list = this.reports.filter((x) => x.status === status);
    const shown = status === 'open' ? list.slice(0, 100) : list.slice(-100).reverse();
    return shown.map((x) => {
      const r = { id: x.id, kind: x.kind, target: x.target, reason: x.reason, note: x.note || '', at: x.at, status: x.status,
        from: who(x.from), about: x.about ? who(x.about) : null, copy: x.copy || null,
        reports: list.filter((y) => y.kind === x.kind && y.target === x.target).length };
      if (x.kind === 'game' || x.kind === 'item') { const a = this.assets.get(x.target); r.name = a ? a.name : '(deleted)'; r.assetKind = a ? a.kind : ''; }
      if (x.kind === 'group') { const g = this.groups.get(x.target); r.name = g ? g.name : '(deleted)'; }
      if (x.kind === 'comment') { const a = this.assets.get(x.target.split(':')[0]); r.name = a ? a.name : '(deleted)'; r.game = a ? a.id : ''; }
      if (x.kind === 'forum') { const th = this.forum.get(x.target.split(':')[0]); r.name = th ? th.title : '(deleted)'; r.thread = th ? th.id : ''; }
      if (x.status === 'closed') { r.outcome = x.outcome; r.closedBy = who(x.closedBy); r.closedAt = x.closedAt; }
      return r;
    });
  }

  // --- The forum: boards of threads, each a list of posts (oldest first). ---
  forumOp(name, me, args) {
    const t = now();
    const staff = this.isStaff(me);
    const who = (id) => { const u = this.users.get(id); return u ? { id: u.id, userId: u.userId, name: u.name, verified: this.isVerified(u), staff: this.isStaff(u) } : { id, userId: 0, name: '?' }; };
    const hidden = (id) => { const u = this.users.get(id); return !!u && this.blocks(me, u); };
    const boardOf = (id) => FORUM_BOARDS.find((b) => b.id === id);
    const threadsIn = (board) => [...this.forum.values()].filter((th) => th.board === board)
      .sort((x, y) => (y.pinned ? 1 : 0) - (x.pinned ? 1 : 0) || y.last - x.last);
    const summary = (th) => ({ id: th.id, title: th.title, by: who(th.by), at: th.at, last: th.last, lastBy: who(th.lastBy),
      replies: th.posts.length - 1, views: th.views || 0, pinned: !!th.pinned, locked: !!th.locked });
    // One page of a thread (page -1 = the last page).
    const threadPage = (th, page) => {
      const pages = Math.max(1, Math.ceil(th.posts.length / kForumPage));
      const p = page < 0 ? pages - 1 : clamp(page, 0, pages - 1);
      const b = boardOf(th.board) || { name: '?' };
      const posts = th.posts.slice(p * kForumPage, (p + 1) * kForumPage).filter((x) => !hidden(x.by))
        .map((x) => ({ id: x.id, text: x.text, at: x.at, edited: x.edited || 0, by: who(x.by), first: x.id === th.posts[0].id,
          canDelete: me.userId !== 0 && (x.by === me.id || staff) }));
      return okay({ thread: Object.assign(summary(th), { board: th.board, boardName: b.name }), posts, page: p, pages,
        canReply: me.userId !== 0 && (!th.locked || staff), canModerate: staff });
    };
    if (name === 'forum.boards') {
      return okay({ boards: FORUM_BOARDS.map((b) => {
        const list = threadsIn(b.id);
        const newest = list.slice().sort((x, y) => y.last - x.last)[0];
        return { id: b.id, name: b.name, about: b.about, staffOnly: !!b.staffOnly, threads: list.length,
          posts: list.reduce((n, th) => n + th.posts.length, 0),
          last: newest ? { thread: newest.id, title: newest.title, by: who(newest.lastBy), at: newest.last } : null };
      }) });
    }
    if (name === 'forum.list') {
      const b = boardOf(str(args, 'board'));
      if (!b) return fail('That board doesn\'t exist.');
      const list = threadsIn(b.id).filter((th) => !hidden(th.by));
      const pages = Math.max(1, Math.ceil(list.length / kForumPage)), page = clamp(num(args, 'page'), 0, pages - 1);
      return okay({ board: { id: b.id, name: b.name, about: b.about, staffOnly: !!b.staffOnly },
        threads: list.slice(page * kForumPage, (page + 1) * kForumPage).map(summary), page, pages,
        canPost: me.userId !== 0 && (!b.staffOnly || staff) });
    }
    if (name === 'forum.post') {
      if (me.userId === 0) return fail('Sign up to post on the forum.');
      const b = boardOf(str(args, 'board'));
      if (!b) return fail('That board doesn\'t exist.');
      if (b.staffOnly && !staff) return fail('Only Guts&Bolts staff post in ' + b.name + '.');
      const title = say(str(args, 'title'), kForumTitle), text = say(str(args, 'text'), kForumText, true);
      if (!title) return fail('Give your thread a title.');
      if (!text) return fail('Write something first.');
      const key = 'ft:' + me.id;
      if (t - (this.lastPost.get(key) || 0) < kThreadCooldown) return fail('Slow down a little - wait a minute between new threads.');
      this.lastPost.set(key, t);
      const th = { id: 'f' + randomHex(6), board: b.id, title, by: me.id, at: t, last: t, lastBy: me.id, views: 0,
        pinned: false, locked: false, posts: [{ id: randomHex(6), by: me.id, text, at: t }] };
      this.forum.set(th.id, th);
      this.dirty.forum.add(th.id);
      // A full board: the threads quiet the longest go (pinned ones stay).
      const all = threadsIn(b.id).filter((x) => !x.pinned);
      for (const old of all.slice(kMaxBoardThreads)) { this.forum.delete(old.id); this.dirty.forum.add(old.id); }
      return threadPage(th, 0);
    }
    const th = this.forum.get(str(args, 'thread'));
    if (!th) return fail('That thread isn\'t there any more.');
    if (name === 'forum.thread') {
      if (me.userId !== 0 && me.id !== th.by) { th.views = (th.views || 0) + 1; this.dirty.forum.add(th.id); }
      return threadPage(th, num(args, 'page'));
    }
    if (name === 'forum.reply') {
      if (me.userId === 0) return fail('Sign up to post on the forum.');
      if (th.locked && !staff) return fail('This thread is locked, so nobody can reply.');
      if (th.posts.length >= kMaxForumPosts) return fail('This thread is full. Start a new one!');
      const starter = this.users.get(th.by);
      if (starter && this.blocks(me, starter)) return fail('You can\'t reply to this thread.');
      const text = say(str(args, 'text'), kForumText, true);
      if (!text) return fail('Write something first.');
      const key = 'fr:' + me.id;
      if (t - (this.lastPost.get(key) || 0) < kReplyCooldown) return fail('Slow down a little - wait a few seconds between posts.');
      this.lastPost.set(key, t);
      th.posts.push({ id: randomHex(6), by: me.id, text, at: t });
      th.last = t; th.lastBy = me.id;
      this.dirty.forum.add(th.id);
      if (starter && starter.id !== me.id) this.notify(starter, 'forum', me.name + ' replied to "' + th.title.slice(0, 60) + '"', th.id);
      return threadPage(th, -1);
    }
    if (name === 'forum.delete') {
      const i = th.posts.findIndex((x) => x.id === str(args, 'post'));
      if (i < 0) return fail('That post is already gone.');
      const p = th.posts[i];
      if (p.by !== me.id && !staff) return fail('You can only delete your own posts.');
      if (p.by !== me.id) this.staffDid(me, 'forum', 'Deleted a forum post in "' + th.title + '": "' + p.text.slice(0, 80) + '"', p.by);
      if (i === 0) {   // the first post: the whole thread goes
        this.forum.delete(th.id);
        this.dirty.forum.add(th.id);
        return okay({ gone: true, board: th.board });
      }
      th.posts.splice(i, 1);
      const lastPost = th.posts[th.posts.length - 1];
      th.last = lastPost.at; th.lastBy = lastPost.by;
      this.dirty.forum.add(th.id);
      return threadPage(th, num(args, 'page'));
    }
    if (name === 'forum.mod') {   // staff: pin it to the top, or lock it (no more replies)
      if (!staff) return fail('Only staff can do that.');
      if ('pinned' in args) th.pinned = !!args.pinned;
      if ('locked' in args) th.locked = !!args.locked;
      this.staffDid(me, 'forum', (th.pinned ? 'Pinned' : 'Unpinned') + ' and ' + (th.locked ? 'locked' : 'unlocked') + ' the thread "' + th.title + '"', th.by);
      this.dirty.forum.add(th.id);
      return threadPage(th, num(args, 'page'));
    }
    return fail('Unknown request.');
  }

  groupOp(name, me, args) {
    const t = now();
    const userJson = (id) => { const u = this.users.get(id); return { id, name: u ? u.name : '?', verified: !!u && this.isVerified(u) }; };
    if (name === 'groups.list') {
      const q = lower(cleanText(str(args, 'query'), 64));
      const found = [...this.groups.values()].filter((g) => !q || lower(g.name).includes(q));
      found.sort((a, b) => {
        const ma = Object.keys(a.members).length, mb = Object.keys(b.members).length;
        return ma !== mb ? mb - ma : b.created - a.created;
      });
      return okay({ groups: found.slice(0, 60).map((g) => this.publicGroup(g)) });
    }
    if (name === 'groups.mine') {
      return okay({ groups: this.groupsOf(me.id).map((g) => {
        const r = this.rankIn(g, me.id);
        return Object.assign(this.publicGroup(g), { role: r.name, perms: r.id === 'Owner' ? GROUP_PERMS.slice() : r.perms });
      }) });
    }
    if (name === 'groups.create') {
      const title = say(str(args, 'name'), 40);
      if (title.length < 3) return fail('Group names need at least 3 letters.');
      if (nameIsReserved(title) && !this.isOfficial(me)) return fail('That name belongs to Guts&Bolts.');
      for (const g of this.groups.values()) if (lower(g.name) === lower(title)) return fail('There\'s already a group called that.');
      const owned = [...this.groups.values()].filter((g) => g.owner === me.id).length;
      if (owned >= kMaxOwned) return fail('You already own ' + kMaxOwned + ' groups.');
      if (this.groupsOf(me.id).length >= kMaxJoined) return fail('You\'re in too many groups. Leave one first.');
      const fee = this.isVerified(me) ? 0 : kGroupFee;
      if (this.balance(me) < fee) return fail('Making a group costs ' + fee + ' Bolts (free for Verified people).');
      const g = { id: 'g-' + randomHex(5), name: title, description: say(str(args, 'description'), 1000, true), owner: me.id,
        created: t, color: clamp(typeof args.color === 'number' ? Math.trunc(args.color) : 0x3a7bd5, 0, 0xffffff),
        open: args.open === undefined ? true : !!args.open, members: { [me.id]: 'Owner' }, requests: [],
        shout: { by: '', text: '', time: 0 }, wall: [] };
      this.groups.set(g.id, g);
      if (fee > 0) this.add(me, -fee, 'Made the group ' + title, 'group:' + g.id);
      this.saveGroup(g);
      return okay({ group: this.publicGroup(g), me: this.meJson(me) });
    }
    const g = this.groups.get(str(args, 'id'));
    if (!g) return fail('That group doesn\'t exist (any more).');
    const staff = this.isStaff(me);
    const myRank = this.rankIn(g, me.id);
    const myLevel = myRank ? myRank.level : 0;
    const can = (perm) => this.groupCan(g, me, perm);
    const isOwner = !!myRank && myRank.id === 'Owner';
    const dropReq = (id) => { const i = g.requests.indexOf(id); if (i >= 0) { g.requests.splice(i, 1); return true; } return false; };
    const rankJson = (r) => ({ id: r.id, name: r.name, level: r.level, perms: r.perms });

    if (name === 'groups.get') {
      const members = Object.keys(g.members).map((id) => [id, this.rankIn(g, id)]).sort((x, y) => y[1].level - x[1].level).slice(0, 500)
        .map(([id, r]) => Object.assign(userJson(id), { role: r.name, rank: r.id, level: r.level }));
      const shoutInfo = g.shout && g.shout.text ? Object.assign(userJson(g.shout.by), { text: g.shout.text, time: g.shout.time }) : {};
      const wall = g.wall.slice(-60).map((p) => Object.assign(userJson(p.by), { text: p.text, time: p.time }));
      const games = [...this.assets.values()].filter((x) => x.kind === 'game' && x.group === g.id && this.canPlay(x, me))
        .sort((x, y) => (y.plays || 0) - (x.plays || 0)).slice(0, 50).map((x) => this.publicAsset(x, me));
      const r = okay({ group: this.publicGroup(g), myRole: myRank ? myRank.name : '', myRank: myRank ? rankJson(myRank) : null,
        ranks: this.ranksOf(g).slice().sort((x, y) => y.level - x.level).map(rankJson), requested: g.requests.includes(me.id),
        memberList: members, shoutInfo, wall, games });
      if (can('manage')) r.requests = g.requests.map(userJson);
      if (can('funds') || staff) {
        r.funds = this.groupFunds(g);
        r.ledger = (g.ledger || []).slice(-50).reverse().map((e) => ({ amount: e[0], reason: e[1], at: e[2] }));
      }
      return r;
    }
    if (name === 'groups.join') {
      if (myRank) return okay();
      if (this.groupsOf(me.id).length >= kMaxJoined) return fail('You\'re in too many groups. Leave one first.');
      if (!g.open) {
        if (!g.requests.includes(me.id)) {
          g.requests.push(me.id);
          this.notify(this.users.get(g.owner), 'group', me.name + ' wants to join ' + g.name + '.', g.id);
        }
        this.saveGroup(g);
        return okay({ requested: true });
      }
      g.members[me.id] = 'Member';
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.leave') {
      if (isOwner) return fail('Owners can\'t leave. Give the group to someone else first, or delete it.');
      delete g.members[me.id];
      dropReq(me.id);
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.post') {
      if (!myRank) return fail('Join the group to post on its wall.');
      const text = say(str(args, 'text'), 300, true);
      if (!text) return fail('Write something first.');
      if (t - (this.lastPost.get(me.id) || 0) < kPostCooldown) return fail('Slow down a little - wait a few seconds between posts.');
      this.lastPost.set(me.id, t);
      g.wall.push({ by: me.id, text, time: t });
      if (g.wall.length > kWallSize) g.wall.shift();
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.deletePost') {
      const time = num(args, 'time'), by = str(args, 'by');
      const i = g.wall.findIndex((p) => p.time === time && p.by === by);
      if (i < 0) return fail('That post is already gone.');
      if (g.wall[i].by !== me.id && !can('manage') && !staff) return fail('You can only delete your own posts.');
      g.wall.splice(i, 1);
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.shout') {
      if (!can('shout')) return fail('Your rank can\'t shout in this group.');
      g.shout = { by: me.id, text: say(str(args, 'text'), 200), time: t };
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.edit') {
      if (!can('manage')) return fail('Your rank can\'t change this group.');
      if ('description' in args) g.description = say(str(args, 'description'), 1000, true);
      if ('color' in args) g.color = clamp(num(args, 'color'), 0, 0xffffff);
      if ('lyreto' in args && !this.setLyreto('group', g, str(args, 'lyreto'))) return fail('That isn\'t a Lyreto link. Copy your server\'s invite link in Lyreto: it starts with https://lyreto.com/');
      if ('open' in args) {
        g.open = !!args.open;
        if (g.open) { for (const id of g.requests) if (!g.members[id]) g.members[id] = 'Member'; g.requests = []; }
      }
      this.saveGroup(g);
      return okay({ group: this.publicGroup(g) });
    }
    if (name === 'groups.request') {
      if (!can('manage')) return fail('Your rank can\'t let people in.');
      const who = lower(str(args, 'user'));
      if (!dropReq(who)) return fail('They\'re not waiting any more.');
      if (args.accept) {
        g.members[who] = 'Member';
        this.notify(this.users.get(who), 'group', 'You\'re in ' + g.name + ' now!', g.id);
      }
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.member') {
      const who = lower(str(args, 'user'));
      let action = str(args, 'action');
      const theirRank = this.rankIn(g, who);
      if (!theirRank) return fail('They\'re not in this group.');
      if (who === me.id) return fail('You can\'t do that to yourself.');
      if (theirRank.id === 'Owner' && action !== 'owner') return fail('Nobody can change the owner.');
      let rankId = str(args, 'rank');
      if (action === 'admin' || action === 'member') { rankId = action === 'admin' ? 'Admin' : 'Member'; action = 'rank'; }   // (older apps)
      if (action === 'kick') {
        if (!((can('manage') && theirRank.level < myLevel) || staff)) return fail('You can\'t remove them.');
        delete g.members[who];
      } else if (action === 'rank') {
        const r = this.ranksOf(g).find((x) => x.id === rankId);
        if (!r || r.id === 'Owner') return fail('Pick a rank.');
        if (!can('ranks') || theirRank.level >= myLevel || (r.level >= myLevel && !isOwner)) return fail('You can only change the rank of people below you, to a rank below yours.');
        g.members[who] = r.id;
      } else if (action === 'owner') {
        if (!isOwner) return fail('Only the owner can give the group away.');
        const next = this.ranksOf(g).filter((x) => x.id !== 'Owner').sort((x, y) => y.level - x.level)[0];
        g.members[who] = 'Owner'; g.members[me.id] = next ? next.id : 'Member'; g.owner = who;
      } else {
        return fail('Unknown member action.');
      }
      this.saveGroup(g);
      return okay();
    }
    // The owner makes and changes ranks: { rank: {id (empty = new), name, level, perms} }.
    if (name === 'groups.rank') {
      if (!isOwner) return fail('Only the owner can change the ranks.');
      const want = args.rank && typeof args.rank === 'object' ? args.rank : {};
      const ranks = this.ranksOf(g).map((r) => ({ ...r, perms: r.perms.slice() }));
      const title = say(typeof want.name === 'string' ? want.name : '', 24);
      if (!title) return fail('Give the rank a name.');
      const perms = (Array.isArray(want.perms) ? want.perms : []).filter((x) => GROUP_PERMS.includes(x));
      let r = ranks.find((x) => x.id === want.id);
      if (!r) {
        if (ranks.length >= kMaxRanks) return fail('A group can have up to ' + kMaxRanks + ' ranks.');
        r = { id: 'r-' + randomHex(4) };
        ranks.push(r);
      }
      if (ranks.some((x) => x !== r && lower(x.name) === lower(title))) return fail('There\'s already a rank called that.');
      r.name = title;
      if (r.id === 'Owner') { r.level = 255; r.perms = GROUP_PERMS.slice(); }
      else {
        r.perms = [...new Set(perms)];
        r.level = r.id === 'Member' ? 1 : Number.isInteger(want.level) ? want.level : 0;
        if (r.id !== 'Member' && (r.level < 2 || r.level > 254)) return fail('Rank levels go from 2 to 254 (higher is more in charge).');
        if (ranks.some((x) => x !== r && x.level === r.level)) return fail('Another rank already has level ' + r.level + '.');
      }
      g.ranks = ranks;
      this.saveGroup(g);
      return okay();
    }
    if (name === 'groups.rankDelete') {
      if (!isOwner) return fail('Only the owner can change the ranks.');
      const id = str(args, 'rank');
      if (id === 'Owner' || id === 'Member') return fail('The Owner and Member ranks can\'t be deleted.');
      const ranks = this.ranksOf(g);
      if (!ranks.some((x) => x.id === id)) return fail('That rank is already gone.');
      g.ranks = ranks.filter((x) => x.id !== id);
      for (const [uid, rid] of Object.entries(g.members)) if (rid === id) g.members[uid] = 'Member';
      this.saveGroup(g);
      return okay();
    }
    // Pay a member from the group's Bolts.
    if (name === 'groups.payout') {
      if (!can('funds')) return fail('Your rank can\'t spend the group\'s Bolts.');
      const who = this.users.get(lower(str(args, 'user')));
      if (!who || !g.members[who.id]) return fail('You can only pay people in the group.');
      const amount = num(args, 'amount');
      if (!Number.isInteger(amount) || amount < 1) return fail('Pay at least 1 Bolt.');
      if (amount > this.groupFunds(g)) return fail('The group only has ' + this.groupFunds(g) + ' Bolts.');
      const ref = 'payout:' + g.id + ':' + randomHex(4);
      this.groupAdd(g, -amount, 'Paid ' + who.name + ' (by ' + me.name + ')', ref);
      this.add(who, amount, 'Payout from ' + g.name, ref);
      this.notify(who, 'group', g.name + ' paid you ' + amount + ' Bolts!', g.id);
      return okay({ funds: this.groupFunds(g) });
    }
    if (name === 'groups.removeGame') {   // the owner (or whoever adds games) takes a game out of the group
      const a = this.assets.get(str(args, 'game'));
      if (!a || a.group !== g.id) return fail('That game isn\'t in this group.');
      if (!isOwner && !(can('games') && a.creator === me.id) && !staff) return fail('Only the owner can take other people\'s games out.');
      a.group = '';
      this.saveAsset(a);
      return okay();
    }
    if (name === 'groups.delete') {
      if (!isOwner && !staff) return fail('Only the owner can delete the group.');
      for (const a of this.assets.values()) if (a.group === g.id) { a.group = ''; this.saveAsset(a); }
      this.groups.delete(g.id);
      this.dirty.groups.add(g.id);
      return okay();
    }
    return fail('The server doesn\'t know how to do "' + name + '". It might need updating.');
  }

  // --- the relay (ServerRelay.cpp) ---
  // People in a server: its players, plus the host when the host is a player (not a game server machine).
  headcount(s) { return s.players.size + (s.dedicated ? 0 : 1); }
  sessionJson(s) {
    const h = this.users.get(s.host);
    // Who's in it (the first few, with their avatars), for the Roblox-style server cards.
    const ids = s.dedicated ? [] : [s.host];
    for (const p of s.players) { const c = this.conns.get(p); if (c && c.account) ids.push(c.account); }
    const people = ids.slice(0, 5).map((id) => {
      const u = this.users.get(id);
      return { id, name: u ? u.name : '?', avatar: (u && u.avatar) || null };
    });
    return { id: s.id, game: s.game, title: s.title, players: this.headcount(s), max: s.max, private: s.priv,
      hostName: s.dedicated ? 'Guts&Bolts' : h ? h.name : '?', hostVerified: s.dedicated || (!!h && this.isVerified(h)),
      dedicated: !!s.dedicated, people };
  }
  sessionOf(userId) {
    for (const s of this.sessions.values()) {
      if (s.host === userId && !s.dedicated) return s;
      for (const p of s.players) { const c = this.conns.get(p); if (c && c.account === userId) return s; }
    }
    return null;
  }
  // The update log (the website's Updates page): built-in ones (updates.js) and ones staff posted.
  updateOp(name, me, args) {
    const t = now();
    if (name === 'updates.list') {
      // Newest first. A date in the future (a typo) counts as now, so it can't sit on top for ever.
      const when = (u) => Math.min(u.time, t);
      const all = [...this.posted, ...BUILT_IN_UPDATES].map((u) => ({ ...u, time: when(u) })).sort((a, b) => b.time - a.time);
      const limit = clamp(Number.isInteger(args.limit) ? args.limit : 50, 1, 100);
      return okay({ updates: all.slice(0, limit), latest: all.length ? all[0].id : '' });
    }
    if (name === 'updates.post') {
      if (!this.isStaff(me)) return fail('Only staff can post updates.');
      const title = cleanText(str(args, 'name'), 40);
      if (!title) return fail('Give the update a name.');
      const summary = cleanText(str(args, 'summary'), 200);
      const items = (Array.isArray(args.items) ? args.items : []).filter((x) => typeof x === 'string')
        .map((x) => cleanText(x, 160)).filter(Boolean).slice(0, 20);
      if (!summary && !items.length) return fail('Say what changed.');
      const tag = UPDATE_TAGS.includes(str(args, 'tag')) ? str(args, 'tag') : 'Engine';
      const u = { id: 'u-' + randomHex(6), name: title, version: cleanText(str(args, 'version'), 12), time: t, tag,
        summary, items, by: me.username || me.name };
      this.posted.unshift(u);
      this.posted = this.posted.slice(0, 300);
      this.dirty.updates = true;
      return okay({ update: u });
    }
    if (name === 'updates.delete') {
      if (!this.isStaff(me)) return fail('Only staff can delete updates.');
      const id = str(args, 'id');
      if (!this.posted.some((u) => u.id === id)) return fail('That update is built in (or already gone).');
      this.posted = this.posted.filter((u) => u.id !== id);
      this.dirty.updates = true;
      return okay({});
    }
    return fail('The server doesn\'t know how to do "' + name + '". It might need updating.');
  }

  serverOp(name, me, args) {
    const game = this.assets.resolve(cleanText(typeof args.game === 'string' ? args.game : '', 80));   // (its number works too)
    const asset = this.assets.get(game);
    const t = now();
    if (asset && !this.canPlay(asset, me)) return fail(this.noPlay(asset));
    if (name === 'servers.play') {
      let best = null;
      for (const s of this.sessions.values()) {
        if (s.priv || s.game !== game || (s.host === me.id && !s.dedicated) || this.headcount(s) + 1 > s.max) continue;
        if (!best || s.players.size > best.players.size) best = s;
      }
      if (best) return okay({ join: best.id });
      // Nobody's playing: a game server machine starts it if one is free, so the game
      // keeps going whoever leaves. (Players ask again shortly: "wait".) Otherwise you host it.
      if (asset && asset.kind === 'game' && args.dedicated !== false && this.startOnPool(asset)) return okay({ wait: 2 });
      return okay({ host: true });
    }
    if (name === 'servers.list') {
      const list = [];
      for (const s of this.sessions.values()) {
        if (game && s.game !== game) continue;
        const friendHost = me.friends.includes(s.host);
        if (s.priv && !friendHost && s.host !== me.id) continue;
        let friends = friendHost ? 1 : 0;
        for (const p of s.players) { const c = this.conns.get(p); if (c && me.friends.includes(c.account)) friends++; }
        list.push(Object.assign(this.sessionJson(s), { friends }));
        if (list.length >= 100) break;
      }
      const r = okay({ servers: list });
      if (asset && asset.kind === 'game') {
        r.privatePrice = asset.privatePrice || 0;
        r.privateUntil = this.privateUntil(me, game);
        r.privateNeedsBuy = !!this.privateBlocked(me, game);
      }
      return r;
    }
    // Buy (or add 30 days to) your own private server of a game.
    if (name === 'servers.buyPrivate') {
      if (me.userId === 0) return fail('Sign up to buy things.');
      if (!asset || asset.kind !== 'game') return fail('That game doesn\'t exist (any more).');
      const price = asset.privatePrice || 0;
      if (price <= 0) return fail('Private servers of this game are free.');
      if (asset.creator === me.id) return fail('Your own game\'s private servers are free for you.');
      if (this.balance(me) < price) return fail('You need ' + (price - this.balance(me)) + ' more Bolts for that.');
      const ref = 'private:' + game + ':' + randomHex(4);
      this.add(me, -price, 'Private server: ' + asset.name, ref);
      this.paySeller(asset, me, Math.floor(price * kCreatorSharePercent / 100), ref);   // to the group, or the creator
      me.privateServers = me.privateServers || {};
      me.privateServers[game] = Math.max(t, this.privateUntil(me, game)) + kPrivateDays * 86400;
      this.saveUser(me);
      return okay({ until: me.privateServers[game], me: this.meJson(me) });
    }
    return fail('Unknown request.');
  }

  // DataStores: what games save between visits (coins, levels...), kept here so every server
  // of a game sees the same data. Only servers we trust can read and write it: game server
  // machines (staff), staff, and the game's creator. Games other people host save on their
  // own computer instead (ScriptEngine falls back), so nobody can hand out free coins.
  dataOp(name, me, args) {
    const game = this.assets.resolve(cleanText(typeof args.game === 'string' ? args.game : '', 80));
    const asset = this.assets.get(game);
    if (!asset || asset.kind !== 'game') return fail('That game isn\'t there.');
    const trusted = this.isStaff(me) || asset.creator === me.id;
    if (name === 'data.can') return okay({ save: trusted });
    if (!trusted) return fail('Only the game\'s own servers can use its saved data.');
    const store = typeof args.store === 'string' ? args.store : '', key = typeof args.key === 'string' ? args.key : '';
    if (!store || !key || store.length > kDataName || key.length > kDataName) return fail('DataStore names and keys are 1-' + kDataName + ' letters.');
    const read = () => {
      const row = [...this.sql.exec('SELECT value FROM gamedata WHERE game = ? AND store = ? AND key = ?', game, store, key)][0];
      return row ? JSON.parse(row.value) : null;
    };
    const write = (value) => {
      if (value === null || value === undefined) {
        this.sql.exec('DELETE FROM gamedata WHERE game = ? AND store = ? AND key = ?', game, store, key);
        return null;
      }
      const text = JSON.stringify(value);
      if (text.length > kDataValue) return fail('That\'s too much to save in one key (256 KB at most).');
      const isNew = read() === null;
      if (isNew) {
        const n = [...this.sql.exec('SELECT COUNT(*) AS n FROM gamedata WHERE game = ?', game)][0].n;
        if (n >= kDataKeys) return fail('This game has saved ' + kDataKeys + ' keys already. Remove some first.');
      }
      this.sql.exec('INSERT OR REPLACE INTO gamedata (game, store, key, value, updated) VALUES (?, ?, ?, ?, ?)', game, store, key, text, now());
      return null;
    };
    if (name === 'data.get') return okay({ value: read() });
    if (name === 'data.set') {
      const bad = write(args.value === undefined ? null : args.value);
      return bad || okay({});
    }
    if (name === 'data.increment') {   // in one step, so two servers adding at once both count
      const old = read();
      if (old !== null && typeof old !== 'number') return fail('IncrementAsync: that key doesn\'t hold a number.');
      const delta = typeof args.delta === 'number' && Number.isFinite(args.delta) ? args.delta : 1;
      const value = (old || 0) + delta;
      const bad = write(value);
      return bad || okay({ value });
    }
    return fail('Unknown request.');
  }

  // Ask a game server machine with room to start `asset` (true if one is, or already was asked).
  startOnPool(asset) {
    const t = now();
    let pick = null, pickFree = 0;
    for (const [cid, pool] of this.pools) {
      for (const [g, when] of pool.starting) if (t - when > kPoolStartWait) pool.starting.delete(g);
      if (pool.starting.has(asset.id)) return true;   // on its way
      let running = 0;
      for (const s of this.sessions.values()) if (s.dedicated && s.host === pool.account) running++;
      const free = pool.slots - running - pool.starting.size;
      if (free > pickFree && this.conns.has(cid)) { pick = cid; pickFree = free; }
    }
    if (!pick) return false;
    const pool = this.pools.get(pick);
    pool.starting.set(asset.id, t);
    this.sendTo(this.conns.get(pick), { t: 'start', game: asset.id, title: asset.name,
      max: clamp(asset.maxPlayers || kDefaultMax, 2, kMostPlayers) });
    return true;
  }

  sendTo(c, message) {
    try { c.ws.send(typeof message === 'string' ? message : JSON.stringify(message)); } catch { /* already gone */ }
  }

  relayRequest(c, req) {
    const reply = (r) => { r.t = 'relay'; this.sendTo(c, r); };
    const close = () => { c.closing = true; this.drop(c.id); };
    let checked;
    try { checked = this.checkRequest(req); } finally { this.flush(); }
    if (checked.bad) { reply(checked.bad); close(); return; }
    const { me, args, opName: op } = checked;
    const t = now();
    if (op === 'relay.pool') {   // a game server machine says it's ready to run games (staff only)
      if (!this.isStaff(me)) { reply(fail('Only Guts&Bolts staff can run game servers.')); close(); return; }
      c.mode = 'pool'; c.account = me.id;
      this.pools.set(c.id, { account: me.id, slots: clamp(Number.isInteger(args.slots) ? args.slots : 4, 1, kPoolMost), starting: new Map() });
      reply(okay({ pool: true }));
      this.scheduleSweep();
      return;
    }
    if (op === 'relay.host') {
      // A game server machine starting a game it was asked to: nobody plays on it, so the game
      // keeps going whoever leaves.
      const dedicated = !!args.dedicated && this.isStaff(me);
      let hosting = 0;
      for (const s of this.sessions.values()) if (s.host === me.id && !s.dedicated) hosting++;
      if (!dedicated && hosting >= kHostedEach) { reply(fail('You\'re already running ' + kHostedEach + ' servers.')); close(); return; }
      const s = { id: 's-' + randomHex(6), game: this.assets.resolve(cleanText(typeof args.game === 'string' ? args.game : '', 80)),
        title: say(typeof args.title === 'string' ? args.title : '', 60) || 'A game', host: me.id, code: '',
        priv: !!args.private, created: t,
        max: clamp((this.assets.get(args.game) || {}).maxPlayers || (Number.isInteger(args.max) ? args.max : kDefaultMax), 2, kMostPlayers),
        control: c.id, players: new Set(), dedicated };
      if (dedicated) {
        s.priv = false;
        for (const pool of this.pools.values()) if (pool.account === me.id) pool.starting.delete(s.game);
      }
      // Taking over a server whose host left: same game, name, privacy and code, so everyone can follow.
      const old = typeof args.continues === 'string' ? this.moved.get(args.continues) : null;
      if (old) {
        if (old.newId || !old.members.includes(me.id)) { reply(fail('Someone else is already hosting the new server.')); close(); return; }
        Object.assign(s, { game: old.game, title: old.title, priv: old.priv, code: old.code, max: old.max });
        old.newId = s.id;
      }
      if (s.priv && !old && !dedicated) {
        const blocked = this.privateBlocked(me, s.game);
        if (blocked) { reply(fail(blocked)); close(); return; }
      }
      if (s.priv && !s.code) {
        do s.code = makeCode(); while ([...this.sessions.values()].some((x) => x.code === s.code));
      }
      c.mode = 'host'; c.account = me.id; c.session = s.id;
      this.sessions.set(s.id, s);
      reply(okay({ session: s.id, code: s.code, private: s.priv }));
      return;
    }
    if (op === 'relay.join') {
      let s = null;
      const code = upper(cleanText(typeof args.code === 'string' ? args.code : '', 12));
      if (code) {
        for (const x of this.sessions.values()) if (x.code === code) s = x;
        if (!s) { reply(fail('There\'s no private server with that code (it may have closed).')); close(); return; }
      } else {
        s = this.sessions.get(typeof args.session === 'string' ? args.session : '') || null;
      }
      // Its host left: follow everyone to the new server (or be asked to host it).
      const old = !s && !code ? this.moved.get(typeof args.session === 'string' ? args.session : '') : null;
      let following = false;
      if (old && old.members.includes(me.id)) {
        s = old.newId ? this.sessions.get(old.newId) || null : null;
        if (!s) {
          if (old.newId) { reply(fail('The new server closed too.')); close(); return; }
          if (t - old.heirSince > kHeirWait) { old.heir++; old.heirSince = t; }   // they didn't start it: next in line
          if (old.heir >= old.members.length) { reply(fail('That server has closed.')); close(); return; }
          reply({ ok: false, hostLeft: true, you: old.members[old.heir] === me.id, error: 'The host left. Moving to a new server...' });
          close();
          return;
        }
        following = true;
      }
      if (!s) { reply(fail('That server has closed.')); close(); return; }
      if (s.host === me.id && !s.dedicated) { reply(fail('That\'s your own server.')); close(); return; }
      if (s.priv && !code && !following && !me.friends.includes(s.host)) {
        reply(fail('That\'s a private server. You need to be the host\'s friend, or have its code.'));
        close();
        return;
      }
      let waiting = 0;
      for (const o of this.conns.values()) if (o.mode === 'pending' && o.session === s.id) waiting++;
      if (this.headcount(s) + 1 + waiting > s.max) { reply(fail('That server is full.')); close(); return; }
      c.mode = 'pending'; c.account = me.id; c.session = s.id; c.ticket = randomHex(16); c.since = t;
      const host = this.conns.get(s.control);
      if (host) this.sendTo(host, { t: 'incoming', ticket: c.ticket, account: me.id, name: me.name, guest: me.userId === 0 });
      this.scheduleSweep();
      return;
    }
    reply(fail('Unknown request.'));
    close();
  }

  relayAccept(c, ticket) {
    let joiner = null;
    if (typeof ticket === 'string' && ticket.length >= 16)
      for (const o of this.conns.values()) if (o.mode === 'pending' && o.ticket === ticket) joiner = o;
    const s = joiner && this.sessions.get(joiner.session);
    if (!joiner || !s) { this.drop(c.id); return; }
    joiner.mode = c.mode = 'pipe';
    joiner.peer = c.id; joiner.ticket = '';
    c.peer = joiner.id; c.session = s.id; c.account = s.host;
    s.players.add(joiner.id);
    this.sendTo(joiner, { t: 'relay', ok: true, title: s.title, session: s.id, game: s.game });
  }

  // A connection went away (or is being closed): take its pipe partner with it,
  // and a host's control line takes its whole server (GbServer::dropClients).
  drop(id) {
    const dead = new Set([id]);
    for (let grew = true; grew;) {
      grew = false;
      for (const d of [...dead]) {
        const c = this.conns.get(d);
        if (c && c.peer && !dead.has(c.peer)) { dead.add(c.peer); grew = true; }
      }
      for (const [sid, s] of this.sessions) {
        if (!dead.has(s.control)) continue;
        // The players stay together: remember them (longest-playing first) so one can host a new server.
        const members = [];
        // (Their pipes die with the host, but they're still here.)
        for (const p of s.players) { const pc = this.conns.get(p); if (pc && pc.account) members.push(pc.account); }
        if (members.length)
          this.moved.set(sid, { game: s.game, title: s.title, priv: s.priv, code: s.code, max: s.max, members,
            heir: 0, heirSince: now(), newId: '', until: now() + kMoveWait });
        for (const p of s.players) if (!dead.has(p)) { dead.add(p); grew = true; }
        this.sessions.delete(sid);
      }
    }
    for (const s of this.sessions.values()) for (const d of dead) s.players.delete(d);
    for (const d of dead) {
      this.pools.delete(d);
      const c = this.conns.get(d);
      if (!c) continue;
      this.conns.delete(d);
      try { c.ws.close(1000, 'bye'); } catch { /* already closed */ }
    }
  }

  scheduleSweep() {
    if (this.sweepTimer || !this.conns.size) return;
    this.sweepTimer = setTimeout(() => { this.sweepTimer = null; this.sweep(); }, 5000);
  }
  sweep() {
    const t = now();
    for (const [id, m] of this.moved) if (t > m.until) this.moved.delete(id);
    for (const c of [...this.conns.values()]) {
      if (!this.conns.has(c.id)) continue;
      if (c.mode === 'pending') {
        const gone = !this.sessions.has(c.session);
        if (gone || t - c.since > kJoinWait) {
          this.sendTo(c, { t: 'relay', ok: false, error: gone ? 'That server just closed.' : 'The server\'s host didn\'t answer. Try another server.' });
          this.drop(c.id);
        }
        continue;
      }
      const quiet = t - c.lastActive;
      if (((c.mode === 'host' || c.mode === 'pool') && quiet > kHostSilence) || (c.mode === 'pipe' && quiet > kPipeSilence) ||
          (c.mode === 'request' && quiet > 120)) this.drop(c.id);
    }
    this.scheduleSweep();
  }

  onMessage(c, data) {
    c.lastActive = now();
    if (c.mode === 'pipe') {   // game traffic: pass it on untouched
      const peer = this.conns.get(c.peer);
      if (peer) { peer.lastActive = c.lastActive; try { peer.ws.send(data); } catch { this.drop(c.id); } }
      return;
    }
    if (c.mode === 'host' || c.mode === 'pending' || c.mode === 'pool') return;   // pings keep it alive
    const text = typeof data === 'string' ? data : new TextDecoder().decode(new Uint8Array(data));
    let req;
    try { req = JSON.parse(text); } catch { req = null; }
    if (!req || typeof req !== 'object' || Array.isArray(req)) { this.sendTo(c, fail('That wasn\'t a proper request.')); return; }
    if (req.t === 'accept') { this.relayAccept(c, req.ticket); return; }
    if (typeof req.op === 'string' && req.op.startsWith('relay.')) { this.relayRequest(c, req); return; }
    const reply = this.handle(req);
    if ('id' in req) reply.id = req.id;
    this.sendTo(c, reply);
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === '/ws') {
      if (request.headers.get('Upgrade') !== 'websocket') return new Response('Expected a WebSocket.', { status: 426 });
      const pair = new WebSocketPair();
      const ws = pair[1];
      ws.accept();
      const c = { id: randomHex(8), ws, mode: 'request', account: '', session: '', ticket: '', peer: '', since: now(), lastActive: now() };
      this.conns.set(c.id, c);
      // Messages are handled one at a time, in order (some arrive as Blobs, which take a moment to read).
      let queue = Promise.resolve();
      ws.addEventListener('message', (e) => {
        queue = queue.then(async () => {
          if (!this.conns.has(c.id)) return;
          let data = e.data;
          if (typeof data !== 'string' && typeof data.arrayBuffer === 'function' && !(data instanceof ArrayBuffer)) data = await data.arrayBuffer();
          this.onMessage(c, data);
        }).catch(() => this.drop(c.id));
      });
      const gone = () => { if (this.conns.has(c.id)) this.drop(c.id); };
      ws.addEventListener('close', gone);
      ws.addEventListener('error', gone);
      this.scheduleSweep();
      return new Response(null, { status: 101, webSocket: pair[0] });
    }
    // GET /wear/<asset id>: a Studio-made accessory's 3D shape (public: anyone can see it worn),
    // so the website's 3D avatars draw hats, hair and the rest like the game does.
    // Gear too, but only its shape (no scripts), so item pictures are drawn from the item itself.
    if (url.pathname.startsWith('/wear/')) {
      const a = this.assets.get(decodeURIComponent(url.pathname.slice(6)));
      if (a && a.kind === 'gear') {
        const raw = this.readFile(a.id);
        let m = null;
        try { m = raw ? JSON.parse(new TextDecoder().decode(raw)) : null; } catch { m = null; }
        if (!m || !Array.isArray(m.nodes)) return new Response('No gear.', { status: 404 });
        const shape = (n) => (n && typeof n === 'object' && n.kind !== 'Script' && n.kind !== 'LocalScript' && n.kind !== 'ModuleScript' ? {
          kind: n.kind, shape: n.shape, mesh: n.mesh, pos: n.pos, rot: n.rot, size: n.size, color: n.color, material: n.material, texture: n.texture,
          transparency: n.transparency, children: (Array.isArray(n.children) ? n.children : []).map(shape).filter(Boolean),
        } : null);
        return new Response(JSON.stringify({ nodes: m.nodes.map(shape).filter(Boolean) }),
          { headers: { 'content-type': 'application/json', 'cache-control': 'public, max-age=3600' } });
      }
      const data = a && isAccessory(a.kind) && a.meta && a.meta.model ? this.readFile(a.id) : null;
      if (!data) return new Response('No accessory.', { status: 404 });
      return new Response(data, { headers: { 'content-type': 'application/json', 'cache-control': 'public, max-age=3600' } });
    }
    // GET /decal/<asset id or number>: a decal's picture (decals are always free and public), e.g. a hat's texture.
    if (url.pathname.startsWith('/decal/')) {
      const a = this.assets.get(decodeURIComponent(url.pathname.slice(7)).replace(/^gb:/, ''));
      const data = a && a.kind === 'decal' && !a.review ? this.readFile(a.id) : null;   // (not before the staff check)
      if (!data) return new Response('No picture.', { status: 404 });
      const jpg = data.length > 2 && data[0] === 0xff && data[1] === 0xd8;
      return new Response(data, { headers: { 'content-type': jpg ? 'image/jpeg' : 'image/png', 'cache-control': 'public, max-age=86400' } });
    }
    // GET /thumb/<asset id> and /icon/<asset id>: a game's picture and icon (public, so pages can show them directly).
    if (url.pathname.startsWith('/thumb/') || url.pathname.startsWith('/icon/')) {
      const icon = url.pathname.startsWith('/icon/');
      const id = decodeURIComponent(url.pathname.slice(icon ? 6 : 7));
      const a = this.assets.get(id);
      let data = a && !a.review && (icon ? a.icon : a.thumb) ? this.readFile((icon ? 'icon:' : 'thumb:') + a.id) : null;
      // Shirts and pants: their picture is the item itself (the template), for the 3D mannequins.
      if (!data && !icon && a && !a.review && (a.kind === 'shirt' || a.kind === 'pants') && a.meta && a.meta.image) data = this.readFile(a.id);
      if (!data) return new Response('No picture.', { status: 404 });
      const jpg = data[0] === 0xff && data[1] === 0xd8;
      return new Response(data, { headers: { 'content-type': jpg ? 'image/jpeg' : 'image/png', 'cache-control': 'public, max-age=86400' } });
    }
    // POST /api: one request, one answer.
    let req;
    try { req = JSON.parse(await request.text()); } catch { req = null; }
    if (!req || typeof req !== 'object' || Array.isArray(req)) return json(fail('That wasn\'t a proper request.'), 400);
    if (typeof req.op === 'string' && req.op.startsWith('relay.')) return json(fail('Multiplayer needs the Guts&Bolts app.'));
    const answer = this.handle(req);
    if (this.outbox.length) await this.sendMail();
    if (this.lyretoQueue.length) await this.fetchLyretoCards();
    return json(answer);
  }
}

function isJson(bytes) {
  try { JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)); return true; } catch { return false; }
}

export function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), { status, headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });
}
