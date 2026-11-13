#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Volumetric fog (SceneRendererFog.cpp): the parts that are plain sums, so the tests
// can check them without a graphics card.
//
// How the fog works, in short. Instead of fading things by how far away they are (the
// simple fog, still used on Low / Medium and on phones), a ray is walked from the camera
// to each thing on screen, a few steps at a time. At every step it asks: how thick is
// the fog here (thick down low, thin up high, with drifting wisps), and how much light
// reaches this spot (is it in the sun or in a shadow, is a lamp near)? Fog in the light
// glows, fog in shadow stays dark, and that is what makes light shafts and glowing
// lamps. It is worked out on a picture a quarter of the size and reused from frame to
// frame, so it stays cheap.
namespace FogVolume {

constexpr int kNoiseSize = 32;      // the wisp pattern: a 32 x 32 x 32 block that repeats
constexpr int kMaxLights = 8;       // the nearest lamps that glow in the fog
constexpr float kMaxDistance = 260.0f;   // rays are walked this far; past it the fog is the simple sum

// The steps a ray takes at each level (GraphicsSettings::volumetricFog): High, Epic, Cinematic.
inline int stepsFor(int level) {
    static const int kSteps[4] = {0, 20, 32, 56};
    return kSteps[std::clamp(level, 0, 3)];
}

// How thick the fog is at a height: `density` at `fogHeight`, thinning out above it
// (falloff 0 = the same everywhere). `noise` is the wisp pattern there, 0..1 (0.5 on
// average), and `wisps` how much it matters (0 = smooth fog).
inline float densityAt(float density, float fogHeight, float falloff, float y, float noise, float wisps) {
    float d = density;
    if (falloff > 0.0f) d *= std::exp(std::clamp(-falloff * (y - fogHeight), -30.0f, 8.0f));
    if (wisps > 0.0f) d *= std::max(0.0f, 1.0f + wisps * 2.4f * (noise - 0.5f));
    return d;
}

// The wisp pattern: soft blobs at two sizes, repeating in every direction (so it can
// scroll with the wind for ever), averaging 0.5.
inline std::vector<uint8_t> noise() {
    const int n = kNoiseSize;
    auto hash = [](int x, int y, int z, int period, uint32_t seed) {
        x = ((x % period) + period) % period;
        y = ((y % period) + period) % period;
        z = ((z % period) + period) % period;
        uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u + seed * 974711u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (float)(h & 0xFFFFu) / 65535.0f;
    };
    auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
    auto value = [&](float x, float y, float z, int period, uint32_t seed) {
        const int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
        const float fx = smooth(x - (float)xi), fy = smooth(y - (float)yi), fz = smooth(z - (float)zi);
        float c[2][2];
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
                c[j][k] = hash(xi, yi + j, zi + k, period, seed) * (1.0f - fx) + hash(xi + 1, yi + j, zi + k, period, seed) * fx;
        const float a = c[0][0] * (1.0f - fy) + c[1][0] * fy, b = c[0][1] * (1.0f - fy) + c[1][1] * fy;
        return a * (1.0f - fz) + b * fz;
    };
    std::vector<float> f((size_t)n * n * n);
    double sum = 0.0;
    for (int z = 0; z < n; ++z)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const float u = ((float)x + 0.5f) / (float)n, v = ((float)y + 0.5f) / (float)n, w = ((float)z + 0.5f) / (float)n;
                const float val = 0.6f * value(u * 4.0f, v * 4.0f, w * 4.0f, 4, 1u) + 0.4f * value(u * 8.0f, v * 8.0f, w * 8.0f, 8, 2u);
                f[((size_t)z * n + y) * n + x] = val;
                sum += val;
            }
    const float mean = (float)(sum / (double)f.size());
    std::vector<uint8_t> out(f.size());
    for (size_t i = 0; i < f.size(); ++i)   // (stretched a little, and moved so the average is 0.5)
        out[i] = (uint8_t)std::clamp((0.5f + (f[i] - mean) * 1.8f) * 255.0f + 0.5f, 0.0f, 255.0f);
    return out;
}

} // namespace FogVolume
