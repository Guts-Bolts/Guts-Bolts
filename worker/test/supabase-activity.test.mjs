import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer, signUp } from './helpers.mjs';

const SUPABASE_ENV = {
  SUPABASE_URL: 'https://example.supabase.co',
  SUPABASE_SECRET_KEY: 'test-secret-not-used-for-network',
};

test('notifications and public activity are queued durably for Supabase', () => {
  const s = makeServer(SUPABASE_ENV);
  s.sql.exec('DELETE FROM supabase_outbox');   // (the example games' search entries, sent at start)
  const player = signUp(s, 'ArchivePlayer');

  s.notify(player, 'friend', 'A player sent you a friend request.', 'friend-1');
  s.notify(player, 'ticket', 'New support ticket: "private account details".', 'ticket-1');
  s.notify(player, 'gift', 'A player sent you a gift: Test Hat! "private gift note".', 'gift-1');
  s.recordAuctionEvent({
    id: 'asset-1', name: 'Test Hat',
    auction: { bids: [{ by: player.id, amount: 75 }], endsAt: 1900000000 },
  }, 'bid_placed', player.name, 75);
  s.recordForumActivity({
    id: 'thread-1', board: 'offtopic', title: 'Test discussion',
  }, 'Off Topic', 'thread_created', player.name);
  s.recordForumActivity({
    id: 'staff-thread', board: 'news', title: 'Internal staff discussion',
  }, 'News & Announcements', 'thread_created', player.name);

  const rows = [...s.sql.exec('SELECT table_name, payload FROM supabase_outbox')];
  assert.equal(rows.length, 5);
  const notificationRows = rows.filter((row) => row.table_name === 'gb_notifications').map((row) => JSON.parse(row.payload));
  const notifications = Object.fromEntries(notificationRows.map((row) => [row.kind, row]));
  const byTable = Object.fromEntries(rows.filter((row) => row.table_name !== 'gb_notifications')
    .map((row) => [row.table_name, JSON.parse(row.payload)]));

  assert.equal(notifications.friend.account_id, player.id);
  assert.equal(notifications.friend.message, 'A player sent you a friend request.');
  assert.equal(notifications.friend.read_at, null);
  assert.equal(notifications.ticket.message, 'A new support ticket was submitted.');
  assert.equal(notifications.gift.message, 'You received a gift notification.');
  assert(!notifications.ticket.message.includes('private account details'));
  assert(!notifications.gift.message.includes('private gift note'));

  assert.equal(byTable.gb_auction_events.event_type, 'bid_placed');
  assert.equal(byTable.gb_auction_events.asset_id, 'asset-1');
  assert.equal(byTable.gb_auction_events.amount, 75);
  assert.equal(byTable.gb_auction_events.current_high_bid, 75);

  assert.equal(byTable.gb_forum_activity.event_type, 'thread_created');
  assert.equal(byTable.gb_forum_activity.thread_id, 'thread-1');
  assert.equal(byTable.gb_forum_activity.post_excerpt, '');
});

test('Supabase outbox stays disabled when server credentials are absent', () => {
  const s = makeServer();
  const player = signUp(s, 'LocalOnly');
  s.notify(player, 'friend', 'This stays in the native notification store.', 'friend-2');
  assert.equal([...s.sql.exec('SELECT id FROM supabase_outbox')].length, 0);
  assert.equal(player.notes[0].text, 'This stays in the native notification store.');
});

test('outbox flush delivers idempotent batches and clears only after successful writes', async () => {
  const s = makeServer(SUPABASE_ENV);
  s.sql.exec('DELETE FROM supabase_outbox');   // (the example games' search entries, sent at start)
  const player = signUp(s, 'DeliveryPlayer');
  s.notify(player, 'friend', 'Queued notification.', 'friend-3');
  s.recordAuctionEvent({
    id: 'asset-2', name: 'Another Hat',
    auction: { bids: [], endsAt: 1900000000 },
  }, 'auction_started', 'Guts', 10);
  const originalFetch = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (url, options) => {
    calls.push({ url: String(url), options });
    return new Response(null, { status: 204 });
  };
  try {
    await s.flushSupabaseOutbox();
    assert.equal(calls.length, 2);
    assert.equal([...s.sql.exec('SELECT id FROM supabase_outbox')].length, 0);
    assert(calls.every((call) => call.options.headers.get('apikey') === SUPABASE_ENV.SUPABASE_SECRET_KEY));
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('failed Supabase delivery leaves events in the durable outbox for retry', async () => {
  const s = makeServer(SUPABASE_ENV);
  s.sql.exec('DELETE FROM supabase_outbox');   // (the example games' search entries, sent at start)
  const player = signUp(s, 'RetryPlayer');
  s.notify(player, 'friend', 'Retry this notification.', 'friend-retry');
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => new Response('unavailable', { status: 503 });
  try {
    await s.flushSupabaseOutbox();
    const rows = [...s.sql.exec('SELECT table_name, payload FROM supabase_outbox')];
    assert.equal(rows.length, 1);
    assert.equal(rows[0].table_name, 'gb_notifications');
    assert.equal(JSON.parse(rows[0].payload).message, 'Retry this notification.');
  } finally {
    globalThis.fetch = originalFetch;
  }
});

