export default {
  id: 'piece-by-piece', name: 'Piece by Piece', time: 1793762224, tag: 'Engine',
  summary: 'Big maps can now load in pieces, so players join faster and phones use less memory.',
  items: [
    'Tick "Load Map In Pieces" on the Workspace (or set workspace.StreamingEnabled = true in a script)',
    'Players who join only get the part of the map near them; the rest loads in as they walk towards it',
    'Things far behind them are put away again, and come back when they return',
    'Streaming Radius (StreamingTargetRadius) sets how far around each player the map is loaded',
    'Roblox places that use StreamingEnabled keep it when imported',
  ],
};
