// Ban lists on the website (worker/banlists.js does the work):
//   mountBanLists(box, call, toast)          Create > Ban lists: make lists, ban people, share, answer appeals, see the log
//   mountGameBans(box, game, call, toast)    a game's Configure page: which lists it uses, and "ban someone from this game"
//   mountBanNotice(box, game, call, toast)   a game's page, for someone who is banned: why, and an appeal box

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const day = (t) => new Date(t * 1000).toLocaleDateString();
const person = (p) => p ? '<a href="#/user/' + esc(p.userId || p.id) + '">' + esc(p.name) + '</a>' : '?';
const until = (e) => e.until ? 'until ' + day(e.until) : 'for good';
const DAYS = [[0, 'For good'], [1, '1 day'], [3, '3 days'], [7, '1 week'], [30, '30 days'], [365, '1 year']];
const daysPick = '<select name="days">' + DAYS.map(([d, l]) => '<option value="' + d + '">' + l + '</option>').join('') + '</select>';
const DID = { create: 'made the list', add: 'banned', change: 'changed the ban on', remove: 'unbanned', share: 'added editor', unshare: 'removed editor',
  rename: 'renamed it to', appeal: 'appealed their ban from', accept: 'accepted the appeal of', deny: 'turned down the appeal of', use: 'used it on', unuse: 'stopped using it on' };

const SAFETY = '<p class="small muted">Ban someone once and they can\'t join any game that uses the list. Lists are private: only you and ' +
  'the editors you add can change them. Every ban needs a reason, keeps who added it, and goes in the log. Banned players see the ' +
  'reason and can appeal, and you answer here.</p>';

