export default {
  id: 'many-hands', name: 'Many Hands', time: 1794641645, tag: 'Engine',
  summary: 'The engine now shares heavy work out over every core of your processor, so busy scenes run smoother.',
  items: [
    'One team of helper threads is made when the game starts and shared by everything that needs it',
    'Working out where every part is and whether the camera can see it now runs on every core at once',
    'Soft parts are worked out side by side on every core',
    'Liquid uses the same team instead of keeping its own',
    'The lists of what to draw are kept from frame to frame instead of being made again every time, so frame times stay steadier',
    'Web Player builds run the same way on their one thread',
  ],
};
