export default {
  id: 'lua-debugger-breakpoints', name: 'Stop Right There, Script', version: '0.7.6', time: 1793044800, tag: 'Studio',
  summary: 'The Lua Debugger is easier to use and stops where it should: click a line number to add a breakpoint.',
  items: [
    'Click a line number in the Script Editor to add a breakpoint (a red dot). Click it again to take it away.',
    'When a script stops at a breakpoint, the Script Editor jumps to that line and lights it up, and the game waits too.',
    'Fixed: a breakpoint inside a loop only stopped every other time round.',
    'Fixed: Step Over and Step Out could stop in a different script that happened to run next.',
    'Code that Lua can\'t pause (like a table.sort comparison) now keeps running with a warning instead of an error.',
    'AI apps can use the debugger too, with the new lua_debugger tool.',
  ],
};
