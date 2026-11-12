#pragma once
// Packing a material's grey pictures into one "ORM" picture: Occlusion in red,
// Roughness in green, Metalness in blue (the layout glTF and most engines use).
//
// Why: the graphics card then reads one picture instead of two or three for every
// pixel of a custom material, and holds one picture in memory instead of three.
//
// Occlusion ("ambient occlusion") is how much sky light reaches a spot: white = all of
// it, dark = little (the gaps between bricks, the bottom of a groove). It only dims
// the soft light from the sky and from bounces, never direct sun or lamp light.
//
// No OpenGL here, so the tests can run it. Textures::orm() (Textures.h) puts the
// result on the graphics card.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace OrmPack {

// One source picture: RGBA, 4 bytes a pixel, its red channel is the value. px = null
// means "there isn't one".
struct Source {
    const unsigned char* px = nullptr;
    int w = 0, h = 0;
    bool has() const { return px && w > 0 && h > 0; }
};

// The red channel at (u, v) in 0..1, smoothed between the 4 nearest pixels, wrapping
// round the edges (these pictures tile).
inline float sample(const Source& s, float u, float v) {
    const float x = u * s.w - 0.5f, y = v * s.h - 0.5f;
    const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    const float fx = x - x0, fy = y - y0;
    auto at = [&](int xi, int yi) {
        xi = ((xi % s.w) + s.w) % s.w;
        yi = ((yi % s.h) + s.h) % s.h;
        return (float)s.px[((size_t)yi * s.w + xi) * 4];
    };
    const float top = at(x0, y0) * (1.0f - fx) + at(x0 + 1, y0) * fx;
    const float bot = at(x0, y0 + 1) * (1.0f - fx) + at(x0 + 1, y0 + 1) * fx;
    return top * (1.0f - fy) + bot * fy;
}

// Packs them into `out` (RGBA, w x h: the biggest of the sources each way). A missing
// one is white in its channel. Returns false (and leaves `out` empty) if all three
// are missing.
inline bool pack(const Source& occlusion, const Source& roughness, const Source& metalness,
                 std::vector<unsigned char>& out, int& w, int& h) {
    const Source* src[3] = {&occlusion, &roughness, &metalness};
    w = h = 0;
    for (const Source* s : src)
        if (s->has()) { w = std::max(w, s->w); h = std::max(h, s->h); }
    out.clear();
    if (w <= 0 || h <= 0) return false;
    out.assign((size_t)w * h * 4, 255);
    for (int c = 0; c < 3; ++c) {
        const Source& s = *src[c];
        if (!s.has()) continue;
        if (s.w == w && s.h == h) {   // same size: straight across
            for (size_t i = 0, n = (size_t)w * h; i < n; ++i) out[i * 4 + c] = s.px[i * 4];
            continue;
        }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                out[((size_t)y * w + x) * 4 + c] =
                    (unsigned char)std::lround(std::clamp(sample(s, (x + 0.5f) / w, (y + 0.5f) / h), 0.0f, 255.0f));
    }
    return true;
}

} // namespace OrmPack