export async function mountBanLists(box, call, toast) {
  let open = new URLSearchParams(location.hash.split('?')[1] || '').get('list') || '';
  const draw = async () => {
    const r = await call('banlist.mine', {});
    if (!r.ok) { box.innerHTML = '<h2 class="boxhead">Ban lists</h2><p class="error">' + esc(r.error) + '</p>'; return; }
    const rows = r.lists.map((l) => '<div><span class="grow"><b>' + esc(l.name) + '</b> <span class="small muted">' +
      (l.role === 'owner' ? 'yours' : 'by ' + esc(l.owner.name)) + ' · ' + l.bans + ' banned' +
      (l.games.length ? ' · used on ' + l.games.map((g) => esc(g.name)).join(', ') : ' · not on any game yet') + '</span>' +
      (l.openAppeals ? ' <span class="badge-pill">' + l.openAppeals + ' appeal' + (l.openAppeals === 1 ? '' : 's') + '</span>' : '') +
      '</span><button type="button" class="btn small" data-open="' + esc(l.id) + '">Open</button></div>').join('');
    box.innerHTML = '<h2 class="boxhead">Ban lists</h2>' + SAFETY +
      (rows ? '<div class="list">' + rows + '</div>' : '<p class="muted">No ban lists yet.</p>') +
      '<form class="form row" data-new><input type="text" name="name" maxlength="40" placeholder="Like: Cheaters" required>' +
      '<button class="btn green">Make a list</button></form><div data-detail></div>';
    box.querySelector('[data-new]').addEventListener('submit', async (e) => {
      e.preventDefault();
      const done = await call('banlist.create', { name: e.currentTarget.name.value });
      if (!done.ok) { toast(done.error); return; }
      open = done.list.id;
      draw();
    });
    box.querySelectorAll('[data-open]').forEach((b) => b.addEventListener('click', () => { open = b.dataset.open; detail(); }));
    if (open) detail();
  };
  const detail = async () => {
    const spot = box.querySelector('[data-detail]');
    const r = await call('banlist.get', { id: open });
    if (!r.ok) { spot.innerHTML = '<p class="error">' + esc(r.error) + '</p>'; return; }
    const l = r.list, owner = l.role === 'owner' || l.role === 'staff';
    const appeals = l.appeals.filter((a) => a.status === 'open');
    spot.innerHTML = '<h3>' + esc(l.name) + ' <span class="small muted">(' + esc(l.role) + ')</span></h3>' +
      (appeals.length ? '<h4>Appeals</h4><div class="list">' + appeals.map((a) => '<div><span class="grow">' + person(a.who) +
        ' <span class="small muted">' + day(a.at) + '</span><br>' + esc(a.text) + '</span>' +
        '<input type="text" maxlength="200" placeholder="A note (optional)" data-note="' + esc(a.id) + '" style="max-width:200px">' +
        '<button type="button" class="btn small green" data-answer="' + esc(a.id) + '" data-accept="1">Unban</button>' +
        '<button type="button" class="btn small red" data-answer="' + esc(a.id) + '">Keep ban</button></div>').join('') + '</div>' : '') +
      '<h4>Banned (' + l.entries.length + ')</h4>' +
      (l.entries.length ? '<div class="list">' + l.entries.map((e) => '<div><span class="grow">' + person(e.who) + ' <span class="small muted">' +
        until(e) + ' · by ' + esc(e.by.name) + ' on ' + day(e.at) + '</span><br>' + esc(e.reason) + '</span>' +
        '<button type="button" class="btn small" data-unban="' + esc(e.who.id) + '">Unban</button></div>').join('') + '</div>' : '<p class="muted">Nobody yet.</p>') +
      '<form class="form" data-ban><label>Ban someone <span class="muted small">(their user number or username)</span></label>' +
      '<input type="text" name="who" maxlength="64" required><label>Why <span class="muted small">(they\'ll see this)</span></label>' +
      '<input type="text" name="reason" maxlength="200" required><label>How long</label>' + daysPick +
      '<p><button class="btn red">Ban</button></p></form>' +
      '<h4>Editors</h4><p class="small muted">Creators you trust to ban people on this list. They can use it on their own games too.</p>' +
      (l.editors.length ? '<div class="list">' + l.editors.map((p) => '<div><span class="grow">' + person(p) + '</span>' +
        (owner ? '<button type="button" class="btn small" data-unshare="' + esc(p.id) + '">Remove</button>' : '') + '</div>').join('') + '</div>' : '<p class="muted">Just you.</p>') +
      (owner ? '<form class="form row" data-share><input type="text" name="who" maxlength="64" placeholder="Username or user number" required>' +
        '<button class="btn">Add editor</button></form>' +
        '<p><button type="button" class="btn small" data-rename>Rename</button> <button type="button" class="btn small red" data-delete>Delete list</button></p>' : '') +
      '<h4>Log</h4><div class="small">' + (l.log.map((x) => '<div>' + day(x.at) + ': <b>' + esc(x.by.name) + '</b> ' + esc(DID[x.did] || x.did) +
        (x.who ? ' ' + esc(x.who.name) : '') + (x.note ? ' <span class="muted">(' + esc(x.note) + ')</span>' : '') + '</div>').join('') || '<span class="muted">Nothing yet.</span>') + '</div>';
    const after = (done, msg) => { if (!done.ok) toast(done.error); else { if (msg) toast(msg); draw(); } };
    spot.querySelector('[data-ban]').addEventListener('submit', async (e) => {
      e.preventDefault();
      const f = e.currentTarget;
      after(await call('banlist.add', { id: l.id, who: f.who.value, reason: f.reason.value, days: Number(f.days.value) }), 'Banned.');
    });
    spot.querySelectorAll('[data-unban]').forEach((b) => b.addEventListener('click', async () => after(await call('banlist.remove', { id: l.id, who: b.dataset.unban }), 'Unbanned.')));
    spot.querySelectorAll('[data-answer]').forEach((b) => b.addEventListener('click', async () => {
      const note = spot.querySelector('[data-note="' + b.dataset.answer + '"]').value;
      after(await call('banlist.answer', { id: l.id, appeal: b.dataset.answer, accept: !!b.dataset.accept, note }), 'Answered.');
    }));
    spot.querySelectorAll('[data-unshare]').forEach((b) => b.addEventListener('click', async () => after(await call('banlist.share', { id: l.id, who: b.dataset.unshare, remove: true }))));
    const share = spot.querySelector('[data-share]');
    if (share) share.addEventListener('submit', async (e) => { e.preventDefault(); after(await call('banlist.share', { id: l.id, who: e.currentTarget.who.value }), 'Editor added.'); });
    const rename = spot.querySelector('[data-rename]');
    if (rename) rename.addEventListener('click', async () => { const n = prompt('New name for the list', l.name); if (n) after(await call('banlist.rename', { id: l.id, name: n })); });
    const del = spot.querySelector('[data-delete]');
    if (del) del.addEventListener('click', async () => {
      if (!confirm('Delete "' + l.name + '"? Everyone on it can play your games again.')) return;
      open = '';
      after(await call('banlist.delete', { id: l.id }), 'List deleted.');
    });
  };
  draw();
}

