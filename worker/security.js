// Shared response hardening for every HTTP route and static asset.
// Google AdSense (worker/ads.js, website/app/ads.js): its script, the ad frames, and the
// consent message Google can show in Europe. Ads themselves draw inside Google's own frames.
const GOOGLE_ADS = 'https://pagead2.googlesyndication.com https://*.googlesyndication.com https://*.doubleclick.net ' +
  'https://*.google.com https://*.googletagservices.com https://*.gstatic.com https://*.adtrafficquality.google';

// Stripe's payment form (worker/stripe.js, website/checkout.html): Stripe.js and its frames.
const STRIPE = 'https://js.stripe.com https://*.stripe.com';

const CSP = [
  "default-src 'self'",
  "base-uri 'self'",
  "object-src 'none'",
  "frame-ancestors 'none'",
  "form-action 'self'",
  "script-src 'self' https://challenges.cloudflare.com 'wasm-unsafe-eval' " + GOOGLE_ADS + ' ' + STRIPE,
  "script-src-attr 'none'",
  "style-src 'self' 'unsafe-inline'",
  "img-src 'self' data: blob: https:",
  "font-src 'self' data:",
  "connect-src 'self' https://challenges.cloudflare.com wss://gutsandbolts.net wss://www.gutsandbolts.net wss://pfpzmmsosyetkcoymoct.supabase.co " + GOOGLE_ADS + ' ' + STRIPE,
  "frame-src https://challenges.cloudflare.com " + GOOGLE_ADS + ' ' + STRIPE,
  "media-src 'self' blob: https:",
  "worker-src 'self' blob:",
  "manifest-src 'self'",
  'upgrade-insecure-requests',
].join('; ');

export function withSecurityHeaders(response, request) {
  // Cloudflare WebSocket upgrade responses (101) must be returned unchanged.
  if (response.status === 101) return response;
  const headers = new Headers(response.headers);
  headers.set('X-Content-Type-Options', 'nosniff');
  headers.set('Referrer-Policy', 'strict-origin-when-cross-origin');
  headers.set('X-Frame-Options', 'DENY');
  headers.set('Permissions-Policy', 'camera=(), microphone=(self), geolocation=(), payment=(), usb=()');
  headers.set('Origin-Agent-Cluster', '?1');
  const url = new URL(request.url);
  if (url.protocol === 'https:' && (url.hostname === 'gutsandbolts.net' || url.hostname.endsWith('.gutsandbolts.net')))
    headers.set('Strict-Transport-Security', 'max-age=31536000; includeSubDomains');
  const type = headers.get('content-type') || '';
  if (type.toLowerCase().startsWith('text/html')) headers.set('Content-Security-Policy', CSP);
  return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
}
