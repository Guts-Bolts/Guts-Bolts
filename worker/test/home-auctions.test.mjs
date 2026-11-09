import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  formatAuctionCountdown,
  getActiveAuctions,
  getAuctionBidSummary,
  getAuctionUrgency,
  isActiveAuction,
} from '../../website/auction-data.mjs';

const NOW = 1_800_000_000_000;

test('active auction filtering excludes expired, completed, cancelled and malformed records', () => {
  const live = { id: 'live', auction: { endsAt: NOW / 1000 + 60 } };
  assert.equal(isActiveAuction(live, NOW), true);
  assert.equal(isActiveAuction({ id: 'expired', auction: { endsAt: NOW / 1000 } }, NOW), false);
  assert.equal(isActiveAuction({ id: 'done', auction: { endsAt: NOW / 1000 + 60, done: true } }, NOW), false);
  assert.equal(isActiveAuction({ id: 'cancelled', auction: { endsAt: NOW / 1000 + 60, cancelled: true } }, NOW), false);
  assert.equal(isActiveAuction({ id: 'bad-time', auction: { endsAt: 'not-a-time' } }, NOW), false);
  assert.equal(isActiveAuction({ id: 'missing' }, NOW), false);
});

test('active auctions are ordered by soonest ending without mutating the API array', () => {
  const later = { id: 'later', auction: { endsAt: NOW / 1000 + 300 } };
  const sooner = { id: 'sooner', auction: { endsAt: NOW / 1000 + 30 } };
  const expired = { id: 'expired', auction: { endsAt: NOW / 1000 - 1 } };
  const input = [later, expired, sooner];
  assert.deepEqual(getActiveAuctions(input, NOW).map((item) => item.id), ['sooner', 'later']);
  assert.deepEqual(input, [later, expired, sooner]);
  assert.deepEqual(getActiveAuctions(null, NOW), []);
});

test('bid summary uses the highest bid and falls back to the starting bid', () => {
  assert.deepEqual(getAuctionBidSummary({
    minBid: 10,
    bids: [{ amount: '25' }, { amount: 40 }, { amount: 32 }],
  }), { hasBids: true, amount: 40 });
  assert.deepEqual(getAuctionBidSummary({ minBid: '15', bids: [] }), { hasBids: false, amount: 15 });
  assert.deepEqual(getAuctionBidSummary({ minBid: 'invalid', bids: [{ amount: 'invalid' }] }), { hasBids: true, amount: 0 });
  assert.deepEqual(getAuctionBidSummary(null), { hasBids: false, amount: 0 });
});

test('countdown formats days and clock fields and clamps expired or invalid timestamps to zero', () => {
  assert.equal(formatAuctionCountdown(NOW / 1000 + 2 * 86400 + 3 * 3600 + 4 * 60 + 5, NOW), '2d 03:04:05');
  assert.equal(formatAuctionCountdown(NOW / 1000 + 3723, NOW), '01:02:03');
  assert.equal(formatAuctionCountdown(NOW / 1000 - 1, NOW), '00:00:00');
  assert.equal(formatAuctionCountdown('invalid', NOW), '00:00:00');
});

test('auction urgency escalates at the 24-hour, one-hour, five-minute and one-minute marks', () => {
  assert.equal(getAuctionUrgency(NOW / 1000 + 86400, NOW).key, 'ending-today');
  assert.equal(getAuctionUrgency(NOW / 1000 + 3600, NOW).key, 'last-hour');
  assert.equal(getAuctionUrgency(NOW / 1000 + 300, NOW).key, 'last-five-minutes');
  assert.equal(getAuctionUrgency(NOW / 1000 + 60, NOW).key, 'last-minute');
  assert.equal(getAuctionUrgency(NOW / 1000 + 86401, NOW).key, 'live');
  assert.equal(getAuctionUrgency('invalid', NOW).key, 'unknown');
});

const appSource = readFileSync(new URL('../../website/app/app.js', import.meta.url), 'utf8');

test('in-app auction banner shows the starting bid until the first bid is placed', () => {
  assert.match(appSource, /const bidSummary = getAuctionBidSummary\\(au\\)/);
  assert.match(appSource, /bidSummary\\.hasBids \\? 'CURRENT HIGH BID' : 'STARTING BID'/);
  assert.match(appSource, /bolts\\(bidSummary\\.amount\\)/);
});
