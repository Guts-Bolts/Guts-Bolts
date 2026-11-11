// Version history for your own creations on their Library page (worker/server.js asset.versions /
// asset.restore / update). Games have theirs on the Configure page.
//   mountAssetVersions(box, asset, call, toast, fileBase64)
// Sounds and decals can get a new file here; models, plugins and animations get new versions when
// you publish them again from Studio. Either way the ID stays the same, so every game using it
// gets the new one, and Restore puts an old one back (as a new version, so nothing is lost).

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const when = (t) => new Date(t * 1000).toLocaleString();
const size = (n) => n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB';

export async function mountAssetVersions(box, a, call, toast, fileBase64) {
  if (!box) return;
  const newFile = a.kind === 'audio' || a.kind === 'decal';
  const ext = a.kind === 'audio' && a.meta && a.meta.ext ? a.meta.ext : '';
  const accept = a.kind === 'decal' ? 'image/png,image/jpeg,image/webp' : ext ? '.' + ext : 'audio/*';
  const draw = async () => {
    const r = await call('asset.versions', { id: a.id });
    if (!r.ok) { box.innerHTML = '<h2 class="boxhead">Versions</h2><p class="error">' + esc(r.error) + '</p>'; return; }
    const rows = r.versions.map((v) => '<tr><td><b>' + v.v + '</b>' + (v.restored ? ' <span class="small muted">(restored from ' + v.restored + ')</span>' : '') +
      '</td><td>' + esc(when(v.at)) + '</td><td>' + esc(v.byName) + '</td><td>' + size(v.size || 0) + '</td><td>' +
      (v.live ? '<span class="tag-live">Live now</span>' : '<button type="button" class="btn small" data-v="' + v.v + '">Restore</button>') + '</td></tr>').join('');
    box.innerHTML = '<h2 class="boxhead">Versions</h2>' +
      '<p class="small muted">Its ID stays the same, so every game using it gets the newest version. Restore puts an old one back ' +
      '(as a new version, so nothing is lost). The newest ' + r.most + ' are kept.</p>' +
      (newFile
        ? '<form class="form row" data-up><input type="file" name="file" accept="' + esc(accept) + '" required>' +
          '<button class="btn green">Upload a new version</button></form>' +
          (ext ? '<p class="small muted">It has to be a .' + esc(ext) + ' file, like the first one.</p>' : '') +
          (a.kind === 'decal' || a.kind === 'audio' ? '<p class="small muted">Unless you\'re Verified, a new version is checked by staff again before others see it.</p>' : '')
        : '<p class="small">To make a new version, publish it again from Studio.</p>') +
      '<table class="versions"><tr><th>Version</th><th>Made</th><th>By</th><th>Size</th><th></th></tr>' + rows + '</table>';
    const up = box.querySelector('[data-up]');
    if (up) up.addEventListener('submit', async (e) => {
      e.preventDefault();
      const f = up.file.files[0];
      if (!f) return;
      if (ext && (f.name.split('.').pop() || '').toLowerCase() !== ext) { toast('Pick a .' + ext + ' file.'); return; }
      up.querySelector('button').disabled = true;
      const done = await call('update', { id: a.id, data: await fileBase64(f) });
      up.querySelector('button').disabled = false;
      if (!done.ok) { toast(done.error); return; }
      toast(done.newVersion ? 'Version ' + done.version + ' is live.' : 'That\'s the same file as now, so nothing changed.');
      draw();
    });
    box.querySelectorAll('[data-v]').forEach((b) => b.addEventListener('click', async () => {
      if (!confirm('Put version ' + b.dataset.v + ' back? It becomes the live one (as a new version), and every game using it gets it.')) return;
      const done = await call('asset.restore', { id: a.id, version: Number(b.dataset.v) });
      toast(done.ok ? 'Version ' + b.dataset.v + ' is live again (as version ' + done.version + ').' : done.error, 5000);
      draw();
    }));
  };
  draw();
}
