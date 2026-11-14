export default {
  id: 'flesh-and-floodlights', name: 'Flesh and Floodlights', time: 1794333900, tag: 'Engine',
  summary: 'Bundles get real bones, hats and capes wobble and flap, gore gets a lot meaner, things break into pieces, and lamps finally cast shadows.',
  items: [
    'Bundles can carry their own bones now, and arms and legs can bend at the elbow and knee (tick "Arms and legs bend at the elbows and knees" when you make one in Studio)',
    'Tick Jiggle on a hat or accessory and it wobbles on a spring (ears, ponytails, antennas); tick Cloth and it flaps like a cape or flag, and blows in the wind (workspace.GlobalWind)',
    'Liquid Blood (Player settings, or workspace.LiquidBlood) turns some of the blood into real liquid that runs, drips and pools',
    'Dents in bodies spring back out after a hit (workspace.SoftTissue). Heads are hard, bellies are squishy, and hitting the same spot hard enough tears it',
    'Cut-off arms and legs have a clean end now: flesh with a bone in the middle, or a metal plate with wires for robots',
    'Fast shots go right through: a bigger exit wound on the other side, and what comes out splats on the wall behind',
    'Gore works on any character shape, not just the usual six parts. Characters can set their own Gore ("Blood", "Oil" or "Off"), BloodColor and BloodAmount as attributes',
    'Tag a part Breakable (or call part:Shatter()) and it smashes into pieces where it was hit, smaller near the hit. Explosions break them too',
    'Lots of loose parts at once is much faster: the physics only checks things that are near each other',
    'Lights have a Shadows box: the nearest lamps with it on cast real shadows',
    'Bouncy Light (Lighting.BounceLight): sunlight and lamplight bounce off coloured things, so a red floor gives the wall next to it a red glow',
    'Lip sync: mouths open and close with voice chat and with sounds playing inside a character. Scripts can set MouthOpen themselves, and a bone called Jaw moves with it. Sound.PlaybackLoudness works too',
  ],
};
