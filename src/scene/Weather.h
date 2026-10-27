#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class Scene;

// Rain and snow (Lighting.Weather): drops falling in a box that follows the camera,
// stopping on the first thing under them (a roof keeps the rain off you), plus how
// wet things are and how much snow has settled, which build up and fade slowly.
// Only for looks: each computer runs its own, nothing is sent or saved.
class WeatherSim {
public:
    // One falling drop or flake, as a short line from `top` to `bottom`.
    struct Streak { glm::vec3 top, bottom; float width; float alpha; };

    // Move everything on by `dt` seconds around the camera at `eye`.
    void update(Scene& scene, const glm::vec3& eye, float dt);
    const std::vector<Streak>& streaks() const { return m_streaks; }
    int   kind() const { return m_kind; }        // what's falling now (Weather), 0 = nothing
    float wetness() const { return m_wet; }      // 0 dry .. 1 soaked (rain)
    float snowCover() const { return m_snow; }   // 0 none .. 1 white tops (snow)

    // The highest thing under a spot (roofs, the ground), as last measured. Drops stop there.
    float groundAt(float x, float z) const;

    static constexpr float kHalfBox = 22.0f;     // drops fall within this many studs of the camera
    static constexpr int   kMaxDrops = 2400;

private:
    struct Drop { glm::vec3 pos; float speed; float seed; };
    void respawn(Drop& d, const glm::vec3& eye, bool anywhere);
    void measureRoofs(Scene& scene, const glm::vec3& eye);
    float random();

    std::vector<Drop>   m_drops;
    std::vector<Streak> m_streaks;
    int      m_kind = 0;
    float    m_wet = 0.0f, m_snow = 0.0f;
    float    m_time = 0.0f;
    float    m_roofTimer = 0.0f;
    uint32_t m_rng = 0x9E3779B9u;
    // The roof grid: kCells x kCells columns around m_roofCenter.
    static constexpr int   kCells = 30;
    static constexpr float kCell = 2.0f * kHalfBox / kCells;
    std::vector<float> m_roof;
    glm::vec2 m_roofCenter{0.0f};
};
