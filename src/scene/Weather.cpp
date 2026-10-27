#include "Weather.h"
#include "Environment.h"
#include "Physics.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Terrain.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float kNoGround = -std::numeric_limits<float>::infinity();
constexpr float kFallBelow = 25.0f;   // drops further than this under the camera start again at the top
}

float WeatherSim::random() {
    // xorshift: quick, and the same on every computer is not needed (only looks).
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return (float)(m_rng & 0xFFFFFF) / (float)0x1000000;
}

float WeatherSim::groundAt(float x, float z) const {
    if (m_roof.empty()) return kNoGround;
    const int cx = (int)std::floor((x - m_roofCenter.x + kHalfBox) / kCell);
    const int cz = (int)std::floor((z - m_roofCenter.y + kHalfBox) / kCell);
    if (cx < 0 || cz < 0 || cx >= kCells || cz >= kCells) return kNoGround;
    return m_roof[(size_t)(cz * kCells + cx)];
}

// The top of the highest thing in each column around the camera. Parts count by
// their box (a tilted part covers a bit more than it really does), and so does the terrain.
void WeatherSim::measureRoofs(Scene& scene, const glm::vec3& eye) {
    m_roof.assign((size_t)(kCells * kCells), kNoGround);
    m_roofCenter = glm::vec2(eye.x, eye.z);
    const float x0 = eye.x - kHalfBox, z0 = eye.z - kHalfBox;
    scene.forEach([&](SceneNode* n) {
        if (!n->isPart() || n->internal || !n->visible || n->transparency >= 0.95f) return;
        const AABB b = Physics::worldBounds(n);
        if (b.max.x < x0 || b.max.z < z0 || b.min.x > x0 + 2.0f * kHalfBox || b.min.z > z0 + 2.0f * kHalfBox) return;
        const int ax = std::max(0, (int)std::floor((b.min.x - x0) / kCell));
        const int bx = std::min(kCells - 1, (int)std::floor((b.max.x - x0) / kCell));
        const int az = std::max(0, (int)std::floor((b.min.z - z0) / kCell));
        const int bz = std::min(kCells - 1, (int)std::floor((b.max.z - z0) / kCell));
        for (int z = az; z <= bz; ++z)
            for (int x = ax; x <= bx; ++x) {
                float& top = m_roof[(size_t)(z * kCells + x)];
                top = std::max(top, b.max.y);
            }
    });
    const Terrain& terrain = scene.terrain();
    if (terrain.empty()) return;
    for (int z = 0; z < kCells; ++z)
        for (int x = 0; x < kCells; ++x) {
            const float cx = x0 + x * kCell, cz = z0 + z * kCell;
            float h = 0.0f;
            if (terrain.highestUnder(cx, cz, cx + kCell, cz + kCell, h)) {
                float& top = m_roof[(size_t)(z * kCells + x)];
                top = std::max(top, h);
            }
        }
}

void WeatherSim::respawn(Drop& d, const glm::vec3& eye, bool anywhere) {
    d.pos.x = eye.x + (random() * 2.0f - 1.0f) * kHalfBox;
    d.pos.z = eye.z + (random() * 2.0f - 1.0f) * kHalfBox;
    d.pos.y = anywhere ? eye.y - 10.0f + random() * 36.0f : eye.y + 18.0f + random() * 8.0f;
    d.seed = random();
    d.speed = m_kind == (int)Weather::Snow ? 2.5f + 2.0f * random() : 42.0f + 14.0f * random();
}

void WeatherSim::update(Scene& scene, const glm::vec3& eye, float dt) {
    const Environment& env = scene.environment();
    const int kind = env.weather > 0 && env.weather < kWeatherCount ? env.weather : 0;
    const float amount = std::clamp(env.weatherAmount, 0.0f, 1.0f);
    dt = std::clamp(dt, 0.0f, 0.1f);
    m_time += dt;

    // Wet and snowy build up over a while, then dry off and melt more slowly.
    const float wantWet = kind == (int)Weather::Rain ? 0.35f + 0.65f * amount : 0.0f;
    const float wantSnow = kind == (int)Weather::Snow ? 0.4f + 0.6f * amount : 0.0f;
    auto toward = [&](float& v, float want, float up, float down) {
        v = v < want ? std::min(want, v + dt / up) : std::max(want, v - dt / down);
    };
    toward(m_wet, wantWet, 15.0f, 45.0f);
    toward(m_snow, wantSnow, 30.0f, 40.0f);

    m_streaks.clear();
    if (kind != m_kind) { m_drops.clear(); m_kind = kind; }
    if (!kind) { m_roof.clear(); return; }

    // Roofs move rarely, so measure them a few times a second (and when the camera wandered off).
    m_roofTimer -= dt;
    if (m_roof.empty() || m_roofTimer <= 0.0f ||
        glm::length(glm::vec2(eye.x, eye.z) - m_roofCenter) > kHalfBox * 0.4f) {
        measureRoofs(scene, eye);
        m_roofTimer = 0.4f;
    }

    const bool snow = kind == (int)Weather::Snow;
    const size_t want = (size_t)(kMaxDrops * (snow ? 0.6f : 1.0f) * (0.08f + 0.92f * amount));
    while (m_drops.size() < want) { Drop d; respawn(d, eye, true); m_drops.push_back(d); }
    if (m_drops.size() > want) m_drops.resize(want);

    const glm::vec2 wind = snow ? glm::vec2(0.6f, 0.3f) : glm::vec2(2.5f, 1.0f) * (0.5f + amount);
    m_streaks.reserve(m_drops.size());
    for (Drop& d : m_drops) {
        glm::vec3 vel(wind.x, -d.speed, wind.y);
        if (snow) {   // flakes flutter about as they drift down
            vel.x += std::sin(m_time * (1.1f + d.seed) + d.seed * 40.0f) * 0.9f;
            vel.z += std::cos(m_time * (0.9f + d.seed) + d.seed * 17.0f) * 0.9f;
        }
        d.pos += vel * dt;
        // Keep them around the camera as it moves (a drop that falls out one side comes back in the other).
        if (d.pos.x < eye.x - kHalfBox) d.pos.x += 2.0f * kHalfBox;
        if (d.pos.x > eye.x + kHalfBox) d.pos.x -= 2.0f * kHalfBox;
        if (d.pos.z < eye.z - kHalfBox) d.pos.z += 2.0f * kHalfBox;
        if (d.pos.z > eye.z + kHalfBox) d.pos.z -= 2.0f * kHalfBox;
        if (d.pos.y > eye.y + 30.0f) d.pos.y = eye.y + 18.0f;   // (the camera fell fast)
        if (d.pos.y < groundAt(d.pos.x, d.pos.z) || d.pos.y < eye.y - kFallBelow) {
            respawn(d, eye, false);
            continue;
        }
        Streak s;
        if (snow) {
            s.width = 0.07f + 0.06f * d.seed;
            s.top = d.pos + glm::vec3(0.0f, s.width * 0.5f, 0.0f);
            s.bottom = d.pos - glm::vec3(0.0f, s.width * 0.5f, 0.0f);
            s.alpha = 0.85f;
        } else {
            // A streak as long as the drop moves in a thirtieth of a second.
            s.top = d.pos - vel * (1.0f / 30.0f);
            s.bottom = d.pos;
            s.width = 0.025f + 0.015f * d.seed;
            s.alpha = 0.18f + 0.12f * amount;
        }
        m_streaks.push_back(s);
    }
}
