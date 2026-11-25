// Levels, daily streaks, profile walls and showcases on the website (worker/levels.js does the work):
//   levelPill(lv)                                  "Lv 7" next to a name
//   levelBox(lv, self)                             the profile box: level, XP bar, streak, how to earn
//   streakLine(lv)                                 the Bolts page: your streak and what tomorrow is worth
//   mountShowcase(box, user, lv, opts)             the "Showcase" box (pick up to 6 things on your own)
//   mountWall(box, user, lv, opts)                 the profile wall: read, post, delete, who can post

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const fmt = (n) => Number(n || 0).toLocaleString();
function ago(t) {
  const s = Math.max(0, Math.floor(Date.now() / 1000) - t);
  if (s < 60) return 'just now';
  if (s < 3600) return Math.floor(s / 60) + ' min ago';
  if (s < 86400) return Math.floor(s / 3600) + ' h ago';
  return Math.floor(s / 86400) + ' d ago';
}

export const levelPill = (lv) => lv ? '<span class="lv-pill" title="Level ' + lv.level + ' (' + fmt(lv.xp) + ' XP)">Lv ' + lv.level + '</span>' : '';

export function levelBox(lv, self) {
  if (!lv) return '';
  const span = Math.max(1, lv.to - lv.from), pct = lv.max ? 100 : Math.min(100, Math.round(100 * (lv.xp - lv.from) / span));
  return '<div class="box lv-box"><h2 class="boxhead">Level</h2>' +
    '<div class="lv-row"><span class="lv-big">' + lv.level + '</span><div class="lv-grow">' +
    '<div class="lv-bar" role="progressbar" aria-valuemin="0" aria-valuemax="100" aria-valuenow="' + pct + '"><i style="width:' + pct + '%"></i></div>' +
    '<div class="small muted">' + (lv.max ? fmt(lv.xp) + ' XP. Top level!' : fmt(lv.xp - lv.from) + ' / ' + fmt(span) + ' XP to level ' + (lv.level + 1)) + '</div></div></div>' +
    (lv.streak > 0 ? '<p class="small lv-streak">🔥 ' + lv.streak + ' day' + (lv.streak === 1 ? '' : 's') + ' in a row</p>' : '') +
    (self ? '<details class="small lv-how"><summary>How do I get XP?</summary><ul>' +
      '<li>Claim your daily Bolts</li><li>Play games (every 5 minutes)</li><li>Post on the forum, comment, write on walls</li>' +
      '<li>Make things: games, items, art, videos, courses, pastes</li><li>Make friends</li></ul>' +
      '<p class="muted">Each thing only counts a few times a day. Every new level gives you free Bolts.</p></details>' : '') + '</div>';
}

export function streakLine(lv) {
  if (!lv) return '';
  return '<p class="small lv-streak">' + (lv.streak > 0 ? '🔥 ' + lv.streak + ' day streak (best ' + lv.best + '). ' : '') +
    'Next claim: <b>' + lv.nextDaily + ' Bolts</b>. Claim every day in a row for up to 60.</p>';
}

// opts: {self, things: [assets they made or own], card(asset) -> html string, call, toast}
export function mountShowcase(box, user, lv, opts) {
  if (!box || !lv) return;
  const draw = (list) => {
    if (!list.length && !opts.self) { box.innerHTML = ''; return; }
    let h = '<div class="box lv-showcase"><h2 class="boxhead">Showcase</h2>';
    h += list.length ? '<div class="grid">' + list.map(opts.card).join('') + '</div>'
      : '<p class="muted small">Show off your best stuff here: pick up to 6 things you made or own.</p>';
    if (opts.self && opts.things.length) {
      const on = new Set(list.map((a) => a.id));
      h += '<details class="lv-pick"><summary class="btn small">Pick what to show</summary><form data-showcase>' +
        '<div class="lv-choices">' + opts.things.map((a) => '<label><input type="checkbox" name="pick" value="' + esc(a.id) + '"' + (on.has(a.id) ? ' checked' : '') +
          '> ' + esc(a.name) + '</label>').join('') + '</div><p><button class="btn green small">Save</button></p></form></details>';
    }
    box.innerHTML = h + '</div>';
    const form = box.querySelector('[data-showcase]');
    if (form) form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const ids = [...form.querySelectorAll('input[name=pick]:checked')].map((x) => x.value);
      const r = await opts.call('showcase.set', { ids });
      if (!r.ok) { opts.toast(r.error); return; }
      opts.toast('Showcase saved.');
      draw(r.showcase);
    });
  };
  draw(lv.showcase || []);
}

