// The events calendar's pages (worker/calendar.js does the work on the server):
//   #/calendar?month=2026-12&kind=   a month grid with what's on each day, and the next events
//   #/calevent/<id>                  one event: when, who hosts it, "Going", a link to where it happens
//   #/calnew?id=<id>                 put an event on the calendar, or change yours (id=)
// Times are shown in your own time zone. app.js hands over its helpers with calendarPages(ctx).

export const CAL_KINDS = { jam: 'Game jam', art: 'Art contest', live: 'Live show', meetup: 'Meetup', launch: 'Launch', other: 'Other' };   // (worker/calendar.js)

const params = () => new URLSearchParams((location.hash.split('?')[1] || ''));
const pad = (n) => String(n).padStart(2, '0');
const sec = (d) => Math.floor(d.getTime() / 1000);
const timeText = (s) => new Date(s * 1000).toLocaleString(undefined, { weekday: 'short', month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit' });
const dayKey = (d) => d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate());
// "2026-12-06T18:00" for <input type="datetime-local">, in your own time zone.
const localInput = (s) => { const d = new Date(s * 1000); return dayKey(d) + 'T' + pad(d.getHours()) + ':' + pad(d.getMinutes()); };
function lengthText(e) {
  const s = e.end - e.start;
  if (s < 86400) { const h = Math.round(s / 360) / 10; return h < 1 ? Math.round(s / 60) + ' minutes' : h + ' hour' + (h === 1 ? '' : 's'); }
  const d = Math.round(s / 86400); return d + ' day' + (d === 1 ? '' : 's');
}

export function calendarPages(ctx) {
  const { html, show, call, pageCall, signedIn, toast, needSignIn, reportLink, verified } = ctx;
  const person = (p) => html`<a href="#/user/${p.userId || p.id}">${p.name}</a>${verified(p.verified)}`;
  const host = (e) => (e.group ? html`<a href="#/group/${e.group.id}">${e.group.name}</a>` : person(e.by));
  const stateTag = (e) => (e.state === 'now' ? html`<span class="cal-tag now">On now</span>` : e.state === 'over' ? html`<span class="cal-tag over">Over</span>` : '');

  const hero = html`<div class="portal-hero cal-hero"><div><span class="portal-kicker">G&amp;B COMMUNITIES</span><h1>Calendar</h1>
      <p>Game jams, art contests, live shows, meetups and launches. Anyone can put an event on the calendar, and joining is always free.
        News from the team is in <a href="#/press">Press</a>.</p>
      <p><a class="btn green" href="#/calnew">Add an event</a></p></div></div>`;

  const row = (e) => html`<a class="cal-row cal-${e.kind}" href="#/calevent/${e.id}">
      <span class="cal-when"><b>${new Date(e.start * 1000).toLocaleDateString(undefined, { month: 'short', day: 'numeric' })}</b>
        <span>${new Date(e.start * 1000).toLocaleTimeString(undefined, { hour: 'numeric', minute: '2-digit' })}</span></span>
      <span class="cal-what"><span class="cal-kind">${e.kindName}</span>${e.official ? html` <span class="cal-tag official">Official</span>` : ''} ${stateTag(e)}
        <b>${e.title}</b><span class="small muted">${lengthText(e)} &middot; by ${e.group ? e.group.name : e.by.name} &middot; ${e.going} going</span></span></a>`;

  const pages = {};

  pages.calendar = async () => {
    const q = params();
    const today = new Date();
    const [y, m] = /^\d{4}-\d{2}$/.test(q.get('month') || '') ? q.get('month').split('-').map(Number) : [today.getFullYear(), today.getMonth() + 1];
    const kind = Object.hasOwn(CAL_KINDS, q.get('kind') || '') ? q.get('kind') : '';
    const first = new Date(y, m - 1, 1), next = new Date(y, m, 1);
    // The grid starts on the Monday on or before the 1st and runs whole weeks.
    const gridStart = new Date(y, m - 1, 1 - ((first.getDay() + 6) % 7));
    const weeks = Math.ceil(((next - gridStart) / 86400000) / 7);
    const gridEnd = new Date(gridStart.getFullYear(), gridStart.getMonth(), gridStart.getDate() + weeks * 7);
    const [month, coming] = await Promise.all([
      pageCall('calendar.list', { from: sec(gridStart), to: sec(gridEnd), kind }),
      pageCall('calendar.list', { kind }),
    ]);
    if (!month.ok) { show(html`${hero}<div class="box"><p class="error">${month.error}</p></div>`); return; }
    const byDay = new Map();
    for (const e of month.events) {
      // an event shows on every day it's on (at most the whole grid)
      for (let d = new Date(Math.max(e.start * 1000, gridStart.getTime())); d.getTime() < Math.min(e.end * 1000, gridEnd.getTime()); d = new Date(d.getFullYear(), d.getMonth(), d.getDate() + 1)) {
        const k = dayKey(d);
        if (!byDay.has(k)) byDay.set(k, []);
        byDay.get(k).push(e);
      }
    }
    const link = (change) => {
      const n = new URLSearchParams(q);
      for (const [k, v] of Object.entries(change)) { if (!v) n.delete(k); else n.set(k, v); }
      const s = n.toString();
      return '#/calendar' + (s ? '?' + s : '');
    };
    const monthKey = (d) => d.getFullYear() + '-' + pad(d.getMonth() + 1);
    const cells = [];
    for (let i = 0; i < weeks * 7; i++) {
      const d = new Date(gridStart.getFullYear(), gridStart.getMonth(), gridStart.getDate() + i);
      const list = byDay.get(dayKey(d)) || [];
      cells.push(html`<div class="cal-day ${d.getMonth() !== m - 1 ? 'other' : ''} ${dayKey(d) === dayKey(today) ? 'today' : ''}">
        <span class="cal-num">${d.getDate()}</span>
        ${list.slice(0, 3).map((e) => html`<a class="cal-chip cal-${e.kind}" href="#/calevent/${e.id}" title="${e.title}">${e.title}</a>`)}
        ${list.length > 3 ? html`<span class="small muted">+${list.length - 3} more</span>` : ''}</div>`);
    }
    const kinds = html`<div class="art-cats"><a class="${kind ? '' : 'on'}" href="${link({ kind: '' })}">Everything</a>${Object.entries(CAL_KINDS).map(([k, n]) =>
      html`<a class="${kind === k ? 'on' : ''}" href="${link({ kind: k })}">${n}</a>`)}</div>`;
    show(html`${hero}
      <div class="box"><h2 class="boxhead">Coming up</h2>
        ${coming.ok && coming.events.length ? html`<div class="cal-list">${coming.events.slice(0, 8).map(row)}</div>`
          : html`<p class="muted">Nothing on the calendar yet. <a href="#/calnew">Add the first event!</a></p>`}</div>
      <div class="box"><h2 class="boxhead">${first.toLocaleDateString(undefined, { month: 'long', year: 'numeric' })}</h2>
        ${kinds}
        <p class="cal-nav"><a class="btn small" href="${link({ month: monthKey(new Date(y, m - 2, 1)) })}">&laquo; ${new Date(y, m - 2, 1).toLocaleDateString(undefined, { month: 'long' })}</a>
          <a class="btn small" href="${link({ month: '' })}">This month</a>
          <a class="btn small" href="${link({ month: monthKey(next) })}">${next.toLocaleDateString(undefined, { month: 'long' })} &raquo;</a></p>
        <div class="cal-grid">${['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'].map((n) => html`<span class="cal-head">${n}</span>`)}${cells}</div>
        <p class="small muted">Times are in your time zone. <a href="#/calendar?when=past" data-cal-past>See past events</a></p>
        <div id="calPast"></div></div>`);
    const past = document.querySelector('[data-cal-past]');
    if (past) past.addEventListener('click', async (e) => {
      e.preventDefault();
      const r = await pageCall('calendar.list', { when: 'past', kind });
      const box = document.getElementById('calPast');
      box.innerHTML = String(r.ok && r.events.length ? html`<h3>Past events</h3><div class="cal-list">${r.events.slice(0, 30).map(row)}</div>`.s : html`<p class="muted">No past events yet.</p>`.s);
    });
  };

  pages.calevent = async (id) => {
    const r = await pageCall('calendar.get', { id });
    if (!r.ok) { show(html`<h1>Calendar</h1><p class="error">${r.error}</p><p><a href="#/calendar">&laquo; Back to the calendar</a></p>`); return; }
    const e = r.event;
    const draw = (ev) => html`<p class="small"><a href="#/calendar">&laquo; Calendar</a></p>
      <div class="box cal-event cal-${ev.kind}">
        <p class="cal-kind">${ev.kindName}${ev.official ? html` <span class="cal-tag official">Official</span>` : ''} ${stateTag(ev)}</p>
        <h1>${ev.title}</h1>
        <dl class="cal-facts"><dt>Starts</dt><dd>${timeText(ev.start)}</dd><dt>Ends</dt><dd>${timeText(ev.end)} <span class="muted">(${lengthText(ev)})</span></dd>
          <dt>Hosted by</dt><dd>${host(ev)}</dd><dt>Going</dt><dd>${ev.going}</dd></dl>
        ${ev.text ? html`<p class="cal-text">${ev.text}</p>` : ''}
        <p class="press-tools">
          ${ev.state !== 'over' && signedIn() ? html`<button class="btn ${ev.amGoing ? '' : 'green'}" data-cal="going">${ev.amGoing ? 'Not going any more' : 'I\'m going'}</button>` : ''}
          ${ev.state !== 'over' && !signedIn() ? html`<a class="btn green" href="#/login">Sign up to say you're going</a>` : ''}
          ${ev.link ? html`<a class="btn" href="${ev.link}">Go there</a>` : ''}
          <button class="btn small" data-cal="link">Copy link</button>
          ${ev.canEdit ? html`<a class="btn small" href="#/calnew?id=${ev.id}">Change it</a> <button class="btn small red" data-cal="delete">Delete</button>` : ''}
          ${signedIn() && !ev.mine ? html`<span class="small">${reportLink('calendar', ev.id, 'the event "' + ev.title + '"')}</span>` : ''}</p>
      </div>`;
    const wire = (ev) => document.querySelectorAll('[data-cal]').forEach((b) => b.addEventListener('click', async (x) => {
      x.preventDefault();
      const what = b.dataset.cal;
      if (what === 'going') {
        const done = await call('calendar.going', { id: ev.id, on: !ev.amGoing });
        if (!done.ok) { toast(done.error); return; }
        toast(done.event.amGoing ? 'See you there!' : 'Okay, you\'re not going.');
        show(draw(done.event)); wire(done.event);
      } else if (what === 'link') {
        try { await navigator.clipboard.writeText(location.origin + '/calendar/' + ev.id); toast('Copied the link.'); }
        catch { toast('Your browser didn\'t let us copy.'); }
      } else if (what === 'delete') {
        if (!confirm('Take "' + ev.title + '" off the calendar? Everyone going gets told.')) return;
        const done = await call('calendar.delete', { id: ev.id });
        if (!done.ok) { toast(done.error); return; }
        toast('Deleted.');
        location.hash = '#/calendar';
      }
    }));
    show(draw(e)); wire(e);
  };

  pages.calnew = async () => {
    if (!signedIn()) { show(html`${hero}${needSignIn('put events on the calendar')}`); return; }
    const id = params().get('id') || '';
    let old = null;
    if (id) { const r = await pageCall('calendar.get', { id }); if (r.ok && r.event.canEdit) old = r.event; }
    const groups = await pageCall('groups.mine', {});
    const mine = groups.ok ? groups.groups.filter((g) => g.role === 'Owner' || (g.perms || []).includes('manage')) : [];
    const soon = Math.ceil(Date.now() / 3600000) * 3600 + 86400;
    show(html`<div class="box"><h2 class="boxhead">${old ? 'Change "' + old.title + '"' : 'Add an event'}</h2>
      <p class="small muted">Game jams, art contests, live shows, meetups, launches: anything people can join on Guts&amp;Bolts. Follow the <a href="#/terms">rules</a>.</p>
      <form class="form" data-cal-new>
        <p><label>Name<br><input type="text" name="title" maxlength="80" value="${old ? old.title : ''}" required style="width:100%"></label></p>
        <div class="row paste-options">
          <label>Kind<br><select name="kind">${Object.entries(CAL_KINDS).map(([k, n]) => html`<option value="${k}" ${(old ? old.kind : 'jam') === k ? 'selected' : ''}>${n}</option>`)}</select></label>
          <label>Starts<br><input type="datetime-local" name="start" value="${localInput(old ? old.start : soon)}" required></label>
          <label>Ends<br><input type="datetime-local" name="end" value="${localInput(old ? old.end : soon + 7200)}" required></label>
          ${mine.length ? html`<label>Hosted by<br><select name="group"><option value="">Me</option>${mine.map((g) =>
            html`<option value="${g.id}" ${old && old.group && old.group.id === g.id ? 'selected' : ''}>${g.name}</option>`)}</select></label>` : ''}
        </div>
        <p><label>Where <span class="small muted">(optional: a page on Guts&amp;Bolts, like #/game/1012 or #/live)</span><br>
          <input type="text" name="link" maxlength="200" value="${old ? old.link : ''}" placeholder="#/game/1012" style="width:100%"></label></p>
        <p><label>What happens <span class="small muted">(the rules, the theme, the prizes...)</span><br><textarea name="text" rows="8" maxlength="3000" style="width:100%">${old ? old.text : ''}</textarea></label></p>
        <p><button class="btn green">${old ? 'Save' : 'Add it'}</button> <span id="calMsg" class="small"></span></p>
      </form></div>`);
    const form = document.querySelector('[data-cal-new]');
    const msg = document.getElementById('calMsg');
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const btn = form.querySelector('button');
      btn.disabled = true; msg.className = 'small'; msg.textContent = 'Saving...';
      const args = { title: form.title.value, kind: form.kind.value, text: form.text.value, link: form.link.value,
        start: sec(new Date(form.start.value)), end: sec(new Date(form.end.value)) };
      if (form.group) args.group = form.group.value;
      const r = await call(old ? 'calendar.edit' : 'calendar.make', old ? Object.assign({ id: old.id }, args) : args);
      btn.disabled = false;
      if (!r.ok) { msg.textContent = r.error; msg.className = 'small error'; return; }
      toast(old ? 'Saved.' : 'It\'s on the calendar!');
      location.hash = '#/calevent/' + r.event.id;
    });
  };

  return pages;
}
