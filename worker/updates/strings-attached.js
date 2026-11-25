export default {
  id: 'strings-attached', name: 'Strings Attached', time: 1795550000, tag: 'Engine',
  summary: 'Six Roblox constraints that used to do nothing now really work, ropes can winch, and parts know which assembly they belong to.',
  items: [
    'RigidConstraint holds two attachments together, turned the same way, and can snap with DestructionEnabled',
    'PlaneConstraint keeps a part sliding on a flat plane, and UniversalConstraint bends any way without twisting (drive shafts, robot arms)',
    'TorsionSpringConstraint springs a part back into line, like a door that closes by itself',
    'NoCollisionConstraint lets two chosen parts pass through each other while still bumping into everything else',
    'LineForce pulls (or pushes) along the line between two attachments, with InverseSquareLaw for magnets and planets',
    'RopeConstraint has a winch: WinchEnabled, WinchTarget, WinchSpeed and WinchForce reel the rope in or out',
    'CurrentDistance, CurrentLength and CurrentAngle read how far apart or turned the attachments are right now',
    'AssemblyRootPart, GetRootPart, AssemblyMass, AssemblyCenterOfMass, IsGrounded, GetNoCollisionConstraints and GetClosestPointOnSurface work like Roblox',
    'Attachment.CFrame is now in its part, like Roblox (it used to be in the world), and Roblox files with these constraints import and export',
  ],
};
