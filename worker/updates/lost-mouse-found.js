export default {
  id: 'lost-mouse-found', name: 'Lost Mouse Found', version: '0.7.7', time: 1793105400, tag: 'Fix',
  summary: 'Scripts that read the mouse finally point at the right spot when you play in Studio.',
  items: [
    'mouse.X, mouse.Y, mouse.Hit and clicks now start at the corner of Studio\'s 3D view, not the corner of the whole window',
    'Studio throws away multiplayer test recordings older than a week, so they stop piling up on your computer',
    'Every sample game now gets loaded and run on each change, so an update that breaks one gets caught before it ships',
  ],
};
