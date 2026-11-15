export default {
  id: 'deep-water', name: 'Deep Water', version: '0.8.0', time: 1794556858, tag: 'Engine',
  summary: 'Water with a swell is now a real ocean: thousands of waves added up with an FFT, like in films, with breaking foam and glowing crests.',
  items: [
    'Water parts with the Waves attribute now get waves from a wave spectrum and an FFT instead of four rolling waves, so the sea looks different everywhere you look',
    'The waves come in bands of different sizes (big rollers, chop and tiny ripples) on squares that never line up, so the sea does not look tiled',
    'Foam appears where a crest is really about to break, and crests glow in the water colour when you look towards the sun',
    'Boats, boxes and swimmers float on exactly the waves you see, on every device and on game servers',
    'Graphics cards with compute shaders work the waves out themselves on finer grids, with a third band of tiny ripples, on High and up',
    'Macs, iPhones, iPads and the browser draw the same sea without the finest ripples, and Settings says so',
    'Far away the waves blur into calm water by themselves, so a huge sea does not sparkle',
  ],
};
