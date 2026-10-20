// The text filter: what people write (messages, statuses, group posts, names,
// descriptions, game chat) becomes "[ Content Deleted ]" if it has a hate word, a link
// or personal info (phone numbers, email addresses) in it. Swearing is fine (the site is 18+).
// src/core/TextFilter.cpp does exactly the same for the C++ server and game chat.

// Slurs, ROT13'd so they don't sit in the code as plain words. A word only counts on
// its own (so "snigger" is fine), with letters repeated, look-alike numbers and
// symbols (n1gg3r), or a space or dot between letters (n i g g e r).
const WORDS = ['avttre', 'avttn', 'fnaqavttre', 'snttbg', 'xvxr', 'fcvp', 'puvax', 'jrgonpx', 'genaal', 'tbbx', 'ornare', 'enturnq', 'gbjryurnq', 'cnxv', 'cbepuzbaxrl', 'wvtnobb', 'pbba'];

const LOOKS = { a: 'a4@', b: 'b8', e: 'e3', g: 'g69', i: 'i1!|', o: 'o0', s: 's5$', t: 't7' };
const rot13 = (w) => w.replace(/[a-z]/g, (c) => String.fromCharCode((c.charCodeAt(0) - 97 + 13) % 26 + 97));
const esc = (s) => s.replace(/[-\\^$*+?.()|[\]{}]/g, '\\$&');
function wordPattern(word) {
  return [...word].map((c) => '[' + esc(LOOKS[c] || c) + ']+').join('[ ._*-]?');
}
// Group 1 is what comes before (start or a non-letter); the rest is masked.
const SLURS = new RegExp('(^|[^a-z0-9])((?:' + WORDS.map((w) => wordPattern(rot13(w))).join('|') + ')(?:e?s)?)(?![a-z])', 'g');
const LINK = /(?:https?:\/\/|www\.)[^\s]+|[a-z0-9-]+(?:\.[a-z0-9-]+)*\.(?:com|net|org|gg|io|xyz|ru|tk|ml|ga|cf|gq|link|site|online|top|click|me|co|us|info|biz|shop|app|dev|ly)(?![a-z0-9])(?:\/[^\s]*)?/g;
const EMAIL = /[a-z0-9._%+-]+@[a-z0-9.-]+\.[a-z]{2,}/g;
const PHONE = /\+?\(?[0-9](?:[ .()-]*[0-9]){9,14}/g;
const ALLOWED_LINK = /^(?:https?:\/\/)?(?:www\.)?gutsandbolts\.net(?:[\/?#].*)?$/;
// Lua code isn't a link: in workspace.Shop.Door or script.Parent.Top:Destroy() the "shop" and
// "top" carry on into a Name with a capital letter, a :Method or a ( [, which a web address never
// does. (Only for bare names: anything with http://, www. or a /path is still a link.)
const isLuaCode = (match, after) => !/^(?:https?:\/\/|www\.)/.test(match) && !match.includes('/') && /^(?:\.[A-Z]|:[A-Za-z]|[([])/.test(after);

// Lower-case A-Z only, so every position still lines up with the original text.
const lowerAscii = (s) => s.replace(/[A-Z]/g, (c) => String.fromCharCode(c.charCodeAt(0) + 32));

function coverText(text) {
  const s = String(text || '');
  if (!s) return s;
  const low = lowerAscii(s);
  const mask = new Array(s.length).fill(false);
  const cover = (from, to) => { for (let i = from; i < to; i++) mask[i] = true; };
  for (const m of low.matchAll(EMAIL)) cover(m.index, m.index + m[0].length);
  for (const m of low.matchAll(LINK))
    if (!ALLOWED_LINK.test(m[0]) && !isLuaCode(m[0], s.slice(m.index + m[0].length, m.index + m[0].length + 2))) cover(m.index, m.index + m[0].length);
  for (const m of low.matchAll(PHONE)) cover(m.index, m.index + m[0].length);
  for (const m of low.matchAll(SLURS)) cover(m.index + m[1].length, m.index + m[0].length);
  let out = '';
  for (let i = 0; i < s.length; i++) out += mask[i] && s[i] !== ' ' && s[i] !== '\n' ? '#' : s[i];
  return out;
}

// Like classic Roblox: if anything in it would be covered, the whole thing becomes this.
export const CONTENT_DELETED = '[ Content Deleted ]';
export function filterText(text) {
  const s = String(text || '');
  return coverText(s) === s ? s : CONTENT_DELETED;
}

// Usernames: a hate word anywhere counts (names have no spaces to tell words apart).
const UNLOOK = { 4: 'a', '@': 'a', 8: 'b', 3: 'e', 6: 'g', 9: 'g', 1: 'i', '!': 'i', '|': 'i', 0: 'o', 5: 's', $: 's', 7: 't' };
const IN_NAME = new RegExp(WORDS.map((w) => [...rot13(w)].map((c) => esc(c) + '+').join('')).join('|'));
export function nameHasHateWord(name) {
  const plain = [...lowerAscii(String(name || ''))].map((c) => UNLOOK[c] || c).filter((c) => c >= 'a' && c <= 'z').join('');
  return IN_NAME.test(plain);
}

// For names: true if the filter would change it.
export const isFiltered = (text) => filterText(text) !== String(text || '');

export const FILTER_WORDS = WORDS;   // (src/core/TextFilter.cpp has the same list)
