// Content levels on the website (worker/ratings.js does the work). Everyone on Guts&Bolts is 18+,
// so these say how intense a game is, never that it's fine for kids.
//   ratingPill(g)                              a small "Strong" label for a game ('' when not rated)
//   mountGameRating(box, game, call, toast)    a game's Configure page: answer the questions, see the history
//   mountRatingWhy(box, game, call)            a game's page: why it got its level
//   mountRatingLimit(box, call, toast)         Settings: the highest level you want to see

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const day = (t) => new Date(t * 1000).toLocaleDateString();
export const RATING_NAMES = { mild: 'Mild', moderate: 'Moderate', strong: 'Strong', extreme: 'Extreme' };
const LEVELS = ['mild', 'moderate', 'strong', 'extreme'];
const nameOf = (r) => RATING_NAMES[r] || 'No content level yet';

// (Its few styles live here, so app.css isn't touched.)
if (typeof document !== 'undefined' && !document.getElementById('rating-style')) {
  const st = document.createElement('style');
  st.id = 'rating-style';
  st.textContent = '.rating-pill{font-weight:700;padding:1px 7px}.rating-mild{border-color:#2e8b57}.rating-moderate{border-color:#c9a400}' +
    '.rating-strong{border-color:#e07b00}.rating-extreme{border-color:#d4202a;color:#d4202a}.rating-q{border:0;padding:0;margin:0 0 10px}' +
    '#ratingWhy summary{cursor:pointer}';
  document.head.appendChild(st);
}

export const ratingPill = (g) => g && g.rating
  ? '<span class="badge-pill rating-pill rating-' + esc(g.rating) + '" title="Content level: ' + esc(nameOf(g.rating)) + '">' + esc(nameOf(g.rating)) + '</span>' : '';

// What the answers give (the same sum as the server, to show it before saving).
function ratingFor(questions, answers) {
  let level = 0;
  for (const q of questions) {
    const a = q.answers.find((x) => x.v === answers[q.id]);
    if (!a) return '';
    level = Math.max(level, a.level);
  }
  return LEVELS[level];
}

const historyRows = (history) => history.length ? '<div class="list">' + history.map((h) => '<div><span class="grow">' +
  esc(h.from ? nameOf(h.from) + ' → ' : '') + '<b>' + esc(nameOf(h.to)) + '</b> <span class="small muted">' + day(h.at) +
  (h.by ? ', by ' + esc(h.by.name) : '') + (h.staff ? ' (staff)' : '') + '</span></span></div>').join('') + '</div>'
  : '<p class="muted small">No changes yet.</p>';

export async function mountGameRating(box, game, call, toast) {
  if (!box) return;
  const r = await call('game.rating', { game });
  if (!r.ok) { box.innerHTML = '<h2 class="boxhead">Content level</h2><p class="error">' + esc(r.error) + '</p>'; return; }
  const answers = Object.assign({}, r.answers);
  box.innerHTML = '<h2 class="boxhead">Content level: <span data-now>' + esc(nameOf(r.rating)) + '</span></h2>' +
    '<p class="small muted">Answer these about what\'s in your game, and its content level comes from your answers (the strongest one counts). ' +
    'Everyone on Guts&amp;Bolts is 18+, so this isn\'t an age rating: it tells adults how intense your game is. ' +
    'Players can see the answers, and can hide games above the level they pick in Settings. Be honest: staff can fix a wrong level.</p>' +
    '<p class="small"><b>Don\'t make games for kids.</b> Games meant for children (or made to get kids to play) aren\'t allowed (Terms section 7).</p>' +
    '<form class="form" data-rate>' + r.questions.map((q) => '<fieldset class="rating-q"><legend><b>' + esc(q.text) + '</b></legend>' +
      q.answers.map((a) => '<label class="choice"><input type="radio" name="' + esc(q.id) + '" value="' + esc(a.v) + '"' +
        (answers[q.id] === a.v ? ' checked' : '') + '> ' + esc(a.label) +
        (a.level ? ' <span class="small muted">(' + esc(nameOf(LEVELS[a.level])) + ')</span>' : '') + '</label>').join('') + '</fieldset>').join('') +
    '<p><button class="btn green">Save content level</button> <span class="small muted" data-preview></span></p></form>' +
    '<h3>Content level history</h3><div data-history>' + historyRows(r.history) + '</div>';
  const form = box.querySelector('[data-rate]');
  const preview = () => {
    for (const q of r.questions) { const c = form.querySelector('input[name="' + q.id + '"]:checked'); answers[q.id] = c ? c.value : ''; }
    const got = ratingFor(r.questions, answers);
    box.querySelector('[data-preview]').textContent = got ? 'These answers make it ' + nameOf(got) + '.' : 'Answer every question.';
  };
  form.addEventListener('change', preview);
  preview();
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    preview();
    const done = await call('game.rate', { game, answers });
    if (!done.ok) { toast(done.error); return; }
    box.querySelector('[data-now]').textContent = nameOf(done.rating);
    box.querySelector('[data-history]').innerHTML = historyRows(done.history);
    toast('Content level saved: ' + nameOf(done.rating) + '.');
  });
}

export async function mountRatingWhy(box, game, call) {
  if (!box) return;
  const r = await call('game.rating', { game });
  if (!r.ok || !r.rating) { box.hidden = true; return; }
  const why = r.questions.map((q) => {
    const a = q.answers.find((x) => x.v === r.answers[q.id]);
    return a && a.v !== 'none' ? '<li>' + esc(q.text.replace(/\?.*$/, '?')) + ' ' + esc(a.label) + '</li>' : '';
  }).join('');
  box.hidden = false;
  box.innerHTML = '<details><summary><b>Content: ' + esc(nameOf(r.rating)) + '</b> <span class="small muted">(why?)</span></summary>' +
    (why ? '<p class="small muted">The creator said:</p><ul class="small">' + why + '</ul>' : '<p class="small muted">The creator said there\'s nothing strong in it.</p>') +
    '<p class="small muted">History:</p>' + historyRows(r.history) + '</details>';
}

export async function mountRatingLimit(box, call, toast) {
  if (!box) return;
  const r = await call('rating.limit', {});
  if (!r.ok) { box.hidden = true; return; }
  const opt = (v, label) => '<option value="' + v + '"' + (r.limit === v ? ' selected' : '') + '>' + label + '</option>';
  box.innerHTML = '<h2 class="boxhead">Game content levels</h2>' +
    '<p class="small muted">Every game can have a content level (Mild, Moderate, Strong or Extreme) that comes from what its creator says is in it. ' +
    'Pick the highest one you want to see, and games above it are left out of game lists for you. Games without a level yet still show.</p>' +
    '<form class="form row" data-limit><select name="limit" style="max-width:260px">' + opt('', 'Show every game') + opt('mild', 'Mild only') +
    opt('moderate', 'Up to Moderate') + opt('strong', 'Up to Strong') + opt('extreme', 'Up to Extreme') + '</select> <button class="btn green">Save</button></form>';
  box.querySelector('[data-limit]').addEventListener('submit', async (e) => {
    e.preventDefault();
    const done = await call('rating.limit', { limit: e.currentTarget.limit.value });
    toast(done.ok ? 'Saved.' : done.error);
  });
}