// opts: {self, signedIn, call, toast}
export function mountWall(box, user, lv, opts) {
  if (!box || !lv) return;
  const id = String(user.userId);
  let posts = [], more = false, canPost = lv.canPost, who = lv.wallWho;
  const draw = () => {
    let h = '<div class="box lv-wall"><h2 class="boxhead">Wall</h2>';
    if (canPost) h += '<form class="form" data-wall><textarea name="text" maxlength="300" rows="2" placeholder="' +
      (opts.self ? 'Say something' : 'Write on ' + esc(user.username) + '\'s wall') + '"></textarea><p><button class="btn blue small">Post</button></p></form>';
    else if (!opts.signedIn) h += '<p class="small muted">Sign up to write on walls.</p>';
    else h += '<p class="small muted">' + (who === 'nobody' ? 'This wall is closed.' : 'Only ' + esc(user.username) + '\'s friends can post here.') + '</p>';
    h += posts.length ? '<div class="lv-posts">' + posts.map((p) => '<div class="lv-post"><div class="small"><a href="#/user/' + p.by.userId + '"><b>' + esc(p.by.name) + '</b></a> ' +
      '<span class="muted">' + ago(p.at) + '</span>' + (p.canDelete ? ' <button type="button" class="linkish small" data-del="' + esc(p.id) + '">Delete</button>' : '') +
      '</div><div class="lv-text">' + esc(p.text).replace(/\n/g, '<br>') + '</div></div>').join('') + '</div>'
      : '<p class="muted small">Nothing on the wall yet.</p>';
    if (more) h += '<p><button type="button" class="btn small" data-more>Show more</button></p>';
    if (opts.self) h += '<p class="small muted">Who can post here: <select data-who>' +
      [['friends', 'Friends'], ['everyone', 'Everyone'], ['nobody', 'Nobody']].map(([k, l]) => '<option value="' + k + '"' + (k === who ? ' selected' : '') + '>' + l + '</option>').join('') + '</select></p>';
    box.innerHTML = h + '</div>';
    const form = box.querySelector('[data-wall]');
    if (form) form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const r = await opts.call('wall.post', { id, text: form.text.value });
      if (!r.ok) { opts.toast(r.error); return; }
      posts.unshift(r.post); draw();
    });
    for (const b of box.querySelectorAll('[data-del]')) b.addEventListener('click', async () => {
      const r = await opts.call('wall.delete', { id, post: b.dataset.del });
      if (!r.ok) { opts.toast(r.error); return; }
      posts = posts.filter((p) => p.id !== b.dataset.del); draw();
    });
    const m = box.querySelector('[data-more]');
    if (m) m.addEventListener('click', () => load(posts.length));
    const sel = box.querySelector('[data-who]');
    if (sel) sel.addEventListener('change', async () => {
      const r = await opts.call('wall.who', { who: sel.value });
      if (!r.ok) { opts.toast(r.error); return; }
      who = r.who; opts.toast('Saved.');
    });
  };
  const load = async (offset) => {
    const r = await opts.call('wall.list', { id, offset });
    if (!r.ok) { box.innerHTML = ''; return; }
    posts = offset ? posts.concat(r.posts) : r.posts; more = r.more; canPost = r.canPost; who = r.who;
    draw();
  };
  draw();
  load(0);
}

if (typeof document !== 'undefined' && !document.getElementById('lv-style')) {
  const st = document.createElement('style');
  st.id = 'lv-style';
  st.textContent = '.lv-pill{display:inline-block;vertical-align:middle;margin-left:8px;padding:1px 8px;border-radius:10px;background:var(--red);color:#fff;font-size:13px;font-weight:700}' +
    '.lv-row{display:flex;align-items:center;gap:10px}.lv-big{font-size:34px;font-weight:900;color:var(--red);min-width:44px;text-align:center}.lv-grow{flex:1}' +
    '.lv-bar{height:10px;background:var(--panel2);border:1px solid var(--line);border-radius:5px;overflow:hidden}.lv-bar i{display:block;height:100%;background:var(--red)}' +
    '.lv-streak{margin:6px 0 0}.lv-how ul{margin:4px 0;padding-left:18px}.lv-choices{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:4px;margin:6px 0}' +
    '.lv-choices label{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.lv-pick{margin-top:6px}' +
    '.lv-post{padding:6px 0;border-top:1px solid var(--line)}.lv-text{overflow-wrap:anywhere;margin-top:2px}' +
    '.lv-wall textarea{width:100%;box-sizing:border-box}.lv-wall select{width:auto;display:inline-block}.linkish{background:none;border:0;padding:0;color:var(--link);cursor:pointer}';
  document.head.appendChild(st);
}
