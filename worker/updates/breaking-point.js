export default {
  id: 'breaking-point', name: 'Breaking Point', time: 1794852743, tag: 'Studio',
  summary: 'Find out how much your game can take before a cheap phone gives up, and dress meshes in pictures made by scripts.',
  items: [
    'New Stress test window (Test menu): pick a cheap phone, a mid-range phone or an old laptop, and how many apps are open in the background',
    'It adds up the memory your game needs and says if the device has room for it (with StreamingEnabled, only the map near the spawn counts)',
    'It drops up to 3000 loose crates into a pit next to your map and times the physics, then guesses the time on the device and how many loose parts would still fit',
    'It also checks the server\'s tick, how quick raycasts are, and how much the moving parts cost each player\'s internet',
    'Everything runs on a copy, so your game never changes, and it ends with a plain list of what to fix',
    'The AI assistant (and connected apps) can run the same check with the new stress_test tool',
    'New SurfaceAppearance: put it in a MeshPart and its colour, bump, rough and metal pictures go on by the mesh\'s own UVs',
    'Scripts can make one with AssetService:CreateSurfaceAppearanceAsync from EditableImages, Library picture IDs or both mixed, with no converting first',
  ],
};
