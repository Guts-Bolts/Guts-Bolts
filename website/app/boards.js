// G&B Boards' pages (worker/boards.js does the work on the server):
//   #/boards             your boards, and a form to start a new one
//   #/boards?public=1    boards people chose to show everyone
//   #/board/<id>         one board: columns of cards. Drag cards between columns (or use the
//                        card's "Move to" box on a phone); click a card to open it.
// app.js hands over its helpers with boardPages(ctx), and adds the pages it returns.

export const BOARD_COLOR_NAMES = { red: 'Red', orange: 'Orange', yellow: 'Yellow', green: 'Green', blue: 'Blue', purple: 'Purple', gray: 'Gray' };
const TEMPLATES = [['blank', 'To do, Doing, Done'], ['game', 'Make a game'], ['bugs', 'Bug tracker'], ['event', 'Plan an event']];   // (worker/boards.js BOARD_TEMPLATES)
const SEES = { private: 'Only people on it', link: 'Anyone with the link can look', public: 'Everyone (in Public boards)' };
const params = () => new URLSearchParams((location.hash.split('?')[1] || ''));
const today = () => new Date().toISOString().slice(0, 10);

// "Due today", "Due in 3 days", "2 days late".
export function dueText(due, now = today()) {
  if (!due) return '';
  const days = Math.round((Date.parse(due + 'T00:00:00Z') - Date.parse(now + 'T00:00:00Z')) / 86400000);
  if (days === 0) return 'Due today';
  if (days === 1) return 'Due tomorrow';
  if (days > 1) return days < 14 ? 'Due in ' + days + ' days' : 'Due ' + due;
  return -days + ' day' + (days === -1 ? '' : 's') + ' late';
}

