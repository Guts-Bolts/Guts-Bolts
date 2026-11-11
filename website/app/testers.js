// Early testers on the website (worker/testers.js does the work):
//   mountGameTesters(box, game, call, toast)   a game's Configure page: pick testers, read their notes
//   mountTesterNote(box, game, call, toast)    a game's page, for a tester: "you're testing this", leave a note

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const day = (t) => new Date(t * 1000).toLocaleDateString();
const person = (p) => p ? '<a href="#/user/' + esc(p.userId || p.id) + '">' + esc(p.name) + '</a>' : '?';
const stars = (n) => '★'.repeat(n) + '☆'.repeat(5 - n);

const ABOUT = '<p class="small muted">Let a few people play your game before everyone else. Set "Who can play" to ' +
  '<b>Testers only</b>, add them here, and they get a message. Each tester can leave you a note (stars and a few words) ' +
  'that only you can read. When you make the game public, they\'re told it\'s out.</p>';

export async function mountGameTesters(box, game, call, toast) {
  if (!box) return;
  const draw = async () => {
    const r = await call('game.testers', { game });
    if (!r.ok) { box.innerHTML = '<h2 class="boxhead">Early testers</h2><p class="error">' + esc(r.error) + '</p>'; return; }
    const rows = r.testers.map((p) => '<div><span class="grow">' + person(p) +
      (p.noted ? ' <span class="small muted">left a note</span>' : '') + '</span>' +
      '<button type="button" class="btn small" data-remove="' + esc(p.userId || p.id) + '">Take off</button></div>').join('');
    const notes = r.notes.map((n) => '<div class="tester-note"><b>' + stars(n.stars) + '</b> ' + person(n.who) +
      ' <span class="small muted">' + day(n.at) + '</span><p>' + esc(n.text).replace(/\n/g, '<br>') + '</p></div>').join('');
    box.innerHTML = '<h2 class="boxhead">Early testers <span class="small muted">(' + r.testers.length + ' of ' + r.most + ')</span></h2>' + ABOUT +
      (rows ? '<div class="list">' + rows + '</div>' : '<p class="muted">No testers yet.</p>') +
      '<form class="form row" data-add><input type="text" name="who" maxlength="40" placeholder="Name or user number" required>' +
      '<button class="btn green">Add tester</button></form>' +
      '<h3>Notes from testers</h3>' + (notes || '<p class="muted">No notes yet.</p>');
    box.querySelector('[data-add]').addEventListener('submit', async (e) => {
      e.preventDefault();
      const done = await call('game.addTester', { game, who: e.currentTarget.who.value });
      if (!done.ok) { toast(done.error); return; }
      draw();
    });
    box.querySelectorAll('[data-remove]').forEach((b) => b.addEventListener('click', async () => {
      const done = await call('game.removeTester', { game, who: b.dataset.remove });
      if (!done.ok) { toast(done.error); return; }
      draw();
    }));
  };
  draw();
}

export async function mountTesterNote(box, game, call, toast) {
  if (!box) return;
  const r = await call('game.amTester', { game });
  if (!r.ok || !r.tester) { box.hidden = true; return; }
  const n = r.note;
  box.hidden = false;
  box.innerHTML = '<h2 class="boxhead">You\'re an early tester</h2>' +
    '<p class="small muted">The creator picked you to play this before everyone else. Tell them what you think: only they can read it.</p>' +
    '<form class="form" data-note><p class="star-pick">' + [1, 2, 3, 4, 5].map((i) =>
      '<label style="display:inline-block;margin-right:14px;cursor:pointer"><input type="radio" name="stars" value="' + i + '"' +
      (n && n.stars === i ? ' checked' : '') + '> ' + '★'.repeat(i) + '</label>').join('') + '</p>' +
    '<textarea name="text" maxlength="500" rows="3" placeholder="What was fun? What was broken or confusing?" required>' + esc(n ? n.text : '') + '</textarea>' +
    '<p><button class="btn green">' + (n ? 'Update my note' : 'Send my note') + '</button>' +
    (n ? ' <span class="small muted">Sent ' + day(n.at) + '</span>' : '') + '</p></form>';
  box.querySelector('[data-note]').addEventListener('submit', async (e) => {
    e.preventDefault();
    const f = e.currentTarget;
    const picked = f.querySelector('input[name=stars]:checked');
    const done = await call('game.testerNote', { game, stars: picked ? Number(picked.value) : 0, text: f.text.value });
    if (!done.ok) { toast(done.error); return; }
    toast('Thanks! Your note was sent.');
    mountTesterNote(box, game, call, toast);
  });
}
