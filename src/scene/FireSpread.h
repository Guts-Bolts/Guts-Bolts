#pragma once
// Fire that spreads (Realism, workspace.FireSpread, off to start with): a part on fire
// (a Fire inside it) sets wood, cloth, grass and plastic touching it alight, a little at
// a time. Burning things char black and, burnt through, fall to pieces. Water puts out
// the fires it started, and rain slows it down. Every fire crackles (a real recording).
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>

class Scene;

namespace FireSpread {

// Chance a second that a fire sets a touching part of this flammability alight.
float catchRate(float flammability, bool raining);
// How charred (0..1) a part of this flammability is after burning this long.
float charAfter(float seconds, float flammability);
constexpr const char* kFireName = "SpreadFire";   // the Fires it starts (so it knows they're its own)

class System {
public:
    // `host`: start and end fires (the host's job: everyone gets the changes). Every
    // computer plays the crackling. `ears` = where you're listening from.
    void update(Scene& scene, float dt, bool host, const glm::vec3& ears);
    void clear();
    int  burning() const { return (int)m_burn.size(); }
private:
    struct Burn { float time = 0.0f; glm::vec3 color0{1.0f}; };
    std::unordered_map<uint64_t, Burn> m_burn;     // part -> how long it's burnt
    std::unordered_map<uint64_t, int>  m_sounds;   // part -> its crackling
    float    m_timer = 0.0f;
    uint32_t m_seed = 0x1234567u;
};

} // namespace FireSpread