export async function mountGameBans(box, game, call, toast) {
  const draw = async () => {
    const [used, mine] = await Promise.all([call('game.banLists', { game }), call('banlist.mine', {})]);
    if (!used.ok || !mine.ok) { box.innerHTML = '<h2 class="boxhead">Bans</h2><p class="error">' + esc((used.ok ? mine : used).error) + '</p>'; return; }
    const on = new Set(used.lists.map((l) => l.id));
    const all = [...used.lists, ...mine.lists.filter((l) => !on.has(l.id))];
    box.innerHTML = '<h2 class="boxhead">Bans</h2><p class="small muted">Keep people out of this game. Ban lists work across all your games: ' +
      'tick the ones this game uses. <a href="#/create/bans">Manage your ban lists</a>.</p>' +
      (all.length ? all.map((l) => '<label class="choice"><input type="checkbox" data-use="' + esc(l.id) + '"' + (on.has(l.id) ? ' checked' : '') + '> ' +
        esc(l.name) + ' <span class="small muted">(' + l.bans + ' banned)</span></label>').join('') : '<p class="muted">You have no ban lists yet. Banning someone below makes one.</p>') +
      '<form class="form" data-ban><label>Ban someone from this game <span class="muted small">(user number or username)</span></label>' +
      '<input type="text" name="who" maxlength="64" required><label>Why <span class="muted small">(they\'ll see this)</span></label>' +
      '<input type="text" name="reason" maxlength="200" required><label>How long</label>' + daysPick +
      '<p><button class="btn red">Ban</button></p></form>';
    box.querySelectorAll('[data-use]').forEach((c) => c.addEventListener('change', async () => {
      const lists = [...box.querySelectorAll('[data-use]')].filter((x) => x.checked).map((x) => x.dataset.use);
      const done = await call('game.banLists', { game, lists });
      if (!done.ok) { toast(done.error); c.checked = !c.checked; } else toast('Saved.');
    }));
    box.querySelector('[data-ban]').addEventListener('submit', async (e) => {
      e.preventDefault();
      const f = e.currentTarget;
      const done = await call('game.ban', { game, who: f.who.value, reason: f.reason.value, days: Number(f.days.value) });
      if (!done.ok) { toast(done.error); return; }
      toast('Banned. They can appeal, and you\'ll get a note if they do.');
      draw();
    });
  };
  draw();
}

export async function mountBanNotice(box, game, call, toast) {
  const r = await call('banlist.check', { game });
  if (!r.ok || !r.banned) { box.innerHTML = ''; return; }
  const b = r.ban, a = b.appeal;
  box.innerHTML = '<div class="box warn"><h2 class="boxhead">You\'re banned from this game</h2>' +
    '<p><b>Reason:</b> ' + esc(b.reason) + '</p><p class="small muted">Banned ' + day(b.at) + ', ' + until(b) + ', by ' + esc(b.owner.name) + '\'s list "' + esc(b.list) + '".</p>' +
    (a ? '<p>Your appeal: <b>' + esc(a.status === 'open' ? 'waiting for an answer' : a.status) + '</b>' + (a.note ? ' · "' + esc(a.note) + '"' : '') + '</p>' : '') +
    (b.canAppeal ? '<form class="form" data-appeal><label>Appeal <span class="muted small">(say why you should be let back in)</span></label>' +
      '<textarea name="text" maxlength="500" rows="3" required></textarea><p><button class="btn">Send appeal</button></p></form>' : '') + '</div>';
  const f = box.querySelector('[data-appeal]');
  if (f) f.addEventListener('submit', async (e) => {
    e.preventDefault();
    const done = await call('banlist.appeal', { game, text: f.text.value });
    if (!done.ok) { toast(done.error); return; }
    toast('Appeal sent.');
    mountBanNotice(box, game, call, toast);
  });
}
