// The Guts&Bolts website and server on Cloudflare.
//
//   /api   one signed request -> one answer   (the website and the apps)
//   /ws    a WebSocket for the multiplayer relay (the apps)
//   /download/...  the apps themselves (downloads.js)
//   else   the website's files (website/)
//
// Normally the Guts&Bolts server itself runs here too (server.js, a Durable
// Object), so it's online even when your computer is off. If GB_SERVER is set
// (host:port), /api is passed on to that server instead, over TCP.
import { connect } from 'cloudflare:sockets';
import { GbServerObject } from './server.js';
import { handleDownload } from './downloads.js';
export { GbServerObject };

const MAX_REQUEST = 40 * 1024 * 1024;   // a whole game, base64
const MAX_REPLY   = 48 * 1024 * 1024;
const TIMEOUT_MS  = 60 * 1000;

function reply(obj, status = 200) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { 'content-type': 'application/json', 'cache-control': 'no-store' },
  });
}

function splitAddress(address) {
  const a = (address || '').trim();
  const colon = a.lastIndexOf(':');
  if (colon > 0 && a.indexOf(':') === colon) return { hostname: a.slice(0, colon), port: Number(a.slice(colon + 1)) || 7780 };
  return { hostname: a, port: 7780 };
}

// Send one message to the server and read one message back.
async function ask(address, text) {
  const body = new TextEncoder().encode(text);
  const frame = new Uint8Array(4 + body.length);
  new DataView(frame.buffer).setUint32(0, body.length);
  frame.set(body, 4);

  const socket = connect(splitAddress(address));
  const timer = new Promise((_, fail) => setTimeout(() => fail(new Error('timeout')), TIMEOUT_MS));
  try {
    const work = (async () => {
      const writer = socket.writable.getWriter();
      await writer.write(frame);
      writer.releaseLock();
      const reader = socket.readable.getReader();
      let got = new Uint8Array(0), need = -1;
      for (;;) {
        const { value, done } = await reader.read();
        if (done) throw new Error('closed');
        const joined = new Uint8Array(got.length + value.length);
        joined.set(got); joined.set(value, got.length);
        got = joined;
        if (need < 0 && got.length >= 4) {
          need = new DataView(got.buffer).getUint32(0);
          if (need > MAX_REPLY) throw new Error('too big');
        }
        if (need >= 0 && got.length >= 4 + need) return new TextDecoder().decode(got.subarray(4, 4 + need));
      }
    })();
    return await Promise.race([work, timer]);
  } finally {
    socket.close().catch(() => {});
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    // The site lives at gutsandbolts.net. The old workers.dev address only keeps answering
    // older apps (the server and its pictures); people opening it in a browser get moved.
    if (url.hostname.endsWith('.workers.dev') && !/^\/(api|ws|thumb|icon|shot|wear|decal|download)(\/|$)/.test(url.pathname))
      return Response.redirect('https://gutsandbolts.net' + url.pathname + url.search, 301);
    if (url.pathname.startsWith('/download/')) return handleDownload(request, env, url);
    if (url.pathname === '/api/info') {
      return reply({ ok: true, server: env.GB_SERVER || 'cloudflare', time: Math.floor(Date.now() / 1000) });
    }
    // The server on Cloudflare: one Durable Object holds everything.
    const builtIn = () => env.GB_SERVER_OBJECT.get(env.GB_SERVER_OBJECT.idFromName('main'));
    if (url.pathname === '/ws') return builtIn().fetch(request);
    if ((url.pathname.startsWith('/thumb/') || url.pathname.startsWith('/icon/') || url.pathname.startsWith('/shot/') || url.pathname.startsWith('/wear/') || url.pathname.startsWith('/decal/')) && !env.GB_SERVER) return builtIn().fetch(request);
    if (url.pathname === '/api') {
      if (request.method !== 'POST') return reply({ ok: false, error: 'Use POST.' }, 405);
      if (!env.GB_SERVER) {
        const len = Number(request.headers.get('content-length') || 0);
        if (len > MAX_REQUEST) return reply({ ok: false, error: 'That\'s too big to send.' }, 413);
        return builtIn().fetch(request);
      }
      const len = Number(request.headers.get('content-length') || 0);
      if (len > MAX_REQUEST) return reply({ ok: false, error: 'That\'s too big to send.' }, 413);
      const text = await request.text();
      if (text.length > MAX_REQUEST) return reply({ ok: false, error: 'That\'s too big to send.' }, 413);
      try {
        JSON.parse(text);   // only pass on proper requests
      } catch {
        return reply({ ok: false, error: 'That wasn\'t a proper request.' }, 400);
      }
      try {
        const answer = await ask(env.GB_SERVER, text);
        return new Response(answer, { headers: { 'content-type': 'application/json', 'cache-control': 'no-store' } });
      } catch (e) {
        return reply({ ok: false, error: 'Couldn\'t reach the Guts&Bolts server (' + (e && e.message || 'error') + '). Is it running?' });
      }
    }
    return env.ASSETS.fetch(request);
  },
};
