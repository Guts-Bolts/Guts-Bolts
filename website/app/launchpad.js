// Launchpad on the website (worker/launchpad.js does the work):
//   mountLaunchpad(box, call, toast, me)        Create > Launchpad: about, apply, your help threads (and staff's list)
//   mountLaunchpadShelf(box, call, cardHtml)     the Games page's Launchpad shelf (hidden when it's empty)
//   launchpadPill(g)                             a small "Launchpad" label for a game ('' when it isn't in it)

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const day = (t) => new Date(t * 1000).toLocaleDateString();
const person = (p) => p ? '<a href="#/user/' + esc(p.userId || p.id) + '">' + esc(p.name) + '</a>' : '?';
const STATUS = { waiting: 'Waiting for staff', accepted: 'In Launchpad', declined: 'Not picked this time', withdrawn: 'Taken back' };

export const launchpadPill = (g) => g && g.launchpad
  ? '<span class="badge-pill" title="Picked for Launchpad, the program where staff help creators build big games">&#128640; Launchpad' +
    (g.launchpad === 'graduated' ? ' graduate' : '') + '</span>' : '';

const ABOUT = '<p><b>Launchpad</b> is a free program for creators making something big: a game with real ideas, ' +
  'tricky multiplayer, lots of systems. Apply with your game, and if staff pick it, you get:</p><ul>' +
  '<li>A spot on the <b>Launchpad shelf</b> on the Games page, and a Launchpad label on your game.</li>' +
  '<li>A <b>help thread with staff</b>: ask about multiplayer, speed, stopping cheaters, design, art or getting players.</li>' +
  '<li><b>More Fyxa cloud steps</b> a day while you\'re in it, so the AI can help you build.</li></ul>' +
  '<p class="small muted">It runs in two stages: <b>Build and test</b> (make it work and test it with people), then ' +
  '<b>Launch and grow</b> (make it public and get players). It costs nothing and doesn\'t pay money: it\'s help, not a grant.</p>';

const threadHtml = (app) => '<div class="lp-thread">' + ((app.notes || []).length ? app.notes.map((n) =>
  '<div class="lp-note' + (n.staff ? ' lp-staff' : '') + '"><b>' + person(n.by) + '</b>' + (n.staff ? ' <span class="badge-pill">Staff</span>' : '') +
  ' <span class="small muted">' + day(n.at) + '</span><p>' + esc(n.text).replace(/\n/g, '<br>') + '</p></div>').join('')
  : '<p class="muted small">No messages yet.</p>') + '</div>' +
  (app.status === 'accepted' || app.status === 'waiting' ? '<form class="form" data-note="' + esc(app.id) + '"><textarea name="text" maxlength="1000" rows="2" ' +
    'placeholder="Ask staff something, or tell them how it\'s going" required></textarea><p><button class="btn small green">Send</button></p></form>' : '');

const appHtml = (app, staff, stages) => '<div class="box lp-app"><h3><a href="#/game/' + esc(app.game.id) + '">' + esc(app.game.name) + '</a> ' +
  '<span class="badge-pill">' + esc(STATUS[app.status] || app.status) + '</span>' +
  (app.stage ? ' <span class="badge-pill">' + esc(stages[app.stage] || app.stage) + '</span>' : '') + '</h3>' +
  '<p class="small muted">' + (staff ? 'By ' + person(app.by) + ', ' : '') + esc(app.round) + ', sent ' + day(app.at) + '</p>' +
  '<p>' + esc(app.pitch).replace(/\n/g, '<br>') + '</p>' +
  (app.note ? '<p class="small"><b>Staff said:</b> ' + esc(app.note) + '</p>' : '') +
  (staff && app.status === 'waiting' ? '<form class="form row" data-decide="' + esc(app.id) + '"><input type="text" name="note" maxlength="300" placeholder="A note for them (optional)">' +
    '<button class="btn small green" name="yes" value="1">Pick it</button> <button class="btn small red" name="no" value="1">Not this time</button></form>' : '') +
  (staff && app.status === 'accepted' ? '<p>Move to: ' + Object.keys(stages).map((k) => '<button type="button" class="btn small" data-stage="' + esc(k) + '" data-id="' +
    esc(app.id) + '"' + (app.stage === k ? ' disabled' : '') + '>' + esc(stages[k]) + '</button>').join(' ') + '</p>' : '') +
  (!staff && app.status === 'waiting' ? '<p><button type="button" class="btn small" data-withdraw="' + esc(app.id) + '">Take back my application</button></p>' : '') +
  (app.notes ? '<h4>Help thread</h4>' + threadHtml(app) : '') + '</div>';

