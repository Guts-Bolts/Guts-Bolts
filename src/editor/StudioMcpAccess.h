#pragma once

#include <string>

namespace StudioMcpAccess {

// Connected apps receive an explicit allowlist. Read tools are available with
// studio.read; mutating tools additionally require studio.write.
inline bool appMay(const std::string& tool, bool canChange) {
    static const char* readTools[] = {
        "get_engine_info", "get_game_tree", "find_objects", "get_object",
        "read_script", "get_output", "get_errors", "diagnose_object",
        "analyze_scene_performance", "analyze_script_dependencies",
        "validate_scene", "find_path", "get_runtime_state", "screenshot",
        "phone_check"
    };
    static const char* writeTools[] = {
        "insert_object", "delete_object", "set_property", "find_and_change",
        "select", "create_script", "edit_script", "visual_script",
        "generate_level", "run_lua", "playtest", "multiplayer_test",
        "physics_replay", "lua_debugger", "create_physical_object",
        "spawn_explosion", "bake_navmesh", "walk_character_to",
        "script_history", "ai_activity", "checkpoint", "undo", "redo",
        "build", "terrain", "create_ui", "add_game_system", "set_atmosphere"
    };
    for (const char* allowed : readTools) if (tool == allowed) return true;
    if (canChange)
        for (const char* allowed : writeTools) if (tool == allowed) return true;
    return false;
}

} // namespace StudioMcpAccess
