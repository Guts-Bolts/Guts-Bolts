#pragma once
// Physics record and replay: runs the physics on its own (no scripts, no player) from
// how the scene is now, keeps where every loose part was each step, so Studio can show
// any moment of it again. A recording can be saved as a physics test: running it later
// says if the parts still end up where they did (it fails when a physics change makes
// them act differently).
#include "../scene/SceneNode.h"
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

class Scene;

namespace PhysicsReplay {

// One loose part at one moment (its own position / rotation, like Properties shows them).
struct PartState {
    uint64_t  id = 0;
    glm::vec3 position{0.0f}, rotation{0.0f};   // local, rotation in degrees
    glm::vec3 velocity{0.0f}, angularVelocity{0.0f};
    float     sleepTime = 0.0f;
};

struct Frame {
    float                  time = 0.0f;
    std::vector<PartState> parts;   // the parts still in the world
    const PartState* find(uint64_t id) const;
};

struct Recording {
    float                    step = 1.0f / 60.0f;   // seconds per physics step
    std::vector<Frame>       frames;                // frames[0] is the start
    std::vector<uint64_t>    ids;                   // every loose part, in order
    std::vector<std::string> names;                 // ...and their names
    double                   ms = 0.0;              // how long recording took
    bool empty() const { return frames.size() < 2; }
    float seconds() const { return frames.empty() ? 0.0f : frames.back().time; }
    std::string nameOf(uint64_t id) const;
};

// Runs the physics for `seconds` from the scene saved as `sceneText` (Serializer::saveScene).
// `start` (optional): put these parts here first (a saved test's starting places).
Recording record(const std::string& sceneText, float seconds, const std::vector<PartState>* start = nullptr);

// Moves the scene's parts to where they were in `frame` (their speeds too).
void apply(Scene& scene, const Frame& frame);

// A saved physics test (a StringValue in ServerStorage > PhysicsTests).
struct Test {
    std::string            name;
    float                  seconds = 3.0f;
    float                  tolerance = 0.1f;    // studs a part may be off by
    Recording              expected;            // testToJson keeps every `kTestEvery`th frame (and the last)
};
constexpr int kTestEvery = 15;                  // (a quarter of a second)
nlohmann::json testToJson(const Test& t);
bool           testFromJson(const nlohmann::json& j, Test& t);

struct Result {
    bool        passed = false;
    float       worst = 0.0f;          // the most any part was off by (studs)
    std::string worstPart;
    float       worstTime = 0.0f;
    std::string message;               // for people: "Same as when it was saved." / "Crate is 2.4 studs off at 1.5 s."
};
// Records again from `sceneText` and compares with what the test expects.
Result run(const std::string& sceneText, const Test& test);

} // namespace PhysicsReplay
