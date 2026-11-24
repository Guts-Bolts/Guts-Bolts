// The "official website" files that search engines, ad buyers and browsers look for:
//   /robots.txt                    which pages search engines may visit, and where the sitemap is
//   /sitemap.xml                   the public pages (now made by seo.js, which adds every game, video...)
//   /.well-known/security.txt      where to report a security problem (see SECURITY.md)
//   /.well-known/gpc.json          says the site respects Global Privacy Control (website/app/ads.js)
//   /terms                         short address for the Terms of Service (they live in the app)
// The legal pages themselves are plain files in website/: privacy, cookies, copyright, contact.

export const LEGAL_PAGES = ['/privacy', '/cookies', '/copyright', '/contact'];
export const PUBLIC_PAGES = ['/', '/app/', '/products', '/products/sysfix', '/products/contentforge', ...LEGAL_PAGES];
// When the legal pages last changed (the date at the top of privacy.html).
export const LEGAL_UPDATED = '2026-11-19';

export function robotsTxt(origin) {
  return [
    'User-agent: *',
    // The API, live connections and private tools aren't pages.
    'Disallow: /api',
    'Disallow: /ws',
    'Disallow: /mcp',
    'Disallow: /ads-config',
    'Disallow: /checkout',
    'Allow: /',
    '',
    'Sitemap: ' + origin + '/sitemap.xml',
    '',
  ].join('\n');
}

export function sitemapXml(origin) {
  const urls = PUBLIC_PAGES.map((p) => '  <url><loc>' + origin + p + '</loc>' +
    (LEGAL_PAGES.includes(p) ? '<lastmod>' + LEGAL_UPDATED + '</lastmod>' : '') + '</url>');
  return '<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n' +
    urls.join('\n') + '\n</urlset>\n';
}

export function securityTxt(origin, now = Date.now()) {
  // "Expires" is required, and should be less than a year away: always one year from today.
  const expires = new Date(now + 365 * 86400 * 1000);
  expires.setUTCHours(0, 0, 0, 0);
  return [
    'Contact: https://github.com/Guts-Bolts/Guts-Bolts/security/advisories/new',
    'Contact: ' + origin + '/app/#/support',
    'Expires: ' + expires.toISOString(),
    'Policy: https://github.com/Guts-Bolts/Guts-Bolts/blob/main/SECURITY.md',
    'Preferred-Languages: en',
    'Canonical: ' + origin + '/.well-known/security.txt',
    '',
  ].join('\n');
}

// Answers the paths above, or null for anything else.
export function handleLegal(request, url) {
  // Always name the real site, even when a test copy answers.
  const origin = url.hostname === 'gutsandbolts.net' || url.hostname === 'www.gutsandbolts.net' ? 'https://gutsandbolts.net' : url.origin;
  const text = (body, type) => new Response(body, { headers: { 'content-type': type + '; charset=utf-8', 'cache-control': 'public, max-age=3600' } });
  if (url.pathname === '/robots.txt') return text(robotsTxt(origin), 'text/plain');
  if (url.pathname === '/sitemap.xml') return text(sitemapXml(origin), 'application/xml');
  if (url.pathname === '/.well-known/security.txt' || url.pathname === '/security.txt') return text(securityTxt(origin), 'text/plain');
  if (url.pathname === '/.well-known/gpc.json') return text(JSON.stringify({ gpc: true, lastUpdate: LEGAL_UPDATED }), 'application/json');
  if (url.pathname === '/terms' || url.pathname === '/terms/') return Response.redirect(url.origin + '/app/#/terms', 302);
  return null;
}
