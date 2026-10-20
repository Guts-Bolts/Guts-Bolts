// The chat / comment filter: a bad word turns the whole message into
// "[ Content Deleted ]", like classic Roblox.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { filterText, isFiltered, nameHasHateWord, FILTER_WORDS, CONTENT_DELETED } from '../textfilter.js';
import { rot13 } from './helpers.mjs';

const bad = rot13(FILTER_WORDS[0]);

test('clean messages go through as they are', () => {
  for (const s of ['hello!', 'gg wp', 'nice build', 'Scunthorpe', 'class assignment', ''])
    assert.equal(filterText(s), s);
});

test('a message with a filtered word is all "[ Content Deleted ]"', () => {
  assert.equal(CONTENT_DELETED, '[ Content Deleted ]');
  assert.equal(filterText(`you are a ${bad} lol`), CONTENT_DELETED);
  assert.equal(filterText(bad.toUpperCase()), CONTENT_DELETED);
  assert.ok(isFiltered(`hi ${bad}`));
});

test('usernames with a hate word hidden inside are caught', () => {
  assert.ok(nameHasHateWord('xx' + bad + '99'));
  assert.ok(!nameHasHateWord('BuilderBob'));
});

test('the C++ filter (src/core/TextFilter.cpp) has the same word list', () => {
  const cpp = readFileSync(new URL('../../src/core/TextFilter.cpp', import.meta.url), 'utf8');
  const line = cpp.match(/kWords\[\]\s*=\s*\{([^}]*)\}/);
  assert.ok(line, 'kWords not found in TextFilter.cpp');
  const words = [...line[1].matchAll(/"([^"]*)"/g)].map((m) => m[1]);
  assert.deepEqual(words, FILTER_WORDS);
});

// Lua code in the Scripting Helpers forum, chat and descriptions: names like Shop, Top and Info
// end in a web address ending (.shop, .top, .info), but code carries on after them.
const LUA = ['local door = workspace.Shop.Door', 'script.Parent.Top.Size = 5', 'game.Players.LocalPlayer.PlayerGui.Shop.Enabled = true',
  'workspace.Shop:Destroy()', 'local t = gui.Top[1]', 'print(button.Info())'];
const LINKS = ['go to evil.shop', 'evil.shop/free-bolts', 'www.evil.shop.Now', 'https://evil.shop.Door', 'free stuff at evil.com: hurry',
  'evil.co.uk', 'EVIL.COM'];

test('Lua code that looks a bit like a web address goes through', () => {
  for (const s of LUA) assert.equal(filterText(s), s, s);
});

test('real web addresses are still deleted', () => {
  for (const s of LINKS) assert.equal(filterText(s), CONTENT_DELETED, s);
});
