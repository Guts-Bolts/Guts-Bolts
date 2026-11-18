#pragma once
// TerrainContact: how far a box sinks into the terrain.
//
// Studio's "Collisions" switch (MODEL tab) stops a part you drag when it runs into
// another part. The terrain is solid ground too, so the same check asks this how deep
// a box has gone under the ground (0 = it's above it, or just resting on it).
//
// It looks two ways, so both cases are caught:
//   - points spread over the box's six sides: any that are under the ground, and
//   - the terrain's own grid points (hill tops): any that poke up inside the box.
#include "Physics.h"
#include "Terrain.h"

#include <algorithm>
#include <cmath>

namespace TerrainContact {

// The lowest point of the box straight above / below (x, z), found by cutting a
// vertical line through it. False when the line misses the box.
inline bool boxBottomAt(const OBB& b, float x, float z, float& bottom) {
    const glm::vec3 from(x, 0.0f, z), up(0.0f, 1.0f, 0.0f);
    float t0 = -1e30f, t1 = 1e30f;
    for (int a = 0; a < 3; ++a) {
        const float along = glm::dot(b.axis[a], up);
        const float at = glm::dot(b.axis[a], from - b.center);
        if (std::abs(along) < 1e-6f) {
            if (std::abs(at) > b.half[a]) return false;
            continue;
        }
        float ta = (-b.half[a] - at) / along, tb = (b.half[a] - at) / along;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    bottom = t0;
    return true;
}

// How many studs the box is under the ground at its deepest spot (0 = not at all).
inline float sinkDepth(const Terrain& terrain, const OBB& b) {
    if (terrain.empty()) return 0.0f;
    float deepest = 0.0f;
    const float cell = terrain.cellSize();

    // The box's own outline on x / z, to skip boxes nowhere near the terrain.
    glm::vec3 reach(0.0f);
    for (int a = 0; a < 3; ++a) reach += glm::abs(b.axis[a]) * b.half[a];
    const glm::vec3 lo = b.center - reach, hi = b.center + reach;
    const float hw = terrain.halfWidth();
    if (hi.x < -hw || lo.x > hw || hi.z < -hw || lo.z > hw) return 0.0f;

    // 1. Points on each of the six sides, about half a grid square apart.
    for (int f = 0; f < 3; ++f) {
        const int u = (f + 1) % 3, v = (f + 2) % 3;
        const int nu = std::clamp((int)std::ceil(b.half[u] * 2.0f / (cell * 0.5f)) + 1, 2, 17);
        const int nv = std::clamp((int)std::ceil(b.half[v] * 2.0f / (cell * 0.5f)) + 1, 2, 17);
        for (int side = -1; side <= 1; side += 2)
            for (int i = 0; i < nu; ++i)
                for (int j = 0; j < nv; ++j) {
                    const float su = -1.0f + 2.0f * i / (nu - 1), sv = -1.0f + 2.0f * j / (nv - 1);
                    const glm::vec3 p = b.center + b.axis[f] * (b.half[f] * side) +
                                        b.axis[u] * (b.half[u] * su) + b.axis[v] * (b.half[v] * sv);
                    float g;
                    if (terrain.heightAt(p.x, p.z, g) && g - p.y > deepest) deepest = g - p.y;
                }
    }

    // 2. Hill tops (grid points) inside the box: how far it would have to go up to clear them.
    const int n = terrain.cells();
    const int i0 = std::max(0, (int)std::ceil((lo.x + hw) / cell)), i1 = std::min(n, (int)std::floor((hi.x + hw) / cell));
    const int k0 = std::max(0, (int)std::ceil((lo.z + hw) / cell)), k1 = std::min(n, (int)std::floor((hi.z + hw) / cell));
    for (int k = k0; k <= k1; ++k)
        for (int i = i0; i <= i1; ++i) {
            const glm::vec3 p = terrain.pointPos(i, k);
            float bottom;
            if (p.y > lo.y && boxBottomAt(b, p.x, p.z, bottom) && p.y - bottom > deepest) deepest = p.y - bottom;
        }
    return deepest;
}

} // namespace TerrainContact
