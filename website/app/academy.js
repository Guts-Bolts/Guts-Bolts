// G&B Academy's pages (worker/academy.js does the work on the server):
//   #/academy?topic=&level=&sort=&q=&by=&page=   published courses (?mine=1: yours and the ones you're taking)
//   #/course/<id>                                one course: its lessons, your progress, like, report
//   #/lesson/<course>/<lesson>                   one lesson to read, with "Mark as done" and next/back
//   #/coursewrite?edit=<id>                      write a new course, or change one of yours
// app.js hands over its helpers with academyPages(ctx), and adds the pages it returns.

export const TOPIC_NAMES = { scripting: 'Scripting', building: 'Building', modeling: '3D modeling', animation: 'Animation',
  gamedesign: 'Game design', art: 'Art', audio: 'Audio', studio: 'Studio basics', other: 'Other' };
export const LEVEL_NAMES = { beginner: 'Beginner', intermediate: 'Intermediate', advanced: 'Advanced' };
const kMostLessons = 30;   // (worker/academy.js)

const params = () => new URLSearchParams((location.hash.split('?')[1] || ''));

export function academyPages(ctx) {
  const { html, show, call, pageCall, signedIn, toast, ago, needSignIn, reportLink, verified, richText } = ctx;
  const me = () => ctx.me();
  const person = (p) => html`<a href="#/user/${p.userId || p.id}">${p.name}</a>${verified(p.verified)}`;

  const hero = html`<div class="portal-hero academy-hero"><div><span class="portal-kicker">G&amp;B LEARNING</span><h1>Academy</h1>
      <p>Courses about scripting, building, modeling, animation and making games, written by the Guts&amp;Bolts community. Free, always.</p>
      <p><a class="btn green" href="#/coursewrite">Write a course</a> <a class="btn" href="#/academy?mine=1">My courses</a></p></div></div>`;
  // Studio Learn: the 20 hands-on lessons inside Studio. Academy is where it grows.
  const studioLearn = html`<a class="academy-card academy-studio" href="#/create/learn">
      <span class="academy-tag">Built into Studio</span><b>Studio Learn: 20 hands-on lessons</b>
      <span class="small">Learn Studio by building things, with a guide that shows you where to click. Start here if you're new.</span></a>`;
  const bar = (c) => (c.lessons ? Math.round((c.done / c.lessons) * 100) : 0);
  const card = (c) => html`<a class="academy-card" href="#/course/${c.id}">
      <span class="academy-tag">${TOPIC_NAMES[c.topic] || 'Other'} &middot; ${LEVEL_NAMES[c.level] || ''}${c.published ? '' : html` &middot; <b class="academy-draft">Draft</b>`}</span>
      <b>${c.title}</b>
      <span class="small">${c.summary}</span>
      <span class="small muted">by ${c.by.name} &middot; ${c.lessons} lesson${c.lessons === 1 ? '' : 's'} &middot; &#128077; ${c.likes} &middot; ${c.learners} learner${c.learners === 1 ? '' : 's'}</span>
      ${c.done ? html`<span class="academy-bar"><i style="width:${bar(c)}%"></i></span>` : ''}</a>`;

  const pages = {};

  pages.academy = async () => {
    const q = params();
    if (q.get('mine')) {
      if (!signedIn()) { show(html`${hero}${needSignIn('see your courses')}`); return; }
      const r = await pageCall('academy.mine', {});
      if (!r.ok) { show(html`${hero}<div class="box"><p class="error">${r.error}</p></div>`); return; }
      show(html`${hero}
        <div class="box"><h2 class="boxhead">Courses I'm taking</h2>
          ${r.learning.length ? html`<div class="academy-grid">${r.learning.map(card)}</div>` : html`<p class="muted">None yet. <a href="#/academy">Find one!</a></p>`}</div>
        <div class="box"><h2 class="boxhead">Courses I wrote</h2>
          ${r.written.length ? html`<div class="academy-grid">${r.written.map(card)}</div>` : html`<p class="muted">None yet. <a href="#/coursewrite">Write your first course</a>: share what you know!</p>`}</div>`);
      return;
    }
    const args = { topic: q.get('topic') || '', level: q.get('level') || '', sort: q.get('sort') || 'new', q: q.get('q') || '', by: q.get('by') || '', page: Number(q.get('page') || 1) };
    const r = await pageCall('academy.list', args);
    if (!r.ok) { show(html`${hero}<div class="box"><p class="error">${r.error}</p></div>`); return; }
    const link = (change) => {
      const n = new URLSearchParams(q);
      for (const [k, v] of Object.entries(change)) { if (v === '' || v === null || v === undefined) n.delete(k); else n.set(k, String(v)); }
      if (!('page' in change)) n.delete('page');
      const s = n.toString();
      return '#/academy' + (s ? '?' + s : '');
    };
    const topics = html`<div class="art-cats"><a class="${args.topic ? '' : 'on'}" href="${link({ topic: '' })}">All topics</a>${Object.entries(TOPIC_NAMES).map(([k, n]) =>
      html`<a class="${args.topic === k ? 'on' : ''}" href="${link({ topic: k })}">${n}</a>`)}</div>`;
    const levels = html`<div class="art-tabs">${[['', 'Any level'], ...Object.entries(LEVEL_NAMES)].map(([k, n]) =>
      html`<a class="btn small ${args.level === k ? 'blue' : ''}" href="${link({ level: k })}">${n}</a> `)}
      <span class="academy-sort"><a class="${args.sort === 'top' ? '' : 'on'}" href="${link({ sort: '' })}">Newest</a> &middot;
        <a class="${args.sort === 'top' ? 'on' : ''}" href="${link({ sort: 'top' })}">Most liked</a></span></div>`;
    const filtered = args.topic || args.level || args.q || args.by;
    show(html`${hero}<div class="box"><h2 class="boxhead">Courses <span class="small muted">(${r.total})</span></h2>
      <form class="row art-search" data-academy-search><input type="search" name="q" value="${args.q}" placeholder="Search courses" style="flex:1">
        <button class="btn small">Search</button></form>
      ${topics}${levels}
      ${filtered ? html`<p class="small"><a href="#/academy">&laquo; All courses</a></p>` : ''}
      <div class="academy-grid">${filtered || r.page > 1 ? '' : studioLearn}${r.courses.map(card)}</div>
      ${!r.courses.length ? html`<p class="muted">No courses here yet. <a href="#/coursewrite">Write the first one!</a></p>` : ''}
      <p class="art-pages">${r.page > 1 ? html`<a class="btn small" href="${link({ page: r.page - 1 })}">&laquo; Newer</a> ` : ''}
        ${r.more ? html`<a class="btn small" href="${link({ page: r.page + 1 })}">More &raquo;</a>` : ''}</p></div>`);
    document.querySelector('[data-academy-search]').addEventListener('submit', (e) => {
      e.preventDefault();
      location.hash = link({ q: e.currentTarget.q.value.trim() });
    });
  };

  pages.course = async (id) => {
    const r = await pageCall('academy.get', { id });
    if (!r.ok) { show(html`<h1>Academy</h1><p class="error">${r.error}</p><p><a href="#/academy">&laquo; All courses</a></p>`); return; }
    const c = r.course;
    const next = c.lessonList.find((l) => !l.done) || c.lessonList[0];
    show(html`<p class="small"><a href="#/academy">&laquo; Academy</a></p>
      ${c.published ? '' : html`<div class="box info"><b>This is a draft.</b> Only you and staff can see it. Press Publish when it's ready.</div>`}
      <div class="box academy-course">
        <p class="small muted">${TOPIC_NAMES[c.topic] || 'Other'} &middot; ${LEVEL_NAMES[c.level] || ''}</p>
        <h1>${c.title}</h1>
        <p>by ${person(c.by)} <span class="small muted">&middot; ${ago(c.at)}${c.edited ? ' (changed ' + ago(c.edited) + ')' : ''}</span></p>
        ${c.summary ? html`<p class="academy-summary">${c.summary}</p>` : ''}
        <p class="small muted">${c.lessons} lesson${c.lessons === 1 ? '' : 's'} &middot; ${c.learners} learner${c.learners === 1 ? '' : 's'}${signedIn() && c.done ? html` &middot; you've done ${c.done} of ${c.lessons}` : ''}</p>
        ${c.done ? html`<p class="academy-bar big"><i style="width:${bar(c)}%"></i></p>` : ''}
        <p class="academy-tools">
          ${next ? html`<a class="btn green" href="#/lesson/${c.id}/${next.id}">${c.done ? (c.done === c.lessons ? 'Read it again' : 'Keep going') : 'Start the course'}</a>` : ''}
          ${c.published && !c.mine ? html`<button class="btn ${c.liked ? 'blue' : ''}" data-course="like">&#128077; ${c.liked ? 'Liked' : 'Helpful'} (${c.likes})</button>` : ''}
          ${c.mine ? html`<a class="btn" href="#/coursewrite?edit=${c.id}">Edit</a>
            <button class="btn ${c.published ? '' : 'green'}" data-course="publish">${c.published ? 'Unpublish' : 'Publish'}</button>` : ''}
          ${c.canDelete ? html`<button class="btn red" data-course="delete">Delete</button>` : ''}
          ${signedIn() && !c.mine ? html`<span class="small">${reportLink('course', c.id, 'the course "' + c.title + '"')}</span>` : ''}</p>
      </div>
      <div class="box"><h2 class="boxhead">Lessons</h2>
        ${c.lessonList.length ? html`<ol class="academy-lessons">${c.lessonList.map((l) => html`<li class="${l.done ? 'done' : ''}">
          <a href="#/lesson/${c.id}/${l.id}">${l.title}</a>${l.done ? html` <span class="academy-tick" title="Done">&#10003;</span>` : ''}</li>`)}</ol>`
          : html`<p class="muted">No lessons yet.${c.mine ? html` <a href="#/coursewrite?edit=${c.id}">Add some</a>.` : ''}</p>`}
        ${!signedIn() ? html`<p class="small muted">Sign in to keep track of the lessons you've done.</p>` : ''}</div>`);
    document.querySelectorAll('[data-course]').forEach((b) => b.addEventListener('click', async (e) => {
      e.preventDefault();
      const what = b.dataset.course;
      if (what === 'like') {
        if (!signedIn()) { toast('Sign up to like courses.'); return; }
        const done = await call('academy.like', { id, on: !c.liked });
        if (!done.ok) { toast(done.error); return; }
      } else if (what === 'publish') {
        const done = await call('academy.publish', { id, on: !c.published });
        if (!done.ok) { toast(done.error); return; }
        toast(c.published ? 'It\'s a draft again.' : 'Published! Everyone can take it now.');
      } else if (what === 'delete') {
        if (!confirm('Delete "' + c.title + '" for good? Everyone\'s progress on it goes too.')) return;
        const done = await call('academy.delete', { id });
        if (!done.ok) { toast(done.error); return; }
        toast('Deleted.');
        location.hash = '#/academy?mine=1';
        return;
      }
      pages.course(id);
    }));
  };

  pages.lesson = async (id, lessonId) => {
    const r = await pageCall('academy.lesson', { id, lesson: lessonId });
    if (!r.ok) { show(html`<h1>Academy</h1><p class="error">${r.error}</p><p><a href="#/course/${id}">&laquo; Back to the course</a></p>`); return; }
    const c = r.course, l = r.lesson;
    const done = (c.lessonList.find((x) => x.id === l.id) || {}).done;
    show(html`<p class="small"><a href="#/academy">Academy</a> &raquo; <a href="#/course/${c.id}">${c.title}</a></p>
      <div class="academy-lesson-page">
        <nav class="box academy-side"><b>${c.title}</b><ol class="academy-lessons">${c.lessonList.map((x) => html`<li class="${x.done ? 'done' : ''} ${x.id === l.id ? 'on' : ''}">
          <a href="#/lesson/${c.id}/${x.id}">${x.title}</a>${x.done ? html` <span class="academy-tick">&#10003;</span>` : ''}</li>`)}</ol></nav>
        <article class="box academy-lesson">
          <p class="small muted">Lesson ${l.n} of ${c.lessons}</p>
          <h1>${l.title}</h1>
          <div class="academy-text">${richText(l.text)}</div>
          <p class="academy-tools">
            ${l.prev ? html`<a class="btn" href="#/lesson/${c.id}/${l.prev}">&laquo; Back</a>` : ''}
            ${c.published && signedIn() ? html`<button class="btn ${done ? '' : 'green'}" data-lesson-done>${done ? '&#10003; Done (undo)' : 'Mark as done'}</button>` : ''}
            ${l.next ? html`<a class="btn blue" href="#/lesson/${c.id}/${l.next}">Next lesson &raquo;</a>` : html`<a class="btn" href="#/course/${c.id}">Back to the course</a>`}</p>
        </article>
      </div>`);
    const b = document.querySelector('[data-lesson-done]');
    if (b) b.addEventListener('click', async () => {
      const r2 = await call('academy.done', { id, lesson: l.id, on: !done });
      if (!r2.ok) { toast(r2.error); return; }
      if (r2.finished && !done) toast('You finished the whole course. Nice work!');
      if (!done && l.next) location.hash = '#/lesson/' + c.id + '/' + l.next;
      else pages.lesson(id, lessonId);
    });
  };

  pages.coursewrite = async () => {
    if (!signedIn()) { show(html`${hero}${needSignIn('write courses')}`); return; }
    const editId = params().get('edit') || '';
    let old = null;
    if (editId) {
      const r = await pageCall('academy.get', { id: editId });
      if (!r.ok || !r.course.mine) { show(html`<h1>Edit course</h1><p class="error">${r.ok ? 'Only the writer can change this course.' : r.error}</p>`); return; }
      old = r.course;
      // (the lesson texts come one at a time)
      old.full = [];
      for (const x of old.lessonList) {
        const lr = await pageCall('academy.lesson', { id: editId, lesson: x.id });
        old.full.push(lr.ok ? { id: x.id, title: lr.lesson.title, text: lr.lesson.text } : { id: x.id, title: x.title, text: '' });
      }
    }
    let lessons = old ? old.full : [{ title: '', text: '' }];
    const lessonBox = (x, i) => html`<div class="academy-edit-lesson box" data-i="${i}">
        <div class="row"><b class="grow">Lesson ${i + 1}</b>
          <button type="button" class="btn small" data-move="-1" ${i ? '' : 'disabled'}>&uarr;</button>
          <button type="button" class="btn small" data-move="1" ${i < lessons.length - 1 ? '' : 'disabled'}>&darr;</button>
          <button type="button" class="btn small red" data-remove>Remove</button></div>
        <p><input type="text" name="ltitle" maxlength="80" placeholder="Lesson title" value="${x.title}" style="width:100%"></p>
        <p><textarea name="ltext" rows="10" maxlength="20000" placeholder="What to learn. Put code between \`\`\` lines to show it as code." style="width:100%">${x.text}</textarea></p></div>`;
    const draw = () => {
      show(html`<div class="box academy-write"><h2 class="boxhead">${editId ? 'Edit your course' : 'Write a course'}</h2>
        <p class="small muted">Teach something you know. It stays a draft (only you can see it) until you press Publish on its page.
          Write in your own words, and follow the <a href="#/terms">rules</a>.</p>
        <form class="form" data-course-form>
          <p><label>Title<br><input type="text" name="title" maxlength="80" required value="${old ? old.title : ''}" style="width:100%"></label></p>
          <p><label>Summary <span class="small muted">(what will people learn?)</span><br><textarea name="summary" maxlength="300" rows="2" style="width:100%">${old ? old.summary : ''}</textarea></label></p>
          <div class="row academy-options">
            <label>Topic<br><select name="topic">${Object.entries(TOPIC_NAMES).map(([k, n]) => html`<option value="${k}" ${old && old.topic === k ? 'selected' : ''}>${n}</option>`)}</select></label>
            <label>Level<br><select name="level">${Object.entries(LEVEL_NAMES).map(([k, n]) => html`<option value="${k}" ${old && old.level === k ? 'selected' : ''}>${n}</option>`)}</select></label>
          </div>
          <h3>Lessons</h3>
          ${lessons.map(lessonBox)}
          <p><button type="button" class="btn" data-add ${lessons.length >= kMostLessons ? 'disabled' : ''}>+ Add a lesson</button></p>
          <p><button class="btn green">${editId ? 'Save changes' : 'Save draft'}</button> <span id="courseMsg" class="small"></span></p>
        </form></div>`);
      const form = document.querySelector('[data-course-form]');
      const keep = () => {   // copy what's typed before redrawing
        lessons = [...form.querySelectorAll('.academy-edit-lesson')].map((d, i) => ({ id: lessons[i] && lessons[i].id, title: d.querySelector('[name=ltitle]').value, text: d.querySelector('[name=ltext]').value }));
        old = Object.assign(old || {}, { title: form.title.value, summary: form.summary.value, topic: form.topic.value, level: form.level.value });
      };
      form.querySelector('[data-add]').addEventListener('click', () => { keep(); lessons.push({ title: '', text: '' }); draw(); });
      form.querySelectorAll('.academy-edit-lesson').forEach((d) => {
        const i = Number(d.dataset.i);
        d.querySelector('[data-remove]').addEventListener('click', () => { keep(); lessons.splice(i, 1); draw(); });
        d.querySelectorAll('[data-move]').forEach((m) => m.addEventListener('click', () => {
          keep(); const j = i + Number(m.dataset.move); [lessons[i], lessons[j]] = [lessons[j], lessons[i]]; draw();
        }));
      });
      form.addEventListener('submit', async (e) => {
        e.preventDefault();
        keep();
        const msg = document.getElementById('courseMsg');
        msg.className = 'small'; msg.textContent = 'Saving...';
        const r = await call('academy.save', { id: editId || undefined, title: old.title, summary: old.summary, topic: old.topic, level: old.level,
          lessons: lessons.map((x) => ({ id: x.id, title: x.title, text: x.text })) });
        if (!r.ok) { msg.textContent = r.error; msg.className = 'small error'; return; }
        toast(r.course.published ? 'Saved.' : 'Saved as a draft. Publish it from its page when it\'s ready.');
        location.hash = '#/course/' + r.course.id;
      });
    };
    draw();
  };

  return pages;
}
