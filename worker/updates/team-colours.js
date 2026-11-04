export default {
  id: 'team-colours', name: 'Team Colours', version: '0.7.9', time: 1793708400, tag: 'Engine',
  summary: 'The in-game player list now shows teams, sorts by score, and each game can colour it or hide it.',
  items: [
    'When players are on teams, the player list puts them under each team\'s name in the team\'s colour, with the team\'s total for every stat',
    'The player with the most of the first stat (like Cash or Kills) goes to the top',
    'Games can change the list\'s title, background, text colours and how see-through it is, in Studio\'s Player panel or with StarterGui:SetCore("PlayerListStyle", {...})',
    'Games can hide the player list: untick "Show player list" in Studio, or a LocalScript calls StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.PlayerList, false)',
    'Players who join someone\'s game see everyone\'s teams too',
  ],
};
