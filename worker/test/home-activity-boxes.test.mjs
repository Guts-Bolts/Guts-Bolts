// The home page's Auction Broadcasts and Forum activity boxes must be drawn as HTML, not
// shown as escaped "<div class=...>" text. html`` escapes plain strings, so a box's .s
// string must never be put into another html`` template (pass the html value itself).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const app = readFileSync(new URL('../../website/app/app.js', import.meta.url), 'utf8');

test('activity boxes are put into the page as HTML, not as escaped text', () => {
  assert.doesNotMatch(app, /\$\{renderAuctionActivity\([^`]*?\)\.s\}/);
  assert.doesNotMatch(app, /\$\{renderForumActivity\([^`]*?\)\.s\}/);
  assert.match(app, /<div id="auctionBroadcasts">\$\{renderAuctionActivity\(/);
  assert.match(app, /<div id="forumActivity">\$\{renderForumActivity\(/);
});
