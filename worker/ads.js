// Banner ads (Google AdSense) for the website.
//
// Nothing shows until the AdSense settings are filled in on Cloudflare (Workers & Pages >
// project-guts-and-bolts > Settings > Variables and Secrets):
//   ADSENSE_CLIENT   your publisher ID from AdSense, like ca-pub-1234567890123456
//   ADSENSE_SLOT     the number of a "Display ad" unit you made in AdSense (Ads > By ad unit)
//   ADS_GOOGLE_CONSENT  set to 1 once Google's own consent message is switched on in AdSense
//                    (Privacy & messaging > European regulations), so people in Europe don't
//                    get asked twice. Until then the site asks them itself (website/app/ads.js).
//
//   /ads.txt      tells ad buyers which AdSense account may sell ads on this site
//   /ads-config   what the website needs to show ads (the IDs above, and whether this visitor
//                 is somewhere that needs a cookie question first: the EU, EEA, UK, Switzerland)
// Every web page also gets Google's "google-adsense-account" tag, which is how AdSense checks
// that the site is yours.

// The EU, the rest of the EEA (Iceland, Liechtenstein, Norway), the UK and Switzerland.
export const CONSENT_COUNTRIES = new Set(('AT BE BG HR CY CZ DK EE FI FR DE GR HU IE IT LV LT LU MT NL PL PT RO SK SI ES SE ' +
  'IS LI NO GB UK CH').split(' '));

// "ca-pub-123..." or "pub-123..." -> "ca-pub-123...", anything else -> ''.
export function adClient(env) {
  const m = /^(?:ca-)?pub-(\d{10,20})$/.exec(String((env && env.ADSENSE_CLIENT) || '').trim());
  return m ? 'ca-pub-' + m[1] : '';
}
export function adSlot(env) {
  const s = String((env && env.ADSENSE_SLOT) || '').trim();
  return /^\d{5,20}$/.test(s) ? s : '';
}

// Unknown country (no Cloudflare info, like on a test computer) asks, to be safe.
export function needsConsent(request) {
  const country = String((request && request.cf && request.cf.country) || '').toUpperCase();
  return !country || country === 'XX' || country === 'T1' ? true : CONSENT_COUNTRIES.has(country);
}

export function adsTxt(env) {
  const client = adClient(env);
  if (!client) return '# Ads are not set up yet.\n';
  // f08c47fec0942fa0 is Google's own ID, the same for every AdSense site.
  return 'google.com, ' + client.slice(3) + ', DIRECT, f08c47fec0942fa0\n';
}

export function adsConfig(request, env) {
  const on = String((env && env.ADS_GOOGLE_CONSENT) || '').trim();
  return { client: adClient(env), slot: adSlot(env), consent: needsConsent(request), googleConsent: on === '1' || on === 'true' };
}

// Answers /ads.txt and /ads-config, or null for anything else.
export function handleAds(request, env, url) {
  if (url.pathname === '/ads.txt')
    return new Response(adsTxt(env), { headers: { 'content-type': 'text/plain; charset=utf-8', 'cache-control': 'public, max-age=3600' } });
  if (url.pathname === '/ads-config')
    return new Response(JSON.stringify(adsConfig(request, env)), { headers: { 'content-type': 'application/json', 'cache-control': 'private, no-store' } });
  return null;
}

// Adds <meta name="google-adsense-account"> to web pages once ADSENSE_CLIENT is set.
export function withAdsMeta(response, env) {
  const client = adClient(env);
  const type = (response.headers.get('content-type') || '').toLowerCase();
  if (!client || !type.startsWith('text/html') || typeof HTMLRewriter === 'undefined') return response;
  return new HTMLRewriter().on('head', {
    element(head) { head.append('<meta name="google-adsense-account" content="' + client + '">', { html: true }); },
  }).transform(response);
}
