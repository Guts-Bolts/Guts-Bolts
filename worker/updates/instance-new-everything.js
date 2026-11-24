export default {
  id: 'instance-new-everything', name: 'Instance.new Everything', time: 1795502400, tag: 'Engine',
  summary: 'About 90 more Roblox classes can be made with Instance.new, so scripts that build them no longer stop with "unknown class".',
  items: [
    'ObjectValue, CFrameValue, BrickColorValue and Configuration work, with .Changed giving the new Value like Roblox',
    'MeshPart, UnionOperation and CornerWedgePart are real parts (a MeshPart loads its MeshId)',
    'An Atmosphere put in Lighting sets the fog, and Clouds set the clouds (in Lighting or workspace.Terrain)',
    'BodyColors colours the character it is put in',
    'Animator and AnimationController can load animations for the rig they are in',
    'UnreliableRemoteEvent, SurfaceLight, Texture and CanvasGroup work as their nearest Guts&Bolts object',
    'Classes we do not draw yet (SelectionBox, handle adornments, Dialog, KeyframeSequence, UISizeConstraint and more) can still be made and keep their properties, with one warning that says so',
  ],
};
