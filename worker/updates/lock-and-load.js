export default {
  id: 'lock-and-load', name: 'Lock and Load', version: '0.7.9', time: 1793725200, tag: 'Engine',
  summary: 'Guns that really hit, a ready-made Blaster gear, and Roblox\'s Weld and Motor6D joints so old weapon scripts work.',
  items: [
    'New ready-made gear: the Blaster. Hold the mouse to fire where you aim, R to reload, 12 shots, 20 damage (double for headshots), with tracers, sparks, sounds and an ammo counter. It works for players who joined your game too',
    'Fast bullets no longer fly straight through things between two frames, so Touched fires when they hit',
    'Weld, ManualWeld, Snap, Motor and Motor6D, with Part0, Part1, C0, C1, DesiredAngle and MaxVelocity, like Roblox',
    'Characters now have Roblox\'s joints (Right Shoulder, Left Hip, Neck...), so scripts that pose arms with C0 work',
    'HingeConstraint limits (LimitsEnabled, UpperAngle, LowerAngle), and CylinderMesh / SpecialMesh / BlockMesh',
    'Old Roblox names work too: cf.p, cf.lookVector, cf:toObjectSpace(), v.magnitude, CFrame.fromOrientation...',
    'player:WaitForChild("PlayerGui") no longer waits forever',
    'Older Roblox scripts get further: FontSize, part surfaces, SizeConstraint, attachment Axis, a GUI\'s Transparency, button MouseButton1Down / Up, mouse.Move, mouse.KeyDown giving the letter, Humanoid.RigType and workspace.FilteringEnabled all work now',
    'The Studio Assistant now knows how to build a gun the right way',
  ],
};
