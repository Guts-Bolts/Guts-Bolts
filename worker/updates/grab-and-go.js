export default {
  id: 'grab-and-go', name: 'Grab and Go', version: '0.7.9', time: 1793794635, tag: 'Engine',
  summary: 'DragDetectors are here: players can grab things with the mouse and drag them around.',
  items: [
    'Put a DragDetector in a part or model (Insert Object, or Instance.new("DragDetector"))',
    'DragStyle picks how it moves: across the floor, along a line, across the screen, or spinning round (great for doors, levers and wheels)',
    'Min/MaxDragTranslation and Min/MaxDragAngle stop it going too far',
    'Loose parts are carried while you hold them and fly off when you let go, so you can throw them',
    'Scripts hear DragStart, DragContinue and DragEnd, with the player who\'s dragging',
    'Works in multiplayer: the host moves it for everyone, and only if you\'re close enough',
  ],
};
