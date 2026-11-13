#include "OceanWaves.h"

#include <cstdint>

namespace OceanWaves {

namespace {

constexpr float kTwoPi = 6.2831853f;

// A well-mixed whole number from three (the same on every computer).
uint32_t mix(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 16; h *= 0x7FEB352Du;
    h ^= h >> 15; h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

// 2D FFT of an n x n grid in place: every row, then every column.
void fft2(glm::vec2* g, int n, bool inverse) {
    for (int z = 0; z < n; ++z) fft(g + (size_t)z * n, n, inverse);
    glm::vec2 col[kGpuN];
    for (int x = 0; x < n; ++x) {
        for (int z = 0; z < n; ++z) col[z] = g[(size_t)z * n + x];
        fft(col, n, inverse);
        for (int z = 0; z < n; ++z) g[(size_t)z * n + x] = col[z];
    }
}

// A grid read between its points, wrapping round (the same way the graphics card
// reads a repeating texture).
glm::vec4 read(const glm::vec4* g, int n, float u, float v) {
    u = u * n - 0.5f;
    v = v * n - 0.5f;
    const float fu = std::floor(u), fv = std::floor(v);
    const float tu = u - fu, tv = v - fv;
    const int m = n - 1;   // (n is a power of two)
    const int x0 = (int)fu & m, x1 = (x0 + 1) & m, z0 = (int)fv & m, z1 = (z0 + 1) & m;
    const glm::vec4 a = g[(size_t)z0 * n + x0] * (1.0f - tu) + g[(size_t)z0 * n + x1] * tu;
    const glm::vec4 b = g[(size_t)z1 * n + x0] * (1.0f - tu) + g[(size_t)z1 * n + x1] * tu;
    return a * (1.0f - tv) + b * tv;
}

} // namespace

glm::vec2 startWave(int band, int nx, int nz) {
    if (band < 0 || band >= kBands || (nx == 0 && nz == 0)) return glm::vec2(0.0f);
    const Band& B = kBand[band];
    const float dk = kTwoPi / B.length;
    const float kx = dk * (float)nx, kz = dk * (float)nz;
    const float k = std::sqrt(kx * kx + kz * kz);
    const float wave = kTwoPi / k;
    if (wave < B.minWave || wave >= B.maxWave) return glm::vec2(0.0f);
    // The spectrum (Phillips): tallest at kPeakWave, falling off fast for shorter waves.
    const float kp = kTwoPi / kPeakWave;
    const float Lp = 1.0f / (kp * 1.41421356f);
    float P = std::exp(-1.0f / (k * Lp * k * Lp)) / (k * k * k * k);
    // Mostly along the wind, some across, little against it.
    const float along = (kx * kWindX + kz * kWindZ) / k;
    float D = 0.15f + 0.85f * along * along;
    if (along < 0.0f) D *= 0.25f;
    P *= D * std::exp(-k * k * 0.0001f);   // (and the very tiniest ripples die out)
    const float amp = std::sqrt(P) * dk;
    // Two made-up numbers on a bell curve (Box-Muller).
    const uint32_t h1 = mix((uint32_t)band, (uint32_t)(nx + 4096), (uint32_t)(nz + 4096));
    const uint32_t h2 = mix(h1, (uint32_t)(nz + 4096), (uint32_t)(nx + 4096));
    const float u1 = ((float)(h1 >> 8) + 1.0f) / 16777217.0f, u2 = (float)(h2 >> 8) / 16777216.0f;
    const float r = std::sqrt(-2.0f * std::log(u1)) * 0.70710678f;
    return glm::vec2(r * std::cos(kTwoPi * u2), r * std::sin(kTwoPi * u2)) * amp;
}

void fft(glm::vec2* a, int n, bool inverse) {
    // Put the numbers in bit-reversed order, then join pairs, fours, eights...
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = (inverse ? kTwoPi : -kTwoPi) / (float)len;
        const glm::vec2 wl(std::cos(ang), std::sin(ang));
        const int half = len >> 1;
        for (int i = 0; i < n; i += len) {
            glm::vec2 w(1.0f, 0.0f);
            for (int j = 0; j < half; ++j) {
                const glm::vec2 u = a[i + j], t = a[i + j + half];
                const glm::vec2 v(t.x * w.x - t.y * w.y, t.x * w.y + t.y * w.x);
                a[i + j] = u + v;
                a[i + j + half] = u - v;
                w = glm::vec2(w.x * wl.x - w.y * wl.y, w.x * wl.y + w.y * wl.x);
            }
        }
    }
}

Field::Field() {
    const int N = kCpuN;
    m_h.resize((size_t)N * N);
    m_d.resize((size_t)N * N);
    double var = 0.0;
    for (int b = 0; b < kCpuBands; ++b) {
        m_modes[b].resize((size_t)N * N);
        m_disp[b].assign((size_t)N * N, glm::vec4(0.0f));
        m_slope[b].assign((size_t)N * N, glm::vec4(0.0f));
        const float dk = kTwoPi / kBand[b].length;
        for (int iz = 0; iz < N; ++iz)
            for (int ix = 0; ix < N; ++ix) {
                Mode& m = m_modes[b][(size_t)iz * N + ix];
                m = {glm::vec2(0.0f), glm::vec2(0.0f), 0.0f, 0.0f, 0.0f};
                const int nx = ix < N / 2 ? ix : ix - N, nz = iz < N / 2 ? iz : iz - N;
                // (The very shortest waves of a grid have no partner going the other way: left out.)
                if (nx == -N / 2 || nz == -N / 2 || (nx == 0 && nz == 0)) continue;
                const float kx = dk * (float)nx, kz = dk * (float)nz, k = std::sqrt(kx * kx + kz * kz);
                m.a = startWave(b, nx, nz);
                const glm::vec2 o = startWave(b, -nx, -nz);
                m.b = glm::vec2(o.x, -o.y);
                m.omega = std::sqrt(kGravity * k);
                m.dx = kx / k;
                m.dz = kz / k;
                var += 2.0 * (double)(m.a.x * m.a.x + m.a.y * m.a.y);
            }
    }
    // Crests about 1 stud high for a swell of 1: the usual height is 0.4, and crests
    // are about two and a half times the usual.
    m_scale = var > 0.0 ? 0.4f / (float)std::sqrt(var) : 1.0f;
    for (int b = 0; b < kCpuBands; ++b)
        for (Mode& m : m_modes[b]) { m.a *= m_scale; m.b *= m_scale; }
    update(0.0f);
}

void Field::update(float time) {
    if (time == m_time) return;
    m_time = time;
    ++m_version;
    const int N = kCpuN, M = N - 1;
    for (int b = 0; b < kCpuBands; ++b) {
        const size_t count = (size_t)N * N;
        for (size_t i = 0; i < count; ++i) {
            const Mode& m = m_modes[b][i];
            const float c = std::cos(m.omega * time), s = std::sin(m.omega * time);
            // This wave now: its start turned by its speed, plus its partner going the other way.
            const glm::vec2 h(m.a.x * c - m.a.y * s + m.b.x * c + m.b.y * s,
                              m.a.x * s + m.a.y * c - m.b.x * s + m.b.y * c);
            m_h[i] = h;
            // The sideways pull along x and z, packed as one complex number (x + i z).
            m_d[i] = glm::vec2(m.dx * h.y + m.dz * h.x, -m.dx * h.x + m.dz * h.y);
        }
        fft2(m_h.data(), N, true);
        fft2(m_d.data(), N, true);
        glm::vec4* disp = m_disp[b].data();
        glm::vec4* slope = m_slope[b].data();
        for (size_t i = 0; i < count; ++i) disp[i] = glm::vec4(m_d[i].x, m_h[i].x, m_d[i].y, 0.0f);
        // How steep and how squeezed, from each point's neighbours.
        const float inv = 0.5f * (float)N / kBand[b].length;
        for (int z = 0; z < N; ++z)
            for (int x = 0; x < N; ++x) {
                const glm::vec4 dxv = (disp[(size_t)z * N + ((x + 1) & M)] - disp[(size_t)z * N + ((x - 1) & M)]) * inv;
                const glm::vec4 dzv = (disp[(size_t)((z + 1) & M) * N + x] - disp[(size_t)((z - 1) & M) * N + x]) * inv;
                slope[(size_t)z * N + x] = glm::vec4(dxv.y, dzv.y, dxv.x, dzv.z);
                disp[(size_t)z * N + x].w = dzv.x;
            }
    }
}

glm::vec3 Field::offset(float x, float z, float amplitude, float edge) const {
    if (amplitude <= 0.0f) return glm::vec3(0.0f);
    glm::vec4 d(0.0f);
    for (int b = 0; b < kCpuBands; ++b) d += read(m_disp[b].data(), kCpuN, x / kBand[b].length, z / kBand[b].length);
    const float side = amplitude * chop(amplitude) * edge;
    return glm::vec3(d.x * side, d.y * amplitude, d.z * side);
}

float Field::heightAt(float x, float z, float amplitude, float edge) const {
    if (amplitude <= 0.0f) return 0.0f;
    float x0 = x, z0 = z;
    for (int i = 0; i < 3; ++i) {   // which calm point ends up over (x, z)?
        const glm::vec3 d = offset(x0, z0, amplitude, edge);
        x0 = x - d.x;
        z0 = z - d.z;
    }
    return offset(x0, z0, amplitude, edge).y;
}

} // namespace OceanWaves
