// Experiments (A/B tests) on a game's Configure page (worker/experiments.js does the counting):
//   mountGameExperiments(box, game, call, toast)
// Make one (a name, 2-4 versions, up to 3 things to count), then watch each version's numbers
// side by side. A green "real" mark means the difference is big enough not to be luck.

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const day = (t) => new Date(t * 1000).toLocaleDateString();
const pct = (a, b) => (b > 0 ? Math.round((a / b) * 1000) / 10 : 0) + '%';
const per = (a, b) => (b > 0 ? Math.round((a / b) * 100) / 100 : 0);

const ABOUT = '<p class="small muted">Try two to four versions of something in your game (a new shop, a shorter tutorial, ' +
  'a bigger reward) and see which one players like more. Each player always gets the same version. In a Script:</p>' +
  '<pre class="small" style="background:var(--panel2);border:1px solid var(--line);padding:8px;overflow-x:auto;white-space:pre">local version = game:GetService("ExperimentService"):GetVariantAsync(player, "Shop")\n' +
  'if version == "New" then ... end\n' +
  'game:GetService("AnalyticsService"):LogCustomEvent(player, "BoughtSword", 1)</pre>' +
  '<p class="small muted">Things to count can be an event name (LogCustomEvent), <b>spent:Coins</b> or <b>earned:Coins</b> ' +
  '(LogEconomyEvent), or <b>funnel:Tutorial:3</b> (players who reached step 3 of LogFunnelStepEvent). Players, minutes played ' +
  'and coming back on another day are always counted. Test a version in Studio with ' +
  '<code>ExperimentService:SetTestVariant("Shop", "New")</code>.</p>';

function results(e) {
  const r = e.results || [];
  if (!r.length) return '';
  const first = r[0];
  const sure = '<span class="small" style="color:#1a8f3a;font-weight:700" title="Big enough not to be luck (95% sure)"> real</span>';
  const head = '<tr><th></th>' + r.map((v) => '<th>' + esc(v.name) + '</th>').join('') + '</tr>';
  const row = (label, cell) => '<tr><td>' + label + '</td>' + r.map(cell).join('') + '</tr>';
  let rows = row('Players', (v) => '<td>' + v.players + '</td>') +
    row('Minutes per player', (v) => '<td>' + per(v.minutes, v.players) + '</td>') +
    row('Came back another day', (v) => '<td>' + pct(v.cameBack, v.players) + (v.cameBackSure ? sure : '') + '</td>');
  e.metrics.forEach((m, i) => {
    rows += row('<b>' + esc(m) + '</b>: players who did it', (v) => '<td>' + pct(v.metrics[i].did, v.players) + (v.metrics[i].sure ? sure : '') + '</td>');
    rows += row('<b>' + esc(m) + '</b>: per player', (v) => '<td>' + per(v.metrics[i].total, v.players) + '</td>');
  });
  const few = first.players < 100 || r.some((v) => v.players < 100);
  return '<div style="overflow-x:auto"><table class="ledger small" style="max-width:none">' + head + rows + '</table></div>' +
    (few ? '<p class="small muted">Wait for at least 100 players in each version before deciding.</p>' : '');
}

export async function mountGameExperiments(box, game, call, toast) {
  if (!box) return;
  const draw = async () => {
    const r = await call('experiments.list', { game });
    if (!r.ok) { box.innerHTML = '<h2 class="boxhead">Experiments</h2><p class="error">' + esc(r.error) + '</p>'; return; }
    const list = r.experiments.slice().reverse().map((e) => '<div class="experiment" style="margin:12px 0;padding-top:8px;border-top:1px solid var(--line, #ddd)">' +
      '<p><b>' + esc(e.name) + '</b> <span class="small muted">' + esc(e.variants.join(' / ')) + ' · since ' + day(e.started) +
      (e.stopped ? ' · stopped ' + day(e.stopped) : ' · running') + '</span> ' +
      (e.stopped ? '' : '<button type="button" class="btn small" data-stop="' + esc(e.id) + '">Stop</button> ') +
      '<button type="button" class="btn small red" data-remove="' + esc(e.id) + '" data-name="' + esc(e.name) + '">Remove</button></p>' +
      results(e) + '</div>').join('');
    box.innerHTML = '<h2 class="boxhead">Experiments <span class="small muted">(A/B tests)</span></h2>' + ABOUT +
      (list || '<p class="muted">No experiments yet.</p>') +
      '<form class="form" data-make><h3>New experiment</h3>' +
      '<label>Name</label><input type="text" name="name" maxlength="40" placeholder="Shop" required>' +
      '<label>Versions <span class="small muted">(2 to 4, with commas; the first is what the others are compared with)</span></label>' +
      '<input type="text" name="variants" maxlength="100" placeholder="Old, New" required>' +
      '<label>Things to count <span class="small muted">(up to 3, with commas; optional)</span></label>' +
      '<input type="text" name="metrics" maxlength="200" placeholder="BoughtSword, spent:Coins, funnel:Tutorial:3">' +
      '<p><button class="btn green">Start experiment</button></p></form>';
    box.querySelector('[data-make]').addEventListener('submit', async (ev) => {
      ev.preventDefault();
      const f = ev.currentTarget;
      const split = (s) => s.split(',').map((x) => x.trim()).filter(Boolean);
      const done = await call('experiments.create', { game, name: f.name.value, variants: split(f.variants.value), metrics: split(f.metrics.value) });
      if (!done.ok) { toast(done.error); return; }
      toast('Experiment started. Players get their version the next time they join.');
      draw();
    });
    box.querySelectorAll('[data-stop]').forEach((b) => b.addEventListener('click', async () => {
      const done = await call('experiments.stop', { game, id: b.dataset.stop });
      if (!done.ok) { toast(done.error); return; }
      draw();
    }));
    box.querySelectorAll('[data-remove]').forEach((b) => b.addEventListener('click', async () => {
      if (!window.confirm('Remove "' + b.dataset.name + '" and all its numbers?')) return;
      const done = await call('experiments.remove', { game, id: b.dataset.remove });
      if (!done.ok) { toast(done.error); return; }
      draw();
    }));
  };
  draw();
}
