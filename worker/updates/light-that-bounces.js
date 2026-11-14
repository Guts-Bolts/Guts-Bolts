export default {
  id: 'light-that-bounces', name: 'Light That Bounces', version: '0.8.0', time: 1794358700, tag: 'Engine',
  summary: 'The first step toward Unreal-style graphics: bounced light, a temporal upscaler, and Low to Epic quality that picks itself.',
  items: [
    'Light now bounces: sunlit floors brighten the walls next to them, and corners get darker, traced across the screen every frame (like Unreal\'s Lumen screen traces)',
    'A temporal upscaler (like Unreal\'s TSR): smooth edges and sharp detail, and lower graphics draw fewer pixels without looking blurry',
    'Graphics are split into 8 groups (shadows, lighting, reflections, effects, textures, view distance, anti-aliasing, resolution), each Low, Medium, High or Epic',
    'The top preset for playing is called Epic now, and you can pick each group yourself in Settings',
    'A new Cinematic level for screenshots and trailers: it draws 1.5x the pixels, traces many more rays and keeps far more past frames (not meant for playing)',
    'The first time you start the Player, a quick test picks the best level your computer can run smoothly',
    'Phones, the web Player and older graphics chips get the cheaper version of the costly parts automatically',
    'Textures stay sharp when you look at them at an angle (up to 16x anisotropic filtering)',
    'Every level keeps the same colours and look, so a game looks like itself on Low too',
  ],
};
