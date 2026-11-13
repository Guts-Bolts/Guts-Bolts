#include "WaterDye.h"
#include <algorithm>
#include <cmath>

namespace WaterDye {

void resize(Grid& g, int nx, int nz, float cell, const glm::vec2& min) {
    g.nx = std::max(2, nx);
    g.nz = std::max(2, nz);
    g.cell = std::max(0.01f, cell);
    g.min = min;
    const size_t n = (size_t)g.nx * g.nz;
    g.c.assign(n, glm::vec4(0.0f));
    g.scratch.assign(n, glm::vec4(0.0f));
    g.px.assign(n * 4, 0);
    g.total = 0.0f;
    g.live = false;
    ++g.version;
}

void clear(Grid& g) {
    if (!g.live && g.total == 0.0f) return;
    std::fill(g.c.begin(), g.c.end(), glm::vec4(0.0f));
    g.total = 0.0f;
    g.live = false;
    ++g.version;
}

bool add(Grid& g, float x, float z, const glm::vec3& color, float amount, float radius) {
    if (g.c.empty() || amount <= 0.0f) return false;
    const float fx = (x - g.min.x) / g.cell, fz = (z - g.min.y) / g.cell;
    if (fx < -0.5f || fz < -0.5f || fx > g.nx - 0.5f || fz > g.nz - 0.5f) return false;
    // A soft round patch. First what the weights add up to, so the same amount goes in
    // however fine the grid is (and whether or not the patch hangs over the edge).
    const float r = std::max(radius * 0.5f / g.cell, 0.75f);
    const int i0 = std::max(0, (int)std::floor(fx - r)), i1 = std::min(g.nx - 1, (int)std::ceil(fx + r));
    const int k0 = std::max(0, (int)std::floor(fz - r)), k1 = std::min(g.nz - 1, (int)std::ceil(fz + r));
    auto weight = [&](int i, int k) {
        const float dx = (i - fx) / r, dz = (k - fz) / r;
        const float d2 = dx * dx + dz * dz;
        return d2 >= 1.0f ? 0.0f : (1.0f - d2) * (1.0f - d2);
    };
    float sum = 0.0f;
    for (int k = k0; k <= k1; ++k)
        for (int i = i0; i <= i1; ++i) sum += weight(i, k);
    if (sum <= 1e-6f) {   // (smaller than one point: it all goes in the nearest)
        const int i = std::clamp((int)std::lround(fx), 0, g.nx - 1), k = std::clamp((int)std::lround(fz), 0, g.nz - 1);
        glm::vec4& c = g.c[g.idx(i, k)];
        const float a = std::min(amount / (g.cell * g.cell), kFull - c.a);
        if (a > 0.0f) c += glm::vec4(color * a, a);
        g.live = true;
        return true;
    }
    const float each = amount / (sum * g.cell * g.cell);
    for (int k = k0; k <= k1; ++k)
        for (int i = i0; i <= i1; ++i) {
            const float w = weight(i, k);
            if (w <= 0.0f) continue;
            glm::vec4& c = g.c[g.idx(i, k)];
            const float a = std::min(each * w, kFull - c.a);   // (a point can only hold so much)
            if (a > 0.0f) c += glm::vec4(color * a, a);
        }
    g.live = true;
    return true;
}

namespace {
// The colour at grid position (fx, fz), in points. Nothing outside the pool.
glm::vec4 sample(const Grid& g, const std::vector<glm::vec4>& c, float fx, float fz) {
    if (fx <= -1.0f || fz <= -1.0f || fx >= (float)g.nx || fz >= (float)g.nz) return glm::vec4(0.0f);
    const int i0 = (int)std::floor(fx), k0 = (int)std::floor(fz);
    const float tx = fx - (float)i0, tz = fz - (float)k0;
    auto C = [&](int i, int k) {
        return i < 0 || k < 0 || i >= g.nx || k >= g.nz ? glm::vec4(0.0f) : c[g.idx(i, k)];
    };
    return glm::mix(glm::mix(C(i0, k0), C(i0 + 1, k0), tx), glm::mix(C(i0, k0 + 1), C(i0 + 1, k0 + 1), tx), tz);
}
}

void step(Grid& g, float dt, const glm::vec2* flow, const glm::vec2& base, const float* churn) {
    if (!g.live || g.c.empty() || dt <= 0.0f) return;
    dt = std::min(dt, 0.1f);
    const int nx = g.nx, nz = g.nz;
    const float inv = 1.0f / g.cell;

    // 1. Carried along by the current: each point takes the colour from where the
    //    water it now holds came from.
    //    Even still water is never quite still: slow swirls that wander about pull the
    //    cloud into wisps (two layers of them, sideways only, so no colour is squeezed
    //    together or pulled apart).
    g.clock += dt;
    const float t = g.clock;
    float had = 0.0f, got = 0.0f;
    for (int k = 0; k < nz; ++k) {
        const float z = g.min.y + (float)k * g.cell;
        for (int i = 0; i < nx; ++i) {
            const float x = g.min.x + (float)i * g.cell;
            glm::vec2 u = flow ? flow[g.idx(i, k)] : base;
            const float a = x * 0.55f + t * 0.21f, b = z * 0.62f - t * 0.17f;
            const float c = x * 1.7f - z * 1.3f + t * 0.4f;
            u.x += kSwirl * (-0.62f * std::sin(a) * std::sin(b) - 0.65f * std::cos(c));
            u.y += kSwirl * (-0.55f * std::cos(a) * std::cos(b) - 0.85f * std::cos(c));
            const glm::vec4 v = sample(g, g.c, (float)i - u.x * dt * inv, (float)k - u.y * dt * inv);
            g.scratch[g.idx(i, k)] = v;
            had += g.c[g.idx(i, k)].a;
            got += v.a;
        }
    }
    g.c.swap(g.scratch);
    // (Looking the colour up between points can round a little extra in: take it back
    // out, so moving a cloud about never makes more of it.)
    if (got > had && got > 0.0f) {
        const float fix = had / got;
        for (glm::vec4& c : g.c) c *= fix;
    }

    // 2. Spreading out: every point shares with its four neighbours. More where the
    //    surface is being churned up. The sides of the pool are walls (nothing leaves).
    const float fade = std::exp(-dt * 0.6931f / kHalfLife);
    float total = 0.0f, most = 0.0f;
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx; ++i) {
            const size_t id = g.idx(i, k);
            const glm::vec4 c = g.c[id];
            const glm::vec4 l = i > 0 ? g.c[id - 1] : c, r = i < nx - 1 ? g.c[id + 1] : c;
            const glm::vec4 d = k > 0 ? g.c[id - nx] : c, u = k < nz - 1 ? g.c[id + nx] : c;
            const float speed = churn ? std::min(std::abs(churn[id]), 6.0f) : 0.0f;
            // (never more than a fifth each way in one step, or it would overshoot)
            const float share = std::min((kSpread + kStir * speed) * dt * inv * inv, 0.2f);
            const glm::vec4 v = glm::max((c + (l + r + d + u - 4.0f * c) * share) * fade, glm::vec4(0.0f));
            total += v.a;
            most = std::max(most, v.a);
            g.scratch[id] = v;
        }
    g.c.swap(g.scratch);
    // 3. Thinning away: once even the thickest part is too thin to see, the pool is
    //    clean again. (Not point by point: that would stop the thin edge of a cloud
    //    from ever creeping outwards.)
    if (most < kGone) { clear(g); return; }
    g.total = total * g.cell * g.cell;
    ++g.version;
}

