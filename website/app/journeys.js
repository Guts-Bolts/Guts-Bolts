// Journeys on a game's Configure page (worker/journeys.js does the counting):
//   mountGameJourneys(box, game, call, toast)
// Lists the stages of a journey and the choices at each one, then draws where players went
// (a Sankey chart: the thicker a band, the more players took it; red bands are players leaving),
// and says in plain words where the most players are lost.

const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const day = (t) => new Date(t * 1000).toLocaleDateString();
const pct = (a, b) => (b > 0 ? Math.round((a / b) * 100) : 0) + '%';
const COLORS = ['#c81e1e', '#2563eb', '#16a34a', '#d97706', '#7c3aed', '#0891b2', '#db2777', '#4d7c0f', '#9333ea', '#0f766e'];
const LEFT_COLOR = '#9ca3af';

const ABOUT = '<p class="small muted">Follow the different ways players go through your game: which class they pick, whether they ' +
  'finish the tutorial, where they go next, and where they leave. Every journey starts with <b>Joined the game</b>. For each ' +
  'stage after it, list the choices players can make there: an event your game sends from a Script.</p>' +
  '<pre class="small" style="background:var(--panel2);border:1px solid var(--line);padding:8px;overflow-x:auto;white-space:pre">' +
  'local Analytics = game:GetService("AnalyticsService")\n' +
  'Analytics:LogCustomEvent(player, "ChoseWarrior")   -- a choice in "Pick a class"\n' +
  'Analytics:LogFunnelStepEvent(player, "Tutorial", nil, 3)   -- counts as funnel:Tutorial:3</pre>' +
  '<p class="small muted">A player\'s first choice at each stage counts, in order. Give ways around a stage their own event ' +
  '(like <b>SkippedTutorial</b>) so they show up too.</p>';

