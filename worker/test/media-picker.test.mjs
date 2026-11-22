import test from 'node:test';
import assert from 'node:assert/strict';
import { safeMediaUrl, normalizeMediaItem } from '../../website/app/media-picker.js';

test('accepts only HTTPS image and GIF paths', () => {
  assert.equal(safeMediaUrl('https://cdn.example.com/a.gif'), 'https://cdn.example.com/a.gif');
  assert.equal(safeMediaUrl('https://example.com/img.webp'), 'https://example.com/img.webp');
  assert.equal(safeMediaUrl('http://example.com/a.png'), '');
  assert.equal(safeMediaUrl('javascript:alert(1)'), '');
  assert.equal(safeMediaUrl('https://example.com/a.svg'), '');
});
test('rejects credentials and malformed or oversized values', () => {
  assert.equal(safeMediaUrl('https://user:pass@example.com/a.png'), '');
  assert.equal(safeMediaUrl('not a url'), '');
  assert.equal(safeMediaUrl('https://example.com/' + 'a'.repeat(2050) + '.gif'), '');
});
test('normalizes supported provider items and rejects bad items', () => {
  assert.deepEqual(normalizeMediaItem({src:'https://cdn.example.com/1.gif', title:'loop'}), {url:'https://cdn.example.com/1.gif', title:'loop'});
  assert.equal(normalizeMediaItem({url:'https://cdn.example.com/1.svg'}), null);
  assert.equal(normalizeMediaItem(null), null);
});

import { composeChatMessage } from '../../website/app/media-picker.js';

test('composeChatMessage keeps media tokens within server message limits', () => {
  const image = {url:'https://cdn.example.com/picture.gif'};
  assert.equal(composeChatMessage('hello', image), 'hello\n[media:https://cdn.example.com/picture.gif]');
  assert.equal(composeChatMessage('', image), '[media:https://cdn.example.com/picture.gif]');
  assert.equal(composeChatMessage('text only'), 'text only');
  assert.equal(composeChatMessage('x'.repeat(1000), image), null);
  assert.equal(composeChatMessage('x'.repeat(1000)), 'x'.repeat(1000));
  assert.equal(composeChatMessage('hello', {url:'javascript:alert(1)'}), null);
});
