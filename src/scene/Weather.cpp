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

// How bright the lightning makes the world: a hard flash, then a second, weaker
// flicker a moment later (real lightning strikes the same path a few times).
float WeatherSim::flash() const {
    const float a = m_boltAge;
    if (a > 0.9f) return 0.0f;
    const float first = std::exp(-a * 10.0f);
    const float second = a > 0.13f ? 0.6f * std::exp(-(a - 0.13f) * 9.0f) : 0.0f;
    return std::clamp(std::max(first, second), 0.0f, 1.0f);
}

bool WeatherSim::takeThunder(float& volume, glm::vec3& from) {
    if (!m_thunderNow) return false;
    m_thunderNow = false;
    volume = m_thunderVolume;
    from = m_strikeAt;
    return true;
}

// A bolt from the clouds to the ground somewhere around the camera: a jagged line with
// a few side branches that stop in mid-air.
void WeatherSim::strike(const glm::vec3& eye, float amount) {
    const float angle = random() * 6.2831853f;
    const float dist = kNearStrike + (kFarStrike - kNearStrike) * random();
    const glm::vec3 top(eye.x + std::cos(angle) * dist, eye.y + 170.0f + 40.0f * random(), eye.z + std::sin(angle) * dist);
    float groundY = groundAt(top.x, top.z);
    if (!(groundY > -1e8f)) groundY = eye.y - 25.0f;   // (beyond what we measured: a bit under the camera)
    const glm::vec3 bottom(top.x + (random() - 0.5f) * 40.0f, groundY, top.z + (random() - 0.5f) * 40.0f);
    m_bolt.clear();
    auto zigzag = [&](glm::vec3 a, const glm::vec3& b, int pieces, float wobble) {
        for (int i = 1; i <= pieces; ++i) {
            glm::vec3 p = glm::mix(a, b, 1.0f / (float)(pieces - i + 1));
            if (i < pieces) p += glm::vec3(random() - 0.5f, (random() - 0.5f) * 0.3f, random() - 0.5f) * wobble;
            m_bolt.push_back(a);
            m_bolt.push_back(p);
            a = p;
        }
    };
    zigzag(top, bottom, 16, 14.0f);
    const size_t trunk = m_bolt.size() / 2;
    for (int b = 0; b < 3; ++b) {   // branches: from a point on the trunk, out and down
        const glm::vec3 from = m_bolt[2 * (size_t)(2 + random() * (float)(trunk - 6)) + 1];
        const glm::vec3 to = from + glm::vec3((random() - 0.5f) * 90.0f, -(25.0f + 40.0f * random()), (random() - 0.5f) * 90.0f);
        zigzag(from, to, 6, 8.0f);
    }
    m_boltAge = 0.0f;
    m_strikeAt = bottom;
    m_thunderIn = dist / kThunderSpeed;
    m_thunderVolume = std::clamp(1.1f - dist / (kFarStrike * 1.2f), 0.25f, 1.0f);
    // The next one: a few seconds to a third of a minute, sooner in a harder storm.
    nextStrike = (4.0f + 14.0f * random()) / (0.5f + amount);
}

void WeatherSim::update(Scene& scene, const glm::vec3& eye, float dt) {
    const Environment& env = scene.environment();
    const int kind = env.weather > 0 && env.weather < kWeatherCount ? env.weather : 0;
    const float amount = std::clamp(env.weatherAmount, 0.0f, 1.0f);
    dt = std::clamp(dt, 0.0f, 0.1f);
    m_time += dt;

    // Wet and snowy build up over a while, then dry off and melt more slowly.
    const bool storm = kind == (int)Weather::Storm;
    const float wantWet = storm ? 1.0f : kind == (int)Weather::Rain ? 0.35f + 0.65f * amount : 0.0f;
    const float wantSnow = kind == (int)Weather::Snow ? 0.4f + 0.6f * amount : 0.0f;
    auto toward = [&](float& v, float want, float up, float down) {
        v = v < want ? std::min(want, v + dt / up) : std::max(want, v - dt / down);
    };
    toward(m_wet, wantWet, 15.0f, 45.0f);
    toward(m_snow, wantSnow, 30.0f, 40.0f);

    // Lightning, and its thunder on the way.
    m_boltAge += dt;
    if (m_thunderIn >= 0.0f) {
        m_thunderIn -= dt;
        if (m_thunderIn < 0.0f) m_thunderNow = true;
    }
    if (storm) {
        nextStrike -= dt;
        if (nextStrike <= 0.0f) strike(eye, amount);
    }

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
    // (A storm always pours: at least three quarters of the drops, whatever the intensity.)
    const float pour = storm ? std::max(amount, 0.75f) : amount;
    const size_t want = (size_t)(kMaxDrops * (snow ? 0.6f : 1.0f) * (0.08f + 0.92f * pour));
    while (m_drops.size() < want) { Drop d; respawn(d, eye, true); m_drops.push_back(d); }
    if (m_drops.size() > want) m_drops.resize(want);

    const glm::vec2 wind = snow ? glm::vec2(0.6f, 0.3f) : glm::vec2(2.5f, 1.0f) * (0.5f + amount) * (storm ? 3.0f : 1.0f);   // (a storm blows the rain sideways)
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
            s.alpha = 0.18f + 0.12f * pour;
        }
        m_streaks.push_back(s);
    }
}