export function boardPages(ctx) {
  const { html, show, call, pageCall, signedIn, toast, ago, needSignIn, reportLink, verified } = ctx;
  const me = () => ctx.me();
  const person = (p) => html`<a href="#/user/${p.userId || p.id}">${p.name}</a>${verified(p.verified)}`;
  const initials = (p) => html`<span class="board-face" title="${p.name}">${String(p.name || '?').slice(0, 2).toUpperCase()}</span>`;

  const hero = html`<div class="portal-hero boards-hero"><div><span class="portal-kicker">G&amp;B PRODUCTIVITY</span><h1>Boards</h1>
      <p>Plan a game, track bugs, run a group event or keep your homework in order. Put cards in columns, drag them along as
        things get done, and invite friends to work on it with you. Free.</p></div></div>`;

  const tile = (b) => html`<a class="board-tile board-${b.color}" href="#/board/${b.id}">
      <b>${b.title}</b>
      <span class="small">${b.cards} card${b.cards === 1 ? '' : 's'}${b.cards ? ' · ' + b.done + ' done' : ''} · ${b.people} ${b.people === 1 ? 'person' : 'people'}</span>
      <span class="small muted">by ${b.by.name} · changed ${ago(b.edited)}${b.visibility !== 'private' ? ' · ' + (b.visibility === 'public' ? 'Public' : 'Link') : ''}</span></a>`;
  const tiles = (list, empty) => list.length ? html`<div class="board-tiles">${list.map(tile)}</div>` : html`<p class="muted">${empty}</p>`;

  const pages = {};

  pages.boards = async () => {
    const q = params();
    if (q.get('public')) {
      const page = Number(q.get('page') || 1);
      const r = await pageCall('board.list', { page });
      show(html`${hero}<div class="box"><h2 class="boxhead">Public boards</h2>
        <p class="small muted">Boards their owners chose to show everyone: game plans, update roadmaps, event plans. You can look, but only people on a board can change it.</p>
        ${r.ok ? tiles(r.boards, 'No public boards yet.') : html`<p class="error">${r.error}</p>`}
        ${r.ok ? html`<p class="art-pages">${r.page > 1 ? html`<a class="btn small" href="#/boards?public=1&page=${r.page - 1}">&laquo; Newer</a> ` : ''}
          ${r.more ? html`<a class="btn small" href="#/boards?public=1&page=${r.page + 1}">Older &raquo;</a>` : ''}</p>` : ''}</div>`);
      return;
    }
    if (!signedIn()) {
      show(html`${hero}${needSignIn('make boards')}
        <div class="box"><h2 class="boxhead">How it works</h2><ul>
          <li><b>Columns</b> are steps, like "To do", "Doing" and "Done". Make as many as you like and name them anything.</li>
          <li><b>Cards</b> are things to do. Give them notes, a colour label, a due date, a checklist and the people doing them.</li>
          <li><b>Drag</b> a card to the next column when it moves along.</li>
          <li><b>Invite</b> friends by name, so you can plan together. Keep a board private, share its link, or show it to everyone.</li>
        </ul><p><a href="#/boards?public=1">See public boards &raquo;</a></p></div>`);
      return;
    }
    const r = await pageCall('board.mine', {});
    show(html`${hero}
      <div class="box"><h2 class="boxhead">Start a board</h2>
        <form class="row board-new" data-board-new>
          <input type="text" name="title" maxlength="80" placeholder="Name it, like &quot;My racing game&quot;" required style="flex:2;min-width:180px">
          <select name="template" style="flex:1;min-width:150px">${TEMPLATES.map(([k, n]) => html`<option value="${k}">${n}</option>`)}</select>
          <button class="btn green">Make board</button></form></div>
      <div class="box"><h2 class="boxhead">My boards</h2>
        ${r.ok ? tiles(r.boards, 'No boards yet. Make your first one above!') : html`<p class="error">${r.error}</p>`}
        <p class="small"><a href="#/boards?public=1">See public boards &raquo;</a></p></div>`);
    const form = document.querySelector('[data-board-new]');
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const done = await call('board.make', { title: form.title.value, template: form.template.value });
      if (!done.ok) { toast(done.error); return; }
      location.hash = '#/board/' + done.board.id;
    });
  };

  pages.board = async (id) => {
    const r = await pageCall('board.get', { id });
    if (!r.ok) { show(html`<h1>Boards</h1><p class="error">${r.error}</p><p><a href="#/boards">&laquo; Back to Boards</a></p>`); return; }
    let b = r.board;
    const hash = location.hash;

    const cardHtml = (k, col) => {
      const checked = k.checks.filter((x) => x.done).length;
      const late = k.due && !k.done && k.due < today();
      return html`<div class="board-card ${k.done ? 'done' : ''}" data-card="${k.id}" data-col="${col.id}" ${b.canEdit ? 'draggable=true' : ''} tabindex="0">
        ${k.labels.length ? html`<span class="board-labels">${k.labels.map((l) => html`<i class="board-label board-${l}" title="${BOARD_COLOR_NAMES[l]}"></i>`)}</span>` : ''}
        <span class="board-card-title">${k.done ? html`<span class="board-tick">&#10003;</span> ` : ''}${k.title}</span>
        <span class="board-card-bits small">
          ${k.due ? html`<span class="board-due ${late ? 'late' : ''}">${dueText(k.due)}</span>` : ''}
          ${k.text ? html`<span title="Has notes">&#9776;</span>` : ''}
          ${k.checks.length ? html`<span class="${checked === k.checks.length ? 'board-all' : ''}">&#9745; ${checked}/${k.checks.length}</span>` : ''}
          ${k.people.map(initials)}</span></div>`;
    };
    const colHtml = (c, i) => html`<section class="board-col" data-col-id="${c.id}">
        <header>${b.canEdit ? html`<input class="board-col-title" value="${c.title}" maxlength="40" data-rename="${c.id}" aria-label="Column name">` : html`<b>${c.title}</b>`}
          <span class="small muted">${c.cards.length}</span>
          ${b.canEdit ? html`<span class="board-col-tools">
            <button class="btn tiny" data-colmove="${c.id}" data-to="${i - 1}" ${i === 0 ? 'disabled' : ''} title="Move left">&#9664;</button>
            <button class="btn tiny" data-colmove="${c.id}" data-to="${i + 1}" ${i === b.lists.length - 1 ? 'disabled' : ''} title="Move right">&#9654;</button>
            <button class="btn tiny" data-coldel="${c.id}" title="Delete column">&#10005;</button></span>` : ''}</header>
        <div class="board-cards" data-drop="${c.id}">${c.cards.map((k) => cardHtml(k, c))}</div>
        ${b.canEdit ? html`<form class="board-add" data-addcard="${c.id}"><input type="text" name="title" maxlength="120" placeholder="+ Add a card"></form>` : ''}
      </section>`;

    const draw = () => {
      show(html`<p class="small"><a href="#/boards">&laquo; Boards</a></p>
        <div class="board-head board-${b.color}">
          <div><h1>${b.title}</h1>
            ${b.text ? html`<p class="board-text">${b.text}</p>` : ''}
            <p class="small">${b.visibility === 'private' ? 'Private' : b.visibility === 'link' ? 'Anyone with the link can look' : 'Public'}
              · ${b.cards} card${b.cards === 1 ? '' : 's'}, ${b.done} done · owner ${person(b.by)}
              ${b.members.length ? html` · with ${b.members.map((m, i) => html`${i ? ', ' : ''}${person(m)}`)}` : ''}</p></div>
          <div class="board-head-tools">
            ${b.owner ? html`<button class="btn small" data-bd="settings">Settings and people</button>` : ''}
            ${b.canEdit && !b.owner ? html`<button class="btn small" data-bd="leave">Leave board</button>` : ''}
            <button class="btn small" data-bd="link">Copy link</button>
            ${signedIn() && !b.owner && b.visibility !== 'private' ? html`<span class="small">${reportLink('board', b.id, 'the board "' + b.title + '"')}</span>` : ''}
          </div>
        </div>
        ${!b.canEdit ? html`<p class="small muted">You can look at this board. Only the people on it can change it.</p>` : ''}
        <div class="board-settings box" data-settings hidden></div>
        <div class="board-cols">${b.lists.map(colHtml)}
          ${b.canEdit ? html`<form class="board-col board-newcol" data-addcol><input type="text" name="title" maxlength="40" placeholder="+ Add a column"></form>` : ''}</div>
        <dialog class="board-dialog" data-dialog></dialog>`);
      wire();
    };
    // Send a change; the server answers with the whole board, which is drawn again.
    const send = async (op, args) => {
      const done = await call(op, Object.assign({ id }, args));
      if (!done.ok) { toast(done.error); return false; }
      if (done.board) { b = done.board; if (location.hash === hash) draw(); }
      return true;
    };

    function wire() {
      const root = document.querySelector('.board-cols');
      document.querySelectorAll('[data-bd]').forEach((x) => x.addEventListener('click', async (e) => {
        e.preventDefault();
        const what = x.dataset.bd;
        if (what === 'link') {
          try { await navigator.clipboard.writeText(location.origin + '/#/board/' + b.id); toast(b.visibility === 'private' ? 'Copied. (It\'s private: only people on it can open it.)' : 'Copied the link.'); }
          catch { toast('Your browser didn\'t let us copy.'); }
        } else if (what === 'leave') {
          if (!confirm('Leave "' + b.title + '"?')) return;
          if (await call('board.member', { id, who: me().id, on: false }).then((d) => { if (!d.ok) toast(d.error); return d.ok; })) location.hash = '#/boards';
        } else if (what === 'settings') settings();
      }));
      if (!b.canEdit) {
        root.querySelectorAll('[data-card]').forEach((el) => el.addEventListener('click', () => openCard(el.dataset.card)));
        return;
      }
      root.querySelectorAll('[data-rename]').forEach((el) => el.addEventListener('change', () => send('board.column', { do: 'rename', column: el.dataset.rename, title: el.value })));
      root.querySelectorAll('[data-colmove]').forEach((el) => el.addEventListener('click', () => send('board.column', { do: 'move', column: el.dataset.colmove, to: Number(el.dataset.to) })));
      root.querySelectorAll('[data-coldel]').forEach((el) => el.addEventListener('click', () => {
        const c = b.lists.find((x) => x.id === el.dataset.coldel);
        if (c.cards.length && !confirm('Delete "' + c.title + '" and its ' + c.cards.length + ' card' + (c.cards.length === 1 ? '' : 's') + '?')) return;
        send('board.column', { do: 'delete', column: c.id });
      }));
      root.querySelectorAll('[data-addcard]').forEach((f) => f.addEventListener('submit', async (e) => {
        e.preventDefault();
        if (!f.title.value.trim()) return;
        const col = f.dataset.addcard;
        if (await send('board.card', { do: 'add', column: col, title: f.title.value })) {
          const again = document.querySelector('[data-addcard="' + col + '"] input');
          if (again) again.focus();   // (keep typing cards)
        }
      }));
      const nc = root.querySelector('[data-addcol]');
      if (nc) nc.addEventListener('submit', (e) => { e.preventDefault(); if (nc.title.value.trim()) send('board.column', { do: 'add', title: nc.title.value }); });
      // Dragging cards: drop between other cards, or at the end of a column.
      let dragging = null;
      root.querySelectorAll('[data-card]').forEach((el) => {
        el.addEventListener('click', () => openCard(el.dataset.card));
        el.addEventListener('keydown', (e) => { if (e.key === 'Enter') openCard(el.dataset.card); });
        el.addEventListener('dragstart', (e) => { dragging = el.dataset.card; el.classList.add('dragging'); e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', dragging); });
        el.addEventListener('dragend', () => { el.classList.remove('dragging'); dragging = null; });
      });
      root.querySelectorAll('[data-drop]').forEach((zone) => {
        zone.addEventListener('dragover', (e) => { if (dragging) { e.preventDefault(); zone.classList.add('over'); } });
        zone.addEventListener('dragleave', () => zone.classList.remove('over'));
        zone.addEventListener('drop', (e) => {
          e.preventDefault(); zone.classList.remove('over');
          if (!dragging) return;
          const cards = [...zone.querySelectorAll('[data-card]')].filter((x) => x.dataset.card !== dragging);
          let to = cards.findIndex((x) => { const r = x.getBoundingClientRect(); return e.clientY < r.top + r.height / 2; });
          if (to < 0) to = cards.length;
          send('board.card', { do: 'move', card: dragging, column: zone.dataset.drop, to });
        });
      });
    }

    function findCard(cid) {
      for (const c of b.lists) { const k = c.cards.find((x) => x.id === cid); if (k) return { k, c }; }
      return null;
    }
    function openCard(cid) {
      const found = findCard(cid);
      if (!found) return;
      const { k, c } = found, edit = b.canEdit;
      const dlg = document.querySelector('[data-dialog]');
      const everyone = [b.by, ...b.members];
      const checks = k.checks.map((x) => ({ ...x }));
      dlg.innerHTML = html`<form method="dialog" class="form board-card-form">
        <p class="small muted">In <b>${c.title}</b> · added by ${k.by.name} ${ago(k.at)}</p>
        ${edit ? html`<p><input type="text" name="title" maxlength="120" value="${k.title}" class="board-card-name" aria-label="Card name"></p>` : html`<h2>${k.title}</h2>`}
        <p><label class="small"><input type="checkbox" name="done" ${k.done ? 'checked' : ''} ${edit ? '' : 'disabled'}> Done</label></p>
        <p><label class="small">Notes<br><textarea name="text" maxlength="2000" rows="5" ${edit ? '' : 'readonly'}>${k.text}</textarea></label></p>
        <div class="row board-card-row">
          <p><span class="small">Labels</span><br>${Object.entries(BOARD_COLOR_NAMES).map(([key, n]) => html`<label class="board-pick board-${key}" title="${n}">
            <input type="checkbox" name="label" value="${key}" ${k.labels.includes(key) ? 'checked' : ''} ${edit ? '' : 'disabled'}><i></i></label>`)}</p>
          <p><label class="small">Due date<br><input type="date" name="due" value="${k.due}" ${edit ? '' : 'readonly'}></label></p>
          ${edit ? html`<p><label class="small">Move to<br><select name="move">${b.lists.map((x) => html`<option value="${x.id}" ${x.id === c.id ? 'selected' : ''}>${x.title}</option>`)}</select></label></p>` : ''}
        </div>
        <p class="small">Checklist</p><div data-checks></div>
        ${edit ? html`<p class="row"><input type="text" maxlength="120" placeholder="Add a thing to check" data-newcheck style="flex:1"> <button class="btn small" data-addcheck>Add</button></p>` : ''}
        <p class="small">Who's doing it<br>${everyone.map((p) => html`<label class="board-person"><input type="checkbox" name="people" value="${p.id}"
          ${k.people.some((x) => x.id === p.id) ? 'checked' : ''} ${edit ? '' : 'disabled'}> ${p.name}</label>`)}</p>
        <p class="board-card-buttons">${edit ? html`<button class="btn green" value="save">Save</button> <button class="btn red small" value="delete" formnovalidate>Delete card</button> ` : ''}
          <button class="btn" value="close" formnovalidate>Close</button></p></form>`.s;
      const f = dlg.querySelector('form'), box = dlg.querySelector('[data-checks]');
      const drawChecks = () => {
        box.innerHTML = html`${checks.map((x, i) => html`<p class="board-check"><label><input type="checkbox" data-ci="${i}" ${x.done ? 'checked' : ''} ${edit ? '' : 'disabled'}> ${x.text}</label>
          ${edit ? html`<a href="#" class="small" data-cx="${i}">remove</a>` : ''}</p>`)}${checks.length ? '' : html`<p class="small muted">Nothing yet.</p>`}`.s;
        box.querySelectorAll('[data-ci]').forEach((el) => el.addEventListener('change', () => { checks[el.dataset.ci].done = el.checked; }));
        box.querySelectorAll('[data-cx]').forEach((el) => el.addEventListener('click', (e) => { e.preventDefault(); checks.splice(Number(el.dataset.cx), 1); drawChecks(); }));
      };
      drawChecks();
      const addCheck = (e) => {
        if (e) e.preventDefault();
        const input = dlg.querySelector('[data-newcheck]');
        if (!input.value.trim()) return;
        checks.push({ text: input.value.trim(), done: false });
        input.value = '';
        drawChecks();
      };
      if (edit) {
        dlg.querySelector('[data-addcheck]').addEventListener('click', addCheck);
        dlg.querySelector('[data-newcheck]').addEventListener('keydown', (e) => { if (e.key === 'Enter') addCheck(e); });
      }
      f.addEventListener('submit', async (e) => {
        const why = e.submitter && e.submitter.value;
        if (why === 'delete') {
          e.preventDefault();
          if (!confirm('Delete "' + k.title + '"?')) return;
          dlg.close();
          send('board.card', { do: 'delete', card: k.id });
        } else if (why === 'save') {
          e.preventDefault();
          const ok = await send('board.card', { do: 'edit', card: k.id, title: f.title.value, text: f.text.value, done: f.done.checked, due: f.due.value,
            labels: [...f.querySelectorAll('[name=label]:checked')].map((x) => x.value), checks,
            people: [...f.querySelectorAll('[name=people]:checked')].map((x) => x.value) });
          if (!ok) return;
          dlg.close();
          if (f.move.value !== c.id) send('board.card', { do: 'move', card: k.id, column: f.move.value });
        }
      });
      dlg.showModal();
    }

    function settings() {
      const box = document.querySelector('[data-settings]');
      if (!box.hidden) { box.hidden = true; return; }
      box.hidden = false;
      box.innerHTML = html`<h2 class="boxhead">Settings and people</h2>
        <form class="form" data-set>
          <p><label>Name<br><input type="text" name="title" maxlength="80" value="${b.title}" required style="width:100%"></label></p>
          <p><label>About it <span class="small muted">(optional)</span><br><textarea name="text" maxlength="500" rows="2" style="width:100%">${b.text}</textarea></label></p>
          <div class="row" style="gap:14px;flex-wrap:wrap">
            <p><label>Who can look<br><select name="visibility">${Object.entries(SEES).map(([k, n]) => html`<option value="${k}" ${b.visibility === k ? 'selected' : ''}>${n}</option>`)}</select></label></p>
            <p><label>Colour<br><select name="color">${Object.entries(BOARD_COLOR_NAMES).map(([k, n]) => html`<option value="${k}" ${b.color === k ? 'selected' : ''}>${n}</option>`)}</select></label></p>
          </div>
          <p><button class="btn green small">Save</button></p></form>
        <h3>People on this board</h3>
        <p class="small muted">Everyone on it can add, change and move cards. Up to 30 people.</p>
        <ul class="board-people">${b.members.map((m) => html`<li>${person(m)} <a href="#" class="small" data-kick="${m.id}">remove</a></li>`)}${b.members.length ? '' : html`<li class="muted small">Just you so far.</li>`}</ul>
        <form class="row" data-invite><input type="text" name="who" placeholder="Their username or number" required style="flex:1"> <button class="btn small">Add</button></form>
        <p style="margin-top:14px"><button class="btn red small" data-delboard>Delete this board</button></p>`.s;
      const f = box.querySelector('[data-set]');
      f.addEventListener('submit', (e) => { e.preventDefault(); send('board.set', { title: f.title.value, text: f.text.value, visibility: f.visibility.value, color: f.color.value }); });
      const inv = box.querySelector('[data-invite]');
      inv.addEventListener('submit', async (e) => {
        e.preventDefault();
        if (await send('board.member', { who: inv.who.value.trim(), on: true })) { toast('Added. They\'ll get a note.'); settings(); settings(); }
      });
      box.querySelectorAll('[data-kick]').forEach((a) => a.addEventListener('click', async (e) => {
        e.preventDefault();
        if (await send('board.member', { who: a.dataset.kick, on: false })) { settings(); settings(); }
      }));
      box.querySelector('[data-delboard]').addEventListener('click', async () => {
        if (!confirm('Delete "' + b.title + '" and all its cards for good?')) return;
        const done = await call('board.delete', { id });
        if (!done.ok) { toast(done.error); return; }
        toast('Deleted.');
        location.hash = '#/boards';
      });
    }

    draw();
    // See what other people changed: look again every 20 seconds while you're on the board
    // (not while you're typing in it or have a card open).
    const timer = setInterval(async () => {
      if (location.hash !== hash) { clearInterval(timer); return; }
      const a = document.activeElement, dlg = document.querySelector('[data-dialog]'), set = document.querySelector('[data-settings]');
      if ((a && /^(INPUT|TEXTAREA|SELECT)$/.test(a.tagName)) || (dlg && dlg.open) || (set && !set.hidden) || document.hidden) return;
      const again = await call('board.get', { id });
      if (location.hash !== hash || !again.ok) return;
      if (JSON.stringify(again.board) !== JSON.stringify(b)) { b = again.board; draw(); }
    }, 20000);
  };

  return pages;
}
