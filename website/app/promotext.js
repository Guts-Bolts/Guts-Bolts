// Promotional text on a game's listing (worker/promotext.js): a short line, separate from the
// game's name, for what's happening right now ("Halloween event: 2x candy!").
//   promoLine(g)                               -> the line for a game card / page ('' if none)
//   mountGamePromo(box, game, current, call, toast)   the box on the game's Configure page

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const MOST = 80;

// (its own small style, so app.css stays as it is)
if (typeof document !== 'undefined' && !document.getElementById('promoStyle')) {
  const st = document.createElement('style');
  st.id = 'promoStyle';
  st.textContent = '.promo-line{color:var(--red,#c81e1e);font-weight:700;font-size:12px;line-height:1.3;margin-top:2px;' +
    'overflow:hidden;text-overflow:ellipsis;white-space:nowrap}' +
    '.promo-big{display:inline-block;background:#fff3f3;border:1px solid #f3c3c3;color:var(--red,#c81e1e);font-weight:700;' +
    'padding:4px 10px;border-radius:4px;margin:4px 0;max-width:100%}' +
    '.promo-preview{border:1px dashed var(--line,#ccc);padding:8px;border-radius:6px;max-width:260px;margin:6px 0}';
  document.head.appendChild(st);
}

const left = (until) => {
  const s = until - Math.floor(Date.now() / 1000);
  if (s <= 0) return 'ended';
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600);
  return d >= 1 ? d + (d === 1 ? ' day' : ' days') + ' left' : Math.max(1, h) + (h === 1 ? ' hour' : ' hours') + ' left';
};

// On cards (small), and on the game's own page (big).
export function promoLine(g, big = false) {
  if (!g || !g.promo || !g.promo.text) return '';
  return big ? '<p><span class="promo-big" title="' + esc(left(g.promo.until)) + '">&#128227; ' + esc(g.promo.text) + '</span></p>'
    : '<div class="promo-line" title="' + esc(g.promo.text) + '">' + esc(g.promo.text) + '</div>';
}

export function mountGamePromo(box, game, current, call, toast) {
  if (!box) return;
  let promo = current || null;
  const draw = () => {
    box.innerHTML = '<h2 class="boxhead">Promotional text</h2>' +
      '<p class="small muted">A short line under your game\'s name on its card and its page, for what\'s on right now: an event, ' +
      'an update, a limited-time mode. Your game\'s name stays the same. It ends by itself, so old news doesn\'t hang around.</p>' +
      (promo ? '<p>Showing now: <span class="promo-big">' + esc(promo.text) + '</span> <span class="small muted">(' + esc(left(promo.until)) + ')</span> ' +
        '<button type="button" class="btn small red" data-clear>Take it off</button></p>' : '<p class="muted">Nothing showing right now.</p>') +
      '<form class="form" data-promo>' +
      '<label>Text <span class="small muted">(up to ' + MOST + ' letters, one line)</span></label>' +
      '<input type="text" name="text" maxlength="' + MOST + '" placeholder="Halloween event: 2x candy until Sunday!" value="' + esc(promo ? promo.text : '') + '">' +
      '<label>Show it for</label><select name="days">' + [1, 3, 7, 14, 30].map((d) => '<option value="' + d + '"' + (d === 7 ? ' selected' : '') + '>' +
        d + (d === 1 ? ' day' : ' days') + '</option>').join('') + '</select>' +
      '<div class="promo-preview small"><b>Preview</b><div data-preview></div></div>' +
      '<p><button class="btn green">Show it</button></p></form>';
    const f = box.querySelector('[data-promo]');
    const preview = () => { box.querySelector('[data-preview]').innerHTML = f.text.value.trim()
      ? '<div class="promo-line">' + esc(f.text.value.trim()) + '</div>' : '<span class="muted">(type something)</span>'; };
    f.text.addEventListener('input', preview);
    preview();
    f.addEventListener('submit', async (ev) => {
      ev.preventDefault();
      const r = await call('game.promo', { game, text: f.text.value, days: Number(f.days.value) });
      if (!r.ok) { toast(r.error); return; }
      promo = r.promo;
      toast(promo ? 'Your promotional text is showing.' : 'Promotional text taken off.');
      draw();
    });
    const clear = box.querySelector('[data-clear]');
    if (clear) clear.addEventListener('click', async () => {
      const r = await call('game.promo', { game, text: '' });
      if (!r.ok) { toast(r.error); return; }
      promo = null;
      toast('Promotional text taken off.');
      draw();
    });
  };
  draw();
}
