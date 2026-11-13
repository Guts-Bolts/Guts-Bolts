#include "Weather.h"
#include "Storms.h"
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

// How bright the lightning is right now: the brightest of its flickers, each a sudden
// jump that dies away fast (the last one glows on a little longer).
float WeatherSim::flash() const {
    const float a = m_boltAge;
    if (a > 1.2f) return 0.0f;
    float f = 0.0f;
    for (const Pulse& p : m_pulses)
        if (a >= p.at) f = std::max(f, p.strength * std::exp(-(a - p.at) * p.fade));
    return f < 0.004f ? 0.0f : std::min(f, 1.0f);
}

bool WeatherSim::takeThunder(float& volume, glm::vec3& from) {
    if (!m_thunderNow) return false;
    m_thunderNow = false;
    volume = m_thunderVolume;
    from = m_strikeAt;
    return true;
}

// One stretch of the bolt, from a to b: bend it in the middle and do the same to both
// halves, smaller each time, until the pieces are short. Now and then a branch forks
// off at the bend, dimmer and thinner, heading out and down.
void WeatherSim::grow(const glm::vec3& a, const glm::vec3& b, int depth, float bright, float width) {
    if (depth <= 0 || m_bolt.size() > 1500) { m_bolt.push_back({a, b, bright, width}); return; }
    const float len = glm::length(b - a);
    glm::vec3 mid = (a + b) * 0.5f;
    mid += glm::vec3(random() - 0.5f, (random() - 0.5f) * 0.3f, random() - 0.5f) * len * 0.26f;
    grow(a, mid, depth - 1, bright, width);
    grow(mid, b, depth - 1, bright, width);
    if (depth >= 2 && bright > 0.12f && random() < 0.34f) {
        glm::vec3 out(random() - 0.5f, 0.0f, random() - 0.5f);
        out = glm::length(out) > 1e-4f ? glm::normalize(out) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 along = len > 1e-4f ? (b - a) / len : glm::vec3(0.0f, -1.0f, 0.0f);
        glm::vec3 dir = glm::normalize(along * 0.75f + out * 0.8f + glm::vec3(0.0f, -0.25f, 0.0f));
        grow(mid, mid + dir * len * (0.45f + 0.4f * random()), depth - 1, bright * 0.42f, width * 0.6f);
    }
}

// A bolt from the clouds to the ground somewhere around the camera.
void WeatherSim::strike(const glm::vec3& eye, float amount) {
    const float angle = random() * 6.2831853f;
    const float dist = kNearStrike + (kFarStrike - kNearStrike) * random();
    const glm::vec3 top(eye.x + std::cos(angle) * dist, eye.y + 170.0f + 40.0f * random(), eye.z + std::sin(angle) * dist);
    float groundY = groundAt(top.x, top.z);
    if (!(groundY > -1e8f)) groundY = eye.y - 25.0f;   // (beyond what we measured: a bit under the camera)
    const glm::vec3 bottom(top.x + (random() - 0.5f) * 50.0f, groundY, top.z + (random() - 0.5f) * 50.0f);
    m_bolt.clear();
    grow(top, bottom, 6, 1.0f, 1.0f);
    m_boltMiddle = (top + bottom) * 0.5f;
    m_boltAge = 0.0f;
    m_strikeAt = bottom;
    // The flickers: the first, brightest strike, then 2 to 4 more along the same path.
    m_pulses.clear();
    m_pulses.push_back({0.0f, 1.0f, 16.0f});
    float at = 0.0f;
    const int more = 2 + (int)(random() * 2.99f);
    for (int i = 0; i < more; ++i) {
        at += 0.045f + 0.09f * random();
        m_pulses.push_back({at, 0.45f + 0.5f * random(), i == more - 1 ? 7.0f : 18.0f});
    }
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
    const bool storm = isStorm(kind);
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
    for (Splash& sp : m_splashes) sp.age += dt;
    m_splashes.erase(std::remove_if(m_splashes.begin(), m_splashes.end(),
                                    [](const Splash& sp) { return sp.age >= kSplashSeconds; }), m_splashes.end());
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

    glm::vec2 wind = snow ? glm::vec2(0.6f, 0.3f) : glm::vec2(2.5f, 1.0f) * (0.5f + amount) * (storm ? 3.0f : 1.0f);   // (a storm blows the rain sideways)
    if (Storms::active(scene)) {   // near a tornado or in a downburst the rain goes where that wind goes
        const glm::vec3 gust = Storms::tornadoWind(scene.storms(), eye) + Storms::downburstWind(scene.storms(), eye);
        glm::vec2 g = glm::vec2(gust.x, gust.z) * 0.45f;
        if (const float len = glm::length(g); len > 38.0f) g *= 38.0f / len;
        wind += g;
    }
    const float hail = snow ? 0.0f : Storms::hailAt(scene, eye);
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
        const float ground = groundAt(d.pos.x, d.pos.z);
        if (d.pos.y < ground || d.pos.y < eye.y - kFallBelow) {
            // It hit something (not just fell out of the box): a little splash there.
            if (!snow && d.pos.y < ground && ground > kNoGround && m_splashes.size() < kMaxSplashes)
                m_splashes.push_back({glm::vec3(d.pos.x, ground, d.pos.z), 0.0f, d.seed});
            respawn(d, eye, false);
            continue;
        }
        Streak s;
        if (!snow && hail > 0.0f && d.seed < hail * 0.45f) {
            // A hailstone: a short white blur, falling faster than the rain round it.
            d.pos.y -= d.speed * 0.5f * dt;
            s.top = d.pos - vel * (1.0f / 110.0f);
            s.bottom = d.pos;
            s.width = 0.16f + 0.5f * d.seed;
            s.alpha = 2.6f;   // (rain is drawn faint: this makes the ice solid white)
        } else if (snow) {
            s.width = 0.07f + 0.06f * d.seed;
            s.top = d.pos + glm::vec3(0.0f, s.width * 0.5f, 0.0f);
            s.bottom = d.pos - glm::vec3(0.0f, s.width * 0.5f, 0.0f);
            s.alpha = 0.85f;
        } else {
            // A streak as long as the drop moves while a camera's shutter is open (about a
            // fortieth of a second): rain in a photo is blurred lines, not dots. Soft-edged,
            // so a bit wider and each one fainter; big drops are rarer and brighter.
            s.top = d.pos - vel * (1.0f / 40.0f) * (0.7f + 0.6f * d.seed);
            s.bottom = d.pos;
            const float big = d.seed * d.seed;
            s.width = 0.03f + 0.045f * big;
            s.alpha = (0.14f + 0.2f * big) * (0.7f + 0.3f * pour);
        }
        m_streaks.push_back(s);
    }
}
