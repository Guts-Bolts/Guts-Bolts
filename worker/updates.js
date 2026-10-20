// The update log on the website (the "Updates" page). Every update gets its own
// name. Newest first. Add a new one at the top whenever an update ships; staff can
// also post updates from the website (those are kept in the server's database).
//
//   id     short and unique (letters, numbers, dashes)
//   name   the update's name: make it fun and about what changed
//   time   when it came out (seconds since 1970, like everything else on the server)
//   tag    what it's mostly about: Engine, Studio, Website, Player, Server, Fix
//   items  what changed, one short line each, in plain words

export const BUILT_IN_UPDATES = [
  {
    id: 'shop-window',
    name: 'Shop Window',
    version: '0.7.3',
    time: 1792456554,
    tag: 'Website',
    summary: 'The front page and download page got a makeover that actually shows off what the engine can do.',
    items: [
      'A big banner that flips through real screenshots: explosions, particles, neon at night, terrain and materials',
      'New sections for the engine, Lua scripting, Studio and its AI helper, playing together and the site',
      'Every picture is straight out of the Player, no touch-ups',
      'The Download box points at the right download for your computer, with three easy steps',
      'A "What\'s new" box that fills itself in from the newest release, and a few common questions answered',
    ],
  },
  {
    id: 'friends-in-high-places',
    name: 'Friends in High Places',
    version: '0.7.3',
    time: 1792456200,
    tag: 'Website',
    summary: 'A new Partner badge for friends of Guts&Bolts, shown right next to their name.',
    items: [
      'Partners are people and projects Guts&Bolts teams up with, like Kermit, who makes Lyreto',
      'They get a teal Partner badge next to their name on their profile, in the forum and in game comments',
      'Staff can hand it out (or take it away) with the Make partner button on the Staff page',
      'It shows up in the Guts&Bolts Badges box on their profile, and in the Player app\'s badge list',
    ],
  },
  {
    id: 'hold-onto-your-hat',
    name: 'Hold Onto Your Hat',
    version: '0.7.3',
    time: 1792456424,
    tag: 'Fix',
    summary: 'Hats and accessories stay on your head now, even when an animation turns it.',
    items: [
      'Hats, hair and face accessories turn and tilt with the head in emotes, animation packs and game animations',
      'Neck, shoulder and waist accessories move with the body the same way',
      'Other players see your hat follow your head too',
      'Works for the built-in hats, the ones made in Studio\'s Accessory window, and hats put on in the middle of a game',
    ],
  },
  {
    id: 'keep-your-head',
    name: 'Keep Your Head',
    version: '0.7.2',
    time: 1792455600,
    tag: 'Fix',
    summary: 'The Bouncy Animation Pack no longer sends your head flying round the map.',
    items: [
      'Walking or running with the Bouncy Animation Pack made your head drift away from your body and fall off, and now it stays on your shoulders',
      'The bounce in the pack now moves the head along with the body each step, instead of adding up step after step',
      'Animation packs that turn the head (like the Robot one) are steadier too',
    ],
  },
  {
    id: 'hello-from-the-other-server',
    name: 'Hello From the Other Server',
    version: '0.7.2',
    time: 1792450920,
    tag: 'Website',
    summary: 'Guts&Bolts teams up with Lyreto: games and groups can show their Lyreto chat server right on their page.',
    items: [
      'Lyreto is a free chat app for servers, voice and hanging out, and it\'s Guts&Bolts\' chat partner',
      'Paste your Lyreto server\'s invite link in a game\'s Configure page (under Community) or in a group\'s Settings',
      'The game or group page then shows a Lyreto card with the server\'s name, picture and blurb, and a Join on Lyreto button',
      'Only real lyreto.com links are accepted, so nobody can sneak in other links',
      'There\'s a Chat on Lyreto link at the bottom of every page too',
    ],
  },
];
