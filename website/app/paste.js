// G&B Paste's pages (worker/paste.js does the work on the server):
//   #/paste?lang=&by=&page=     recent public pastes (by=me: all of yours, unlisted too)
//   #/pasteview/<id>            one paste: the text with line numbers, copy, raw, download
//   #/pastenew?from=<id>        make a new paste (from=: start from a copy of another one)
// app.js hands over its helpers with pastePages(ctx), and adds the pages it returns.

export const PASTE_LANG_NAMES = { text: 'Plain text', lua: 'Lua', luau: 'Luau', python: 'Python', javascript: 'JavaScript', json: 'JSON',
  cpp: 'C++', c: 'C', csharp: 'C#', html: 'HTML', css: 'CSS', bash: 'Shell', log: 'Log', markdown: 'Markdown', other: 'Other' };
const EXT = { text: 'txt', lua: 'lua', luau: 'luau', python: 'py', javascript: 'js', json: 'json', cpp: 'cpp', c: 'c', csharp: 'cs',
  html: 'html', css: 'css', bash: 'sh', log: 'log', markdown: 'md', other: 'txt' };
const EXPIRES = [[0, 'Never'], [1, '1 hour'], [24, '1 day'], [168, '1 week'], [720, '1 month']];   // (worker/paste.js PASTE_EXPIRES)
const kMostBytes = 100 * 1024;   // (worker/paste.js kMostBytes)

const params = () => new URLSearchParams((location.hash.split('?')[1] || ''));
const rawUrl = (id) => '/pasteraw/' + encodeURIComponent(id);
const sizeText = (n) => (n < 1024 ? n + ' bytes' : (n / 1024).toFixed(n < 10240 ? 1 : 0) + ' KB');
// "runs out in 3 hours" for a paste with an end time.
function runsOut(until) {
  if (!until) return '';
  const s = until - Math.floor(Date.now() / 1000);
  if (s <= 0) return 'runs out now';
  const [n, unit] = s < 3600 ? [Math.ceil(s / 60), 'minute'] : s < 86400 ? [Math.round(s / 3600), 'hour'] : [Math.round(s / 86400), 'day'];
  return 'runs out in ' + n + ' ' + unit + (n === 1 ? '' : 's');
}

