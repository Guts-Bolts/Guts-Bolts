self.addEventListener('push', (event) => {
  let data = {};
  try { data = event.data ? event.data.json() : {}; } catch { data = { body: 'The home server reported an alert.' }; }
  const title = typeof data.title === 'string' ? data.title.slice(0, 80) : 'Guts & Bolts server alert';
  const body = typeof data.body === 'string' ? data.body.slice(0, 180) : 'The home server needs attention.';
  event.waitUntil(self.registration.showNotification(title, {
    body,
    icon: '/Icon.png',
    badge: '/Icon.png',
    tag: 'gb-home-server-alert',
    renotify: true,
    data: { url: '/server/' },
  }));
});
self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  const target = new URL('/server/', self.location.origin).toString();
  event.waitUntil(clients.matchAll({ type: 'window', includeUncontrolled: true }).then((windows) => {
    for (const client of windows) {
      if (client.url.startsWith(target) && 'focus' in client) return client.focus();
    }
    return clients.openWindow(target);
  }));
});
