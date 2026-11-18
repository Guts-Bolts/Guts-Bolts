export default {
  id: 'solid-ground', name: 'Solid Ground', version: '0.8.1', time: 1795013961, tag: 'Studio',
  summary: 'With Collisions on, parts you drag in Studio stop on the terrain instead of sinking through it.',
  items: [
    'Fixed blocks going straight through hills and valleys when you drag them in Studio, even with Collisions turned on. Collisions only checked other parts, never the ground',
    'Now a part you drag down onto the terrain stops right on top of it, like it would on another part. Hill tops poking into the middle of a big part count too',
    'Works with the Move gizmo, the Move arrows and Blender\'s G key',
    'Parts that already start inside a hill can still be pulled out of it',
  ],
};
