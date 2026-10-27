export default {
  id: 'snap-together-code', name: 'Snap-Together Code', version: '0.7.6', time: 1793042100, tag: 'Studio',
  summary: 'Make scripts without typing: connect blocks like "When touched", "If it\'s a player" and "Give points" in the new Visual Scripting window.',
  items: [
    'New Visual Scripting window (VIEW > Blocks): drag blocks onto a board and connect them with lines.',
    '26 blocks to start with: events (touched, clicked, prompts, players joining, every few seconds), actions, player blocks (points, damage, teleport) and ifs, repeats and cooldowns.',
    'The blocks turn into real Lua, shown right next to them, so you can read how they work and learn from it.',
    'Block scripts are normal Scripts: they save, publish and run like any other, and the Script Editor has an "Edit as blocks" button.',
    'Blocks that can\'t work where they are (like "the part that touched" outside When touched) are listed under Problems instead of breaking the game.',
    'The Assistant and AI apps can make block scripts too, with the new visual_script tool.',
  ],
};
