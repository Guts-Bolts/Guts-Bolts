// The "Web requests and secrets" box on a game's Configure page (worker/game-web.js does the work).
// Turn web requests on, and save secrets (API keys) the game's server scripts can use with
// HttpService:GetSecret("Name"). A secret's value is never shown again, not even to you.

const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// Fills `box` (an empty element) for game `game` (its id). `call(op, args)` talks to the server.
export async function mountGameWeb(box, game, call, toast) {
  const draw = (r) => {
    if (!r.ok) { box.innerHTML = '<h2 class="boxhead">Web requests and secrets</h2><p class="error">' + esc(r.error) + '</p>'; return; }
    const rows = r.secrets.map((x) => '<div><span class="grow"><b>' + esc(x.name) + '</b> <span class="small muted">for ' + esc(x.host) +
      (x.dedicatedOnly ? ' · Guts&amp;Bolts game servers only' : '') + '</span><br><code class="small">HttpService:GetSecret("' + esc(x.name) +
      '")</code></span><button type="button" class="btn small red" data-secret-remove="' + esc(x.name) + '">Remove</button></div>').join('');
    box.innerHTML = '<h2 class="boxhead">Web requests and secrets</h2>' +
      '<p class="small muted">Let this game\'s server Scripts talk to other websites with <code>HttpService:RequestAsync</code>, ' +
      '<code>GetAsync</code> and <code>PostAsync</code>. The requests go through Guts&amp;Bolts, never straight from the computer ' +
      'hosting the game, and only to sites on our list: ' + r.hosts.map(esc).join(', ') + '.</p>' +
      '<label class="choice"><input type="checkbox" data-web-on ' + (r.http ? 'checked' : '') + '> Allow web requests</label>' +
      '<h3>Secrets</h3><p class="small muted">API keys your scripts use, like a Lyreto bridge key. Scripts only get a stand-in ' +
      '(<code>HttpService:GetSecret("LyretoKey")</code>) that can go in a request\'s Headers; Guts&amp;Bolts puts the real value in ' +
      'on the way out, so whoever hosts a server can\'t see or copy it. Once saved, nobody can read a secret back, not even you: ' +
      'to change one, save it again with the same name.</p>' +
      (rows ? '<div class="list">' + rows + '</div>' : '<p class="muted">No secrets yet.</p>') +
      '<form class="form" data-secret-form><label>Name <span class="muted small">(letters, numbers and _)</span></label>' +
      '<input type="text" name="name" maxlength="50" pattern="[A-Za-z0-9_]+" placeholder="LyretoKey" required>' +
      '<label>Value</label><input type="password" name="value" maxlength="2048" autocomplete="off" required>' +
      '<label>Only sent to</label><select name="host">' + r.hosts.map((h) => '<option>' + esc(h) + '</option>').join('') + '</select>' +
      '<label class="choice"><input type="checkbox" name="dedicatedOnly"> Only on Guts&amp;Bolts game servers ' +
      '<span class="muted small">(not when a player hosts the game; the safest choice, but it only works while a Guts&amp;Bolts game server is running)</span></label>' +
      '<p><button class="btn green">Save secret</button></p></form>';
    box.querySelector('[data-web-on]').addEventListener('change', async (e) => {
      const done = await call('game.settings', { id: game, http: e.target.checked });
      toast(done.ok ? (e.target.checked ? 'Web requests are on.' : 'Web requests are off.') : done.error);
      if (!done.ok) e.target.checked = !e.target.checked;
    });
    box.querySelector('[data-secret-form]').addEventListener('submit', async (e) => {
      e.preventDefault();
      const f = e.currentTarget;
      const done = await call('game.secretSet', { id: game, name: f.name.value, value: f.value.value, host: f.host.value, dedicatedOnly: f.dedicatedOnly.checked });
      f.value.value = '';
      if (!done.ok) { toast(done.error); return; }
      toast('Secret saved.');
      draw(done);
    });
    box.querySelectorAll('[data-secret-remove]').forEach((b) => b.addEventListener('click', async () => {
      if (!confirm('Remove the secret ' + b.dataset.secretRemove + '? Scripts using it stop working.')) return;
      const done = await call('game.secretRemove', { id: game, name: b.dataset.secretRemove });
      if (!done.ok) toast(done.error); else draw(done);
    }));
  };
  draw(await call('game.secrets', { id: game }));
}