// The chart, as an SVG. Columns: Joined, then each stage (its choices, plus "Left" for players
// who left after the column before).
export function sankeySvg(j) {
  const players = j.players || 0;
  if (!players) return '<p class="muted">No players yet. Numbers show up after players join your published game.</p>';
  const cols = [[{ name: 'Joined the game', players, left: 0, color: '#374151' }]];
  j.stages.forEach((s, si) => {
    const col = s.nodes.map((n, i) => ({ name: n.name, players: n.players, left: n.left, color: COLORS[i % COLORS.length] }));
    const leftHere = si === 0 ? j.leftAtJoin : j.stages[si - 1].nodes.reduce((t, n) => t + n.left, 0);
    col.push({ name: 'Left', players: leftHere, left: 0, color: LEFT_COLOR, isLeft: true });
    cols.push(col);
  });
  const H = 300, GAP = 12, NW = 14, COLW = 170, PAD = 20;
  const most = Math.max(...cols.map((c) => c.reduce((t, n) => t + n.players, 0)));
  const busiest = Math.max(...cols.map((c) => c.length));
  const scale = (H - GAP * (busiest - 1)) / Math.max(most, 1);
  const hOf = (n) => (n.players > 0 ? Math.max(3, n.players * scale) : 0);
  cols.forEach((col, c) => {
    let y = PAD;
    col.forEach((n) => { n.x = PAD + c * COLW; n.y = y; n.h = hOf(n); n.outY = y; n.inY = y; y += n.h + (n.h ? GAP : 0); });
  });
  const W = PAD * 2 + (cols.length - 1) * COLW + 160;
  const links = [];
  const band = (a, b, n, color, title) => {
    if (n <= 0) return;
    const h = Math.max(1, n * scale);
    const x0 = a.x + NW, x1 = b.x, y0 = a.outY, y1 = b.inY, xm = (x0 + x1) / 2;
    a.outY += h; b.inY += h;
    links.push('<path d="M' + x0 + ' ' + y0 + ' C' + xm + ' ' + y0 + ' ' + xm + ' ' + y1 + ' ' + x1 + ' ' + y1 + ' L' + x1 + ' ' + (y1 + h) +
      ' C' + xm + ' ' + (y1 + h) + ' ' + xm + ' ' + (y0 + h) + ' ' + x0 + ' ' + (y0 + h) + ' Z" fill="' + color + '" fill-opacity="0.28">' +
      '<title>' + esc(title) + '</title></path>');
  };
  // Flows stage by stage, in node order (so bands don't cross more than they must).
  j.stages.forEach((s, si) => {
    const from = cols[si], to = cols[si + 1];
    const here = j.flows.filter((f) => f.stage === si).sort((a, b) => (a.from - b.from) || (a.to - b.to));
    for (const f of here) {
      const a = si === 0 ? from[0] : from[f.from], b = to[f.to];
      if (a && b) band(a, b, f.players, b.color, f.players + ' players: ' + a.name + ' → ' + b.name);
    }
    // ...and the ones who left after this column.
    const leftNode = to[to.length - 1];
    if (si === 0) band(from[0], leftNode, j.leftAtJoin, LEFT_COLOR, j.leftAtJoin + ' players left before ' + s.name);
    else from.forEach((a) => { if (!a.isLeft) band(a, leftNode, a.left, LEFT_COLOR, a.left + ' players left after ' + a.name); });
  });
  const nodes = [];
  cols.forEach((col, c) => col.forEach((n) => {
    if (!n.h) return;
    nodes.push('<rect x="' + n.x + '" y="' + n.y + '" width="' + NW + '" height="' + n.h + '" fill="' + n.color + '" rx="2">' +
      '<title>' + esc(n.name + ': ' + n.players + ' players') + '</title></rect>');
    nodes.push('<text x="' + (n.x + NW + 5) + '" y="' + (n.y + Math.min(n.h, 26) / 2 + 4) + '" font-size="11" ' +
      'style="paint-order:stroke;stroke:#fff;stroke-width:3px" fill="' + (n.isLeft ? '#6b7280' : '#111') + '">' +
      esc(n.name) + ' <tspan font-weight="700">' + n.players + '</tspan> <tspan fill="#6b7280">' + pct(n.players, players) + '</tspan></text>');
  }));
  const heads = ['Start', ...j.stages.map((s) => s.name)].map((t, c) =>
    '<text x="' + (PAD + c * COLW) + '" y="12" font-size="11" font-weight="700" fill="#374151">' + esc(t) + '</text>').join('');
  const height = PAD + H + 30;
  return '<div style="overflow-x:auto;border:1px solid var(--line,#ddd);border-radius:6px;background:#fff">' +
    '<svg role="img" aria-label="Where players went in ' + esc(j.name) + '" width="' + W + '" height="' + height + '" viewBox="0 0 ' + W + ' ' + height +
    '" style="display:block;font-family:inherit">' + heads + links.join('') + nodes.join('') + '</svg></div>';
}

// What the numbers say, in plain words: where the most players leave, and choices few pick.
export function journeyInsights(j) {
  const out = [];
  const players = j.players || 0;
  if (!players) return out;
  if (j.stages.length && j.leftAtJoin / players >= 0.2)
    out.push(pct(j.leftAtJoin, players) + ' of players left before <b>' + esc(j.stages[0].name) + '</b>. The start of your game is where most are lost: get them to it sooner.');
  let worst = null;
  j.stages.forEach((s, si) => s.nodes.forEach((n) => {
    if (si === j.stages.length - 1 || n.players < 10) return;
    const r = n.left / n.players;
    if (!worst || r > worst.r) worst = { r, n, s };
  }));
  if (worst && worst.r >= 0.25)
    out.push('Of the ' + worst.n.players + ' players who reached <b>' + esc(worst.n.name) + '</b> (' + esc(worst.s.name) + '), ' +
      pct(worst.n.left, worst.n.players) + ' left there. That\'s the biggest drop: look at what comes right after it.');
  for (const s of j.stages) {
    const total = s.nodes.reduce((t, n) => t + n.players, 0);
    if (s.nodes.length < 2 || total < 20) continue;
    for (const n of s.nodes)
      if (n.players / total < 0.1) out.push('Few players pick <b>' + esc(n.name) + '</b> in ' + esc(s.name) + ' (' + pct(n.players, total) + '). Is it easy to find, or worth choosing?');
  }
  if (players < 50) out.push('Only ' + players + ' players so far: wait for at least 50 before deciding anything.');
  return out;
}

