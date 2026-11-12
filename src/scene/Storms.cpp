#include "Storms.h"
#include "Environment.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Terrain.h"

#include <algorithm>
#include <cmath>

namespace Storms {
namespace {

float rnd(State& st) {
    st.rng ^= st.rng << 13;
    st.rng ^= st.rng >> 17;
    st.rng ^= st.rng << 5;
    return (float)(st.rng & 0xFFFFFF) / (float)0x1000000;
}

float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool stormy(const Environment& e) { return e.weather == (int)Weather::Storm || e.weather == (int)Weather::Supercell; }

// Where the players are (the middle of their characters), or the middle of the map.
glm::vec3 focus(Scene& scene) {
    glm::vec3 sum(0.0f);
    int n = 0;
    scene.forEach([&](SceneNode* node) {
        if (node->name != "HumanoidRootPart") return;
        sum += glm::vec3(node->worldMatrix()[3]);
        ++n;
    });
    return n ? sum / (float)n : glm::vec3(0.0f);
}

// The ground under a spot: the terrain if there is some, else the given height.
float groundAt(const Scene& scene, const glm::vec3& p) {
    float h = 0.0f;
    if (!scene.terrain().empty() && scene.terrain().heightAt(p.x, p.z, h)) return h;
    return p.y;
}

glm::vec3 rounded(const glm::vec3& v, float step) { return glm::round(v / step) * step; }

} // namespace

glm::vec3 wallCloud(const Scene& scene) {
    const Environment& e = scene.environment();
    if (e.tornado > 0.0f) return e.tornadoPos;
    // Behind and to one side of the storm's middle, as seen along the way it moves
    // (real wall clouds sit at the back of the storm, next to the rain).
    const State& st = scene.storms();
    const glm::vec3 h = st.cellHeading;
    return e.stormCenter + (-h * 0.7f + glm::vec3(-h.z, 0.0f, h.x) * 0.7f) * kWallCloudOffset;
}

void spawnTornado(Scene& scene, const glm::vec3& foot, float strength, float seconds) {
    State& st = scene.storms();
    st.tornadoPeak = std::clamp(strength, 0.1f, kMaxTornado);
    st.tornadoLife = std::clamp(seconds, 12.0f, 600.0f);
    st.tornadoAge = 0.0f;
    st.tornadoAt = glm::vec3(foot.x, groundAt(scene, foot), foot.z);
    Environment& e = scene.environment();
    e.tornadoPos = rounded(st.tornadoAt, 0.5f);
    e.tornado = 0.05f;   // (so every player sees it start straight away)
    st.publish = 0.0f;
}

void spawnDownburst(Scene& scene, const glm::vec3& ground, float strength, float seconds) {
    State& st = scene.storms();
    st.downburstPeak = std::clamp(strength, 0.1f, 1.0f);
    st.downburstLife = std::clamp(seconds, 6.0f, 120.0f);
    st.downburstAge = 0.0f;
    Environment& e = scene.environment();
    e.downburstPos = rounded(glm::vec3(ground.x, groundAt(scene, ground), ground.z), 0.5f);
    e.downburst = 0.05f;
    st.publish = 0.0f;
}

void clear(Scene& scene) {
    State& st = scene.storms();
    st.tornadoLife = st.downburstLife = 0.0f;
    st.tornadoPeak = st.downburstPeak = 0.0f;
    Environment& e = scene.environment();
    e.tornado = e.downburst = 0.0f;
    e.tornadoVel = glm::vec3(0.0f);
}

void simulate(Scene& scene, float dt) {
    State& st = scene.storms();
    Environment& e = scene.environment();
    dt = std::clamp(dt, 0.0f, 0.1f);
    st.clock += dt;
    const bool supercell = e.weather == (int)Weather::Supercell;
    const float amount = std::clamp(e.weatherAmount, 0.0f, 1.0f);

    // The supercell: placed upwind of the players the first time, then it drifts along.
    if (supercell) {
        if (!st.cellPlaced) {
            const float a = rnd(st) * 6.2831853f;
            st.cellHeading = glm::vec3(std::cos(a), 0.0f, std::sin(a));
            if (glm::length(e.stormCenter) < 1e-3f) {   // (a game can put it somewhere itself first)
                const glm::vec3 f = focus(scene);
                e.stormCenter = rounded(glm::vec3(f.x, 0.0f, f.z) - st.cellHeading * 260.0f, 1.0f);
            }
            st.cellPlaced = true;
            st.nextTornado = 18.0f + 30.0f * rnd(st);
        }
    } else {
        st.cellPlaced = false;
        st.nextTornado = -1.0f;
    }
    if (stormy(e)) {
        if (st.nextDownburst < 0.0f) st.nextDownburst = 25.0f + 45.0f * rnd(st);
    } else {
        st.nextDownburst = -1.0f;
    }

    // Things forming by themselves.
    if (e.severeWeather) {
        if (supercell && st.nextTornado >= 0.0f && st.tornadoLife <= 0.0f) {
            st.nextTornado -= dt * (0.5f + amount);
            if (st.nextTornado <= 0.0f) {
                // Most tornadoes are weak; the monsters are rare.
                const float roll = rnd(st);
                const float strength = roll < 0.45f ? 1.0f : roll < 0.75f ? 2.0f : roll < 0.9f ? 3.0f : roll < 0.97f ? 4.0f : 5.0f;
                glm::vec3 at = wallCloud(scene);
                at.y = focus(scene).y - 3.0f;
                spawnTornado(scene, at, strength, 50.0f + 60.0f * rnd(st));
                st.nextTornado = 40.0f + 80.0f * rnd(st);
            }
        }
        if (st.nextDownburst >= 0.0f && st.downburstLife <= 0.0f) {
            st.nextDownburst -= dt * (0.5f + amount);
            if (st.nextDownburst <= 0.0f) {
                const glm::vec3 f = focus(scene);
                const float a = rnd(st) * 6.2831853f, d = 30.0f + 110.0f * rnd(st);
                spawnDownburst(scene, glm::vec3(f.x + std::cos(a) * d, f.y - 3.0f, f.z + std::sin(a) * d), 0.6f + 0.4f * rnd(st));
                st.nextDownburst = 35.0f + 60.0f * rnd(st);
            }
        }
    }

    // The tornado's life: it grows down from the cloud, wanders along with the storm,
    // then thins out like a rope and is gone.
    float tornadoNow = 0.0f;
    glm::vec3 tornadoVel(0.0f);
    if (st.tornadoLife > 0.0f) {
        st.tornadoAge += dt;
        if (st.tornadoAge >= st.tornadoLife) {
            st.tornadoLife = 0.0f;
        } else {
            const float grow = smooth(0.0f, 8.0f, st.tornadoAge), fade = 1.0f - smooth(st.tornadoLife - 10.0f, st.tornadoLife, st.tornadoAge);
            tornadoNow = st.tornadoPeak * grow * fade;
            // Its path: with the storm, swinging from side to side.
            const glm::vec3 h = st.cellHeading, side(-h.z, 0.0f, h.x);
            tornadoVel = h * 7.0f + side * std::sin(st.tornadoAge * 0.21f + st.tornadoPeak) * 6.0f +
                         h * std::sin(st.tornadoAge * 0.13f) * 3.0f;
            st.tornadoAt += tornadoVel * dt;
            st.tornadoAt.y = groundAt(scene, st.tornadoAt);
        }
    }
    float downburstNow = 0.0f;
    if (st.downburstLife > 0.0f) {
        st.downburstAge += dt;
        if (st.downburstAge >= st.downburstLife) st.downburstLife = 0.0f;
        else downburstNow = st.downburstPeak * smooth(0.0f, 3.0f, st.downburstAge) *
                            (1.0f - smooth(st.downburstLife - 6.0f, st.downburstLife, st.downburstAge));
    }

    // Tell everyone, a few times a second (rounded, so the Lighting isn't sent every frame).
    st.publish -= dt;
    if (st.publish <= 0.0f) {
        st.publish = 0.2f;
        const float t = std::round(tornadoNow * 20.0f) / 20.0f;
        if (t > 0.0f || e.tornado > 0.0f) {
            e.tornado = st.tornadoLife > 0.0f ? std::max(t, 0.05f) : 0.0f;
            if (e.tornado > 0.0f) { e.tornadoPos = rounded(st.tornadoAt, 0.5f); e.tornadoVel = rounded(tornadoVel, 0.5f); }
            else e.tornadoVel = glm::vec3(0.0f);
        }
        const float d = std::round(downburstNow * 20.0f) / 20.0f;
        if (d > 0.0f || e.downburst > 0.0f) e.downburst = st.downburstLife > 0.0f ? std::max(d, 0.05f) : 0.0f;
        if (supercell) e.stormCenter = rounded(e.stormCenter + st.cellHeading * 5.0f * 0.2f, 0.25f);
    }
}

void follow(Scene& scene, float dt) {
    State& st = scene.storms();
    const Environment& e = scene.environment();
    dt = std::clamp(dt, 0.0f, 0.1f);
    st.fresh = true;
    const float k = 1.0f - std::exp(-dt * 6.0f);
    // The tornado: carry on along its path between updates, and close the gap smoothly.
    if (e.tornado > 0.0f) {
        if (st.tornado <= 0.001f) st.tornadoPos = e.tornadoPos;   // (a new one: no sliding in from the last one's place)
        st.tornadoPos += e.tornadoVel * dt;
        const glm::vec3 gap = e.tornadoPos - st.tornadoPos;
        if (glm::length(gap) > 40.0f) st.tornadoPos = e.tornadoPos;
        else st.tornadoPos += gap * (1.0f - std::exp(-dt * 1.5f));
    }
    st.tornado += (e.tornado - st.tornado) * k;
    if (e.tornado <= 0.0f && st.tornado < 0.02f) st.tornado = 0.0f;
    if (e.downburst > 0.0f) st.downburstPos = e.downburstPos;
    st.downburst += (e.downburst - st.downburst) * k;
    if (e.downburst <= 0.0f && st.downburst < 0.02f) st.downburst = 0.0f;
}

void snap(Scene& scene) {
    State& st = scene.storms();
    const Environment& e = scene.environment();
    st.tornado = e.tornado;
    st.tornadoPos = e.tornadoPos;
    st.downburst = e.downburst;
    st.downburstPos = e.downburstPos;
}

bool active(const Scene& scene) {
    const State& st = scene.storms();
    return st.tornado > 0.0f || st.downburst > 0.0f;
}

glm::vec3 tornadoWind(const State& st, const glm::vec3& p) {
    if (st.tornado <= 0.0f) return glm::vec3(0.0f);
    const glm::vec3 d(p.x - st.tornadoPos.x, 0.0f, p.z - st.tornadoPos.z);
    const float up = p.y - st.tornadoPos.y;
    if (up < -6.0f || up > kFunnelHeight) return glm::vec3(0.0f);
    const float r = glm::length(d);
    // The funnel is wider higher up (it hangs from the cloud like a cone).
    const float core = coreRadius(st.tornado) * (1.0f + 1.5f * std::clamp(up / kFunnelHeight, 0.0f, 1.0f));
    const float reach = core * 14.0f;
    if (r > reach) return glm::vec3(0.0f);
    const float peak = peakWind(st.tornado);
    // A Rankine vortex: like a wheel inside the core, dropping off with distance outside.
    const float turn = r < core ? peak * r / core : peak * core / r;
    const float edge = 1.0f - smooth(reach * 0.6f, reach, r);
    glm::vec3 wind(0.0f);
    if (r > 1e-3f) {
        const glm::vec3 out = d / r;
        wind += glm::vec3(-out.z, 0.0f, out.x) * turn;               // round and round (anticlockwise from above)
        // Sucked in along the ground, most of all just outside the core.
        const float low = 1.0f - smooth(10.0f, 60.0f, up);
        wind -= out * turn * 0.55f * low * smooth(0.0f, core, r);
    }
    // Up the middle: fastest in and around the core.
    wind.y += peak * 0.75f * std::exp(-(r * r) / (core * core * 5.0f));
    return wind * edge;
}

glm::vec3 downburstWind(const State& st, const glm::vec3& p) {
    if (st.downburst <= 0.0f) return glm::vec3(0.0f);
    const glm::vec3 d(p.x - st.downburstPos.x, 0.0f, p.z - st.downburstPos.z);
    const float up = p.y - st.downburstPos.y;
    if (up < -6.0f || up > kCloudBase) return glm::vec3(0.0f);
    const float r = glm::length(d), R = kDownburstRadius;
    if (r > R * 7.0f) return glm::vec3(0.0f);
    const float speed = 75.0f * st.downburst;
    glm::vec3 wind(0.0f);
    // The column of air coming down, turning sideways as it nears the ground.
    const float inShaft = 1.0f - smooth(R * 0.6f, R * 1.2f, r);
    wind.y -= speed * inShaft * smooth(4.0f, 40.0f, up);
    // The burst along the ground: nothing right under the middle, strongest in a ring
    // at the edge of the column, fading out over a few hundred studs.
    if (r > 1e-3f) {
        const float ring = r < R ? r / R : std::exp(-(r - R) / (R * 2.2f));
        const float low = 1.0f - smooth(20.0f, 70.0f, up);
        wind += d / r * speed * ring * low;
    }
    return wind;
}

glm::vec3 windAt(const Scene& scene, const glm::vec3& p) {
    const State& st = scene.storms();
    return scene.world().globalWind + tornadoWind(st, p) + downburstWind(st, p);
}

float airBoost(const Scene& scene, const glm::vec3& p) {
    const State& st = scene.storms();
    float boost = 1.0f;
    if (st.tornado > 0.0f) {
        const float r = glm::length(glm::vec2(p.x - st.tornadoPos.x, p.z - st.tornadoPos.z));
        const float core = coreRadius(st.tornado);
        boost += (25.0f + 15.0f * st.tornado) * (1.0f - smooth(core * 2.0f, core * 9.0f, r));
    }
    if (st.downburst > 0.0f) {
        const float r = glm::length(glm::vec2(p.x - st.downburstPos.x, p.z - st.downburstPos.z));
        boost += 12.0f * st.downburst * (1.0f - smooth(kDownburstRadius * 2.0f, kDownburstRadius * 6.0f, r));
    }
    return boost;
}

glm::vec3 characterPush(const Scene& scene, const glm::vec3& p, const glm::vec3& velocity) {
    const State& st = scene.storms();
    const glm::vec3 wind = tornadoWind(st, p) + downburstWind(st, p);
    const float speed = glm::length(wind);
    if (speed < 12.0f) return glm::vec3(0.0f);   // (a breeze doesn't move a person)
    // Like the air on parts: the push grows with the square of how much faster the
    // wind is than you. About 60 studs/s shoves you along; over 125 lifts you.
    const glm::vec3 rel = wind - velocity;
    const float rs = glm::length(rel);
    if (rs < 1e-3f) return glm::vec3(0.0f);
    const float push = std::min(0.0062f * rs * rs, 260.0f);
    return rel / rs * push;
}

} // namespace Storms
