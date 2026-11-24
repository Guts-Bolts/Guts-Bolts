// G&B's YouTube-style video player (website/app/videoplayer.js): its keys, times, speeds, the
// grey "loaded" bar and picking up where you left off, and that Videos and Live both use it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { playerTime, keyAction, nextSpeed, loadedTo, resumeAt, SPEEDS } from '../../website/app/videoplayer.js';

const read = (f) => readFileSync(new URL('../../' + f, import.meta.url), 'utf8');

test('times look like YouTube\'s', () => {
  assert.equal(playerTime(0), '0:00');
  assert.equal(playerTime(75.9), '1:15');
  assert.equal(playerTime(3725), '1:02:05');
  assert.equal(playerTime(NaN), '0:00');
  assert.equal(playerTime(-4), '0:00');
});

test('the YouTube keys do the YouTube things', () => {
  assert.deepEqual(keyAction(' '), { do: 'toggle' });
  assert.deepEqual(keyAction('k'), { do: 'toggle' });
  assert.deepEqual(keyAction('K'), { do: 'toggle' });
  assert.deepEqual(keyAction('j'), { do: 'seek', by: -10 });
  assert.deepEqual(keyAction('l'), { do: 'seek', by: 10 });
  assert.deepEqual(keyAction('ArrowLeft'), { do: 'seek', by: -5 });
  assert.deepEqual(keyAction('ArrowRight'), { do: 'seek', by: 5 });
  assert.equal(keyAction('ArrowUp').do, 'volume');
  assert.equal(keyAction('f').do, 'fullscreen');
  assert.equal(keyAction('m').do, 'mute');
  assert.equal(keyAction('t').do, 'theater');
  assert.equal(keyAction('i').do, 'pip');
  assert.equal(keyAction('c').do, 'captions');
  assert.deepEqual(keyAction('5'), { do: 'jump', to: 0.5 });
  assert.deepEqual(keyAction('0'), { do: 'jump', to: 0 });
  assert.deepEqual(keyAction('>'), { do: 'speed', by: 1 });
  assert.deepEqual(keyAction('<'), { do: 'speed', by: -1 });
  assert.equal(keyAction('.').do, 'frame');
  assert.equal(keyAction('q'), null);
  assert.equal(keyAction('Tab'), null);
});

test('speeds step through the list and stop at the ends', () => {
  assert.equal(nextSpeed(1, 1), 1.25);
  assert.equal(nextSpeed(1, -1), 0.75);
  assert.equal(nextSpeed(2, 1), 2);
  assert.equal(nextSpeed(0.25, -1), 0.25);
  assert.equal(nextSpeed(1.1, 1), 1.25);   // (a speed not in the list starts from normal)
  assert.ok(SPEEDS.includes(1));
});

test('the grey loaded bar follows the loaded piece you are in', () => {
  const ranges = (...r) => ({ length: r.length, start: (i) => r[i][0], end: (i) => r[i][1] });
  assert.equal(loadedTo(ranges([0, 30]), 10, 100), 0.3);
  assert.equal(loadedTo(ranges([0, 30], [50, 80]), 60, 100), 0.8);
  assert.equal(loadedTo(ranges([0, 30]), 40, 100), 0);
  assert.equal(loadedTo(ranges(), 0, 100), 0);
  assert.equal(loadedTo(ranges([0, 30]), 10, 0), 0);
});

test('picks up where you left off, but not at the very start or end', () => {
  assert.equal(resumeAt('120', 600), 120);
  assert.equal(resumeAt('5', 600), 0);
  assert.equal(resumeAt('590', 600), 0);
  assert.equal(resumeAt(null, 600), 0);
  assert.equal(resumeAt('120', 0), 0);
});

test('Videos and Live both use the player, and its looks are loaded', () => {
  const videos = read('website/app/videos.js'), streams = read('website/app/streams.js'), page = read('website/app/index.html');
  assert.match(videos, /import \{ gbPlayer \} from '\.\/videoplayer\.js'/);
  assert.match(videos, /gbPlayer\(playerVideo,/);
  assert.match(streams, /gbPlayer\(video, \{ live: true/);
  assert.match(page, /href="videoplayer\.css"/);
  // The browser's own plain controls are gone from both watch pages.
  assert.doesNotMatch(videos, /<video controls/);
  assert.doesNotMatch(streams, /id="livePlayer"[^>]*controls/);
  // Its looks stay inside .gbp, so it can't change the rest of the page.
  const css = read('website/app/videoplayer.css').replace(/\/\*[\s\S]*?\*\//g, '');
  for (const rule of css.split('}')) {
    const sel = rule.split('{')[0].trim();
    if (!sel || sel.startsWith('@') || /^(from|to|\d+%)/.test(sel)) continue;
    for (const one of sel.split(',')) assert.match(one.trim(), /\.gbp/, 'selector outside the player: ' + one.trim());
  }
});

test('the big play button is a plain circle, centred however the page styles buttons', () => {
  const css = read('website/app/videoplayer.css');
  const rule = /\.gbp \.gbp-big \{([^}]*)\}/.exec(css);
  assert.ok(rule, 'the big button rule is strong enough to beat ".gbp button"');
  assert.match(rule[1], /inset: 0; margin: auto/);
  assert.match(rule[1], /border-radius: 50%/);
  // Not a red rounded rectangle (too close to another site's logo).
  assert.doesNotMatch(css, /\.gbp-big[^{]*\{[^}]*background: var\(--gbp-red\)/);
});
