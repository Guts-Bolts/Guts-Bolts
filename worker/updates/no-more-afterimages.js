export default {
  id: 'no-more-afterimages', name: 'No More Afterimages', version: '0.8.1', time: 1794972000, tag: 'Fix',
  summary: 'Moving characters no longer leave a faint afterimage behind them.',
  items: [
    'The upscaler that smooths edges mixes each new frame with the ones before it. It only knew how the camera moved, so where a character had just been, the old frames still showed it for a moment',
    'Now every pixel remembers how far away it was. If that changes, something moved there, so the old frames are dropped and you see only what is really there',
    'Edges of things that just came into view are softened a little, so they don\'t look jagged',
    'Still scenes keep their smooth edges, same as before',
  ],
};