export function pastePages(ctx) {
  const { html, show, call, pageCall, signedIn, toast, ago, needSignIn, reportLink, verified } = ctx;
  const me = () => ctx.me();
  const person = (p) => html`<a href="#/user/${p.userId || p.id}">${p.name}</a>${verified(p.verified)}`;

  const hero = html`<div class="portal-hero paste-hero"><div><span class="portal-kicker">G&amp;B DEVELOPMENT</span><h1>Paste</h1>
      <p>Share code, crash logs and settings with one link. It's free, and nothing in a paste ever runs: it's just text.</p>
      <p><a class="btn green" href="#/pastenew">New paste</a></p></div></div>`;

  const row = (p) => html`<tr>
      <td><a href="#/pasteview/${p.id}"><b>${p.title}</b></a>${p.visibility === 'unlisted' ? html` <span class="paste-flag">Unlisted</span>` : ''}</td>
      <td><a href="#/paste?lang=${p.lang}">${PASTE_LANG_NAMES[p.lang] || 'Other'}</a></td>
      <td class="small">${p.lines} line${p.lines === 1 ? '' : 's'}</td>
      <td class="small">${person(p.by)}</td>
      <td class="small muted">${ago(p.at)}${p.until ? html`<br>${runsOut(p.until)}` : ''}</td></tr>`;
  const table = (list) => html`<table class="paste-table"><thead><tr><th>Title</th><th>Language</th><th>Size</th><th>By</th><th>Made</th></tr></thead>
      <tbody>${list.map(row)}</tbody></table>`;

  const pages = {};

  pages.paste = async () => {
    const q = params();
    if (q.get('by') === 'me') {
      if (!signedIn()) { show(html`${hero}${needSignIn('see your pastes')}`); return; }
      const r = await pageCall('paste.mine', {});
      show(html`${hero}<div class="box"><h2 class="boxhead">My pastes</h2>
        <p class="small muted">All of yours, unlisted ones too. Only people with the link can open an unlisted paste.</p>
        ${!r.ok ? html`<p class="error">${r.error}</p>` : r.pastes.length ? table(r.pastes)
          : html`<p class="muted">You haven't made any yet. <a href="#/pastenew">Make one!</a></p>`}</div>`);
      return;
    }
    const args = { lang: q.get('lang') || '', by: q.get('by') || '', page: Number(q.get('page') || 1) };
    const r = await pageCall('paste.list', args);
    if (!r.ok) { show(html`${hero}<div class="box"><p class="error">${r.error}</p></div>`); return; }
    const link = (change) => {
      const n = new URLSearchParams(q);
      for (const [k, v] of Object.entries(change)) { if (v === '' || v === null || v === undefined) n.delete(k); else n.set(k, String(v)); }
      if (!('page' in change)) n.delete('page');
      const s = n.toString();
      return '#/paste' + (s ? '?' + s : '');
    };
    const langBar = html`<div class="art-cats"><a class="${args.lang ? '' : 'on'}" href="${link({ lang: '' })}">All</a>${Object.entries(PASTE_LANG_NAMES).map(([k, n]) =>
      html`<a class="${args.lang === k ? 'on' : ''}" href="${link({ lang: k })}">${n}</a>`)}</div>`;
    const title = args.by && r.pastes[0] ? 'Pastes by ' + r.pastes[0].by.name : args.lang ? (PASTE_LANG_NAMES[args.lang] || 'Other') + ' pastes' : 'Recent pastes';
    show(html`${hero}<div class="box"><h2 class="boxhead">${title}</h2>
      ${langBar}
      ${(args.lang || args.by) ? html`<p class="small"><a href="#/paste">&laquo; All pastes</a></p>` : ''}
      ${r.pastes.length ? table(r.pastes) : html`<p class="muted">No pastes here yet. <a href="#/pastenew">Make the first one!</a></p>`}
      <p class="art-pages">${r.page > 1 ? html`<a class="btn small" href="${link({ page: r.page - 1 })}">&laquo; Newer</a> ` : ''}
        ${r.more ? html`<a class="btn small" href="${link({ page: r.page + 1 })}">Older &raquo;</a>` : ''}</p></div>`);
  };

  pages.pasteview = async (id) => {
    const r = await pageCall('paste.get', { id });
    if (!r.ok) { show(html`<h1>Paste</h1><p class="error">${r.error}</p><p><a href="#/paste">&laquo; Back to Paste</a></p>`); return; }
    const p = r.paste;
    const lines = p.text.split('\n');
    const numbers = lines.map((_, i) => i + 1).join('\n');
    show(html`<p class="small"><a href="#/paste">&laquo; Paste</a></p>
      <div class="box paste-view">
        <h1>${p.title}</h1>
        <p class="small">by ${person(p.by)} <span class="muted">&middot; ${ago(p.at)} &middot; ${PASTE_LANG_NAMES[p.lang] || 'Other'}
          &middot; ${lines.length} line${lines.length === 1 ? '' : 's'} &middot; ${sizeText(p.size)} &middot; ${p.views} views
          ${p.visibility === 'unlisted' ? html` &middot; <span class="paste-flag">Unlisted</span>` : ''}${p.until ? html` &middot; ${runsOut(p.until)}` : ''}</span></p>
        <p class="paste-tools">
          <button class="btn small green" data-paste="copy">Copy text</button>
          <button class="btn small" data-paste="link">Copy link</button>
          <a class="btn small" href="${rawUrl(p.id)}" target="_blank" rel="noopener">Raw</a>
          <button class="btn small" data-paste="download">Download</button>
          <a class="btn small" href="#/pastenew?from=${p.id}">Copy and edit</a>
          ${p.canDelete ? html`<button class="btn small red" data-paste="delete">Delete</button>` : ''}
          ${signedIn() && !p.mine ? html`<span class="small">${reportLink('paste', p.id, 'the paste "' + p.title + '"')}</span>` : ''}</p>
        <div class="paste-code"><pre class="paste-nums" aria-hidden="true">${numbers}</pre><pre class="paste-text"><code>${p.text}</code></pre></div>
      </div>`);
    document.querySelectorAll('[data-paste]').forEach((b) => b.addEventListener('click', async (e) => {
      e.preventDefault();
      const what = b.dataset.paste;
      if (what === 'copy' || what === 'link') {
        const text = what === 'copy' ? p.text : location.origin + '/#/pasteview/' + p.id;
        try { await navigator.clipboard.writeText(text); toast(what === 'copy' ? 'Copied the text.' : 'Copied the link.'); }
        catch { toast('Your browser didn\'t let us copy. Select it and copy it yourself.'); }
      } else if (what === 'download') {
        const a = document.createElement('a');
        a.href = URL.createObjectURL(new Blob([p.text], { type: 'text/plain' }));
        a.download = (p.title.replace(/[^a-z0-9 _.-]+/gi, '').trim().replace(/\s+/g, '-') || 'paste') + '.' + (EXT[p.lang] || 'txt');
        a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 1000);
      } else if (what === 'delete') {
        if (!confirm('Delete "' + p.title + '" for good?')) return;
        const done = await call('paste.delete', { id });
        if (!done.ok) { toast(done.error); return; }
        toast('Deleted.');
        location.hash = '#/paste?by=me';
      }
    }));
  };

  pages.pastenew = async () => {
    if (!signedIn()) { show(html`${hero}${needSignIn('make pastes')}`); return; }
    const from = params().get('from') || '';
    let old = null;
    if (from) {
      const r = await pageCall('paste.get', { id: from });
      if (r.ok) old = r.paste;
    }
    show(html`<div class="box paste-new"><h2 class="boxhead">${old ? 'New paste (copied from "' + old.title + '")' : 'New paste'}</h2>
      <p class="small muted">Up to ${kMostBytes >> 10} KB of text. Pastes can't be changed after, but you can delete them or make a copy.
        Never paste passwords or keys, and follow the <a href="#/terms">rules</a>.</p>
      <form class="form" data-paste-new>
        <p><label>Title <span class="small muted">(optional)</span><br><input type="text" name="title" maxlength="80" value="${old ? old.title : ''}" style="width:100%"></label></p>
        <p><textarea name="text" class="paste-input" rows="18" spellcheck="false" placeholder="Paste your code or log here" required>${old ? old.text : ''}</textarea>
          <span class="small muted" id="pasteSize"></span></p>
        <div class="row paste-options">
          <label>Language<br><select name="lang">${Object.entries(PASTE_LANG_NAMES).map(([k, n]) =>
            html`<option value="${k}" ${(old ? old.lang : 'text') === k ? 'selected' : ''}>${n}</option>`)}</select></label>
          <label>Who can see it<br><select name="visibility"><option value="public">Everyone (in the list)</option>
            <option value="unlisted" ${old && old.visibility === 'unlisted' ? 'selected' : ''}>Only people with the link</option></select></label>
          <label>Runs out<br><select name="expires">${EXPIRES.map(([h, n]) => html`<option value="${h}">${n}</option>`)}</select></label>
        </div>
        <p><button class="btn green">Make paste</button> <span id="pasteMsg" class="small"></span></p>
      </form></div>`);
    const form = document.querySelector('[data-paste-new]');
    const msg = document.getElementById('pasteMsg'), size = document.getElementById('pasteSize');
    const enc = new TextEncoder();
    const sizeNow = () => {
      const n = enc.encode(form.text.value).length;
      size.textContent = sizeText(n) + ' of ' + (kMostBytes >> 10) + ' KB';
      size.className = 'small ' + (n > kMostBytes ? 'error' : 'muted');
    };
    form.text.addEventListener('input', sizeNow);
    sizeNow();
    // Tab types a tab (it's code), instead of jumping to the next box.
    form.text.addEventListener('keydown', (e) => {
      if (e.key !== 'Tab' || e.shiftKey || e.ctrlKey || e.altKey || e.metaKey) return;
      e.preventDefault();
      form.text.setRangeText('\t', form.text.selectionStart, form.text.selectionEnd, 'end');
      sizeNow();
    });
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      const btn = form.querySelector('button');
      btn.disabled = true; msg.className = 'small'; msg.textContent = 'Saving...';
      const r = await call('paste.make', { title: form.title.value, text: form.text.value, lang: form.lang.value,
        visibility: form.visibility.value, expires: Number(form.expires.value) });
      btn.disabled = false;
      if (!r.ok) { msg.textContent = r.error; msg.className = 'small error'; return; }
      toast('Made! Share the link to show it to people.');
      location.hash = '#/pasteview/' + r.paste.id;
    });
  };

  return pages;
}
