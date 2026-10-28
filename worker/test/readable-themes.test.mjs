// Text has to stay readable on every theme (light, dark, and Gutstober in both).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const css = readFileSync(new URL('../../website/app/app.css', import.meta.url), 'utf8');
const js = readFileSync(new URL('../../website/app/app.js', import.meta.url), 'utf8');
const lum = (hex) => {
  const [r, g, b] = hex.match(/[0-9a-f]{2}/gi).map((h) => {
    const v = parseInt(h, 16) / 255;
    return v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
  });
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
};
const contrast = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };

test('update tags have white text that can be read on their colour', () => {
  const tags = js.match(/UPDATE_TAG_COLORS = \{([^}]+)\}/)[1];
  for (const [, name, col] of tags.matchAll(/(\w+): '(#[0-9a-f]{6})'/gi))
    assert.ok(contrast(col, '#ffffff') >= 3, `${name} tag ${col} is too light for white text`);
});

test('title bar links and notes use the bar\'s own text colour', () => {
  assert.match(css, /\.boxhead a, \.boxhead \.muted, h2 > \.muted \{ color: var\(--headText\); \}/);
  assert.match(css, /html\.gutstober \.status \{ color: #2a0f3a; \}/);
  assert.ok(contrast('#2a0f3a', '#e8740c') >= 4.5, 'server status on the orange tab row');
});

test('red error text is lightened in dark mode', () => {
  assert.match(css, /:root:not\(\[data-theme="light"\]\) \.error \{ color: #ff7d7d; \}/);
  assert.ok(contrast('#ff7d7d', '#1b212b') >= 4.5);
  assert.ok(contrast('#ff7d7d', '#21162b') >= 4.5);
});
