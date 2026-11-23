import test from 'node:test';
import assert from 'node:assert/strict';
import { safeMediaUrl, normalizeMediaItem, composeChatMessage } from '../../website/app/media-picker.js';

test('accepts only HTTPS pictures from KLIPY', () => {
  assert.equal(safeMediaUrl('https://static.klipy.com/ii/a/b/c.gif'), 'https://static.klipy.com/ii/a/b/c.gif');
  assert.equal(safeMediaUrl('https://static.klipy.com/x/sticker.webp'), 'https://static.klipy.com/x/sticker.webp');
  assert.equal(safeMediaUrl('http://static.klipy.com/a.png'), '');
  assert.equal(safeMediaUrl('https://cdn.example.com/a.gif'), '', 'other hosts would be "[ Content Deleted ]" by the text filter');
  assert.equal(safeMediaUrl('https://klipy.com.evil.example/a.gif'), '');
  assert.equal(safeMediaUrl('javascript:alert(1)'), '');
  assert.equal(safeMediaUrl('https://static.klipy.com/a.svg'), '');
  assert.equal(safeMediaUrl('https://static.klipy.com/a.gif?x=1'), '');
});
test('rejects credentials and malformed or oversized values', () => {
  assert.equal(safeMediaUrl('https://user:pass@static.klipy.com/a.png'), '');
  assert.equal(safeMediaUrl('not a url'), '');
  assert.equal(safeMediaUrl('https://static.klipy.com/' + 'a'.repeat(2050) + '.gif'), '');
});
test('normalizes KLIPY items: medium size to send, small size to preview, no ads', () => {
  const item = { type: 'gif', title: 'reaction', slug: 'reaction-gif', file: {
    hd: { gif: { url: 'https://static.klipy.com/t/hd.gif', width: 498, height: 205 } },
    md: { gif: { url: 'https://static.klipy.com/t/md.gif', width: 500, height: 206 } },
    sm: { webp: { url: 'https://static.klipy.com/t/sm.webp' }, gif: { url: 'https://static.klipy.com/t/sm.gif' } } } };
  assert.deepEqual(normalizeMediaItem(item), { url: 'https://static.klipy.com/t/md.gif', preview: 'https://static.klipy.com/t/sm.webp',
    title: 'reaction', slug: 'reaction-gif', w: 500, h: 206 });
  assert.equal(normalizeMediaItem({ type: 'ad', url: 'https://static.klipy.com/ad.gif' }), null);
  assert.equal(normalizeMediaItem({ title: 'bad', file: { hd: { mp4: { url: 'https://static.klipy.com/video.mp4' } } } }), null);
  assert.equal(normalizeMediaItem(null), null);
});
test('normalizes PNG sticker renditions', () => {
  const r = normalizeMediaItem({ type: 'sticker', title: 'sticker', file: { hd: { png: { url: 'https://static.klipy.com/stickers/sticker.png' } } } });
  assert.equal(r.url, 'https://static.klipy.com/stickers/sticker.png');
  assert.equal(r.preview, 'https://static.klipy.com/stickers/sticker.png');
});
test('composeChatMessage keeps media tokens within server message limits', () => {
  const image = { url: 'https://static.klipy.com/picture.gif' };
  assert.equal(composeChatMessage('hello', image), 'hello\n[media:https://static.klipy.com/picture.gif]');
  assert.equal(composeChatMessage('', image), '[media:https://static.klipy.com/picture.gif]');
  assert.equal(composeChatMessage('text only'), 'text only');
  assert.equal(composeChatMessage('x'.repeat(1000), image), null);
  assert.equal(composeChatMessage('x'.repeat(1000)), 'x'.repeat(1000));
  assert.equal(composeChatMessage('hello', { url: 'javascript:alert(1)' }), null);
});
