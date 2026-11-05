// The chat panel: your chats and parties in the bottom corner of every page on a computer
// (#/chat on a phone, its own tab at the top), with voice calls, Do Not Disturb and chats
// linked to Lyreto. The server side is worker/chats.js (inbox, calls, gateway) and the
// party.* requests in worker/server.js (the conversations themselves).
//
// Calls use WebRTC: each browser sends its microphone straight to the others (or through
// Cloudflare's relay when the server has one, so nobody sees anybody's internet address).
// The server only passes the "how to reach me" notes between them (call.signal / call.poll).

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const ICON = {
  pencil: '<svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true"><path fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" d="M4 20h4L19 9l-4-4L4 16v4zM14 6l4 4M13 4h-9v16h16v-9"/></svg>',
  search: '<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><circle cx="10.5" cy="10.5" r="6.5" fill="none" stroke="currentColor" stroke-width="2"/><path d="M15.5 15.5 21 21" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>',
  phone: '<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path fill="currentColor" d="M6.6 10.8a15.1 15.1 0 0 0 6.6 6.6l2.2-2.2a1 1 0 0 1 1-.25 11.4 11.4 0 0 0 3.6.57 1 1 0 0 1 1 1V20a1 1 0 0 1-1 1A17 17 0 0 1 3 4a1 1 0 0 1 1-1h3.5a1 1 0 0 1 1 1c0 1.25.2 2.45.57 3.57a1 1 0 0 1-.25 1z"/></svg>',
  back: '<svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true"><path d="M15 5l-7 7 7 7" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  mic: '<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path fill="currentColor" d="M12 15a3 3 0 0 0 3-3V6a3 3 0 0 0-6 0v6a3 3 0 0 0 3 3zm5-3a5 5 0 0 1-10 0H5a7 7 0 0 0 6 6.92V21h2v-2.08A7 7 0 0 0 19 12z"/></svg>',
  chev: '<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path d="M6 15l6-6 6 6" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round"/></svg>',
  more: '<svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true"><circle cx="5" cy="12" r="2" fill="currentColor"/><circle cx="12" cy="12" r="2" fill="currentColor"/><circle cx="19" cy="12" r="2" fill="currentColor"/></svg>',
};
const COLORS = ['#e8833a', '#3a8ee8', '#4bb46b', '#b45bd6', '#d6455b', '#2fb5b0', '#c9a227', '#6b7bd6'];
const colorOf = (s) => { let h = 0; for (const c of String(s)) h = (h * 31 + c.charCodeAt(0)) >>> 0; return COLORS[h % COLORS.length]; };
// "4:58 PM" today, "Oct 9" this year, "Oct 9, 2025" before.
export function when(t) {
  if (!t) return '';
  const d = new Date(t * 1000), n = new Date();
  if (d.toDateString() === n.toDateString()) return d.toLocaleTimeString(undefined, { hour: 'numeric', minute: '2-digit' });
  return d.toLocaleDateString(undefined, d.getFullYear() === n.getFullYear() ? { month: 'short', day: 'numeric' } : { month: 'short', day: 'numeric', year: 'numeric' });
}
const store = { get(k) { try { return localStorage.getItem(k); } catch { return null; } }, set(k, v) { try { localStorage.setItem(k, v); } catch { /* private window */ } } };
// Fenced snippets are rendered as inert text, never HTML or executable content.
function richChatBody(value) {
  const source = String(value ?? '');
  const fence = /```([a-zA-Z0-9_+-]{0,24})[ \t]*\n([\s\S]*?)```/g;
  let out = '', last = 0, match;
  const plain = (text) => esc(text).replace(/\r\n?/g, '\n').replace(/\n/g, '<br>');
  while ((match = fence.exec(source))) {
    out += plain(source.slice(last, match.index));
    const language = match[1].toLowerCase(), code = match[2].replace(/\n$/, '');
    out += '<div class="rich-code"><div class="rich-code-head"><span>' + esc(language || 'Code') +
      '</span><button type="button" class="btn small" data-copy-code="' + esc(code) +
      '">Copy code</button></div><pre><code>' + esc(code) + '</code></pre></div>';
    last = fence.lastIndex;
  }
  return out + plain(source.slice(last));
}
function insertChatCode(textarea, language = 'lua') {
  if (!textarea) return;
  const start = textarea.selectionStart ?? textarea.value.length, end = textarea.selectionEnd ?? start;
  const selected = textarea.value.slice(start, end);
  textarea.setRangeText('```' + language + '\n' + selected + '\n```', start, end, 'select');
  if (!selected) { const caret = start + language.length + 4; textarea.setSelectionRange(caret, caret); }
  textarea.focus();
}