float at(const Grid& g, float x, float z, glm::vec3* color) {
    if (g.c.empty()) return 0.0f;
    const glm::vec4 v = sample(g, g.c, (x - g.min.x) / g.cell, (z - g.min.y) / g.cell);
    if (color) *color = v.a > 1e-5f ? glm::vec3(v) / v.a : glm::vec3(0.0f);
    return v.a;
}

uint8_t encode(float amount) {
    return (uint8_t)std::lround(std::sqrt(std::clamp(amount / kFull, 0.0f, 1.0f)) * 255.0f);
}

float decode(uint8_t byte) {
    const float t = (float)byte / 255.0f;
    return t * t * kFull;
}

const std::vector<uint8_t>& pixels(Grid& g) {
    glm::vec4 all(0.0f);
    for (const glm::vec4& c : g.c) all += c;
    const glm::vec3 average = all.a > 1e-5f ? glm::vec3(all) / all.a : glm::vec3(0.0f);
    auto byte = [](float v) { return (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    for (size_t i = 0; i < g.c.size(); ++i) {
        const glm::vec4& c = g.c[i];
        const glm::vec3 col = c.a > 1e-4f ? glm::vec3(c) / c.a : average;
        g.px[i * 4] = byte(col.r); g.px[i * 4 + 1] = byte(col.g); g.px[i * 4 + 2] = byte(col.b);
        g.px[i * 4 + 3] = encode(c.a);
    }
    return g.px;
}

} // namespace WaterDye