const stageRow = (i, s) => '<div class="journey-stage" style="display:flex;gap:6px;flex-wrap:wrap;margin:4px 0">' +
  '<input type="text" name="stage" maxlength="40" placeholder="Stage ' + (i + 1) + ' (like Pick a class)" value="' + esc(s ? s.name : '') + '" style="flex:1;min-width:140px">' +
  '<input type="text" name="nodes" maxlength="700" placeholder="Choices: ChoseWarrior, ChoseMage, SkippedClass" value="' + esc(s ? s.nodes.join(', ') : '') + '" style="flex:2;min-width:200px"></div>';

export async function mountGameJourneys(box, game, call, toast) {
  if (!box) return;
  const draw = async () => {
    const r = await call('journeys.list', { game });
    if (!r.ok) { box.innerHTML = '<h2 class="boxhead">Journeys</h2><p class="error">' + esc(r.error) + '</p>'; return; }
    const list = r.journeys.slice().reverse().map((j) => {
      const tips = journeyInsights(j);
      return '<div class="journey" style="margin:12px 0;padding-top:8px;border-top:1px solid var(--line, #ddd)">' +
        '<p><b>' + esc(j.name) + '</b> <span class="small muted">' + j.stages.length + ' stages · ' + j.players + ' players · since ' + day(j.started) +
        (j.stopped ? ' · stopped ' + day(j.stopped) : ' · counting') + '</span> ' +
        (j.stopped ? '' : '<button type="button" class="btn small" data-stop="' + esc(j.id) + '">Stop</button> ') +
        '<button type="button" class="btn small red" data-remove="' + esc(j.id) + '" data-name="' + esc(j.name) + '">Remove</button></p>' +
        sankeySvg(j) +
        (tips.length ? '<ul class="small" style="margin:6px 0 0 18px">' + tips.map((t) => '<li>' + t + '</li>').join('') + '</ul>' : '') + '</div>';
    }).join('');
    box.innerHTML = '<h2 class="boxhead">Journeys <span class="small muted">(where players go, and where they leave)</span></h2>' + ABOUT +
      (list || '<p class="muted">No journeys yet.</p>') +
      '<form class="form" data-make><h3>New journey</h3>' +
      '<label>Name</label><input type="text" name="name" maxlength="40" placeholder="First ten minutes" required>' +
      '<label>Stages <span class="small muted">(after "Joined the game"; up to ' + r.mostStages + ', each with up to ' + r.mostNodes +
      ' choices, with commas)</span></label><div data-stages>' + stageRow(0) + stageRow(1) + '</div>' +
      '<p><button type="button" class="btn small" data-more>Add a stage</button></p>' +
      '<p><button class="btn green">Start counting</button></p></form>';
    const stagesBox = box.querySelector('[data-stages]');
    box.querySelector('[data-more]').addEventListener('click', () => {
      const n = stagesBox.querySelectorAll('.journey-stage').length;
      if (n >= r.mostStages) { toast('A journey can have up to ' + r.mostStages + ' stages.'); return; }
      stagesBox.insertAdjacentHTML('beforeend', stageRow(n));
    });
    box.querySelector('[data-make]').addEventListener('submit', async (ev) => {
      ev.preventDefault();
      const f = ev.currentTarget;
      const stages = [...stagesBox.querySelectorAll('.journey-stage')].map((row) => ({
        name: row.querySelector('[name=stage]').value,
        nodes: row.querySelector('[name=nodes]').value.split(',').map((x) => x.trim()).filter(Boolean),
      })).filter((s) => s.name.trim() || s.nodes.length);
      const done = await call('journeys.create', { game, name: f.name.value, stages });
      if (!done.ok) { toast(done.error); return; }
      toast('Journey started. It counts players who join from now on.');
      draw();
    });
    box.querySelectorAll('[data-stop]').forEach((b) => b.addEventListener('click', async () => {
      const done = await call('journeys.stop', { game, id: b.dataset.stop });
      if (!done.ok) { toast(done.error); return; }
      draw();
    }));
    box.querySelectorAll('[data-remove]').forEach((b) => b.addEventListener('click', async () => {
      if (!window.confirm('Remove "' + b.dataset.name + '" and all its numbers?')) return;
      const done = await call('journeys.remove', { game, id: b.dataset.remove });
      if (!done.ok) { toast(done.error); return; }
      draw();
    }));
  };
  draw();
}