// Everything the panel needs from the page: call(op, args), me() (your account), toast(text),
// and onLive(fn) (live pings from the server).
let api = null;
let inbox = { chats: [], invites: [], dnd: false, relay: false, bridge: false };
let friends = [];
const panels = new Set();   // the drawn panels (the docked one, and #/chat's)
let ringing = null;         // the call ringing for us: {chat, title, by}
let session = null;         // the call we're in (CallSession)
let inboxTimer = null;

export function setupChat(options) {
  api = options;
  api.onLive((topic, event) => {
    if (event === 'chat' || event === 'call' || event === 'friends') refresh();
    if (event === 'call' && session) session.poll();
    if (event === 'chat') panels.forEach((p) => p.chatId && p.loadChat(true));
  });
  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'visible') refresh(); });
  inboxTimer = setInterval(() => { if (document.visibilityState === 'visible') refresh(); }, 15000);
  window.addEventListener('beforeunload', () => { if (session) session.leave(true); });
}
const signedIn = () => { const m = api && api.me(); return !!(m && m.userId > 0 && !m.ban); };

let refreshing = null;
export async function refresh() {
  if (!signedIn()) { inbox = { chats: [], invites: [], dnd: false }; panels.forEach((p) => p.draw()); updateDock(); return; }
  if (refreshing) return refreshing;
  refreshing = (async () => {
    const [r, f] = await Promise.all([api.call('chat.inbox', {}), friends.length ? null : api.call('friends.list', {})]);
    if (r.ok) inbox = r;
    else if (/doesn't know how to do/.test(r.error || '')) inbox = await oldInbox();   // (a server that hasn't been updated yet)
    if (f && f.ok) friends = f.friends || [];
    // A call ringing for us? (Not on Do Not Disturb, and not one we're already in.)
    const ring = inbox.chats.find((c) => c.call && c.call.ringing && (!session || session.chat !== c.id));
    ringing = ring && !inbox.dnd ? { chat: ring.id, title: titleOf(ring), by: (ring.call.members[0] || {}).name || 'Someone' } : null;
    panels.forEach((p) => p.draw());
    updateDock();
    drawRing();
  })().finally(() => { refreshing = null; });
  return refreshing;
}
// Servers from before the chat panel only have party.list: build the same summary here,
// so chats still show up. Calls and Do Not Disturb need the newer server (old: true hides them).
async function oldInbox() {
  const r = await api.call('party.list', {});
  if (!r.ok) return inbox;
  const meId = api.me() && api.me().id;
  const chats = (r.chats || []).filter((c) => (c.members || []).some((m) => m.id === meId)).map((c) => {
    const msgs = c.messages || [], last = msgs[msgs.length - 1] || null, readAt = (c.readAt || {})[meId] || 0;
    let unread = 0;
    for (let i = msgs.length - 1; i >= 0 && unread < 99; i--) { if (msgs[i].at <= readAt) break; if (msgs[i].from !== meId) unread++; }
    return { id: c.id, title: c.title, owner: c.owner, members: c.members || [], updated: c.updated || c.created, unread, call: null, bridge: null,
      last: last ? { name: last.name, body: last.body, at: last.at, system: !!last.system, mine: last.from === meId } : null };
  }).sort((a, b) => b.updated - a.updated);
  return { chats, invites: r.invites || [], dnd: false, relay: false, bridge: false, old: true };
}
async function refreshFriends() { const f = await api.call('friends.list', {}); if (f.ok) friends = f.friends || []; }

function titleOf(c) {
  const meId = api.me() && api.me().id;
  if (c.members && c.members.length === 2 && !c.bridge) {
    const other = c.members.find((m) => m.id !== meId);
    if (other) return other.name;
  }
  return c.title || 'Chat';
}
function otherOf(c) {
  const meId = api.me() && api.me().id;
  return c.members && c.members.length === 2 ? c.members.find((m) => m.id !== meId) : null;
}
function avatar(c) {
  const other = otherOf(c), name = titleOf(c);
  const dot = other ? (other.dnd ? '<i class="cd-dot dnd" title="Do Not Disturb"></i>' : other.online ? '<i class="cd-dot on" title="Online"></i>' : '') : '';
  const group = c.members && c.members.length > 2;
  return '<span class="cd-av" style="background:' + colorOf(c.id) + '">' + esc(group ? (c.members.length) : name.slice(0, 1).toUpperCase()) + dot + '</span>';
}

