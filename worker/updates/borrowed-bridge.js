export default {
  id: 'borrowed-bridge', name: 'Borrowed Bridge', time: 1793727226, tag: 'Engine',
  summary: 'Chat-bridge scripts made for Roblox games, like Lyreto\'s Game Bridge, now run in Guts&Bolts games as they are.',
  items: [
    'Roblox-style loops like "for _, player in Players:GetPlayers() do" and "if ... then ... else ..." values work in scripts',
    'TextService:FilterStringAsync runs text through the Guts&Bolts chat filter',
    'RunService:IsServer, IsClient and IsStudio, and game.JobId (each server\'s own ID)',
    'StarterGui:SetCore("ChatMakeSystemMessage") puts a line in your own chat, and DisplaySystemMessage from a LocalScript only shows to that player',
    'Things in ReplicatedStorage, ServerScriptService and the other service folders no longer count as inside the Workspace',
    'A LocalScript kept in ServerScriptService or ServerStorage doesn\'t run, and one put in StarterPlayerScripts during a game waits for the next player, like on Roblox',
    'Web requests wait up to 25 seconds for a site\'s answer, so live chat bridges can wait for new messages',
  ],
};
