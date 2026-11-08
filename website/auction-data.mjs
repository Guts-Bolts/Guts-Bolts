// Pure auction display helpers shared by the public homepage and automated tests.
// Keep time injectable so countdown and expiry behavior can be tested deterministically.

export function isActiveAuction(item, nowMs = Date.now()) {
  const auction = item && item.auction;
  const endsAt = Number(auction && auction.endsAt);
  return !!auction
    && !auction.done
    && !auction.cancelled
    && Number.isFinite(endsAt)
    && endsAt > nowMs / 1000;
}

export function getActiveAuctions(items, nowMs = Date.now()) {
  if (!Array.isArray(items)) return [];
  return items
    .filter((item) => isActiveAuction(item, nowMs))
    .sort((a, b) => Number(a.auction.endsAt) - Number(b.auction.endsAt));
}

export function getAuctionBidSummary(auction) {
  const bids = Array.isArray(auction && auction.bids) ? auction.bids : [];
  let highest = 0;
  for (const bid of bids) {
    const amount = Number(bid && bid.amount);
    if (Number.isFinite(amount)) highest = Math.max(highest, amount);
  }
  const minimum = Number(auction && auction.minBid);
  return {
    hasBids: bids.length > 0,
    amount: highest || (Number.isFinite(minimum) ? minimum : 0),
  };
}

export function formatAuctionCountdown(end, nowMs = Date.now()) {
  const endSeconds = Number(end);
  let left = Number.isFinite(endSeconds)
    ? Math.max(0, Math.floor(endSeconds - nowMs / 1000))
    : 0;
  const days = Math.floor(left / 86400);
  left %= 86400;
  const hours = Math.floor(left / 3600);
  const minutes = Math.floor((left % 3600) / 60);
  const seconds = left % 60;
  const pad = (value) => String(value).padStart(2, '0');
  return (days ? days + 'd ' : '') + pad(hours) + ':' + pad(minutes) + ':' + pad(seconds);
}

export function getAuctionUrgency(end, nowMs = Date.now()) {
  const endSeconds = Number(end);
  if (!Number.isFinite(endSeconds)) return { key: 'unknown', label: 'LIVE AUCTION', className: '' };
  const secondsLeft = endSeconds - nowMs / 1000;
  if (secondsLeft <= 60) return { key: 'last-minute', label: 'AUCTION · LAST MINUTE', className: 'last-minute' };
  if (secondsLeft <= 300) return { key: 'last-five-minutes', label: 'AUCTION · 5 MINUTES LEFT', className: 'last-five-minutes' };
  if (secondsLeft <= 3600) return { key: 'last-hour', label: 'AUCTION · ENDING SOON', className: 'urgent' };
  if (secondsLeft <= 86400) return { key: 'ending-today', label: 'AUCTION · ENDING TODAY', className: 'ending-today' };
  return { key: 'live', label: 'LIVE AUCTION', className: '' };
}
