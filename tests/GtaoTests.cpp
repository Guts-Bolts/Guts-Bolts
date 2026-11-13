// Ground-truth ambient occlusion (src/renderer/Gtao.h): what each lighting level gets,
// and the maths that turns two horizon angles into an amount of light.
//
//   GtaoTests
#include "renderer/Gtao.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
const float kHalfPi = 1.5707963f;
} // namespace

int main() {
    using namespace Gtao;

    // Levels.
    check(levelFor(-1, false).slices == 0 && levelFor(-1, true).slices == 0, "lighting off: no ambient occlusion");
    {
        const Level low = levelFor(0, false);
        check(low.slices == 2 && low.steps == 4 && near(low.strength, 1.0f), "Low: 2 slices of 4 steps, full strength");
        check(low.slices * low.steps * 2 <= 16, "Low reads the depth no more often than the old SSAO did");
    }
    check(levelFor(1, true).slices == 0, "Medium with bounce light skips it");
    check(levelFor(1, false).slices > 0, "Medium without bounce light (it couldn't be drawn) still gets it");
    {
        bool ok = true;
        for (int gi = 2; gi <= 4; ++gi) {
            const Level with = levelFor(gi, true), without = levelFor(gi, false);
            if (with.slices <= 0 || !near(with.strength, 0.5f) || !near(without.strength, 1.0f)) ok = false;
            if (with.radius >= without.radius) ok = false;
            if (gi > 2 && levelFor(gi, true).slices * levelFor(gi, true).steps <= levelFor(gi - 1, true).slices * levelFor(gi - 1, true).steps) ok = false;
        }
        check(ok, "High and up: half strength and a smaller radius beside bounce light, more reads each level");
    }

    // The slice maths, facing the camera.
    check(near(integrate(0.0f, -kHalfPi, kHalfPi), 1.0f), "wide open lets all the light in");
    check(near(integrate(0.0f, 0.0f, 0.0f), 0.0f), "shut lets none in");
    check(near(integrate(0.0f, -kHalfPi, 0.0f), 0.5f) && near(integrate(0.0f, 0.0f, kHalfPi), 0.5f), "half open lets half in");
    // Light that arrives straight on counts more than light that grazes: closing the
    // lowest 15 degrees (a twelfth of the angle) costs well under a twelfth of the light.
    {
        const float lost = 1.0f - integrate(0.0f, -kHalfPi, kHalfPi - 0.2618f);
        check(lost > 0.0f && lost < 0.05f, "a low wall far away barely darkens");
    }
    // Opening up never makes it darker.
    {
        bool ok = true;
        for (float n = -1.2f; n <= 1.2f; n += 0.3f) {
            float last = -1.0f;
            for (float h = 0.0f; h <= kHalfPi + 1e-3f; h += 0.05f) {
                const float v = integrate(n, std::min(-h, 0.0f), h);
                if (v < last - 1e-4f || v < -1e-4f) ok = false;
                last = v;
            }
        }
        check(ok, "more open is never darker, at any tilt");
    }
    // A horizon can't be below the surface: angles past it are cut off.
    check(near(integrate(0.3f, -3.0f, 3.0f), integrate(0.3f, 0.3f - kHalfPi, 0.3f + kHalfPi)), "horizons below the surface are cut off");

    std::cout << (failures ? "\nFAILED\n" : "\nall passed\n");
    return failures ? 1 : 0;
}
