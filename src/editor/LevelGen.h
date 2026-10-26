#pragma once
// Procedural levels: makes a level from a few rules and a seed (the same seed always makes
// the same level), then checks it the way a player would: is there a way from the spawn to
// the goal? Broken levels can be thrown out and the next seed tried.
//
//   Dungeon: rooms joined by corridors, with crates and coins, nothing overlapping.
//   Obby:    a course of platforms with jumps a character can make, lava underneath.
#include "../scene/SceneNode.h"
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

class Scene;

namespace LevelGen {

enum class Style { Dungeon, Obby };
inline const char* kStyleNames[] = {"Dungeon", "Obby"};

struct Settings {
    Style    style = Style::Dungeon;
    uint32_t seed = 1;
    int      count = 8;               // rooms (Dungeon) or platforms (Obby)
    int      roomMin = 14, roomMax = 26;
    float    wallHeight = 8.0f;
    bool     props = true;            // crates (Dungeon)
    bool     coins = true;
    glm::vec3 floorColor{0.42f, 0.40f, 0.38f};
    glm::vec3 wallColor{0.62f, 0.58f, 0.52f};
};

// One part of the level (positions are from the level's origin; the floor's top is y = 0).
struct Piece {
    enum Kind { Floor, Wall, Platform, Crate, Coin, Spawn, Goal, Checkpoint, Lava } kind;
    std::string name;
    glm::vec3 pos, size;
    glm::vec3 color{0.6f};
    Material  material = Material::Plastic;
    int       room = -1;
};

struct Level {
    Settings           settings;
    std::vector<Piece> pieces;
    int                placed = 0;     // rooms / platforms that fitted
    glm::vec3          spawn{0.0f}, goal{0.0f};
    std::vector<std::string> problems;  // found by check()
};

Level make(const Settings& s);
// The rules a level must keep: everything placed, nothing overlapping, jumps not too far.
// Fills level.problems; true when there are none.
bool check(Level& level);

// The level as parts, in a Model called `name` in Workspace, `at` its origin.
SceneNode* build(Scene& scene, const Level& level, const glm::vec3& at, const std::string& name);

// The robot test: builds the level on its own in an empty world, makes a navmesh and looks
// for a route from the spawn to the goal (walking and jumping like a character).
struct Route {
    bool        ok = false;
    std::string status;       // NavMesh status name
    float       length = 0.0f;
    int         jumps = 0;
};
Route walk(const Level& level);

} // namespace LevelGen
