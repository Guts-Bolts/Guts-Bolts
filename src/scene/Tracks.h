#pragma once
// Tyre tracks and skid marks (Realism): wheels (motor hinges, like Vehicles.h's) leave
// tracks in sand, snow, mud and dirt, and black rubber on anything when they slide
// (braking hard, spinning the wheels, drifting round a corner). Only for looks: each
// computer makes its own, and they fade after a minute.
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

class Scene;

namespace Tracks {

// How fast the bottom of a wheel slides over the ground (0 = rolling cleanly).
// `v` the wheel's speed, `w` its spin (radians a second about each axis), `toGround`
// from its middle to where it touches the ground.
float slip(const glm::vec3& v, const glm::vec3& w, const glm::vec3& toGround);
constexpr float kSkidSlip = 6.0f;   // sliding faster than this (studs a second) leaves rubber

class System {
public:
    void update(Scene& scene, float dt);
    void clear() { m_wheels.clear(); m_list.clear(); m_listTimer = 0.0f; }
    int  marksMade() const { return m_made; }
private:
    struct Wheel { glm::vec3 last{0.0f}; bool has = false; };
    std::unordered_map<uint64_t, Wheel> m_wheels;
    std::vector<uint64_t> m_list;     // the wheels (found again every second)
    float m_listTimer = 0.0f;
    int   m_made = 0;
};

} // namespace Tracks
