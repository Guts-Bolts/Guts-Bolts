export default {
  id: 'snap-crackle-push', name: 'Snap, Crackle, Push', version: '0.7.6', time: 1793038607, tag: 'Engine',
  summary: 'The physics workbench: joints that snap when pulled too hard, force zones that blow things around, and a Physics View that shows what the physics is doing.',
  items: [
    'Every joint (Weld, Rope, Rod, Spring, Hinge, BallSocket...) can be Breakable: pull it harder than its BreakForce and it snaps',
    'Scripts can set BreakForce and read Broken. Turning a broken joint back on joins it again',
    'Force zones: tag a part ForceZone and loose parts inside get blown (Wind), pushed away (Push) or sucked in (Pull)',
    'New Physics View (MODEL tab or View menu): moving and sleeping parts, speed arrows, where things touch, and how hard each joint is pulled, going from green to red as it gets close to snapping',
    'Two new ready-mades to try it: Wind Zone and Breakable Bridge (drop a heavy ball on it)',
  ],
};
