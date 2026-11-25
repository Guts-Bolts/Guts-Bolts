// The products that make up Guts&Bolts. Games is one product; Videos, Live and the
// rest sit beside it, each with its own pages and its own second bar of links.
//
// This list is the one place that says which products exist. The top bar's
// highlight, the second bar under it, and the product cards on the Home page all
// come from it, so adding a product later means adding one entry here.
//
// Each product:
//   id      short name, also used in #/soon/<id> for products that aren't open yet
//   name    what people see
//   status  'open' (works now), 'soon' (coming: its link opens a short "what's
//           coming" page), or 'studio' (only inside Studio for now)
//   href    where its top-bar link goes
//   blurb   one line for its card on the Home page
//   pages   the site's page names (the word after #/) that belong to it, so the
//           top bar can light it up and show its second bar
//   links   its second bar: [label, href, page name it lights up]
//   plans   for products that aren't open yet: what's planned, in plain words

export const PRODUCTS = [
  {
    id: 'games', name: 'Games', status: 'open', href: '#/games',
    blurb: 'Play games people made, or build your own in Studio.',
    pages: ['games', 'game', 'catalog', 'item', 'create', 'library', 'configure', 'events', 'trades', 'trade'],
    links: [
      ['Discover', '#/games', 'games'],
      ['All games', '#/games?sort=popular', 'games-list'],
      ['Catalog', '#/catalog', 'catalog'],
      ['Create', '#/create', 'create'],
      ['Library', '#/create/library', 'library'],
      ['Events', '#/events', 'events'],
      ['Trades', '#/trades', 'trades'],
      ['Download', '../#download', ''],
    ],
  },
  {
    id: 'videos', name: 'Videos', status: 'open', href: '#/videos',
    blurb: 'Upload videos about anything, follow channels, like and comment.',
    pages: ['videos', 'watch', 'videoupload'],
    links: [
      ['Home', '#/videos', 'videos'],
      ['Trending', '#/videos?sort=week', 'videos-week'],
      ['Most liked', '#/videos?sort=top', 'videos-top'],
      ['My videos', '#/videos?by=me', 'videos-mine'],
      ['Upload', '#/videoupload', 'videoupload'],
      ['Live', '#/live', ''],
    ],
  },
  {
    id: 'art', name: 'Art', status: 'open', href: '#/art',
    blurb: 'Post drawings, pixel art, comics and renders. Fave the best.',
    pages: ['art', 'artpiece', 'artpost'],
    links: [
      ['Gallery', '#/art', 'art'],
      ['Best this week', '#/art?sort=week', 'art-week'],
      ['Most faved', '#/art?sort=top', 'art-top'],
      ['My art', '#/art?by=me', 'art-mine'],
      ['Post art', '#/artpost', 'artpost'],
    ],
  },
  {
    id: 'paste', name: 'Paste', status: 'open', href: '#/paste',
    blurb: 'Share code, crash logs and settings with one link.',
    pages: ['paste', 'pasteview', 'pastenew'],
    links: [
      ['Recent', '#/paste', 'paste'],
      ['My pastes', '#/paste?by=me', 'paste-mine'],
      ['New paste', '#/pastenew', 'pastenew'],
    ],
  },
  {
    id: 'academy', name: 'Academy', status: 'open', href: '#/academy',
    blurb: 'Free courses about scripting, building, modeling and making games.',
    pages: ['academy', 'course', 'lesson', 'coursewrite'],
    links: [
      ['Courses', '#/academy', 'academy'],
      ['Studio Learn', '#/create/learn', 'studio-learn'],
      ['My courses', '#/academy?mine=1', 'academy-mine'],
      ['Write a course', '#/coursewrite', 'coursewrite'],
    ],
  },
  {
    id: 'live', name: 'Live', status: 'open', href: '#/live',
    blurb: 'Go live from your browser and hang out in chat: games, art, talk shows, anything allowed.',
    pages: ['live', 'golive'],
    links: [
      ['Live now', '#/live', 'live'],
      ['Go live', '#/golive', 'golive'],
      ['Videos', '#/videos', ''],
    ],
  },
  {
    id: 'communities', name: 'Communities', status: 'open', href: '#/forum',
    blurb: 'Forums, groups and people to meet.',
    pages: ['forum', 'forumsearch', 'groups', 'group', 'people', 'leaderboards', 'calendar', 'calevent', 'calnew'],
    links: [
      ['Forum', '#/forum', 'forum'],
      ['Groups', '#/groups', 'groups'],
      ['Calendar', '#/calendar', 'calendar'],
      ['People', '#/people', 'people'],
      ['Leaderboards', '#/leaderboards', 'leaderboards'],
    ],
  },
  {
    id: 'press', name: 'Press', status: 'open', href: '#/press',
    blurb: 'News from the team, creator spotlights and stories from behind the scenes.',
    pages: ['press', 'pressview', 'presswrite'],
    links: [
      ['Latest', '#/press', 'press'],
      ['News', '#/press?kind=news', 'press-news'],
      ['Creator spotlights', '#/press?kind=spotlight', 'press-spotlight'],
      ['Behind the scenes', '#/press?kind=devlog', 'press-devlog'],
      ['Calendar', '#/calendar', ''],
    ],
  },
  {
    id: 'chat', name: 'Chat', status: 'open', href: '#/chat',
    blurb: 'Chats, parties, calls and your inbox.',
    pages: ['chat', 'parties', 'messages'],
    links: [
      ['Chats', '#/chat', 'chat'],
      ['Inbox', '#/messages', 'messages'],
    ],
  },
  {
    id: 'fyxa', name: 'Fyxa', status: 'studio', href: '#/soon/fyxa',
    blurb: 'The G&B AI helper. In Studio today, everywhere later.',
    pages: [],
    links: [],
    plans: [
      'Works in Studio today: ask it to build or fix things in your game',
      'Later: help with videos, streams, search and community tools',
      'Only ever does what you yourself are allowed to do',
    ],
  },
];