// --- One panel (docked, or on the #/chat page) ----------------------------------------
class Panel {
  constructor(root, opts = {}) {
    this.root = root;
    this.page = !!opts.page;     // the #/chat page (a phone): no collapsing
    this.view = 'list';          // list | new | chat | info
    this.chatId = opts.chat || '';
    this.chat = null;            // the open chat, with its messages (party.read)
    this.query = '';
    if (this.chatId) this.view = 'chat';
    panels.add(this);
    root.addEventListener('click', (e) => this.click(e));
    root.addEventListener('submit', (e) => this.submit(e));
    root.addEventListener('input', (e) => { if (e.target.name === 'q') { this.query = e.target.value; this.drawList(true); } });
    this.draw();
    if (this.chatId) this.loadChat();
  }
  destroy() { panels.delete(this); clearInterval(this.timer); }
  draw() {
    if (!signedIn()) {
      this.root.innerHTML = '<div class="cd-empty">Log in to chat with your friends.<br><a class="btn blue small" href="#/login">Log in</a></div>';
      return;
    }
    if (this.view === 'chat') return this.drawChat();
    if (this.view === 'new') return this.drawNew();
    if (this.view === 'info') return this.drawInfo();
    this.drawList();
  }
  head(title, left = '', right = '') {
    return '<div class="cd-head">' + left + '<b class="cd-title">' + title + '</b>' + right + '</div>';
  }
  drawList(onlyRows = false) {
    const q = this.query.trim().toLowerCase();
    const chats = inbox.chats.filter((c) => !q || titleOf(c).toLowerCase().includes(q) || (c.members || []).some((m) => (m.name || '').toLowerCase().includes(q)));
    // Friends you haven't got a chat with yet, when searching.
    const withChat = new Set(inbox.chats.filter((c) => c.members.length === 2).flatMap((c) => c.members.map((m) => m.id)));
    const newOnes = q ? friends.filter((f) => !withChat.has(f.id) && (f.username || f.name || '').toLowerCase().includes(q)).slice(0, 5) : [];
    const rows = chats.map((c) => {
      const last = c.last;
      const preview = !last ? 'Say hi!' : last.system ? last.body : (last.mine ? 'You: ' : c.members.length > 2 || last.via ? last.name + ': ' : '') + last.body;
      return '<button type="button" class="cd-row' + (c.unread ? ' unread' : '') + '" data-open="' + esc(c.id) + '">' + avatar(c) +
        '<span class="cd-copy"><span class="cd-line"><b>' + esc(titleOf(c)) + '</b>' + (c.call ? '<span class="cd-live" title="Call going on">' + ICON.phone + '</span>' : '') +
        '<small>' + esc(when(c.last ? c.last.at : c.updated)) + '</small></span><span class="cd-line"><span class="cd-prev">' + esc(preview) + '</span>' +
        (c.unread ? '<span class="cd-badge">' + c.unread + '</span>' : '') + '</span></span></button>';
    }).join('') + newOnes.map((f) => '<button type="button" class="cd-row" data-dm="' + esc(f.id) + '"><span class="cd-av" style="background:' + colorOf(f.id) + '">' +
      esc((f.username || f.name || '?').slice(0, 1).toUpperCase()) + '</span><span class="cd-copy"><span class="cd-line"><b>' + esc(f.username || f.name) +
      '</b></span><span class="cd-line"><span class="cd-prev">Start a chat</span></span></span></button>').join('');
    const invites = inbox.invites.map((x) => '<div class="cd-invite"><span><b>' + esc(x.title) + '</b><small>' + esc(x.ownerName) + ' invited you</small></span>' +
      '<button type="button" class="btn small green" data-accept="' + esc(x.id) + '">Join</button><button type="button" class="btn small" data-decline="' + esc(x.id) + '">No</button></div>').join('');
    const list = invites + (rows || '<p class="cd-none">' + (q ? 'Nobody by that name.' : 'No chats yet. Press the pencil to start one.') + '</p>');
    if (onlyRows && this.root.querySelector('.cd-list')) { this.root.querySelector('.cd-list').innerHTML = list; return; }
    this.root.innerHTML = this.head('Chat', '', (inbox.old ? '' : '<button type="button" class="cd-dnd' + (inbox.dnd ? ' on' : '') + '" data-dnd title="Do Not Disturb: no calls ring, and friends see you\'re busy">' +
        (inbox.dnd ? 'Do Not Disturb' : 'Available') + '</button>') + '<button type="button" class="cd-icon" data-new title="New chat" aria-label="New chat">' + ICON.pencil + '</button>' +
        (this.page ? '' : '<button type="button" class="cd-icon" data-collapse title="Close" aria-label="Close chat">' + ICON.chev + '</button>')) +
      callBar() + '<label class="cd-search">' + ICON.search + '<input name="q" type="search" placeholder="Search for friends" autocomplete="off" value="' + esc(this.query) + '"></label>' +
      '<div class="cd-list">' + list + '</div>';
  }
  drawNew() {
    const rows = friends.map((f) => '<label class="cd-pick"><input type="checkbox" name="members" value="' + esc(f.id) + '"><span class="cd-av small" style="background:' + colorOf(f.id) + '">' +
      esc((f.username || f.name || '?').slice(0, 1).toUpperCase()) + '</span>' + esc(f.username || f.name) + '</label>').join('');
    this.root.innerHTML = this.head('New chat', '<button type="button" class="cd-icon" data-back aria-label="Back">' + ICON.back + '</button>') +
      (friends.length ? '<form class="cd-new" data-create><input name="title" maxlength="60" placeholder="Party name (optional)"><div class="cd-picks">' + rows + '</div>' +
        '<p class="cd-note">Pick one friend for a private chat, or a few for a party (up to 19).</p><button class="btn green">Start chat</button></form>'
        : '<p class="cd-none">Add some friends first, then you can chat with them.</p>');
  }
  async loadChat(quiet = false) {
    if (!this.chatId) return;
    const r = await api.call('party.read', { id: this.chatId });
    if (!r.ok) { if (!quiet) api.toast(r.error); this.view = 'list'; this.chatId = ''; this.draw(); return; }
    const box = this.root.querySelector('.cd-msgs');
    const atBottom = !box || box.scrollHeight - box.scrollTop - box.clientHeight < 60;
    const draft = this.root.querySelector('textarea[name=body]');
    const text = draft ? draft.value : '', focused = draft && document.activeElement === draft;
    this.chat = r.chat;
    if (this.view === 'chat') this.drawChat();
    const t = this.root.querySelector('textarea[name=body]');
    if (t && text) t.value = text;
    if (t && focused) t.focus();
    const b = this.root.querySelector('.cd-msgs');
    if (b && atBottom) b.scrollTop = b.scrollHeight;
    const row = inbox.chats.find((c) => c.id === this.chatId);
    if (row && row.unread) { row.unread = 0; updateDock(); }
  }
  drawChat() {
    const c = this.chat, sum = inbox.chats.find((x) => x.id === this.chatId) || c;
    if (!c) { this.root.innerHTML = this.head('...', '<button type="button" class="cd-icon" data-back aria-label="Back">' + ICON.back + '</button>') + '<p class="cd-none">Loading...</p>'; return; }
    const meId = api.me().id;
    const msgs = (c.messages || []).map((m) => m.system ? '<div class="cd-sys">' + richChatBody(m.body) + ' · ' + esc(when(m.at)) + '</div>'
      : '<div class="cd-msg' + (m.from === meId ? ' mine' : '') + '"><div class="cd-meta">' + (m.from === meId ? '' : '<b>' + esc(m.name || 'Player') + '</b>') +
        (m.via === 'lyreto' ? '<span class="cd-via" title="Sent from Lyreto">Lyreto</span>' : '') + '<span>' + esc(when(m.at)) + '</span></div><div class="cd-bubble">' + esc(m.body) +
        (m.game ? ' <a class="btn small blue" href="#/game/' + encodeURIComponent(m.game) + '">View game</a>' : '') + '</div></div>').join('');
    const call = sum && sum.call, inThis = session && session.chat === c.id;
    let banner = '';
    if (inThis) banner = callBar();
    else if (call && call.ringing) banner = '<div class="cd-callnote">' + esc(call.members.map((m) => m.name).join(', ')) + ' ' + (call.members.length === 1 ? 'is' : 'are') + ' calling <button type="button" class="btn small green" data-join>Answer</button> <button type="button" class="btn small" data-declinecall>Decline</button></div>';
    else if (call) banner = '<div class="cd-callnote">Call going on with ' + esc(call.members.map((m) => m.name).join(', ')) + ' <button type="button" class="btn small green" data-join>Join</button></div>';
    this.root.innerHTML = this.head(esc(titleOf(sum || c)), '<button type="button" class="cd-icon" data-back aria-label="Back">' + ICON.back + '</button>',
        (inThis || inbox.old ? '' : '<button type="button" class="cd-icon" data-call title="Voice call" aria-label="Voice call">' + ICON.phone + '</button>') +
        '<button type="button" class="cd-icon" data-info title="Chat details" aria-label="Chat details">' + ICON.more + '</button>') + banner +
      (c.bridge ? '<div class="cd-bridge">Linked to Lyreto: messages here are shared with the channel "' + esc(c.bridge.name) + '".</div>' : '') +
      '<div class="cd-msgs">' + (msgs || '<p class="cd-none">No messages yet. Say hi!</p>') + '</div>' +
      '<form class="cd-compose" data-send><textarea name="body" maxlength="1000" rows="1" placeholder="Message..." required></textarea><div class="cd-compose-actions"><button type="button" class="btn small" data-code-insert="lua">Code</button><button class="btn blue">Send</button></div></form>';
    const b = this.root.querySelector('.cd-msgs');
    if (b) b.scrollTop = b.scrollHeight;
  }
  drawInfo() {
    const c = this.chat;
    if (!c) { this.view = 'chat'; this.draw(); return; }
    const meId = api.me().id, owner = c.owner === meId;
    const inChat = new Set(c.members.map((m) => m.id));
    const others = friends.filter((f) => !inChat.has(f.id));
    const members = c.members.map((m) => '<div class="cd-member"><a href="#/user/' + esc(m.userId || m.id) + '">' + esc(m.name || 'Player') + '</a>' +
      (m.id === c.owner ? ' <small>Owner</small>' : '') + (owner && m.id !== meId ? '<button type="button" class="btn small red" data-kick="' + esc(m.id) + '">Remove</button>' : '') + '</div>').join('');
    this.root.innerHTML = this.head('Chat details', '<button type="button" class="cd-icon" data-back-chat aria-label="Back">' + ICON.back + '</button>') +
      '<div class="cd-info"><h4>' + c.members.length + ' in this chat</h4>' + members +
      (owner && others.length ? '<form class="cd-row-form" data-invite><select name="user">' + others.map((f) => '<option value="' + esc(f.id) + '">' + esc(f.username || f.name) + '</option>').join('') +
        '</select><button class="btn small green">Invite</button></form>' : '') +
      (owner ? '<form class="cd-row-form" data-rename><input name="title" maxlength="60" value="' + esc(c.title) + '"><button class="btn small">Rename</button></form>' : '') +
      '<h4>Lyreto</h4>' + (c.bridge
        ? '<p class="cd-note">Linked to the Lyreto channel "' + esc(c.bridge.name) + '". Lyreto users there see this chat and can talk in it.</p>' + (owner ? '<button type="button" class="btn small" data-unbridge>Unlink from Lyreto</button>' : '')
        : owner ? (inbox.bridge ? '<p class="cd-note">Link this chat to a Lyreto channel, so your friends on Lyreto can talk here too. You get a code to type into Lyreto\'s Guts&amp;Bolts bridge in that channel.</p><button type="button" class="btn small blue" data-bridge>Get a Lyreto code</button><div class="cd-code"></div>'
          : '<p class="cd-note">Linking chats to Lyreto is coming soon.</p>') : '<p class="cd-note">Not linked to Lyreto.</p>') +
      '<h4>Leave</h4><button type="button" class="btn small red" data-leave>Leave this chat</button></div>';
  }
  async click(e) {
    const el = e.target.closest('button, [data-open]');
    if (!el || !this.root.contains(el)) return;
    const d = el.dataset;
    if ('copyCode' in d) {
      try { await navigator.clipboard.writeText(d.copyCode || ''); el.textContent = 'Copied'; }
      catch { api.toast('Clipboard access is unavailable in this browser.'); }
      return;
    }
    if ('codeInsert' in d) { insertChatCode(this.root.querySelector('.cd-compose textarea[name="body"]'), d.codeInsert || 'lua'); return; }
    if ('collapse' in d) { setDockOpen(false); return; }
    if ('dnd' in d) { const r = await api.call('presence.set', { dnd: !inbox.dnd }); if (r.ok) { inbox.dnd = !inbox.dnd; api.toast(inbox.dnd ? 'Do Not Disturb is on: calls won\'t ring.' : 'You\'re available again.'); refresh(); } else api.toast(r.error); return; }
    if ('new' in d) { await refreshFriends(); this.view = 'new'; this.draw(); return; }
    if ('back' in d) { this.view = 'list'; this.chatId = ''; this.chat = null; if (this.page) history.replaceState(null, '', '#/chat'); this.draw(); return; }
    if ('backChat' in d) { this.view = 'chat'; this.draw(); return; }
    if ('open' in d) { this.openChat(d.open); return; }
    if ('dm' in d) { const r = await api.call('party.create', { members: [d.dm] }); if (!r.ok) api.toast(r.error); else { api.toast('Chat started. They join when they accept.'); await refresh(); this.openChat(r.chat.id); } return; }
    if ('accept' in d || 'decline' in d) {
      const r = await api.call('party.respond', { id: d.accept || d.decline, accept: !!d.accept });
      if (!r.ok) api.toast(r.error); else { await refresh(); if (r.chat) this.openChat(r.chat.id); }
      return;
    }
    if ('info' in d) { await refreshFriends(); this.view = 'info'; this.draw(); return; }
    if ('call' in d || 'join' in d) { startCall(this.chatId, titleOf(inbox.chats.find((x) => x.id === this.chatId) || this.chat)); return; }
    if ('declinecall' in d) { await api.call('call.decline', { id: this.chatId }); refresh(); return; }
    if ('hangup' in d) { if (session) session.leave(); return; }
    if ('mute' in d) { if (session) session.toggleMute(); return; }
    if ('kick' in d) { if (!confirm('Remove this person from the chat?')) return; const r = await api.call('party.kick', { id: this.chatId, user: d.kick }); api.toast(r.ok ? 'Removed.' : r.error); await this.loadChat(); this.view = 'info'; this.draw(); return; }
    if ('leave' in d) {
      if (!confirm('Leave this chat?')) return;
      if (session && session.chat === this.chatId) session.leave();
      const r = await api.call('party.leave', { id: this.chatId });
      if (!r.ok) { api.toast(r.error); return; }
      this.view = 'list'; this.chatId = ''; this.chat = null; refresh(); return;
    }
    if ('bridge' in d) {
      const r = await api.call('chat.bridge', { id: this.chatId });
      const box = this.root.querySelector('.cd-code');
      if (!r.ok) { api.toast(r.error); return; }
      box.innerHTML = '<p>Your code: <b class="cd-codetext">' + esc(r.code) + '</b></p><p class="cd-note">Type it into Lyreto\'s Guts&amp;Bolts bridge in the channel you want to link, within ' + r.minutes + ' minutes. Only you can see it.</p>';
      return;
    }
    if ('unbridge' in d) { if (!confirm('Unlink this chat from Lyreto?')) return; const r = await api.call('chat.unbridge', { id: this.chatId }); if (!r.ok) api.toast(r.error); await this.loadChat(); this.view = 'info'; this.draw(); return; }
  }
  async submit(e) {
    const f = e.target;
    e.preventDefault();
    if ('send' in f.dataset) {
      const body = f.body.value.trim();
      if (!body) return;
      f.body.value = '';
      const r = await api.call('party.send', { id: this.chatId, body });
      if (!r.ok) { api.toast(r.error); f.body.value = body; return; }
      this.chat = r.chat; this.drawChat(); refresh();
      return;
    }
    if ('create' in f.dataset) {
      const members = [...f.querySelectorAll('input[name=members]:checked')].map((x) => x.value);
      if (!members.length) { api.toast('Pick at least one friend.'); return; }
      const r = await api.call('party.create', { title: f.title.value, members });
      if (!r.ok) { api.toast(r.error); return; }
      api.toast('Chat started. Your friends join when they accept.');
      await refresh(); this.openChat(r.chat.id);
      return;
    }
    if ('invite' in f.dataset) { const r = await api.call('party.invite', { id: this.chatId, user: f.user.value }); api.toast(r.ok ? 'Invitation sent.' : r.error); return; }
    if ('rename' in f.dataset) { const r = await api.call('party.rename', { id: this.chatId, title: f.title.value }); api.toast(r.ok ? 'Renamed.' : r.error); if (r.ok) { await this.loadChat(); refresh(); } }
  }
  openChat(id) {
    this.chatId = id; this.chat = null; this.view = 'chat';
    if (this.page && location.hash !== '#/chat/' + id) history.replaceState(null, '', '#/chat/' + encodeURIComponent(id));
    this.draw(); this.loadChat();
    clearInterval(this.timer);
    this.timer = setInterval(() => { if (this.view === 'chat' && document.visibilityState === 'visible' && this.root.isConnected) this.loadChat(true); }, 8000);
  }
}

