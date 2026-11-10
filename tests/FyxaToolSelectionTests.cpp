#include "../src/editor/FyxaToolSelection.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using nlohmann::json;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAILED at line " << __LINE__ << ": " #condition << "\n"; \
    return EXIT_FAILURE; \
} } while (false)

bool containsName(const json& tools, const std::string& name) {
    for (const auto& tool : tools) if (tool.value("name", std::string()) == name) return true;
    return false;
}

int main() {
    const json native = json::array({
        {{"name", "get_engine_info"}, {"title", "Engine reference"}, {"description", "Learn engine capabilities."},
         {"inputSchema", json::object()}, {"_meta", {{"gutsbolts/domain", "DOCS"}}}},
        {{"name", "get_game_tree"}, {"title", "Inspect scene"}, {"description", "Show all game objects."},
         {"inputSchema", json::object()}, {"_meta", {{"gutsbolts/domain", "SCENE"}}}},
        {{"name", "find_objects"}, {"title", "Find objects"}, {"description", "Search scene objects."},
         {"inputSchema", json::object()}, {"_meta", {{"gutsbolts/domain", "SCENE"}}}},
        {{"name", "insert_object"}, {"title", "Insert object"}, {"description", "Create a part or object."},
         {"inputSchema", json::object()}, {"_meta", {{"gutsbolts/domain", "SCENE"}}}},
        {{"name", "set_property"}, {"title", "Set property"}, {"description", "Change object properties."},
         {"inputSchema", json::object()}, {"_meta", {{"gutsbolts/domain", "OBJECTS"}}}},
        {{"name", "create_script"}, {"title", "Create script"}, {"description", "Write Lua code."},
         {"inputSchema", json::object()}, {"_meta", {{"gutsbolts/domain", "SCRIPTING"}}}},
        {{"name", "bake_navmesh"}, {"title", "Bake navigation mesh"}, {"description", "Build navmesh for pathfinding."},
         {"inputSchema", json::object()}, {"_meta", {{"gutsbolts/domain", "NAVIGATION"}}}},
        {{"name", "find_path"}, {"title", "Find path"}, {"description", "Calculate a path through the navmesh."},
         {"inputSchema", json::object()}, {"_meta", {{"gutsbolts/domain", "NAVIGATION"}}}},
        {{"name", "publish_game"}, {"title", "Publish game"}, {"description", "Publish the current game."},
         {"inputSchema", json::object()}, {"_meta", {{"gutsbolts/domain", "PUBLISH"}}}}
    });
    const json external = json::array({
        {{"qualifiedName", "mcp__github__issues"}, {"description", "Search GitHub issues and pull requests."},
         {"inputSchema", json::object()}},
        {{"qualifiedName", "mcp__weather__forecast"}, {"description", "Get a weather forecast."},
         {"inputSchema", json::object()}}
    });

    const json scene = FyxaToolSelection::selectNative(native, "Make a red ball", {}, 24);
    CHECK(containsName(scene, "get_engine_info"));
    CHECK(containsName(scene, "get_game_tree"));
    CHECK(containsName(scene, "insert_object"));
    CHECK(containsName(scene, "set_property"));
    CHECK(!containsName(scene, "publish_game"));
    CHECK(!containsName(scene, "create_script"));

    const json scripting = FyxaToolSelection::selectNative(native, "Write a Lua script for the door", {}, 24);
    CHECK(containsName(scripting, "create_script"));
    CHECK(!containsName(scripting, "publish_game"));

    const json navigation = FyxaToolSelection::discover(native, external, "navmesh pathfinding", 10);
    CHECK(containsName(navigation, "bake_navmesh"));
    CHECK(containsName(navigation, "find_path"));

    const json externalSelection = FyxaToolSelection::selectExternal(external, "Fix the GitHub issue", {}, 10);
    CHECK(containsName(externalSelection, "mcp__github__issues"));
    CHECK(!containsName(externalSelection, "mcp__weather__forecast"));

    const json discovered = FyxaToolSelection::selectNative(native, "What is in my game?", {"publish_game"}, 24);
    CHECK(containsName(discovered, "publish_game"));

    const json bounded = FyxaToolSelection::selectNative(native, "Build a game with scripts and publish it", {}, 3);
    CHECK(bounded.size() <= 3);

    std::cout << "Fyxa dynamic tool selection tests passed\n";
    return EXIT_SUCCESS;
}
