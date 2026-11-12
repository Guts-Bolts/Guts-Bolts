// Profile backgrounds on the website (worker/backgrounds.js does the work):
//   backgroundStyle(bg)                           the CSS that puts a background behind the avatar
//   mountBackgroundPicker(box, bg, call, toast)   your own profile: pick a decal, or take it off

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

export const backgroundStyle = (bg) => bg ? "background:#222 center / cover no-repeat url('/decal/" + encodeURIComponent(bg.id) + "')" : '';

export function mountBackgroundPicker(box, bg, call, toast) {
  if (!box) return;
  const avatarBox = box.closest('.avatar-box');
  const draw = (cur) => {
    box.innerHTML = '<details class="bg-pick"><summary class="small">' + (cur ? 'Background: ' + esc(cur.name) : 'Add a background') + '</summary>' +
      '<p class="small muted">Put a picture behind your avatar. Any decal from the <a href="#/create/library?cat=decal">Library</a> works: ' +
      'paste its number or ID, or upload your own decal first.</p>' +
      '<form class="form row" data-bg><input type="text" name="decal" maxlength="80" placeholder="Decal number or ID" value="' + esc(cur ? cur.num || cur.id : '') + '">' +
      '<button class="btn small green">Use it</button>' + (cur ? ' <button type="button" class="btn small" data-off>Take it off</button>' : '') + '</form></details>';
    const set = async (decal) => {
      const r = await call('profile.background', { decal });
      if (!r.ok) { toast(r.error); return; }
      avatarBox.style.cssText = backgroundStyle(r.background);
      avatarBox.classList.toggle('has-background', !!r.background);
      toast(r.background ? 'Background set.' : 'Background taken off.');
      draw(r.background);
    };
    box.querySelector('[data-bg]').addEventListener('submit', (e) => { e.preventDefault(); set(e.currentTarget.decal.value); });
    const off = box.querySelector('[data-off]');
    if (off) off.addEventListener('click', () => set(''));
  };
  draw(bg);
}

if (typeof document !== 'undefined' && !document.getElementById('bg-style')) {
  const st = document.createElement('style');
  st.id = 'bg-style';
  st.textContent = '.avatar-box.has-background .small.muted{color:#fff;text-shadow:0 1px 3px #000}' +
    '.avatar-box.has-background .bg-pick summary{color:#fff;text-shadow:0 1px 3px #000}.bg-pick{margin-top:6px;text-align:left}' +
    '.avatar-box.has-background .bg-pick[open]{background:var(--panel);padding:6px;border-radius:6px}' +
    '.avatar-box.has-background .bg-pick[open] .small.muted{color:inherit;text-shadow:none}';
  document.head.appendChild(st);
}
