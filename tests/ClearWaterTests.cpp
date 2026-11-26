// Clear Water (graphics overhaul part 2): the water fixes in the shaders.
//
//  - sun and sky light reach the water as light (linear), like everything else
//  - water and liquid read the same sharp shadow cascades as the ground
//  - ripples from smooth gradient noise (no square grid), caustics as a net of lines
//  - the glint's roughness from what a pixel can't show (no 2 x 2 blocks)
//  - the water's reflection noise changes every frame for the upscaler to average
//
//   ClearWaterTests
#include "renderer/Shaders.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
bool has(const char* text, const char* what) { return std::string(text).find(what) != std::string::npos; }
} // namespace

int main() {
    using namespace Shaders;

    // Light: the renderer hands the water linear light, so the shader mustn't lin() it again.
    check(!has(waterFrag, "lin(uSunColor)") && !has(fluidShadeFrag, "lin(uSunColor)"),
          "the water's sun colour isn't converted twice");

    // Shadows.
    for (const char* s : {waterFrag, fluidShadeFrag}) {
        check(has(s, "#pragma gb_watersun"), "water and liquid share the cascaded sun shadows");
        check(!has(s, "s.z - 0.0002"), "the old fixed shadow bias on water is gone");
    }
    check(has(waterSun, "uCascadeSpace[WS_MAX]") && has(waterSun, "sampler2DShadow uShadowAtlasCmp"),
          "the water's shadows read every cascade through the shadow filter");
    check(has(waterSun, "uTemporalNoise ? uFrame : 0.0"), "the water's noise only moves when frames are mixed");

    // Ripples and caustics.
    check(has(waterFrag, "vec3 gnoised(vec2 p)") && !has(waterFrag, "vnoise(a + e.xy * 1.3)"),
          "ripples use gradient noise with exact slopes, not value noise read three times");
    check(has(waterFrag, "float causticLayer(vec2 x, float t)") && !has(waterFrag, "abs(sin(q.x) * sin(q.y))"),
          "caustics are a net of cell edges, not a grid of sin x sin");

    // Glint and reflections.
    check(!has(waterFrag, "length(fwidth(N))"), "the glint no longer jumps between 2 x 2 pixel blocks");
    check(has(waterFrag, "float foot = length(fwidth(wp));"), "the glint widens with the pixel's size on the water");
    check(has(waterFrag, "float stepLen = 0.2 + 0.25 * wsNoise();"), "the water's reflection steps start at per-frame noise");

    std::cout << (failures ? "FAILED: " : "all passed: ") << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
