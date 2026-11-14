export default {
  id: 'card-picks', name: 'The Card Decides', time: 1794694900, tag: 'Engine',
  summary: 'On Windows and Linux computers, the graphics card now works out by itself which parts are on screen and how detailed to draw them.',
  items: [
    'Plain solid parts that share a shape (bricks, fence posts, trees made of the same mesh) now sit in a list on the graphics card',
    'Each frame the graphics card checks every one of them at once: is it on screen, is it close enough, and how detailed should it be',
    'Only parts that changed are sent again, so a scene where nothing moves sends nothing at all',
    'The processor has less to do each frame, which leaves more time for physics and scripts in big builds',
    'Macs, iPhones, the web Player, older graphics cards and Safe mode keep the old way, which draws the same picture',
  ],
};
