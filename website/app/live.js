// Live updates: the website listens to Supabase Realtime (Broadcast) and refreshes the
// right bit of the page the moment something happens, instead of waiting for its timer.
//
// Pings carry no data, only "look again" (a new bid, a forum post, a notification for you).
// The page then asks the Guts&Bolts server like it always does, so nothing secret ever goes
// through Supabase. If the connection drops, the page's normal timers still keep it fresh.
//
// This is the small part of the Phoenix channel protocol that Realtime speaks, so the
// website doesn't need the whole Supabase library:
//   join:      { topic: 'realtime:<name>', event: 'phx_join', payload: { config }, ref, join_ref }
//   heartbeat: { topic: 'phoenix', event: 'heartbeat', payload: {}, ref } every 25 seconds
//   a ping:    { topic: 'realtime:<name>', event: 'broadcast', payload: { event, payload } }

const HEARTBEAT_MS = 25000;
const QUIET_MS = 2000;   // at most one refresh per topic and event every 2 seconds

let config = null;          // { url, key } from /api/supabase/live (null: live updates are off)
let socket = null;
let ref = 0;
let heartbeat = null;
let retryMs = 2000;
const topics = new Map();   // topic -> join ref (the topics we want to hear)
const handlers = [];        // (topic, event) => void
const lastRun = new Map();  // "topic|event" -> when it last ran

function send(msg) {
  if (socket && socket.readyState === 1) socket.send(JSON.stringify(msg));
}

function join(topic) {
  const joinRef = String(++ref);
  topics.set(topic, joinRef);
  send({ topic: 'realtime:' + topic, event: 'phx_join', ref: joinRef, join_ref: joinRef,
    payload: { config: { broadcast: { self: false, ack: false }, presence: { key: '' }, postgres_changes: [], private: false } } });
}

function leave(topic) {
  const joinRef = topics.get(topic);
  if (!joinRef) return;
  topics.delete(topic);
  send({ topic: 'realtime:' + topic, event: 'phx_leave', ref: String(++ref), join_ref: joinRef, payload: {} });
}

function heard(raw) {
  let msg;
  try { msg = JSON.parse(raw); } catch { return; }
  if (!msg || msg.event !== 'broadcast' || typeof msg.topic !== 'string' || !msg.topic.startsWith('realtime:')) return;
  const topic = msg.topic.slice(9), event = msg.payload && typeof msg.payload.event === 'string' ? msg.payload.event : '';
  if (!topics.has(topic) || !event) return;
  // Anyone holding the publishable key could send pings to the public topic, so a burst of
  // them is folded into one refresh.
  const key = topic + '|' + event, now = Date.now();
  if (now - (lastRun.get(key) || 0) < QUIET_MS) return;
  lastRun.set(key, now);
  for (const fn of handlers) {
    try { fn(topic, event); } catch { /* one bad handler shouldn't stop the others */ }
  }
}

function connect() {
  if (!config || socket) return;
  const url = config.url.replace(/^https:/, 'wss:') + '/realtime/v1/websocket?apikey=' + encodeURIComponent(config.key) + '&vsn=1.0.0';
  let ws;
  try { ws = new WebSocket(url); } catch { return; }
  socket = ws;
  ws.onopen = () => {
    retryMs = 2000;
    const want = [...topics.keys()];
    topics.clear();
    for (const t of want) join(t);
    heartbeat = setInterval(() => send({ topic: 'phoenix', event: 'heartbeat', payload: {}, ref: String(++ref) }), HEARTBEAT_MS);
  };
  ws.onmessage = (e) => heard(e.data);
  ws.onclose = () => {
    clearInterval(heartbeat);
    heartbeat = null;
    if (socket === ws) socket = null;
    setTimeout(connect, retryMs);
    retryMs = Math.min(retryMs * 2, 60000);
  };
  ws.onerror = () => { try { ws.close(); } catch { /* already closed */ } };
}

// Start listening (safe to call more than once). Returns false when the server has live updates off.
export async function startLive() {
  if (config) return true;
  try {
    const r = await fetch('/api/supabase/live', { headers: { accept: 'application/json' } });
    const body = r.ok ? await r.json() : null;
    if (!body || !body.ok || !body.live || typeof body.live.url !== 'string' || !/^https:\/\/[a-z0-9]+\.supabase\.co$/.test(body.live.url)) return false;
    config = body.live;
  } catch { return false; }
  topics.set('gb-activity', '');
  connect();
  return true;
}

// Hear a topic (the public 'gb-activity', or your own account's topic from me.live).
export function listen(topic) {
  if (!topic || topics.has(topic)) return;
  if (socket && socket.readyState === 1) join(topic);
  else topics.set(topic, '');
}

// Stop hearing a topic (signing out).
export function stopListening(topic) {
  if (topic && topics.has(topic)) {
    if (socket && socket.readyState === 1) leave(topic);
    else topics.delete(topic);
  }
}

// fn(topic, event) runs for every ping heard.
export function onLive(fn) { handlers.push(fn); }

// For tests: feed in a message as if it came from Realtime.
export function _heard(raw) { heard(raw); }
