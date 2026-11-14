export default {
  id: 'squish-and-wobble', name: 'Squish and Wobble', time: 1794638211, tag: 'Engine',
  summary: 'Parts can now be soft: they sag, squash flat when they land, wobble back into shape and can even tear in two.',
  items: [
    'Give a part the attribute Soft = true and its surface turns springy, like jelly or a lump of meat',
    'Soft parts sag a little under their own weight, flatten and bulge out at the sides when they land, then wobble back',
    'They keep their size while they squash, so a soft ball pressed flat spreads out instead of shrinking',
    'Things that knock into a soft part leave a dent that ripples and springs back',
    'Softness (0 to 1) picks how squishy it is: 0 is firm rubber, 1 is wobbly jelly',
    'TearAt makes it tear in two when it is stretched too far, with meat and bone showing on a Flesh part',
    'New part:Poke(point, velocity) for scripts, to hit or tug a soft part',
    'A soft part that has stopped wobbling costs nothing until something touches it again',
  ],
};
