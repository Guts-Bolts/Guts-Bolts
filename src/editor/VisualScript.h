#pragma once
// Visual scripting ("Blocks"): a Script made by connecting blocks, like "When touched" ->
// "If it's a player" -> "Set colour". The blocks turn into normal Lua (you can read it in
// the Script Editor to learn), and the blocks themselves are kept at the top of the
// script's code, in a comment, so they're saved and shared with the game like any script.
//
// A graph (JSON): {"nodes": [{"id", "type", "x", "y", "f": {field: text}}],
//                  "links": [{"from": node id, "pin": "next" / "then" / ..., "to": node id}]}
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace VisualScript {

struct Field {
    enum Kind { Text, Number, Color, Target, Choice } kind;
    const char* key;
    const char* label;
    const char* def;                     // what a new block starts with
    std::vector<const char*> choices;    // (Choice)
};

struct Block {
    const char* type;
    const char* category;                // Events, Actions, Players, Control
    const char* title;
    const char* tip;
    std::vector<Field> fields;
    std::vector<const char*> outs;       // where it carries on: "next", or "then" / "else" / "next"...
    bool event = false;                  // starts a chain (no way in)
};

const std::vector<Block>& blocks();
const Block* find(const std::string& type);

// A Target field: "this" (the script's parent), "other" (the part that touched), "character"
// (the player's character), or "path:Workspace.Door".
const char* targetLabel(const std::string& value);

// The Lua for a graph. Problems (a block that can't work where it is) go in `problems`;
// those blocks are left out.
std::string toLua(const nlohmann::json& graph, std::vector<std::string>& problems);

// A script's code made from blocks: the blocks in a comment at the top, then the Lua.
std::string scriptSource(const nlohmann::json& graph, std::vector<std::string>& problems);
// The blocks of a script made with them (null if it wasn't). `edited`: its Lua was changed
// by hand since (changing the blocks writes over that).
nlohmann::json graphOf(const std::string& source, bool* edited = nullptr);
inline bool isBlocks(const std::string& source) { return source.rfind("--[==[blocks", 0) == 0; }

} // namespace VisualScript
