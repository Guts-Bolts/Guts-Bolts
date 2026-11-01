import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const read = (p) => readFileSync(resolve(root, p), 'utf8');
const extract = (source, pattern, label) => {
  const match = source.match(pattern);
  assert.ok(match, label + ' is present');
  return [...match[1].matchAll(/["']([a-z0-9-]+)["']/g)].map((x) => x[1]);
};

test('website, Studio, worker and standalone server share the same 20 stable lesson IDs', () => {
  const editor = read('src/editor/Editor.cpp');
  const site = read('website/app/app.js');
  const worker = read('worker/server.js');
  const server = read('src/server/Server.cpp');
  const ids = extract(editor, /kStudioLearnIds\[\]\s*=\s*\{([^}]+)\}/, 'Studio lesson ID list');
  const siteBlock = site.match(/const STUDIO_LESSONS\s*=\s*\[([\s\S]*?)\n\];/);
  assert.ok(siteBlock, 'website lesson catalogue is present');
  const siteIds = [...siteBlock[1].matchAll(/^\s*\['([a-z0-9-]+)'/gm)].map((x) => x[1]);
  const workerIds = extract(worker, /const lessonIds\s*=\s*new Set\(\[([^\]]+)\]\)/, 'worker allowlist');
  const link = read('src/player/LaunchLink.cpp');
  const linkIds = extract(link, /static const char\\* lessonIds\\[\\]\\s*=\\s*\\{([^}]+)\\}/, 'desktop link allowlist');
  const serverIds = extract(server, /lessonIds\s*=\s*\{([^}]+)\}/, 'standalone server allowlist');
  assert.equal(ids.length, 20);
  assert.equal(new Set(ids).size, 20);
  assert.deepEqual(siteIds, ids);
  assert.deepEqual(workerIds, ids);
  assert.deepEqual(serverIds, ids);
  assert.deepEqual(linkIds, ids);
});

test('every Studio lesson exposes a guided demonstration, practice challenge, hints and a validator', () => {
  const editor = read('src/editor/Editor.cpp');
  const body = editor.slice(editor.indexOf('static const Lesson lessons[]'), editor.indexOf('const char* titles[]'));
  assert.equal((body.match(/\{\"(?:Your First Build|Make It Move|Build a Tiny Obby|Your First Script|Light Up the Scene|Publish Your First Game|Meet the Explorer|The Properties Panel|Copy, Group, Organize|Color and Materials|Shape the World|Spawn Into the Action|Collision Course|Connect the Parts|Sound Check|Your First GUI|Make Things Happen|Remembering Values|Find and Fix Bugs|Build, Test, Improve)\"/g) || []).length, 20);
  for (let i = 0; i < 20; i++) assert.ok(body.includes('}, ' + i + '}'), 'lesson ' + (i + 1) + ' has a validator');
  assert.ok(body.includes('const char* demo[3]'));
  assert.ok(body.includes('const char* hints[3]'));
  assert.ok(body.includes('const char* practice'));
});

test('desktop lesson links are strictly validated and routed to the Studio command line', () => {
  const link = read('src/player/LaunchLink.cpp');
  const player = read('src/player/PlayerLogin.cpp');
  const main = read('src/main.cpp');
  assert.ok(link.includes('gutsandbolts://learn/'));
  assert.ok(link.includes("c == '-'"));
  assert.ok(player.includes('Paths::sibling("GutsAndBolts")'));
  assert.ok(player.includes('"--learn=" + lesson'));
  assert.ok(main.includes('"--learn=", 8'));
});

test('website Learn tab and its launch action exist', () => {
  const site = read('website/app/app.js');
  assert.ok(site.includes("['learn', 'Learn']"));
  assert.ok(site.includes("data-act=\"startLearn\""));
  assert.ok(site.includes("gutsandbolts://learn/"));
  assert.ok(site.includes("learn.progress.get"));
});