// --- The docked panel (computers) --------------------------------------------------------
let dock = null, dockPanel = null;
const dockOpen = () => store.get('gb-chat-open') === '1';
function setDockOpen(open) {
  store.set('gb-chat-open', open ? '1' : '0');
  if (!dock) return;
  dock.classList.toggle('open', open);
  if (open && !dockPanel) dockPanel = new Panel(dock.querySelector('.cd-body'));
  if (open) refresh();
}
export function mountDock() {
  if (dock) return;
  dock = document.createElement('div');
  dock.className = 'chat-dock';
  dock.innerHTML = '<button type="button" class="cd-tab" data-open-dock><span>Chat</span><span class="cd-badge" hidden></span>' + ICON.chev + '</button><div class="cd-body cd-panel"></div>';
  document.body.append(dock);
  dock.querySelector('[data-open-dock]').addEventListener('click', () => setDockOpen(true));
  setDockOpen(dockOpen());
  updateDock();
}
function updateDock() {
  if (!dock) return;
  dock.hidden = !signedIn() || /^#\/(chat|parties)(\/|$)/.test(location.hash);
  const n = inbox.chats.reduce((s, c) => s + (c.unread || 0), 0) + inbox.invites.length;
  const badge = dock.querySelector('.cd-tab .cd-badge');
  badge.hidden = !n; badge.textContent = n > 99 ? '99+' : String(n);
  const tab = document.querySelector('#nav a[data-page=chat]');
  if (tab) tab.innerHTML = 'Chat' + (n ? ' <span class="unread">' + (n > 99 ? '99+' : n) + '</span>' : '');
}
export function routeChanged() { updateDock(); }