// Apps you download and run on your own computer, made by Guts&Bolts but not part of
// the website. Each has its own page under /products (website/products/<id>.html) and
// a card in the "Apps to download" row on Home. Not in the top bar: they aren't site pages.
//   id, name, blurb  as above
//   href      its product page
//   platform  what it runs on, shown on its card
//   stage     'out' (you can download it) or 'test' (early test version)
export const APPS = [
  { id: 'sysfix', name: 'Sysfix', href: '/products/Sysfix', platform: 'Linux', stage: 'out',
    blurb: 'Checks your Linux PC, explains what is wrong, and never changes things without asking.' },
  { id: 'contentforge', name: 'ContentForge', href: '/products/ContentForge', platform: 'Windows', stage: 'test',
    blurb: 'Turns an idea into a short video, with AI that runs on your own PC.' },
];

// The groups the Home page shows products in. Guts&Bolts is the parent brand: games are
// one group of many. `later` names ideas that aren't built yet (shown small, as "on the
// way"), so adding a product to a group later means adding its id to `products`.
export const GROUPS = [
  { id: 'games', name: 'Games', products: ['games'], later: [] },
  { id: 'media', name: 'Media', products: ['videos', 'live'], later: ['Audio'] },
  { id: 'creation', name: 'Creation', products: ['art'], later: ['Modeler', 'Image', 'Capture'] },
  { id: 'community', name: 'Community', products: ['communities', 'chat', 'press'], later: [] },
  { id: 'ai', name: 'AI', products: ['fyxa'], later: ['Fyxa Desktop', 'AI Lab'] },
  { id: 'learning', name: 'Learning', products: ['academy'], later: [] },
  { id: 'development', name: 'Development', products: ['paste'], later: ['Code'] },
  { id: 'commerce', name: 'Commerce', products: [], later: ['Services', 'Storefronts'] },
  { id: 'productivity', name: 'Productivity', products: [], later: ['Drive', 'Projects', 'Boards'] },
];

// Which product a page belongs to (or null for pages that belong to the whole
// site, like Home, Settings, a profile or Updates).
export function productOf(page) {
  return PRODUCTS.find((p) => p.pages.includes(page)) || null;
}

export function productById(id) {
  return PRODUCTS.find((p) => p.id === id) || null;
}

// Which second-bar link is lit. The Games "Discover" home and its full list are
// both the 'games' page, told apart by whether the address has a '?'.
export function activeLink(page, hash) {
  if (page === 'games') return String(hash || '').includes('?') ? 'games-list' : 'games';
  if (page === 'game') return 'games';
  if (page === 'art') {   // the gallery's tabs are told apart by what's after the '?'
    const h = String(hash || '');
    return /[?&]sort=week/.test(h) ? 'art-week' : /[?&]sort=top/.test(h) ? 'art-top' : /[?&]by=me/.test(h) ? 'art-mine' : 'art';
  }
  if (page === 'artpiece') return 'art';
  if (page === 'videos') {   // (videos.js) like the art gallery's tabs
    const h = String(hash || '');
    return /[?&]sort=week/.test(h) ? 'videos-week' : /[?&]sort=top/.test(h) ? 'videos-top' : /[?&]by=me/.test(h) ? 'videos-mine' : 'videos';
  }
  if (page === 'watch') return 'videos';
  if (page === 'paste') return /[?&]by=me/.test(String(hash || '')) ? 'paste-mine' : 'paste';
  if (page === 'pasteview') return 'paste';
  if (page === 'academy') return /[?&]mine=/.test(String(hash || '')) ? 'academy-mine' : 'academy';
  if (page === 'course' || page === 'lesson') return 'academy';
  if (page === 'item') return 'catalog';
  if (page === 'trade') return 'trades';
  if (page === 'forumsearch') return 'forum';
  if (page === 'calevent' || page === 'calnew') return 'calendar';
  if (page === 'press') { const m = /[?&]kind=(news|spotlight|devlog)/.exec(String(hash || '')); return m ? 'press-' + m[1] : 'press'; }
  if (page === 'pressview') return 'press';
  if (page === 'group') return 'groups';
  if (page === 'parties') return 'chat';
  if (page === 'create' && /^#\/create\/library/.test(String(hash || ''))) return 'library';
  return page;
}
