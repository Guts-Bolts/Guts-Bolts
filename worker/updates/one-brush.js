export default {
  id: 'one-brush', name: 'One Brush Stroke', time: 1794679320, tag: 'Engine',
  summary: 'Blood stains, gibs, sparks and bolts are now drawn hundreds at a time, so a messy battle no longer slows the game down.',
  items: [
    'Every stain, drop, gib, spark and bolt used to be sent to the graphics card one at a time. Now they go in one list each and are drawn in one go',
    'Each one still has its own look: stains keep their own wobbly edge, how dry they are and whether they are a footprint or a smear',
    'Gibs keep their own skin colour and shape',
    'The lists of parts that share a mesh are kept from frame to frame instead of being made again every time',
    'Looks exactly the same as before, just faster',
  ],
};
