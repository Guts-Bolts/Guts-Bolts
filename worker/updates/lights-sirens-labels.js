export default {
  id: 'lights-sirens-labels', name: 'Lights, Sirens, Labels', version: '0.7.9', time: 1793681488, tag: 'Engine',
  summary: 'Screens just for one player, respawning on command, and the Roblox bits a cops and robbers game needs.',
  items: [
    'player.PlayerGui is now that player\'s own screen: a server script can show "You are a Cop!" to just one person. StarterGui is still the screen everyone shares',
    'Things in StarterGui can still be found through PlayerGui (PlayerGui.MainGui), so older scripts keep working',
    'RichText on text labels, buttons and text boxes: <b>bold</b>, <font color="#ff0000">colours</font>, <font size="30">sizes</font>, underline and strike-through in one label',
    'player:LoadCharacter() respawns someone right now, player:Kick("reason") sends them out of the game, and Players.RespawnTime / Players.CharacterAutoLoads change how dying works',
    'team:GetPlayers(), Instance.new("Team"), BrickColor.new("Bright blue"), Players:GetPlayerByUserId and typeof()',
    'New events: ChildAdded, ChildRemoved, DescendantAdded, DescendantRemoving, AncestryChanged, Destroying, part.TouchEnded and humanoid.HealthChanged, plus part:GetTouchingParts()',
    'WaitForChild now really waits for the thing to show up (it used to give up straight away), with an optional time limit',
    'game:BindToClose for saving stats before a server closes, TweenPosition / TweenSize on UI, DateTime.now() and UserInputService.TouchEnabled',
    'The Studio Assistant and AI tools now know all about screen UI, so they stop saying the engine has no GUI text',
  ],
};
