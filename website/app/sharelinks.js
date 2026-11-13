// Share links on the website (worker/sharelinks.js does the counting):
//   openShareLink(code, call)               the #/s/<code> page: count the click, then go to the thing
//   checkShareLinks(call, signedIn)         now and then: did someone who came by a link play / buy / follow?
//   addShareBox(view, kind, id, call, toast) a "Share links" box for the owner (games, items, profiles, groups)

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const PENDING = 'gb-share-pending', VISITOR = 'gb-visitor', KEEP = 7 * 86400 * 1000;
const WORDS = { game: ['play', 'played'], item: ['buy', 'bought'], profile: ['follow', 'followed'], group: ['join', 'joined'] };

function load(key, fallback) {
  try { const v = JSON.parse(localStorage.getItem(key)); return v ?? fallback; } catch { return fallback; }
}
function save(key, value) { try { localStorage.setItem(key, JSON.stringify(value)); } catch { /* private window: fine */ } }

// This browser's random id (so one person opening a link twice is one person).
function visitor() {
  let v = load(VISITOR, '');
  if (!/^[a-z0-9]{16,40}$/.test(v)) {
    v = [...crypto.getRandomValues(new Uint8Array(12))].map((b) => b.toString(16).padStart(2, '0')).join('');
    save(VISITOR, v);
  }
  return v;
}

export async function openShareLink(code, call) {
  const r = await call('share.open', { code, visitor: visitor() });
  if (!r.ok) return r;
  const list = load(PENDING, []).filter((p) => p && p.code !== code && Date.now() - p.at < KEEP).slice(-19);
  list.push({ code, at: Date.now() });
  save(PENDING, list);
  location.replace(r.path);
  return r;
}

let lastCheck = 0;
export async function checkShareLinks(call, signedIn) {
  if (!signedIn || Date.now() - lastCheck < 60000) return;
  lastCheck = Date.now();
  const list = load(PENDING, []).filter((p) => p && Date.now() - p.at < KEEP);
  save(PENDING, list);
  for (const p of list) { try { await call('share.done', { code: p.code }); } catch { /* next time */ } }
}

export function addShareBox(view, kind, id, call, toast) {
  if (!view) return;
  const box = document.createElement('div');
  box.className = 'box';
  box.id = 'shareLinks';
  view.appendChild(box);
  const [verb, did] = WORDS[kind];
  const draw = async () => {
    const r = await call('share.list', { kind, id });
    if (!r.ok) { box.innerHTML = '<h2 class="boxhead">Share links</h2><p class="error">' + esc(r.error) + '</p>'; return; }
    const rows = r.links.map((l) => {
      const url = location.origin + l.path;
      const t = l.total;
      return '<tr><td><b>' + esc(l.label) + '</b><br><span class="small muted">' + esc(url) + '</span></td>' +
        '<td>' + t.clicks + '</td><td>' + t.people + '</td><td>' + t[verb] + '</td><td>' + l.conversion + '%</td>' +
        (kind === 'item' ? '<td>' + t.spent + '</td>' : '') + (kind === 'game' || kind === 'item' ? '<td>' + t.favorite + '</td>' : '') +
        '<td><button type="button" class="btn small" data-copy="' + esc(url) + '">Copy</button> ' +
        '<button type="button" class="btn small red" data-remove="' + esc(l.code) + '">Remove</button></td></tr>';
    }).join('');
    box.innerHTML = '<h2 class="boxhead">Share links <span class="small muted">(' + r.links.length + ' of ' + r.most + ')</span></h2>' +
      '<p class="small muted">Make a link for each place you post (a video, a Discord server, a forum) and see which one brings people in: ' +
      'clicks, different people, and how many of them ' + did + ' it. Links never run out; removing one leaves this page alone. ' +
      'Numbers cover the last 90 days.</p>' +
      (rows ? '<div style="overflow-x:auto"><table class="ledger small" style="max-width:none"><tr><th>Link</th><th>Clicks</th><th>People</th><th>' +
        did[0].toUpperCase() + did.slice(1) + '</th><th>Rate</th>' + (kind === 'item' ? '<th>Bolts spent</th>' : '') +
        (kind === 'game' || kind === 'item' ? '<th>Favourited</th>' : '') + '<th></th></tr>' + rows + '</table></div>'
        : '<p class="muted">No links yet.</p>') +
      '<form class="form row" data-make><input type="text" name="label" maxlength="40" placeholder="Where it goes, like \'YouTube video\'">' +
      '<button class="btn green">Make a link</button></form>';
    box.querySelector('[data-make]').addEventListener('submit', async (e) => {
      e.preventDefault();
      const done = await call('share.create', { kind, id, label: e.currentTarget.label.value });
      if (!done.ok) { toast(done.error); return; }
      try { await navigator.clipboard.writeText(location.origin + done.link.path); toast('Link made and copied.'); } catch { toast('Link made.'); }
      draw();
    });
    box.querySelectorAll('[data-copy]').forEach((b) => b.addEventListener('click', async () => {
      try { await navigator.clipboard.writeText(b.dataset.copy); toast('Copied.'); } catch { toast(b.dataset.copy); }
    }));
    box.querySelectorAll('[data-remove]').forEach((b) => b.addEventListener('click', async () => {
      if (!window.confirm('Remove this link? People who click it will get nowhere.')) return;
      const done = await call('share.remove', { code: b.dataset.remove });
      if (!done.ok) { toast(done.error); return; }
      draw();
    }));
  };
  draw();
}