// --- The #/chat page (phones, or anyone who opens it) -----------------------------------
let pagePanel = null;
export function mountChatPage(root, chatId = '') {
  if (pagePanel) pagePanel.destroy();
  root.classList.add('cd-panel', 'cd-page');
  pagePanel = new Panel(root, { page: true, chat: chatId });
  refresh();
  updateDock();
}

// --- Ringing ------------------------------------------------------------------------------
let ringEl = null, ringTone = null;
function drawRing() {
  if (!ringing) { if (ringEl) { ringEl.remove(); ringEl = null; } stopTone(); return; }
  if (!ringEl) {
    ringEl = document.createElement('div');
    ringEl.className = 'cd-ring';
    document.body.append(ringEl);
    ringEl.addEventListener('click', async (e) => {
      const b = e.target.closest('button');
      if (!b || !ringing) return;
      const r = ringing;
      ringing = null; drawRing();
      if (b.dataset.answer !== undefined) startCall(r.chat, r.title);
      else { await api.call('call.decline', { id: r.chat }); refresh(); }
    });
    startTone();
  }
  ringEl.innerHTML = '<span class="cd-ringicon">' + ICON.phone + '</span><span><b>' + esc(ringing.by) + '</b> is calling' + (ringing.title !== ringing.by ? '<small>' + esc(ringing.title) + '</small>' : '') + '</span>' +
    '<button type="button" class="btn green" data-answer>Answer</button><button type="button" class="btn red" data-no>Decline</button>';
}
// A soft two-beep ring made in the browser (no sound files), every 3 seconds.
function startTone() {
  stopTone();
  let ctx;
  try { ctx = new (window.AudioContext || window.webkitAudioContext)(); } catch { return; }
  const beep = () => {
    for (const at of [0, 0.35]) {
      const o = ctx.createOscillator(), g = ctx.createGain();
      o.frequency.value = 660; g.gain.value = 0.0001;
      o.connect(g); g.connect(ctx.destination);
      const t = ctx.currentTime + at;
      g.gain.exponentialRampToValueAtTime(0.12, t + 0.02); g.gain.exponentialRampToValueAtTime(0.0001, t + 0.3);
      o.start(t); o.stop(t + 0.32);
    }
  };
  beep();
  ringTone = { ctx, timer: setInterval(beep, 3000) };
}
function stopTone() { if (ringTone) { clearInterval(ringTone.timer); ringTone.ctx.close().catch(() => {}); ringTone = null; } }

