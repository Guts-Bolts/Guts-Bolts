export default {
  id: 'red-pen', name: 'Red Pen', version: '0.7.9', time: 1793799769, tag: 'Studio',
  summary: 'The Script editor now underlines mistakes while you type, before you ever press Play.',
  items: [
    'Red wavy lines where Lua can\'t read the script, with plain words on what\'s wrong (like "use == to check if two things are the same")',
    'A missing "end" now points at the function or if that needs it, not the bottom of the script',
    'Orange wavy lines for likely typos: pirnt, math.flor, task.wiat, game:GetService("Plyers"), Instance.new("Prat"), Enum.KeyCode.Spase',
    'part.Destroy() with a dot gets a line too: it needs a colon, part:Destroy()',
    'Point at a line to see what\'s wrong; put the cursor on that line and press Fix to change it to the right spelling',
    'View > Script Analysis and the AI assistant\'s script tools use the same check',
    'Fixed: a Luau "type Name = ..." line after other code made the whole script fail to load',
  ],
};
