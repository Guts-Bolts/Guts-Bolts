#pragma once
// Scene compare and merge: what's different between two versions of a game (two saved
// files, or a file and what's open in Studio), object by object, and bringing chosen
// differences into the open game. Objects keep their id when a game is saved and opened
// again, so the same object is found in both versions even if it was renamed or moved.
//
// With a third version (the one both started from), only what the other version changed
// is listed, and a change is a conflict when this version changed the same object too.
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class Scene;

namespace SceneDiff {

struct Field {
    std::string name;           // "pos", "color", "parent", "source"...
    std::string before, after;  // short text for people
};

struct Change {
    enum Kind { Added, Removed, Moved, Changed };
    Kind        kind = Changed;
    uint64_t    id = 0;          // 0 = the game's settings (lighting, info, rules...)
    std::string name, path, className;
    std::vector<Field> fields;   // Moved / Changed: what's different
    int         inside = 0;      // Added / Removed: how many objects are inside it too
    bool        conflict = false;
    std::string conflictWhy;     // "You changed it too." ...
    // (to bring it in: the other version's object, without children / with them for Added)
    nlohmann::json theirs;
    uint64_t    parent = 0;      // its parent in the other version
};
const char* kindName(Change::Kind k);

struct Comparison {
    std::vector<Change> changes;
    std::string         error;   // the texts weren't scenes
    std::string         otherText;
    int count(Change::Kind k) const;
};

// `mine` = the open game, `other` = the other version, `base` (optional) = where both started.
// All three are scene texts (Serializer::saveScene).
Comparison compare(const std::string& mine, const std::string& other, const std::string* base = nullptr);

// Brings changes[i] (for each i in `chosen`) into the scene. Returns how many were done;
// what couldn't be is added to `problems`.
int apply(Scene& scene, const Comparison& c, const std::vector<int>& chosen, std::vector<std::string>& problems);

// A saved game file as scene text (any format Studio opens). Empty + error on failure.
std::string loadText(const std::string& path, std::string* error);

} // namespace SceneDiff
