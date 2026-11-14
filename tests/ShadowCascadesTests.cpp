// Sun shadow cascades (src/renderer/ShadowCascades.h): how many maps, how wide each is,
// where they sit in the atlas, pixel snapping and which ones are redrawn each frame.
//
//   ShadowCascadesTests
#include "renderer/ShadowCascades.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
} // namespace

int main() {
    using namespace ShadowCascades;

    // How many maps each quality gets.
    check(mapsFor(0, false) == 1 && mapsFor(1, false) == 2 && mapsFor(2, false) == 4, "hard 1, soft 2, best 4 maps");
    check(mapsFor(2, true) == 2 && mapsFor(0, true) == 1, "phones stop at 2 maps");

    // One map allowed: just the base, exactly as asked.
    {
        const Plan p = plan(22.5f, 1);
        check(p.count == 1 && p.base == 0 && near(p.extent[0], 22.5f), "one map is the base alone");
    }
    // The default ShadowDistance (45 -> 22.5 each way) with 4 maps: 1 sharper, 2 wider.
    {
        const Plan p = plan(22.5f, 4);
        check(p.count == 4 && p.base == 1, "default distance: 4 maps, the base is second");
        check(near(p.extent[0], 9.0f) && near(p.extent[1], 22.5f) && near(p.extent[2], 56.25f) && near(p.extent[3], 140.625f),
              "default distance: 9, 22.5, 56.25, 140.6 studs");
    }
    // Two maps: the extra one goes inside, for sharpness.
    {
        const Plan p = plan(22.5f, 2);
        check(p.count == 2 && p.base == 1 && p.extent[0] < p.extent[1], "two maps: one sharper map inside the base");
    }
    // A tiny base has no room for a sharper map, so the extras all go outside.
    {
        const Plan p = plan(5.0f, 4);
        check(p.base == 0 && p.count == 4 && near(p.extent[1], 12.5f), "tiny base: extras go outside");
    }
    // A big base gets two sharper maps and one wider.
    {
        const Plan p = plan(100.0f, 4);
        check(p.count == 4 && p.base == 2 && near(p.extent[0], 16.0f) && near(p.extent[1], 40.0f) && near(p.extent[3], 250.0f),
              "big base: 16, 40, 100, 250 studs");
    }
    // Nothing is ever wider than the cap, and the list always grows.
    for (float e : {1.0f, 6.0f, 7.5f, 20.0f, 37.5f, 80.0f, 200.0f, 390.0f, 2000.0f})
        for (int most = 1; most <= 6; ++most) {
            const Plan p = plan(e, most);
            bool ok = p.count >= 1 && p.count <= kMax && p.base >= 0 && p.base < p.count && near(p.extent[p.base], e);
            for (int i = 0; i < p.count; ++i) {
                if (i < p.base && p.extent[i] < kSmallest) ok = false;   // sharper maps never get silly small
                if (i > p.base && p.extent[i] > kWidest) ok = false;     // wider maps never go past the cap
                if (i > 0 && p.extent[i] <= p.extent[i - 1]) ok = false;
            }
            if (!ok) check(false, "plan(" + std::to_string(e) + ", " + std::to_string(most) + ") is sound");
        }
    check(true, "every plan is sorted, in range and keeps the base as asked");

    // Atlas tiles: four quarters that don't overlap.
    {
        bool ok = true;
        for (int a = 0; a < 4; ++a) {
            const glm::vec4 r = tileRect(a);
            if (r.x < 0 || r.y < 0 || r.x + r.z > 1.0f || r.y + r.w > 1.0f) ok = false;
            for (int b = a + 1; b < 4; ++b) {
                const glm::vec4 q = tileRect(b);
                if (near(r.x, q.x) && near(r.y, q.y)) ok = false;
            }
        }
        check(ok, "four atlas tiles, each its own quarter");
    }

    // Pixel snapping: moving the camera a little either keeps the map still or moves it
    // by whole pixels, so shadow edges never crawl.
    {
        const glm::vec3 sun = glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f));
        glm::mat4 view;
        float texel = 0, range = 0;
        const glm::mat4 a = lightSpace(sun, glm::vec3(3.0f, 0, 5.0f), 56.25f, 2048, 22.5f, view, texel, range);
        check(near(texel, 112.5f / 2048.0f, 1e-6f), "one pixel is the map width over its size");
        check(range > 2.0f * 56.25f, "depth covers the whole map and more");
        bool ok = true;
        for (int i = 1; i <= 40; ++i) {
            float t2 = 0, r2 = 0;
            glm::mat4 v2;
            const glm::mat4 b = lightSpace(sun, glm::vec3(3.0f + i * 0.0137f, 0, 5.0f + i * 0.0071f), 56.25f, 2048, 22.5f, v2, t2, r2);
            // Where the world's middle lands, in pixels: the change must be whole pixels.
            const glm::vec4 pa = a * glm::vec4(0, 0, 0, 1), pb = b * glm::vec4(0, 0, 0, 1);
            const float dx = (pb.x - pa.x) * 0.5f * 2048.0f, dy = (pb.y - pa.y) * 0.5f * 2048.0f;
            if (!near(dx, std::round(dx), 0.02f) || !near(dy, std::round(dy), 0.02f)) ok = false;
        }
        check(ok, "the map only ever moves by whole pixels");
        // The focus is inside the map.
        const glm::vec4 f = a * glm::vec4(3.0f, 0, 5.0f, 1);
        check(std::fabs(f.x) < 0.01f && std::fabs(f.y) < 0.01f && std::fabs(f.z) < 1.0f, "the map is centred on what the camera looks at");
    }

    // Taking turns: with 4 maps the two widest tiles alternate, the rest are drawn every frame.
    {
        const Plan p = plan(22.5f, 4);   // base = 1, widest tiles = 3 and 2
        bool always = true, turns = true;
        for (long f = 0; f < 8; ++f) {
            if (!redrawNow(p, 0, f, false) || !redrawNow(p, p.base, f, false)) always = false;
            if (redrawNow(p, 2, f, false) == redrawNow(p, 3, f, false)) turns = false;
            if (redrawNow(p, 3, f, false) == redrawNow(p, 3, f + 1, false)) turns = false;
        }
        check(always, "the sharpest map and the base are redrawn every frame");
        check(turns, "the two widest maps take turns");
        check(redrawNow(p, 2, 0, true) && redrawNow(p, 2, 1, true) && redrawNow(p, 3, 0, true) && redrawNow(p, 3, 1, true),
              "a map with no picture yet is always drawn");
        const Plan two = plan(22.5f, 2);
        check(redrawNow(two, 0, 0, false) && redrawNow(two, 0, 1, false), "with fewer than 4 maps nothing waits its turn");
        // A big base: the base itself never waits, even though it is one of the widest.
        const Plan big = plan(100.0f, 4);
        check(redrawNow(big, big.base, 0, false) && redrawNow(big, big.base, 1, false), "the base never waits its turn");
    }

    std::cout << (failures ? "\nFAILED\n" : "\nall passed\n");
    return failures ? 1 : 0;
}
