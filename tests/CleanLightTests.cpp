// Clean Light (graphics overhaul part 1): the lighting fixes in the shaders.
//
//  - random numbers without sin() (blocky patterns far from the middle of the map)
//  - hardware-filtered sun shadows with noise that changes every frame
//  - lamp shadows that don't leak light under walls far from the lamp
//  - bounce light that doesn't shine through what it bounced off
//  - specular anti-aliasing, AO only on sky/bounce light, Karis bloom, dithering
//
//   CleanLightTests
#include "renderer/Shaders.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
bool has(const char* text, const char* what) { return std::string(text).find(what) != std::string::npos; }

float fract(float x) { return x - std::floor(x); }
// The shaders' hash12 ("Hash without Sine"), in the same 32-bit floats a graphics card uses.
float hash12(float x, float y) {
    float q0 = fract(x * 0.1031f), q1 = fract(y * 0.1031f), q2 = fract(x * 0.1031f);
    const float d = q0 * (q1 + 33.33f) + q1 * (q2 + 33.33f) + q2 * (q0 + 33.33f);
    q0 += d; q1 += d; q2 += d;
    return fract((q0 + q1) * q2);
}
// How evenly a hash fills 0..1 over a 64 x 64 patch of the map around (x0, z0): the
// fullest of 16 equal buckets compared with an even share (1 = perfectly even).
float unevenness(float x0, float z0, float step) {
    int buckets[16] = {};
    for (int i = 0; i < 64; ++i)
        for (int k = 0; k < 64; ++k) {
            const float h = hash12(x0 + i * step, z0 + k * step);
            ++buckets[std::min(15, std::max(0, (int)(h * 16.0f)))];
        }
    int most = 0;
    for (int b : buckets) most = std::max(most, b);
    return most / (64.0f * 64.0f / 16.0f);
}
} // namespace

int main() {
    using namespace Shaders;

    // Random numbers.
    for (const char* s : {common, litFrag, puffFrag, spriteFrag, sunRaysFrag, compositeFrag})
        check(!has(s, "43758.5453"), "no fract(sin(x) * 43758) random numbers left");
    check(unevenness(0.0f, 0.0f, 1.0f) < 1.5f, "the hash is even near the middle of the map");
    check(unevenness(20000.0f, -35000.0f, 0.37f) < 1.5f, "...and still even 35,000 studs out");
    check(has(common, "float ign("), "per-frame noise (interleaved gradient noise) is there for the upscaler to average");

    // Sun shadows.
    check(has(litFrag, "sampler2DShadow uShadowMapCmp") && has(litFrag, "sampler2DShadow uShadowAtlasCmp"),
          "sun shadows are read through the graphics card's shadow filter");
    check(has(litFrag, "uTemporalNoise ? ign(gl_FragCoord.xy, uFrame)"), "soft-shadow noise changes every frame when frames are mixed");
    check(!has(litFrag, "0.00004 + 0.0001"), "the old fixed depth bias is gone");
    check(has(litFrag, "texelWorld * (0.25 + 0.5 * min(tanT, 6.0)) / max(depthRange"), "the bias is sized from each cascade's own texels");

    // Lamp shadows: the old margin was 1.5% of the distance + 0.04 studs; the new one is
    // a fraction of a shadow-map pixel there. At 20 studs on a 256-pixel tile:
    {
        const float d = 20.0f, tile = 256.0f;
        const float oldMargin = d * 0.015f + 0.04f;
        const float newMargin = 0.02f + 2.0f * d / tile * 0.6f;
        check(newMargin < oldMargin * 0.4f, "lamp shadows leak less than half as far under walls");
        check(!has(litFrag, "dd * 0.985 - 0.04"), "the old lamp margin is gone");
    }
    check(!has(litFrag, "dot(-Ld, uLightDir[i].xyz) + 0.3"), "bounce light no longer shines back through its own surface");

    // Shine, AO, bloom, banding.
    check(has(litFrag, "min(2.0 * variance, 0.2)"), "specular anti-aliasing widens highlights on curved and bumpy parts");
    check(has(litFrag, "uAoOn") && has(litFrag, "aoMulti"), "ambient occlusion goes on the sky and bounce light in the lit shader");
    check(has(litFrag, "exp2(-16.0 * rough * rough - 1.0)"), "reflections get specular occlusion");
    check(has(gtaoBlurFrag, "uWithDist"), "the AO keeps each pixel's distance for the next frame");
    check(has(bloomPrefilterFrag, "uTexel") && has(bloomPrefilterFrag, "1.0 / (1.0 + max(a.r"), "bloom's first step uses a Karis average (no flicker from glints)");
    check(has(compositeFrag, "c += n / 255.0;"), "the final picture is dithered (no stripes in gradients)");

    std::cout << (failures ? "FAILED: " : "all passed: ") << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
