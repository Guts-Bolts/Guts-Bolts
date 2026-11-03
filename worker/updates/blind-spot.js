export default {
  id: 'blind-spot', name: 'Blind Spot', version: '0.7.9', time: 1793717668, tag: 'Studio',
  summary: 'An eye in the Explorer to hide things while you build, Blender\'s G / R / S in Build mode, and Roblox animations that fit our dummies.',
  items: [
    'Every row in the Explorer has an eye (like Blender\'s): click it to hide that thing while you build. Only you stop seeing it: the game, saving and playtests still have it. Shift+click the eye to hide everything else',
    'H hides what\'s selected, Shift+H hides everything else, Alt+H shows it all again, and / shows just the selection (press / again to get everything back). Hidden things stay hidden after undo and after a playtest',
    'G moves and R rotates what\'s selected by following the mouse, like Blender. Press X, Y or Z to lock an axis (twice for the object\'s own axis), type a number like 3 or 90, hold Ctrl for steps. Click or Enter to finish, right-click or Esc to put it back',
    'More Blender keys in Build mode: X deletes, M moves things into a Model or Folder, Ctrl+P puts things inside the last one you picked, Alt+P takes them out, Home shows the whole map',
    'Turn on "Blender keys" (in the F1 window or the View menu) for S to scale, A to pick everything, Shift+D to duplicate and move and Shift+A to add. With it on, W A S D fly the camera while you hold the right mouse button',
    'Animation Editor: "..." > Import > From Roblox loads a Roblox animation (.rbxm / .rbxmx, saved from Roblox Studio\'s KeyframeSequence) and fixes it to fit: turned to face our way, shrunk to our size, and R15 animations fitted onto R6 dummies (and back)',
    'Roblox models with animations in them now come in with a real Animation object, fitted to the rig it\'s in',
    '"..." > Turn Around flips an animation front to back, for a character that faces the other way',
  ],
};
