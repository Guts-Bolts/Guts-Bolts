export default {
  id: 'copy-paste-forest', name: 'Copy-Paste Forest', time: 1793757000, tag: 'Engine',
  summary: 'Lots of copies of the same thing are now drawn in one go, so big builds lag less.',
  items: [
    'Parts with the same shape (bricks, balls, the same mesh) are drawn together instead of one by one',
    'Each copy keeps its own colour, size, material and spot, so nothing looks different',
    'Parts with clothing, a face, a MaterialVariant or bones are still drawn on their own',
  ],
};
