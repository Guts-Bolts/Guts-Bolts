import { signedRequest } from '/app/gb.js';

const $ = (id) => document.getElementById(id);
const message = $('message');
let lastAudit = new Set();
let notificationReady = false;
let auditInitialized = false;

function say(text, kind = '') {
  message.textContent = text;
  message.className = 'notice' + (kind ? ' ' + kind : '');
}
async function api(action) {
  const auth = await signedRequest('staff.database.stats', {});
  const response = await fetch('/api/remote', {
    method: 'POST',
    credentials: 'same-origin',
    cache: 'no-store',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ auth, action }),
  });
  const result = await response.json();
  if (!response.ok || !result.ok) throw new Error(result.error || 'Remote request failed.');
  return result;
}
function timeText(seconds) {
  if (!seconds) return 'Never';
  return new Date(seconds * 1000).toLocaleString();
}
function percent(value) { return Number.isFinite(value) ? Math.round(value) + '%' : '—'; }
function renderStatus(result) {
  const online = result.online === true;
  $('connection').textContent = online ? 'PC online' : 'PC offline';
  $('connection').className = 'pill ' + (online ? 'online' : 'offline');
  $('last-seen').textContent = result.lastSeen ? 'Last heartbeat: ' + timeText(result.lastSeen) : 'No heartbeat received';
  const s = result.status || {};
  $('cpu').textContent = percent(s.cpuPercent);
  $('memory').textContent = percent(s.memoryPercent);
  $('memory-detail').textContent = s.memoryUsedMB != null && s.memoryTotalMB != null ? Math.round(s.memoryUsedMB) + ' / ' + Math.round(s.memoryTotalMB) + ' MB' : '';
  $('gpu').textContent = percent(s.gpuPercent);
  $('gpu-detail').textContent = s.gpuTemperatureC != null ? Math.round(s.gpuTemperatureC) + '°C' : 'GPU telemetry unavailable';
  $('server-state').textContent = s.serverRunning ? 'Running' : 'Stopped';
  $('server-detail').textContent = (s.serverPid ? 'PID ' + s.serverPid : 'No process') + (s.serverUptimeSeconds ? ' · ' + Math.floor(s.serverUptimeSeconds / 60) + ' min' : '');
  if (s.slots) $('slots').value = s.slots;
  if (s.priority) $('priority').value = s.priority;
  $('auto-restart').checked = s.autoRestart === true;
}
function renderAudit(entries) {
  const list = $('audit');
  list.replaceChildren();
  if (!entries.length) {
    const li = document.createElement('li'); li.className = 'muted'; li.textContent = 'No events recorded yet.'; list.append(li); return;
  }
  for (const entry of entries) {
    const li = document.createElement('li');
    const time = document.createElement('time'); time.textContent = timeText(entry.at);
    const detail = document.createElement('span'); detail.textContent = entry.action + ' · ' + entry.result;
    li.append(time, detail); list.append(li);
    if (auditInitialized && notificationReady && entry.action.startsWith('alert.') && !lastAudit.has(entry.id)) {
      try { new Notification('Guts & Bolts server alert', { body: entry.result, tag: entry.id }); } catch { /* notifications unavailable */ }
    }
  }
  lastAudit = new Set(entries.map((x) => x.id));
  auditInitialized = true;
}
async function refresh() {
  try {
    const status = await api({ action: 'status' });
    renderStatus(status);
    renderAudit(status.entries || []);
    say(status.online ? 'Secure connection established. The home PC is responding.' : 'The home PC is offline or its agent has not checked in.', status.online ? 'good' : '');
  } catch (error) {
    $('connection').textContent = 'Access denied';
    $('connection').className = 'pill offline';
    say(error.message + ' Sign in to the official Guts & Bolts account in the Player, then reload this page.', 'error');
  }
}
async function command(name, args = {}, confirmText = '') {
  if (confirmText && !window.confirm(confirmText)) return;
  try {
    say('Sending ' + name + '…');
    const result = await api({ action: 'command', command: name, args });
    say('Command accepted. The PC agent will acknowledge it shortly.', 'good');
    await refresh();
  } catch (error) { say(error.message, 'error'); }
}
document.querySelectorAll('[data-command]').forEach((button) => button.addEventListener('click', () => {
  const commandName = button.dataset.command;
  const prompt = commandName === 'stop' ? 'Stop the dedicated game server?' :
    commandName === 'restart' ? 'Restart the dedicated game server? Active sessions may disconnect.' :
    'Start the dedicated game server?';
  command(commandName, {}, prompt);
}));
$('refresh').addEventListener('click', refresh);
$('save-slots').addEventListener('click', () => {
  const slots = Number($('slots').value);
  if (!Number.isInteger(slots) || slots < 1 || slots > 50) return say('Slots must be a whole number from 1 to 50.', 'error');
  command('set-slots', { slots }, 'Changing slots restarts the dedicated server and disconnects active players. Continue?');
});
$('save-priority').addEventListener('click', () => command('set-priority', { priority: $('priority').value }));
$('save-auto').addEventListener('click', () => command('set-auto-restart', { enabled: $('auto-restart').checked }));
$('pair').addEventListener('click', async () => {
  const code = $('pair-code').value.trim().toLowerCase();
  if (!/^[a-f0-9]{64}$/.test(code)) return say('Pairing code must be the complete 64-character hexadecimal code shown on the PC.', 'error');
  try {
    await api({ action: 'pair', code });
    $('pair-code').value = '';
    say('PC paired. Its device credential was delivered over the encrypted connection.', 'good');
    await refresh();
  } catch (error) { say(error.message, 'error'); }
});
$('revoke').addEventListener('click', async () => {
  if (!window.confirm('Immediately revoke this PC agent credential? It will disconnect and require a new pairing.')) return;
  try { await api({ action: 'revoke' }); say('Device access revoked.', 'good'); await refresh(); }
  catch (error) { say(error.message, 'error'); }
});
$('enable-notifications').addEventListener('click', async () => {
  if (!('Notification' in window) || !('serviceWorker' in navigator)) return say('This browser does not support background push notifications.', 'error');
  try {
    const permission = await Notification.requestPermission();
    if (permission !== 'granted') return say('Notification permission was not granted.', 'error');
    const config = await api({ action: 'push.config' });
    if (!config.pushPublicKey) return say('Phone push is not configured on the Guts & Bolts Worker yet. The dashboard remains usable; the operator must set the VAPID secrets.', 'error');
    const registration = await navigator.serviceWorker.register('/server/sw.js', { scope: '/server/' });
    let subscription = await registration.pushManager.getSubscription();
    if (!subscription) {
      const key = Uint8Array.from(atob(config.pushPublicKey.replace(/-/g, '+').replace(/_/g, '/') + '==='.slice((config.pushPublicKey.length + 3) % 4)), c => c.charCodeAt(0));
      subscription = await registration.pushManager.subscribe({ userVisibleOnly: true, applicationServerKey: key });
    }
    await api({ action: 'push.subscribe', subscription: subscription.toJSON() });
    notificationReady = true;
    say('Phone push alerts are enabled for this browser.', 'good');
  } catch (error) { say(error.message, 'error'); }
});
$('disable-notifications').addEventListener('click', async () => {
  try {
    const registration = await navigator.serviceWorker.getRegistration('/server/');
    const subscription = registration && await registration.pushManager.getSubscription();
    if (subscription) {
      await api({ action: 'push.remove', endpoint: subscription.endpoint });
      await subscription.unsubscribe();
    }
    notificationReady = false;
    say('Phone push alerts disabled for this browser.', 'good');
  } catch (error) { say(error.message, 'error'); }
});
refresh();
setInterval(() => { if (document.visibilityState === 'visible') refresh(); }, 10000);