export async function mountLaunchpad(box, call, toast, me) {
  if (!box) return;
  const staff = !!(me && me.staff);
  const draw = async () => {
    const [info, games, all] = await Promise.all([call('launchpad.info', {}), me ? call('list', { creator: me.id, kind: 'game', limit: 100 }) : { ok: false },
      staff ? call('launchpad.list', {}) : Promise.resolve(null)]);
    if (!info.ok) { box.innerHTML = '<h2 class="boxhead">Launchpad</h2>' + ABOUT + '<p class="muted">' + esc(info.error) + '</p>'; return; }
    const r = info.round;
    const mineGames = games.ok ? games.assets.filter((g) => g.access === 'public' || g.access === 'testers') : [];
    const helpPicks = Object.entries(info.help).map(([k, l]) => '<label class="choice"><input type="checkbox" name="help" value="' + esc(k) + '"> ' + esc(l) + '</label>').join('');
    const apply = !r.open ? '<p class="box gold small">Launchpad isn\'t taking applications right now. Check back for the next round!</p>'
      : !mineGames.length ? '<p class="muted">Publish a game (public or testers only) to apply with it.</p>'
      : '<form class="form" data-apply><label>Your game</label><select name="game">' +
        mineGames.map((g) => '<option value="' + esc(g.id) + '">' + esc(g.name) + '</option>').join('') + '</select>' +
        '<label>What is it, and what makes it special?</label><textarea name="pitch" maxlength="1500" rows="5" required ' +
        'placeholder="The idea, what works already, what you want to build next, who\'s on your team..."></textarea>' +
        '<label>What do you want help with?</label><div class="genre-picks">' + helpPicks + '</div>' +
        '<p><button class="btn green">Apply to ' + esc(r.name) + '</button>' + (r.closes ? ' <span class="small muted">Closes ' + day(r.closes) + '</span>' : '') + '</p></form>';
    box.innerHTML = '<h2 class="boxhead">&#128640; Launchpad' + (r.name && r.name !== 'Launchpad' ? ': ' + esc(r.name) : '') + '</h2>' + ABOUT +
      (info.perk ? '<p class="box small">You have a game in Launchpad, so you get more Fyxa cloud steps a day.</p>' : '') +
      '<h3>Apply</h3>' + apply +
      '<h3>Your applications</h3>' + (info.mine.length ? info.mine.map((a) => appHtml(a, false, info.stages)).join('') : '<p class="muted">None yet.</p>') +
      (staff && all && all.ok ? '<h3>All applications <span class="small muted">(staff)</span></h3>' +
        '<form class="form row" data-round><input type="text" name="name" maxlength="60" value="' + esc(all.round.name) + '" style="max-width:220px">' +
        '<label class="choice"><input type="checkbox" name="open"' + (all.round.open ? ' checked' : '') + '> Taking applications</label>' +
        '<input type="date" name="closes" value="' + (all.round.closes ? new Date(all.round.closes * 1000).toISOString().slice(0, 10) : '') + '" style="max-width:170px">' +
        '<button class="btn small blue">Save round</button></form>' +
        (all.apps.length ? all.apps.map((a) => appHtml(a, true, info.stages)).join('') : '<p class="muted">No applications yet.</p>') : '');
    const done = (r2, msg) => { if (!r2.ok) { toast(r2.error); return false; } if (msg) toast(msg); draw(); return true; };
    const ap = box.querySelector('[data-apply]');
    if (ap) ap.addEventListener('submit', async (e) => {
      e.preventDefault();
      const help = [...ap.querySelectorAll('input[name=help]:checked')].map((x) => x.value);
      done(await call('launchpad.apply', { game: ap.game.value, pitch: ap.pitch.value, help }), 'Sent! Staff will look at it.');
    });
    box.querySelectorAll('[data-note]').forEach((f) => f.addEventListener('submit', async (e) => {
      e.preventDefault();
      done(await call('launchpad.note', { id: f.dataset.note, text: f.text.value }));
    }));
    box.querySelectorAll('[data-withdraw]').forEach((b) => b.addEventListener('click', async () => done(await call('launchpad.withdraw', { id: b.dataset.withdraw }))));
    box.querySelectorAll('[data-decide]').forEach((f) => f.addEventListener('submit', async (e) => {
      e.preventDefault();
      const accept = !!(e.submitter && e.submitter.name === 'yes');
      done(await call('launchpad.decide', { id: f.dataset.decide, accept, note: f.note.value }), accept ? 'Picked.' : 'Answered.');
    }));
    box.querySelectorAll('[data-stage]').forEach((b) => b.addEventListener('click', async () =>
      done(await call('launchpad.stage', { id: b.dataset.id, stage: b.dataset.stage }), 'Moved.')));
    const rf = box.querySelector('[data-round]');
    if (rf) rf.addEventListener('submit', async (e) => {
      e.preventDefault();
      const closes = rf.closes.value ? Math.floor(new Date(rf.closes.value + 'T23:59:59Z').getTime() / 1000) : 0;
      done(await call('launchpad.round', { name: rf.name.value, open: rf.open.checked, closes }), 'Round saved.');
    });
  };
  draw();
}

export async function mountLaunchpadShelf(box, call, cardHtml) {
  if (!box) return;
  const r = await call('launchpad.games', {});
  if (!r.ok || !r.games.length) { box.hidden = true; return; }
  box.hidden = false;
  box.innerHTML = '<h2 class="boxhead">&#128640; Launchpad <a href="#/create/launchpad" style="float:right">What\'s this?</a></h2>' +
    '<p class="small muted">Ambitious games that staff are helping their creators build.</p>' +
    '<div class="grid">' + r.games.slice(0, 8).map(cardHtml).join('') + '</div>';
}

if (typeof document !== 'undefined' && !document.getElementById('launchpad-style')) {
  const st = document.createElement('style');
  st.id = 'launchpad-style';
  st.textContent = '.lp-thread{border-left:3px solid var(--line);padding-left:10px;margin:6px 0}.lp-note p{margin:2px 0 8px}' +
    '.lp-staff{background:var(--panel2);padding:4px 8px;border-radius:6px}.lp-app h3{margin-top:0}';
  document.head.appendChild(st);
}
