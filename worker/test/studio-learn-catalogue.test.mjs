import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import { Script } from 'node:vm';

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
  const linkIds = extract(link, /static const char\* lessonIds\[\]\s*=\s*\{([^}]+)\}/, 'desktop link allowlist');
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
  assert.ok(editor.includes('const char* demo[3]'));
  assert.ok(editor.includes('const char* hints[3]'));
  assert.ok(editor.includes('const char* practice'));
  assert.ok(editor.includes('m_learnSawScriptError') && editor.includes('m_learnSecondPlayRun'), 'debugging lesson checks a real typo error and corrected output');
  assert.ok(editor.includes('hasLiteralPrintOutput(newScripts, Log::lastRun())'), 'script lessons verify the exact print output from a new script');
  assert.ok(editor.includes('hasLocalVariablePrintOutput(newScripts, Log::lastRun())'), 'variable lesson verifies the printed value of its declared local variable');
  assert.ok(editor.includes('m_learnSecondPlayAfterEdit'), 'final lesson requires a second playtest after the edit');
  assert.ok(editor.includes('Log::currentRun()') && editor.includes('entry.text == expectedOutput'), 'debugging lesson verifies the exact corrected print output');
});

test('Studio Learn preserves guest progress when the learner signs in and exposes pause, resume, repeat and exit controls', () => {
  const editor = read('src/editor/Editor.cpp');
  const header = read('src/editor/Editor.h');
  assert.ok(editor.includes('mergeFile("guest", true)'), 'guest progress is merged with account progress on sign-in');
  assert.ok(editor.includes('savedUpdatedAt > m_learnProgressUpdatedAt'), 'the newest local account or guest resume point is selected');
  assert.ok(editor.includes('remoteUpdatedAt > m_learnProgressUpdatedAt'), 'a newer account resume point wins over stale local state');
  assert.ok(header.includes('m_learnProgressUpdatedAt'));
  assert.ok(header.includes('m_learnPaused'));
  assert.ok(editor.includes('Pause'));
  assert.ok(editor.includes('Resume lesson'));
  assert.ok(editor.includes('Repeat demonstration'));
  assert.ok(editor.includes('Exit lesson'));
});

test('Studio Learn outlines the exact visible control for the instrumented editor targets', () => {
  const highlight = read('src/editor/LearnHighlight.h');
  const editor = read('src/editor/Editor.cpp');
  const properties = read('src/editor/panels/PropertiesPanel.cpp');
  const explorer = read('src/editor/panels/OutlinerPanel.cpp');
  const scripts = read('src/editor/panels/ScriptEditorPanel.cpp');
  const output = read('src/editor/panels/OutputPanel.cpp');
  const ribbon = read('src/editor/EditorRibbon.cpp');
  const publish = read('src/editor/EditorOnline.cpp');
  assert.ok(highlight.includes('ImGui::GetItemRectMin()') && highlight.includes('ImGui::GetFrameCount()'));
  assert.ok(highlight.includes('inline void markRect'), 'non-ImGui target rectangles use the shared markRect API');
  assert.ok(editor.includes('StudioLearnHighlight::find(targetControl'));
  assert.ok(properties.includes('StudioLearnHighlight::mark("properties.Anchored")'));
  assert.ok(properties.includes('StudioLearnHighlight::mark("properties.Can Collide")'));
  assert.ok(properties.includes('StudioLearnHighlight::mark("properties.Size")'));
  assert.ok(explorer.includes('StudioLearnHighlight::mark(std::string("explorer.insert.") + what)'));
  assert.ok(explorer.includes('StudioLearnHighlight::markRect("explorer.insert.open"'));
  assert.ok(scripts.includes('StudioLearnHighlight::mark("script.editor")'));
  assert.ok(output.includes('StudioLearnHighlight::mark("output.messages")'));
  assert.ok(ribbon.includes('StudioLearnHighlight::mark(std::string("ribbon.") + label)'));
  assert.ok(ribbon.includes('ribbon.tab.'));
  assert.ok(publish.includes('StudioLearnHighlight::mark("publish.confirm")'));
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
  assert.ok(site.includes("remoteUpdatedAt >= progress.updatedAt"), 'website chooses the latest timestamped resume point');
  assert.ok(site.includes("Date.now() / 1000"), 'website stamps local lesson starts');
  assert.doesNotThrow(() => new Script(site, { filename: 'website/app/app.js' }), 'website app JavaScript parses');
});
