#pragma once
// Ground-truth ambient occlusion (GTAO): how much of the sky's soft light reaches each
// pixel, worked out from the depth picture. It replaces the older SSAO, which threw
// random test points round each pixel and counted the ones behind something.
//
// GTAO looks at it the way light really works. It cuts a few thin slices through the
// half-ball above the surface. In each slice it walks outwards both ways and finds the
// "horizon": the steepest thing in the way. The open angle between the two horizons is
// how much sky that slice sees, and a little maths (integrate() below) turns the angle
// into the right amount of light, counting light that arrives straight on more than
// light that grazes. The result is cleaner corners and creases with no grey haze on
// flat walls, for the same number of depth reads.
//
// The drawing is in Shaders.h (gtaoFrag, gtaoBlurFrag). This file has the settings for
// each quality level and the same maths in C++, so the tests can check it.
#include <algorithm>
#include <cmath>

namespace Gtao {

struct Level {
    int   slices = 0;      // directions looked in (0 = don't draw it at all)
    int   steps = 0;       // depth reads each way along a slice
    float radius = 0.0f;   // how far it looks, in studs
    float strength = 0.0f; // 1 = full; less when bounce light already darkens corners
};

// The settings for a lighting level (GraphicsSettings::giQuality: -1 none, 0 corners
// only, 1..4 traced bounce light) and whether traced bounce light was really drawn this
// frame. Bounce light already darkens big corners, so with it GTAO only adds the small
// close-up creases, at half strength, and Medium skips it to save time.
inline Level levelFor(int giQuality, bool bounceDrawn) {
    if (giQuality < 0) return {};
    if (!bounceDrawn) {
        if (giQuality >= 4) return {4, 10, 1.6f, 1.0f};
        if (giQuality >= 3) return {3, 6, 1.6f, 1.0f};
        return {2, giQuality >= 2 ? 6 : 4, 1.6f, 1.0f};
    }
    if (giQuality <= 1) return {};
    if (giQuality >= 4) return {4, 10, 1.0f, 0.5f};
    if (giQuality == 3) return {3, 6, 1.0f, 0.5f};
    return {2, 6, 1.0f, 0.5f};
}

// How much light one slice lets in. `n` is the angle between the surface's way out and
// the way to the camera, inside the slice. `h0` (0 or less) and `h1` (0 or more) are
// the two horizon angles, measured from the way to the camera. All in radians.
// Facing the camera (n = 0): wide open (-90 and +90 degrees) gives 1, half open gives
// a half, shut gives 0.
inline float integrate(float n, float h0, float h1) {
    const float kHalfPi = 1.5707963f;
    h0 = n + std::max(h0 - n, -kHalfPi);   // (a horizon can't be below the surface itself)
    h1 = n + std::min(h1 - n, kHalfPi);
    const float cosN = std::cos(n), sinN = std::sin(n);
    const float a0 = (cosN + 2.0f * h0 * sinN - std::cos(2.0f * h0 - n)) * 0.25f;
    const float a1 = (cosN + 2.0f * h1 * sinN - std::cos(2.0f * h1 - n)) * 0.25f;
    return a0 + a1;
}

} // namespace Gtao
