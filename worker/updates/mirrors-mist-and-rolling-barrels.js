export default {
  id: 'mirrors-mist-and-rolling-barrels', name: 'Mirrors, Mist and Rolling Barrels', version: '0.8.0', time: 1794414248, tag: 'Engine',
  summary: 'Shiny things mirror the world, fog can lie low like real mist, round parts finally roll, and the air pushes light things about.',
  items: [
    'Shiny parts now mirror the world around them, not just the sky: metal, glass, ice, marble and rain-soaked floors show the buildings, players and lights next to them (like Unreal\'s screen-space reflections)',
    'Mirror reflections come on at High and get longer and cleaner on Epic and Cinematic; Low and Medium keep the cheap sky reflections, and phones stay on them for now',
    'Height fog: fog can now be thick down low and thin up high, so mist fills valleys and lies on the water while towers poke out of it (Lighting.FogFalloff and Lighting.FogHeight, or the Fog section in Studio)',
    'Volumetric fog: on High and up the fog takes light. Sunlight cuts bright shafts through gaps (and shadows cut dark ones), and lamps glow in the mist in their own colour',
    'The mist drifts in wisps that follow the wind (Lighting.FogWisps, 0 smooth to 1 very wispy, or the Wisps slider in Studio)',
    'It is worked out on a small picture and reused from frame to frame so it stays fast; Low, Medium, phones and the web Player keep the simple fog',
    'Rain and snow leave a low mist on the ground, and the Sunset, Overcast and Horror looks use height fog too',
    'Cylinders are really round now: barrels and logs roll down hills, wheels made of cylinders roll smoothly, and a drum stands on its flat end (before, they acted like boxes)',
    'Air: loose parts are slowed by the air and pushed by the wind (workspace.AirDensity and workspace.GlobalWind). It\'s real-world air, so bricks don\'t notice, but a light foam sheet floats down slowly and blows away',
    'Studio\'s Lighting window has Wind and Air Density under World',
  ],
};
