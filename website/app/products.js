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
    id: 'videos', name: 'Videos', status: 'soon', href: '#/soon/videos',
    blurb: 'Upload videos about anything, follow channels, make playlists.',
    pages: [],
    links: [],
    plans: [
      'Upload and watch videos about anything, not just games',
      'Channels, subscriptions, likes and comments',
      'Playlists, watch history and search',
      'Reports and staff moderation from day one',
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
    id: 'live', name: 'Live', status: 'soon', href: '#/soon/live',
    blurb: 'Go live and hang out in chat: games, art, talk shows, anything allowed.',
    pages: [],
    links: [],
    plans: [
      'Go live straight from your browser',
      'Live chat with timeouts, slow mode and moderators',
      'Follow streamers and get told when they go live',
      'Categories, so games are one kind of stream among many',
    ],
  },
  {
    id: 'communities', name: 'Communities', status: 'open', href: '#/forum',
    blurb: 'Forums, groups and people to meet.',
    pages: ['forum', 'forumsearch', 'groups', 'group', 'people'],
    links: [
      ['Forum', '#/forum', 'forum'],
      ['Groups', '#/groups', 'groups'],
      ['People', '#/people', 'people'],
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

// The groups the Home page shows products in. Guts&Bolts is the parent brand: games are
// one group of many. `later` names ideas that aren't built yet (shown small, as "on the
// way"), so adding a product to a group later means adding its id to `products`.
export const GROUPS = [
  { id: 'games', name: 'Games', products: ['games'], later: [] },
  { id: 'media', name: 'Media', products: ['videos', 'live'], later: ['Audio'] },
  { id: 'creation', name: 'Creation', products: ['art'], later: ['Modeler', 'Image', 'Capture'] },
  { id: 'community', name: 'Community', products: ['communities', 'chat'], later: ['Press'] },
  { id: 'ai', name: 'AI', products: ['fyxa'], later: ['Fyxa Desktop', 'AI Lab'] },
  { id: 'learning', name: 'Learning', products: [], later: ['Academy'] },
  { id: 'development', name: 'Development', products: [], later: ['Paste', 'Links', 'Code'] },
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
  if (page === 'item') return 'catalog';
  if (page === 'trade') return 'trades';
  if (page === 'forumsearch') return 'forum';
  if (page === 'group') return 'groups';
  if (page === 'parties') return 'chat';
  if (page === 'create' && /^#\/create\/library/.test(String(hash || ''))) return 'library';
  return page;
}
