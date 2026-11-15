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
  const auth = await signedRequest('staff.check', {});
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
  $('server-detail').textContent = (s.serverPid ? 'PID ' + s.serverPid : 'No managed process') + (s.serverUptimeSeconds ? ' · ' + Math.floor(s.serverUptimeSeconds / 60) + ' min' : '') + (s.lastError ? ' · ' + s.lastError : '');
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
    say(error.message + ' Sign in to the official Guts & Bolts account on this website, in this same browser (the Player app doesn\'t count), then reload this page.', 'error');
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
    const key = Uint8Array.from(atob(config.pushPublicKey.replace(/-/g, '+').replace(/_/g, '/') + '==='.slice((config.pushPublicKey.length + 3) % 4)), c => c.charCodeAt(0));
    let subscription = await registration.pushManager.getSubscription();
    if (subscription) {
      const currentKey = subscription.options.applicationServerKey;
      const currentBytes = currentKey ? new Uint8Array(currentKey) : new Uint8Array(0);
      const sameKey = currentBytes.length === key.length && key.every((value, index) => value === currentBytes[index]);
      if (!sameKey) {
        await api({ action: 'push.remove', endpoint: subscription.endpoint });
        await subscription.unsubscribe();
        subscription = null;
      }
    }
    if (!subscription) {
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

async function infraApi(action) {
  const auth = await signedRequest('staff.check', {});
  const response = await fetch('/api/infrastructure', {
    method: 'POST', credentials: 'same-origin', cache: 'no-store',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ auth, action }),
  });
  const result = await response.json();
  if (!response.ok || !result.ok) throw new Error(result.error || 'Infrastructure request failed.');
  return result;
}
function renderInfrastructure(result) {
  const devices = result.devices || [];
  const online = devices.filter((device) => device.online && !device.revoked);
  $('infra-total').textContent = String(devices.filter((device) => !device.revoked).length);
  $('infra-online').textContent = String(online.length);
  $('infra-online-detail').textContent = online.length + ' of ' + devices.filter((device) => !device.revoked).length + ' registered';
  $('infra-ai').textContent = String(online.filter((device) => (device.status.capabilities || []).includes('ai-inference')).length);
  $('infra-gpu').textContent = String(online.filter((device) => device.status.gpuMemoryTotalMB > 0).length);
  const list = $('infra-devices');
  list.replaceChildren();
  if (!devices.length) {
    const empty = document.createElement('p'); empty.className = 'muted'; empty.textContent = 'No machines paired yet. Install the G&B Nexus agent on a computer, then pair its one-time code here.'; list.append(empty);
  }
  for (const device of devices) {
    const card = document.createElement('article'); card.className = 'device-card';
    const head = document.createElement('div'); head.className = 'device-head';
    const identity = document.createElement('div');
    const name = document.createElement('strong'); name.textContent = device.name || 'Unnamed machine';
    const meta = document.createElement('p'); meta.className = 'muted small'; meta.textContent = (device.platform || 'Unknown platform') + ' · ' + (device.id || '');
    identity.append(name, meta);
    const state = document.createElement('span'); state.className = 'pill ' + (device.revoked ? 'offline' : device.online ? 'online' : 'offline');
    state.textContent = device.revoked ? 'Revoked' : device.online ? 'Online' : 'Offline';
    head.append(identity, state); card.append(head);
    const status = device.status || {};
    const metrics = document.createElement('div'); metrics.className = 'device-metrics';
    const values = [
      ['CPU', percent(status.cpuPercent)],
      ['RAM', percent(status.memoryPercent)],
      ['GPU', percent(status.gpuPercent)],
      ['GPU temp', status.gpuTemperatureC == null ? '—' : Math.round(status.gpuTemperatureC) + '°C'],
      ['Disk', status.diskUsedGB != null && status.diskTotalGB ? status.diskUsedGB.toFixed(1) + ' / ' + status.diskTotalGB.toFixed(1) + ' GB' : '—'],
      ['Cores', status.cpuCores == null ? '—' : String(status.cpuCores)],
    ];
    for (const [label, value] of values) {
      const item = document.createElement('div'); item.className = 'device-stat';
      const caption = document.createElement('span'); caption.textContent = label;
      const strong = document.createElement('strong'); strong.textContent = value;
      item.append(caption, strong); metrics.append(item);
    }
    card.append(metrics);
    const detail = document.createElement('p'); detail.className = 'muted small';
    detail.textContent = 'Last heartbeat: ' + timeText(device.lastSeen) + ' · Agent ' + (status.agentVersion || 'unknown') +
      ((status.capabilities || []).length ? ' · ' + status.capabilities.join(', ') : '');
    card.append(detail);
    if (!device.revoked) {
      const actions = document.createElement('div'); actions.className = 'actions';
      const revoke = document.createElement('button'); revoke.className = 'button danger'; revoke.textContent = 'Revoke machine';
      revoke.addEventListener('click', async () => {
        if (!window.confirm('Revoke this machine credential? It will be disconnected and must be paired again.')) return;
        try { await infraApi({ action: 'revoke', deviceId: device.id }); say('Machine access revoked.', 'good'); await refreshInfrastructure(); }
        catch (error) { say(error.message, 'error'); }
      });
      actions.append(revoke); card.append(actions);
    }
    list.append(card);
  }
  const audit = $('infra-audit'); audit.replaceChildren();
  const entries = result.entries || [];
  if (!entries.length) { const li = document.createElement('li'); li.className = 'muted'; li.textContent = 'No infrastructure events recorded yet.'; audit.append(li); }
  for (const entry of entries) {
    const li = document.createElement('li');
    const time = document.createElement('time'); time.textContent = timeText(entry.at);
    const detail = document.createElement('span'); detail.textContent = entry.action + ' · ' + entry.detail;
    li.append(time, detail); audit.append(li);
  }
}
async function refreshInfrastructure() {
  try {
    const result = await infraApi({ action: 'list' });
    document.body.classList.add('owner-ready');
    renderInfrastructure(result);
  } catch (error) {
    say(error.message + ' Sign in to the official owner account in this browser, then reload.', 'error');
  }
}
$('infra-refresh').addEventListener('click', refreshInfrastructure);
$('infra-pair').addEventListener('click', async () => {
  const code = $('infra-pair-code').value.trim().toLowerCase();
  if (!/^[a-f0-9]{64}$/.test(code)) return say('Pairing code must be the complete 64-character hexadecimal code displayed by the G&B Nexus agent.', 'error');
  try {
    const result = await infraApi({ action: 'pair', code });
    $('infra-pair-code').value = '';
    say('Machine paired: ' + result.name + '. Its credential is stored by the agent.', 'good');
    await refreshInfrastructure();
  } catch (error) { say(error.message, 'error'); }
});

refresh();
refreshInfrastructure();
setInterval(() => { if (document.visibilityState === 'visible') { refresh(); refreshInfrastructure(); } }, 10000);
