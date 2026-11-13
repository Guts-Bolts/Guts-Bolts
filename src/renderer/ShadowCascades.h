#pragma once
// Sun shadows in cascades: several shadow maps of different sizes round what the
// camera looks at. A small one gives sharp shadows close up; bigger ones carry shadows
// far into the distance with fewer pixels per stud, where you can't see the difference.
//
// The "base" map is the one Lighting.ShadowDistance asks for (water, fog and liquid
// read it too). The others live as tiles in one shared picture (the shadow atlas):
// sharper ones inside the base, wider ones outside it. Up to 4 maps in all.
//
// Each map's middle is snapped to its own pixel grid, so shadows don't shimmer when the
// camera moves. The two widest tiles take turns being redrawn (one each frame): things
// far away move few pixels a frame, and it keeps the cost at 3 maps a frame, not 4.
//
// No OpenGL here, so the tests can run it. SceneRenderer does the drawing.
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ShadowCascades {

inline constexpr int   kMax = 4;            // maps in all (the base + up to 3 tiles)
inline constexpr float kStep = 2.5f;        // each map is this much wider than the one inside it
inline constexpr float kSmallest = 6.0f;    // no map is smaller than this (half-width, studs)
inline constexpr float kWidest = 400.0f;    // ...or wider than this

struct Plan {
    int   count = 1;                 // how many maps, smallest first
    float extent[kMax] = {0, 0, 0, 0}; // each one's half-width in studs
    int   base = 0;                  // which of them is the base map (the rest are atlas tiles)
};

// How many maps a shadow quality gets: 0 (hard) 1, 1 (soft) 2, 2 (best) 4. Phones stop at 2.
inline int mapsFor(int shadowQuality, bool phone) {
    const int most = shadowQuality <= 0 ? 1 : shadowQuality == 1 ? 2 : kMax;
    return phone ? std::min(most, 2) : most;
}

// The maps for a base half-width (Lighting.ShadowDistance / 2) and how many are allowed.
inline Plan plan(float baseExtent, int most) {
    const float E = std::max(baseExtent, 1.0f);
    most = std::clamp(most, 1, kMax);
    // Sharper maps inside the base: worth it only when they're clearly smaller than it.
    int inner = E >= kSmallest * kStep * kStep ? 2 : E >= kSmallest * 1.25f ? 1 : 0;
    inner = std::min(inner, most - 1);
    // With 4 maps the rest go outside the base, so shadows reach further. With 2 the one
    // extra goes inside if it can (sharpness first), outside if not.
    int outer = most - 1 - inner;
    if (E * kStep > kWidest) outer = 0;
    Plan p;
    p.count = 0;
    for (int i = inner; i >= 1; --i) {
        float e = E / std::pow(kStep, (float)i);
        if (inner == 1) e = std::min(e, 20.0f);        // (close up, 20 studs is plenty)
        p.extent[p.count++] = std::max(e, kSmallest);
    }
    p.base = p.count;
    p.extent[p.count++] = E;
    for (int i = 1; i <= outer; ++i) {
        const float e = E * std::pow(kStep, (float)i);
        if (e > kWidest) break;
        p.extent[p.count++] = e;
    }
    return p;
}

// One map's light-space matrix: looking along the sun at `focus`, `extent` studs each
// way, its middle snapped to whole pixels of a `mapSize` wide map. Also gives the size
// of one pixel in studs and how many studs depth 0..1 covers.
inline glm::mat4 lightSpace(const glm::vec3& sunDir, const glm::vec3& focus, float extent, int mapSize,
                            float baseExtent, glm::mat4& lightView, float& texel, float& depthRange) {
    // (The sun sits at the same place for every map, so they all agree on the view.)
    const float sunDist = baseExtent * 2.0f + 30.0f;
    lightView = glm::lookAt(sunDir * sunDist, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    texel = (2.0f * extent) / (float)std::max(mapSize, 1);
    glm::vec3 c = glm::vec3(lightView * glm::vec4(focus, 1.0f));
    c.x = std::floor(c.x / texel) * texel;
    c.y = std::floor(c.y / texel) * texel;
    const float reach = extent * 2.0f + 30.0f;   // far enough both ways that nothing casting into it is cut off
    depthRange = reach * 2.0f;
    return glm::ortho(c.x - extent, c.x + extent, c.y - extent, c.y + extent, -c.z - reach, -c.z + reach) * lightView;
}

// Where tile 0..3 sits in the 2 x 2 atlas: x, y of its corner and its width, height, in
// 0..1 of the atlas.
inline glm::vec4 tileRect(int tile) {
    return glm::vec4((tile % 2) * 0.5f, (tile / 2) * 0.5f, 0.5f, 0.5f);
}

// Is this map redrawn this frame? `order` is its place in the plan (0 = smallest). The
// base and the first tile always are; with 4 maps the two widest tiles take turns.
// (`never` = it has no picture yet, so it must be.)
inline bool redrawNow(const Plan& p, int order, long frame, bool never) {
    if (never || order == p.base || p.count < kMax) return true;
    int widest[2], n = 0;
    for (int i = p.count - 1; i >= 0 && n < 2; --i)
        if (i != p.base) widest[n++] = i;
    if (n < 2) return true;
    if (order != widest[0] && order != widest[1]) return true;
    return (order == widest[0]) == (frame % 2 == 0);
}

} // namespace ShadowCascades
