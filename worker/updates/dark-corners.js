export default {
  id: 'dark-corners', name: 'Dark Corners', time: 1794541946, tag: 'Engine',
  summary: 'Corners, creases and the spots where things touch are shaded in a new, more true-to-life way (GTAO), on every graphics level.',
  items: [
    'The soft shading in corners now works out how much open sky each spot can really see, instead of guessing with random test points',
    'Stairs, gaps between parts and things standing on the floor get a clear, clean contact shadow, and flat walls no longer get a grey haze',
    'On Low it costs the same as the old shading. On High and up it is added on top of bounce light, at half strength, for the small creases bounce light misses',
    'A small blur that stops at edges takes the grain out, so nothing sparkles',
    'Bright surfaces keep a bit more light in their corners than dark ones, like in real life',
    'Works on every device, phones and the browser too. The Ambient Occlusion slider in Studio still sets how strong it is in your game',
  ],
};
