export default {
  id: 'roll-the-dice', name: 'Roll the Dice', version: '0.7.6', time: 1793131200, tag: 'Studio',
  summary: 'Pick a number and get a whole level: the new Level Generator makes dungeons and obbies, and checks that you can actually finish them.',
  items: [
    'New Level Generator window (MODEL > Generate): makes a Dungeon (rooms, corridors, crates and coins) or an Obby (platforms, checkpoints and lava).',
    'Every level comes from a seed number. The same seed always makes the same level, so you can share a good one by its number.',
    'Each level is checked before you get it: everything fits, nothing is stuck inside anything else, and a robot walks and jumps from the spawn to the goal.',
    '"Find one that works" tries up to 20 seeds for you, and "Test 10 seeds" lets you pick from a list.',
    'The Assistant and AI apps can make levels too, with the new generate_level tool.',
  ],
};