// --- Calls ----------------------------------------------------------------------------
function callBar() {
  if (!session) return '';
  const secs = Math.floor((Date.now() - session.since) / 1000);
  // Everyone else in the call, and how our connection to each is going.
  const meId = api.me() && api.me().id;
  const ids = [...new Set([...(session.nameOf ? session.nameOf.keys() : []), ...session.peers.keys()])].filter((id) => id !== meId);
  const who = ids.length ? ids.map((id) => {
    const p = session.peers.get(id), state = p ? p.state : 'connecting';
    return esc((p && p.name !== 'Someone' ? p.name : session.nameOf && session.nameOf.get(id)) || 'Someone') + (state === 'connected' ? '' : ' <small>(' + esc(state) + ')</small>');
  }).join(', ') : 'Waiting for others to join...';
  return '<div class="cd-callbar"><span class="cd-callinfo"><b>' + ICON.phone + ' ' + esc(session.title) + '</b> <small data-calltime>' + Math.floor(secs / 60) + ':' + String(secs % 60).padStart(2, '0') +
    '</small><br><small>' + who + '</small>' + (session.relay ? '' : '<br><small class="cd-warn">Calls connect directly: the people in it could see your internet address (IP).</small>') + '</span>' +
    '<button type="button" class="cd-icon' + (session.muted ? ' off' : '') + '" data-mute title="' + (session.muted ? 'Unmute' : 'Mute') + '" aria-label="Mute">' + ICON.mic + '</button>' +
    '<button type="button" class="btn small red" data-hangup>Hang up</button></div>';
}
function redrawCall() { panels.forEach((p) => { if (p.view === 'list' || p.view === 'chat') p.draw(); }); }

