#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class Scene;

// Rain and snow (Lighting.Weather): drops falling in a box that follows the camera,
// stopping on the first thing under them (a roof keeps the rain off you), plus how
// wet things are and how much snow has settled, which build up and fade slowly.
// A Storm is heavy rain with lightning: every so often a bolt strikes somewhere around
// the camera, the world lights up for a moment, and the thunder arrives a bit later
// (later and quieter the further away the bolt was).
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

    // Where drops just hit something (rain): a small ring of water that spreads and fades.
    struct Splash { glm::vec3 pos; float age; float seed; };
    const std::vector<Splash>& splashes() const { return m_splashes; }
    static constexpr float kSplashSeconds = 0.28f;
    static constexpr size_t kMaxSplashes = 900;

    // Lightning (Storm). The bolt is a jagged tree of line pieces: a bright trunk from the
    // clouds to the ground and dimmer, thinner branches that die out in the air (like the
    // real thing, each piece is the one before bent a little, at every size). It flickers
    // a few times in half a second, as real bolts strike again along the same path.
    // flash() is how much it lights the world up right now (0..1).
    struct BoltPiece { glm::vec3 a, b; float bright; float width; };
    const std::vector<BoltPiece>& bolt() const { return flash() > 0.0f ? m_bolt : m_noBolt; }
    float boltGlow() const { return flash(); }
    // The middle of the trunk: where its light comes from.
    glm::vec3 boltMiddle() const { return m_boltMiddle; }
    float flash() const;
    glm::vec3 strikeAt() const { return m_strikeAt; }
    // True once, when a strike's thunder reaches the camera: how loud (0..1) and from where.
    bool takeThunder(float& volume, glm::vec3& from);
    // Seconds until the next bolt (Storm only). Tests set it to 0 to strike now.
    float nextStrike = 6.0f;
    static constexpr float kNearStrike = 60.0f, kFarStrike = 300.0f;   // studs from the camera
    static constexpr float kThunderSpeed = 300.0f;   // studs a second (slower than real, so you hear the gap)

    // The highest thing under a spot (roofs, the ground), as last measured. Drops stop there.
    float groundAt(float x, float z) const;

    static constexpr float kHalfBox = 22.0f;     // drops fall within this many studs of the camera
    static constexpr int   kMaxDrops = 3600;

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
    void strike(const glm::vec3& eye, float amount);
    void grow(const glm::vec3& a, const glm::vec3& b, int depth, float bright, float width);
    std::vector<BoltPiece> m_bolt, m_noBolt;
    glm::vec3 m_boltMiddle{0.0f};
    struct Pulse { float at, strength, fade; };   // one flicker: when (seconds after the strike), how bright, how fast it dies
    std::vector<Pulse> m_pulses;
    std::vector<Splash> m_splashes;
    float     m_boltAge = 1e9f;        // seconds since the last strike
    glm::vec3 m_strikeAt{0.0f};
    float     m_thunderIn = -1.0f;     // seconds until its thunder is heard (< 0 = none on the way)
    float     m_thunderVolume = 0.0f;
    bool      m_thunderNow = false;
    // The roof grid: kCells x kCells columns around m_roofCenter.
    static constexpr int   kCells = 30;
    static constexpr float kCell = 2.0f * kHalfBox / kCells;
    std::vector<float> m_roof;
    glm::vec2 m_roofCenter{0.0f};
};
