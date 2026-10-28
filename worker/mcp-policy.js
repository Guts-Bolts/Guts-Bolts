// Which of Studio's AI tools a connected app (like Lyreto) may use through https://gutsandbolts.net/mcp,
// by the permission (OAuth scope) the person approved. Used by the endpoint (worker/mcp.js) and again by
// the relay in the server (worker/mcp-relay.js). Studio itself has the same lists (src/editor/StudioMcp.cpp,
// kAppReadTools / kAppWriteTools): worker/test/mcp-policy.test.mjs checks they match.
//
//   studio.read   tools that only look (Studio's own "read only" flag in src/editor/AiTools.cpp)
//   studio.write  tools that change the game or run it (includes everything studio.read can do)
//   (no list)     never: publishing, and tools that reach files on the person's computer
//
// A tool that isn't in a list is refused and hidden, so a new Studio tool stays invisible to apps
// until someone decides it's safe and adds it here (and in StudioMcp.cpp).
// Based on the plan the Lyreto team sent (MIT).

export const READ_TOOLS = [
  'get_engine_info', 'get_game_tree', 'find_objects', 'get_object', 'read_script', 'get_output', 'get_errors',
  'diagnose_object', 'analyze_scene_performance', 'analyze_script_dependencies', 'validate_scene', 'find_path',
  'get_runtime_state', 'screenshot',
];
export const WRITE_TOOLS = [
  'insert_object', 'delete_object', 'set_property', 'find_and_change', 'select', 'create_script', 'edit_script',
  'visual_script', 'generate_level', 'run_lua', 'playtest', 'multiplayer_test', 'physics_replay', 'lua_debugger',
  'create_physical_object', 'spawn_explosion', 'bake_navmesh', 'walk_character_to', 'script_history', 'ai_activity',
  'checkpoint', 'undo', 'redo',
];
// Never, whatever the token says. publish_game puts a game online as you; compare_scenes opens and
// saves game files by path on your computer. Names like these are refused too, even if someone adds
// one to a list above by mistake.
export const DENIED_TOOLS = ['publish_game', 'compare_scenes'];
const DENIED_PATTERN = /publish|upload|release|sell|price|purchase|payout|bolts/i;

// MCP methods other than tools/call, and the scope each needs (initialize and ping need just a Studio scope).
const METHOD_SCOPES = {
  'tools/list': 'studio.read', 'resources/list': 'studio.read', 'resources/templates/list': 'studio.read',
  'resources/read': 'studio.read', 'prompts/list': 'studio.read', 'prompts/get': 'studio.read',
};

export const isDenied = (name) => typeof name !== 'string' || DENIED_TOOLS.includes(name) || DENIED_PATTERN.test(name);
const has = (scopes, s) => scopes.includes(s) || (s === 'studio.read' && scopes.includes('studio.write'));
export const hasStudio = (scopes) => has(scopes, 'studio.read');

// The scope a tool needs, or '' if no app may ever use it (denied or unknown).
export function scopeForTool(name) {
  if (isDenied(name)) return '';
  if (READ_TOOLS.includes(name)) return 'studio.read';
  if (WRITE_TOOLS.includes(name)) return 'studio.write';
  return '';
}

// May a token with these scopes call this tool? { ok: true } or { ok: false, message }.
export function canCall(name, scopes) {
  const shown = String(name).slice(0, 60);
  if (isDenied(name)) return { ok: false, message: '"' + shown + '" can\'t be used by connected apps. Publishing stays in Studio, done by you.' };
  const need = scopeForTool(name);
  if (!need) return { ok: false, message: 'Studio has no tool called "' + shown + '" that connected apps can use.' };
  if (!has(scopes, need)) return { ok: false, message: '"' + shown + '" needs the ' + need + ' permission. Connect the app again and allow it.' };
  return { ok: true };
}
export const canUseMethod = (method, scopes) => !!METHOD_SCOPES[method] && has(scopes, METHOD_SCOPES[method]);

// tools/list as this token should see it: denied, unknown and out-of-scope tools taken out.
export const filterTools = (tools, scopes) => (Array.isArray(tools) ? tools : []).filter((t) => t && canCall(t.name, scopes).ok);