async function startCall(chatId, title) {
  if (session) {
    if (session.chat === chatId) return;
    if (!confirm('Leave your other call?')) return;
    await session.leave();
  }
  if (!navigator.mediaDevices || !window.RTCPeerConnection) { api.toast('This browser can\'t make calls.'); return; }
  const s = new CallSession(chatId, title);
  session = s;
  const ok = await s.begin();
  if (!ok && session === s) session = null;
  refresh();
}

class CallSession {
  constructor(chat, title) {
    this.chat = chat; this.title = title || 'Call';
    this.peers = new Map();   // their account id -> {pc, name, state, audio}
    this.after = 0; this.since = Date.now(); this.muted = false; this.ended = false;
  }
  async begin() {
    const ice = await api.call('call.ice', {});
    if (!ice.ok) { api.toast(ice.error); return false; }
    this.ice = ice.iceServers; this.relay = !!ice.relay;
    try { this.stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true }, video: false }); }
    catch { api.toast('Allow the microphone to make calls.'); return false; }
    const row = inbox.chats.find((c) => c.id === this.chat);
    const r = await api.call(row && row.call ? 'call.join' : 'call.start', { id: this.chat });
    if (!r.ok) { api.toast(r.error); this.stop(); return false; }
    this.names(r.call);
    for (const id of r.peers || []) this.offer(id);   // we call everyone already in
    this.timer = setInterval(() => this.poll(), 1000);
    this.clock = setInterval(() => { const t = document.querySelectorAll('[data-calltime]'); const secs = Math.floor((Date.now() - this.since) / 1000);
      t.forEach((x) => { x.textContent = Math.floor(secs / 60) + ':' + String(secs % 60).padStart(2, '0'); }); }, 1000);
    redrawCall();
    return true;
  }
  names(call) { this.nameOf = new Map((call ? call.members : []).map((m) => [m.id, m.name])); }
  peer(id) {
    let p = this.peers.get(id);
    if (p) return p;
    const pc = new RTCPeerConnection({ iceServers: this.ice, iceTransportPolicy: this.relay ? 'relay' : 'all' });
    for (const track of this.stream.getTracks()) pc.addTrack(track, this.stream);
    const audio = new Audio();
    audio.autoplay = true;
    pc.ontrack = (e) => { audio.srcObject = e.streams[0]; audio.play().catch(() => {}); };
    p = { pc, audio, name: (this.nameOf && this.nameOf.get(id)) || 'Someone', state: 'connecting' };
    pc.onconnectionstatechange = () => {
      p.state = pc.connectionState === 'connected' ? 'connected' : pc.connectionState === 'failed' ? 'couldn\'t connect' : pc.connectionState;
      redrawCall();
    };
    this.peers.set(id, p);
    return p;
  }
  // Wait until the browser has found its ways to be reached (so one note has them all).
  gathered(pc) {
    if (pc.iceGatheringState === 'complete') return Promise.resolve();
    return new Promise((done) => {
      const t = setTimeout(done, 3000);
      pc.addEventListener('icegatheringstatechange', () => { if (pc.iceGatheringState === 'complete') { clearTimeout(t); done(); } });
    });
  }
  async offer(id) {
    const { pc } = this.peer(id);
    await pc.setLocalDescription(await pc.createOffer());
    await this.gathered(pc);
    await api.call('call.signal', { id: this.chat, to: id, kind: 'offer', sdp: pc.localDescription.sdp });
  }
  async poll() {
    if (this.ended || this.polling) return;
    this.polling = true;
    try {
      const r = await api.call('call.poll', { id: this.chat, after: this.after });
      if (!r.ok || !r.call) { if (!this.ended) { api.toast('The call ended.'); this.stop(); refresh(); } return; }
      const before = this.nameOf ? this.nameOf.size : 0;
      this.names(r.call);
      if (this.nameOf.size !== before) redrawCall();
      const here = new Set(r.call.members.map((m) => m.id));
      for (const [id, p] of this.peers) if (!here.has(id)) { p.pc.close(); p.audio.srcObject = null; this.peers.delete(id); redrawCall(); }
      for (const sig of r.signals) {
        this.after = Math.max(this.after, sig.n);
        if (sig.kind === 'offer') {
          const old = this.peers.get(sig.from);
          if (old) { old.pc.close(); this.peers.delete(sig.from); }   // they started again
          const { pc } = this.peer(sig.from);
          await pc.setRemoteDescription({ type: 'offer', sdp: sig.sdp });
          await pc.setLocalDescription(await pc.createAnswer());
          await this.gathered(pc);
          await api.call('call.signal', { id: this.chat, to: sig.from, kind: 'answer', sdp: pc.localDescription.sdp });
        } else if (sig.kind === 'answer') {
          const p = this.peers.get(sig.from);
          if (p && p.pc.signalingState === 'have-local-offer') await p.pc.setRemoteDescription({ type: 'answer', sdp: sig.sdp });
        }
      }
      for (const [id, p] of this.peers) if (p.name === 'Someone' && this.nameOf.get(id)) p.name = this.nameOf.get(id);
    } catch { /* a bad note: the next poll carries on */ } finally { this.polling = false; }
  }
  toggleMute() {
    this.muted = !this.muted;
    for (const t of this.stream.getAudioTracks()) t.enabled = !this.muted;
    redrawCall();
  }
  stop() {
    this.ended = true;
    clearInterval(this.timer); clearInterval(this.clock);
    for (const p of this.peers.values()) { p.pc.close(); p.audio.srcObject = null; }
    this.peers.clear();
    if (this.stream) for (const t of this.stream.getTracks()) t.stop();
    if (session === this) session = null;
    redrawCall();
  }
  async leave(unloading = false) {
    const chat = this.chat;
    this.stop();
    if (unloading) return;   // (the server drops us after a few quiet seconds)
    await api.call('call.leave', { id: chat });
    refresh();
  }
}
