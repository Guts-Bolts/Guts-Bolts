export default {
  id: 'point-of-contact', name: 'Point of Contact', time: 1793296000, tag: 'Engine',
  summary: 'Scripts can now ask where the mouse is pointing in the 3D world, not just on the screen.',
  items: [
    'New getmouse3d() gives the spot in the world under the mouse, the part it\'s on and which way that surface faces (getmouse() is still the 2D one)',
    'getmouse3d(50) only looks 50 studs out, and getmouse3d(nil, part) looks through that part; your own character never gets in the way',
    'Roblox\'s way works too: mouse.Hit now faces along the mouse\'s aim, and mouse.TargetSurface says which face of mouse.Target you\'re pointing at',
    'UserInputService:GetMouseLocation() added, ready for camera:ViewportPointToRay and workspace:Raycast',
    'Fixed CFrame.new(pos, lookAt) and CFrame.lookAt breaking when looking straight up or down, like from a top-down camera',
  ],
};
