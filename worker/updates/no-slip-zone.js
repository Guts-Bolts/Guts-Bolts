export default {
  id: 'no-slip-zone', name: 'No-Slip Zone', time: 1794893715, tag: 'Engine',
  summary: 'Water slides stop teleporting you, dry ground stays dry, and pools stop going spiky.',
  items: [
    'Fixed getting teleported back over and over while riding a water slide in an online game. The server\'s anti-cheat thought the slide\'s speed was a speed hack',
    'Water slides, streams and pools can now carry you as fast as the water goes. Speed hacks on dry ground are still caught',
    'Fixed sliding around the whole map, even standing still, after water splashed onto the ground. One splash made the entire baseplate as slippery as the slide',
    'Now a big floor is only slippery where the water actually is. Water slides are still slippery all the way down',
    'Fixed pools (like the Splash Pool) turning into spiky white mountains after a while. The water pouring in from the slide slowly sank the surface until it hit its limit, and there it bounced up and down every frame',
    'Pools now keep their level while water pours in, and even a huge splash settles down again',
  ],
};
