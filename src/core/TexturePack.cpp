// Pictures packed ahead of time (see TexturePack.h).
#include "TexturePack.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace TexturePack {

namespace {

constexpr char     kMagic[4] = {'G', 'B', 'T', 'X'};
constexpr uint32_t kVersion = 1;

int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

// ---------------------------------------------------------------- BC7 (mode 6 only)
// One colour line through all 16 pixels, 16 steps along it, alpha included: simple,
// and close to the best BC7 can do for most game pictures.
constexpr int kW4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

struct Bits128 {
    uint8_t b[16] = {};
    int     at = 0;
    void put(uint32_t v, int n) {
        for (int i = 0; i < n; ++i, ++at)
            if (v >> i & 1u) b[at >> 3] |= (uint8_t)(1u << (at & 7));
    }
};
uint32_t get128(const uint8_t* b, int& at, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i, ++at) v |= (uint32_t)(b[at >> 3] >> (at & 7) & 1u) << i;
    return v;
}

int lerp6(int a, int b, int w) { return ((64 - w) * a + w * b + 32) >> 6; }

// The squared error of the best indices for two 8-bit endpoints; fills `idx`.
long bc7Fit(const uint8_t px[64], const int e0[4], const int e1[4], uint8_t idx[16]) {
    int pal[16][4];
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 4; ++c) pal[i][c] = lerp6(e0[c], e1[c], kW4[i]);
    long total = 0;
    for (int p = 0; p < 16; ++p) {
        long best = 1L << 40;
        int  bi = 0;
        for (int i = 0; i < 16; ++i) {
            long d = 0;
            for (int c = 0; c < 4; ++c) { const int x = pal[i][c] - px[p * 4 + c]; d += (long)x * x; }
            if (d < best) { best = d; bi = i; }
        }
        idx[p] = (uint8_t)bi;
        total += best;
    }
    return total;
}

// Endpoints as 7 bits + a shared lowest bit (the "p-bit"), back to 8 bits.
void quantize7(const float e[4], int p, int out7[4], int out8[4]) {
    for (int c = 0; c < 4; ++c) {
        out7[c] = std::clamp((int)std::lround((e[c] - (float)p) * 0.5f), 0, 127);
        out8[c] = out7[c] * 2 + p;
    }
}

} // namespace

void encodeBC7(const uint8_t px[64], uint8_t out[16]) {
    // The line: through the middle, along the way the colours spread the most.
    float mean[4] = {0, 0, 0, 0};
    for (int p = 0; p < 16; ++p) for (int c = 0; c < 4; ++c) mean[c] += px[p * 4 + c] / 16.0f;
    float cov[4][4] = {};
    for (int p = 0; p < 16; ++p)
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) cov[i][j] += (px[p * 4 + i] - mean[i]) * (px[p * 4 + j] - mean[j]);
    float axis[4] = {1, 1, 1, 0.5f};
    for (int it = 0; it < 8; ++it) {
        float n[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) n[i] += cov[i][j] * axis[j];
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] + n[3] * n[3]);
        if (len < 1e-6f) break;
        for (int i = 0; i < 4; ++i) axis[i] = n[i] / len;
    }
    float tmin = 1e9f, tmax = -1e9f;
    for (int p = 0; p < 16; ++p) {
        float t = 0;
        for (int c = 0; c < 4; ++c) t += (px[p * 4 + c] - mean[c]) * axis[c];
        tmin = std::min(tmin, t);
        tmax = std::max(tmax, t);
    }
    float e0[4], e1[4];
    for (int c = 0; c < 4; ++c) {
        e0[c] = std::clamp(mean[c] + tmin * axis[c], 0.0f, 255.0f);
        e1[c] = std::clamp(mean[c] + tmax * axis[c], 0.0f, 255.0f);
    }
    long bestErr = -1;
    int  best7[2][4] = {}, bestP[2] = {0, 0};
    uint8_t bestIdx[16] = {};
    for (int pass = 0; pass < 2; ++pass) {
        for (int p0 = 0; p0 < 2; ++p0)
            for (int p1 = 0; p1 < 2; ++p1) {
                int a7[4], a8[4], b7[4], b8[4];
                quantize7(e0, p0, a7, a8);
                quantize7(e1, p1, b7, b8);
                uint8_t idx[16];
                const long err = bc7Fit(px, a8, b8, idx);
                if (bestErr < 0 || err < bestErr) {
                    bestErr = err;
                    std::memcpy(best7[0], a7, sizeof(a7));
                    std::memcpy(best7[1], b7, sizeof(b7));
                    bestP[0] = p0; bestP[1] = p1;
                    std::memcpy(bestIdx, idx, 16);
                }
            }
        if (pass == 1 || bestErr == 0) break;
        // Once more, with the ends that best fit the steps each pixel picked (least squares).
        float aa = 0, ab = 0, bb = 0, ax[4] = {0, 0, 0, 0}, bx[4] = {0, 0, 0, 0};
        for (int p = 0; p < 16; ++p) {
            const float w = kW4[bestIdx[p]] / 64.0f, v = 1.0f - w;
            aa += v * v; ab += v * w; bb += w * w;
            for (int c = 0; c < 4; ++c) { ax[c] += v * px[p * 4 + c]; bx[c] += w * px[p * 4 + c]; }
        }
        const float det = aa * bb - ab * ab;
        if (std::fabs(det) < 1e-6f) break;
        for (int c = 0; c < 4; ++c) {
            e0[c] = std::clamp((ax[c] * bb - bx[c] * ab) / det, 0.0f, 255.0f);
            e1[c] = std::clamp((bx[c] * aa - ax[c] * ab) / det, 0.0f, 255.0f);
        }
    }
    // The first pixel's step must be under 8 (its top bit isn't stored): swap the ends if not.
    if (bestIdx[0] >= 8) {
        for (int c = 0; c < 4; ++c) std::swap(best7[0][c], best7[1][c]);
        std::swap(bestP[0], bestP[1]);
        for (uint8_t& i : bestIdx) i = (uint8_t)(15 - i);
    }
    Bits128 w;
    w.put(1u << 6, 7);   // mode 6
    for (int c = 0; c < 4; ++c) { w.put((uint32_t)best7[0][c], 7); w.put((uint32_t)best7[1][c], 7); }
    w.put((uint32_t)bestP[0], 1);
    w.put((uint32_t)bestP[1], 1);
    w.put(bestIdx[0], 3);
    for (int p = 1; p < 16; ++p) w.put(bestIdx[p], 4);
    std::memcpy(out, w.b, 16);
}

void decodeBC7(const uint8_t in[16], uint8_t px[64]) {
    int at = 0;
    if (get128(in, at, 7) != (1u << 6)) { std::memset(px, 0, 64); return; }   // (only mode 6)
    int e[2][4];
    for (int c = 0; c < 4; ++c) { e[0][c] = (int)get128(in, at, 7); e[1][c] = (int)get128(in, at, 7); }
    const int p0 = (int)get128(in, at, 1), p1 = (int)get128(in, at, 1);
    for (int c = 0; c < 4; ++c) { e[0][c] = e[0][c] * 2 + p0; e[1][c] = e[1][c] * 2 + p1; }
    for (int p = 0; p < 16; ++p) {
        const int i = (int)get128(in, at, p == 0 ? 3 : 4);
        for (int c = 0; c < 4; ++c) px[p * 4 + c] = (uint8_t)lerp6(e[0][c], e[1][c], kW4[i]);
    }
}

// ---------------------------------------------------------------- BC4 / BC5
void encodeBC4(const uint8_t v[16], uint8_t out[8]) {
    int lo = 255, hi = 0;
    for (int i = 0; i < 16; ++i) { lo = std::min(lo, (int)v[i]); hi = std::max(hi, (int)v[i]); }
    std::memset(out, 0, 8);
    out[0] = (uint8_t)hi;
    out[1] = (uint8_t)lo;
    if (hi == lo) return;   // (every step 0 = the top end)
    int pal[8] = {hi, lo};
    for (int k = 1; k <= 6; ++k) pal[k + 1] = ((7 - k) * hi + k * lo + 3) / 7;
    uint64_t bits = 0;
    for (int p = 0; p < 16; ++p) {
        int best = 0, bd = 1 << 30;
        for (int i = 0; i < 8; ++i) { const int d = std::abs(pal[i] - v[p]); if (d < bd) { bd = d; best = i; } }
        bits |= (uint64_t)best << (3 * p);
    }
    for (int i = 0; i < 6; ++i) out[2 + i] = (uint8_t)(bits >> (8 * i));
}

void decodeBC4(const uint8_t in[8], uint8_t v[16]) {
    const int e0 = in[0], e1 = in[1];
    int pal[8] = {e0, e1};
    if (e0 > e1) for (int k = 1; k <= 6; ++k) pal[k + 1] = ((7 - k) * e0 + k * e1 + 3) / 7;
    else {
        for (int k = 1; k <= 4; ++k) pal[k + 1] = ((5 - k) * e0 + k * e1 + 2) / 5;
        pal[6] = 0; pal[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= (uint64_t)in[2 + i] << (8 * i);
    for (int p = 0; p < 16; ++p) v[p] = (uint8_t)pal[(bits >> (3 * p)) & 7];
}

// ---------------------------------------------------------------- ETC2 (ETC1 modes) + EAC
namespace {

constexpr int kEtcMod[8][2] = {{2, 8}, {5, 17}, {9, 29}, {13, 42}, {18, 60}, {24, 80}, {33, 106}, {47, 183}};
// Step -> amount: 0 +small, 1 +large, 2 -small, 3 -large.
int etcMod(int table, int step) { const int m = kEtcMod[table][step & 1]; return step & 2 ? -m : m; }

constexpr int kEac[16][8] = {
    {-3, -6, -9, -15, 2, 5, 8, 14}, {-3, -7, -10, -13, 2, 6, 9, 12}, {-2, -5, -8, -13, 1, 4, 7, 12},
    {-2, -4, -6, -13, 1, 3, 5, 12}, {-3, -6, -8, -12, 2, 5, 7, 11}, {-3, -7, -9, -11, 2, 6, 8, 10},
    {-4, -7, -8, -11, 3, 6, 7, 10}, {-3, -5, -8, -11, 2, 4, 7, 10}, {-2, -6, -8, -10, 1, 5, 7, 9},
    {-2, -5, -8, -10, 1, 4, 7, 9},  {-2, -4, -8, -10, 1, 3, 7, 9},  {-2, -5, -7, -10, 1, 4, 6, 9},
    {-3, -4, -7, -10, 2, 3, 6, 9},  {-1, -2, -3, -10, 0, 1, 2, 9},  {-4, -6, -8, -9, 3, 5, 7, 8},
    {-3, -5, -7, -9, 2, 4, 6, 8}};

// ETC numbers its 16 pixels down the columns: pixel x*4+y.
int col(int p) { return (p & 3) * 4 + (p >> 2); }   // row-major p -> ETC's number

void putBE(uint64_t v, uint8_t out[8]) { for (int i = 0; i < 8; ++i) out[i] = (uint8_t)(v >> (56 - 8 * i)); }
uint64_t getBE(const uint8_t in[8]) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v = v << 8 | in[i]; return v; }

// Best table and steps for one half of the block around `base`; returns the error.
long etcHalf(const uint8_t px[64], const bool in[16], const int base[3], int& table, uint8_t steps[16]) {
    long best = -1;
    for (int t = 0; t < 8; ++t) {
        long err = 0;
        uint8_t s[16] = {};
        for (int p = 0; p < 16; ++p) {
            if (!in[p]) continue;
            long bd = 1L << 40;
            for (int k = 0; k < 4; ++k) {
                long d = 0;
                for (int c = 0; c < 3; ++c) { const int x = clamp255(base[c] + etcMod(t, k)) - px[p * 4 + c]; d += (long)x * x; }
                if (d < bd) { bd = d; s[p] = (uint8_t)k; }
            }
            err += bd;
        }
        if (best < 0 || err < best) {
            best = err;
            table = t;
            for (int p = 0; p < 16; ++p) if (in[p]) steps[p] = s[p];
        }
    }
    return best;
}

// ETC2's "T" mode: one colour for some pixels and three shades of a second colour for
// the rest. It keeps a thin line of a different colour (mortar between bricks) that
// ETC1's modes, which can only make a half lighter or darker, smear.
constexpr int kTDist[8] = {3, 6, 11, 16, 23, 32, 41, 64};

void tPaints(const int a[3], const int b[3], int d, int paint[4][3]) {
    for (int c = 0; c < 3; ++c) {
        paint[0][c] = a[c] * 17;
        paint[1][c] = clamp255(b[c] * 17 + kTDist[d]);
        paint[2][c] = b[c] * 17;
        paint[3][c] = clamp255(b[c] * 17 - kTDist[d]);
    }
}

void tryT(const uint8_t px[64], long& bestErr, uint64_t& bestBits) {
    // Split the pixels into two groups of similar colours (2-means).
    float ctr[2][3];
    int far0 = 0, far1 = 0;
    float mean[3] = {0, 0, 0};
    for (int p = 0; p < 16; ++p) for (int c = 0; c < 3; ++c) mean[c] += px[p * 4 + c] / 16.0f;
    auto dist = [&](int p, const float* q) {
        float d = 0;
        for (int c = 0; c < 3; ++c) { const float x = px[p * 4 + c] - q[c]; d += x * x; }
        return d;
    };
    for (int p = 1; p < 16; ++p) if (dist(p, mean) > dist(far0, mean)) far0 = p;
    float f0[3] = {(float)px[far0 * 4], (float)px[far0 * 4 + 1], (float)px[far0 * 4 + 2]};
    for (int p = 1; p < 16; ++p) if (dist(p, f0) > dist(far1, f0)) far1 = p;
    for (int c = 0; c < 3; ++c) { ctr[0][c] = px[far0 * 4 + c]; ctr[1][c] = px[far1 * 4 + c]; }
    int group[16] = {};
    for (int it = 0; it < 4; ++it) {
        float sum[2][3] = {}; int n[2] = {0, 0};
        for (int p = 0; p < 16; ++p) {
            group[p] = dist(p, ctr[1]) < dist(p, ctr[0]) ? 1 : 0;
            ++n[group[p]];
            for (int c = 0; c < 3; ++c) sum[group[p]][c] += px[p * 4 + c];
        }
        for (int g = 0; g < 2; ++g) if (n[g]) for (int c = 0; c < 3; ++c) ctr[g][c] = sum[g][c] / n[g];
    }
    for (int single = 0; single < 2; ++single) {
        int a[3], b[3];
        for (int c = 0; c < 3; ++c) {
            a[c] = std::clamp((int)std::lround(ctr[single][c] / 17.0f), 0, 15);
            b[c] = std::clamp((int)std::lround(ctr[1 - single][c] / 17.0f), 0, 15);
        }
        for (int d = 0; d < 8; ++d) {
            int paint[4][3];
            tPaints(a, b, d, paint);
            long err = 0;
            uint32_t idx = 0;
            for (int p = 0; p < 16; ++p) {
                long bd = 1L << 40; int bi = 0;
                for (int k = 0; k < 4; ++k) {
                    long e = 0;
                    for (int c = 0; c < 3; ++c) { const int x = paint[k][c] - px[p * 4 + c]; e += (long)x * x; }
                    if (e < bd) { bd = e; bi = k; }
                }
                err += bd;
                const int n = col(p);
                idx |= (uint32_t)(bi >> 1) << (16 + n) | (uint32_t)(bi & 1) << n;
            }
            if (bestErr >= 0 && err >= bestErr) continue;
            bestErr = err;
            // The first colour's red is split in two (bits 60-59 and 57-56); the bits around
            // it are set so the "red + step" of the other modes runs off the end: that's
            // how a graphics chip knows it's T mode.
            const int ra = a[0] >> 2, rb = a[0] & 3;
            const bool low = ra + rb < 4;   // (red 0-3 plus a step of -4..-1 goes below 0; else 28-31 plus 0..3 goes over 31)
            uint64_t v = 0;
            v |= (uint64_t)(low ? 0u : 7u) << 61 | (uint64_t)ra << 59 | (uint64_t)(low ? 1u : 0u) << 58 | (uint64_t)rb << 56;
            v |= (uint64_t)a[1] << 52 | (uint64_t)a[2] << 48;
            v |= (uint64_t)b[0] << 44 | (uint64_t)b[1] << 40 | (uint64_t)b[2] << 36;
            v |= (uint64_t)(d >> 1) << 34 | (uint64_t)1 << 33 | (uint64_t)(d & 1) << 32;
            bestBits = v | idx;
        }
    }
}

uint64_t encodeEtcColour(const uint8_t px[64]) {
    long bestErr = -1;
    uint64_t bestBits = 0;
    for (int flip = 0; flip < 2; ++flip) {
        bool in[2][16];
        float avg[2][3] = {};
        for (int p = 0; p < 16; ++p) {
            const int x = p & 3, y = p >> 2;
            const int half = flip ? (y >= 2) : (x >= 2);
            in[0][p] = half == 0;
            in[1][p] = half == 1;
            for (int c = 0; c < 3; ++c) avg[half][c] += px[p * 4 + c] / 8.0f;
        }
        for (int diff = 0; diff < 2; ++diff) {
            // Each half's colour: its average, or one step lighter or darker, whichever
            // fits the pixels best. (In the second mode the
            // second half's colour is stored as a small step from the first's.)
            int q[2][3], base[2][3], tables[2] = {0, 0};
            uint8_t steps[16] = {};
            long err = 0;
            for (int h = 0; h < 2; ++h) {
                int start[3];
                for (int c = 0; c < 3; ++c)
                    start[c] = diff ? std::clamp((int)std::lround(avg[h][c] * 31.0f / 255.0f), 0, 31)
                                    : std::clamp((int)std::lround(avg[h][c] / 17.0f), 0, 15);
                long hb = -1;
                for (int d : {13, 0, 26}) {   // (no change, all one step down, all one step up)
                    int cq[3], cb[3];
                    bool ok = true;
                    for (int c = 0; c < 3; ++c) {
                        cq[c] = start[c] + (d / (c == 0 ? 1 : c == 1 ? 3 : 9)) % 3 - 1;
                        const int top = diff ? 31 : 15;
                        if (cq[c] < 0 || cq[c] > top) ok = false;
                        if (diff && h == 1 && (cq[c] - q[0][c] < -4 || cq[c] - q[0][c] > 3)) ok = false;
                        cb[c] = diff ? (cq[c] << 3) | (cq[c] >> 2) : cq[c] * 17;
                    }
                    if (!ok) continue;
                    int t = 0;
                    uint8_t s2[16] = {};
                    const long e = etcHalf(px, in[h], cb, t, s2);
                    if (hb < 0 || e < hb) {
                        hb = e;
                        tables[h] = t;
                        for (int c = 0; c < 3; ++c) { q[h][c] = cq[c]; base[h][c] = cb[c]; }
                        for (int p = 0; p < 16; ++p) if (in[h][p]) steps[p] = s2[p];
                    }
                }
                if (hb < 0) {   // (the second half's average is too far from the first's for this mode)
                    err = -1;
                    break;
                }
                err += hb;
            }
            if (err < 0) continue;
            (void)base;
            if (bestErr >= 0 && err >= bestErr) continue;
            bestErr = err;
            uint64_t v = 0;
            if (diff) {
                v |= (uint64_t)q[0][0] << 59 | (uint64_t)((q[1][0] - q[0][0]) & 7) << 56;
                v |= (uint64_t)q[0][1] << 51 | (uint64_t)((q[1][1] - q[0][1]) & 7) << 48;
                v |= (uint64_t)q[0][2] << 43 | (uint64_t)((q[1][2] - q[0][2]) & 7) << 40;
            } else {
                v |= (uint64_t)q[0][0] << 60 | (uint64_t)q[1][0] << 56;
                v |= (uint64_t)q[0][1] << 52 | (uint64_t)q[1][1] << 48;
                v |= (uint64_t)q[0][2] << 44 | (uint64_t)q[1][2] << 40;
            }
            v |= (uint64_t)tables[0] << 37 | (uint64_t)tables[1] << 34 | (uint64_t)diff << 33 | (uint64_t)flip << 32;
            for (int p = 0; p < 16; ++p) {
                const int n = col(p);
                v |= (uint64_t)(steps[p] >> 1) << (16 + n);
                v |= (uint64_t)(steps[p] & 1) << n;
            }
            bestBits = v;
        }
    }
    tryT(px, bestErr, bestBits);
    return bestBits;
}

void decodeEtcColour(uint64_t v, uint8_t px[64]) {
    const int diff = (int)(v >> 33 & 1), flip = (int)(v >> 32 & 1);
    if (diff) {
        const int r = (int)(v >> 59 & 31);
        int dr = (int)(v >> 56 & 7);
        if (dr >= 4) dr -= 8;
        if (r + dr < 0 || r + dr > 31) {   // T mode (see tryT)
            const int a[3] = {(int)((v >> 59 & 3) << 2 | (v >> 56 & 3)), (int)(v >> 52 & 15), (int)(v >> 48 & 15)};
            const int b[3] = {(int)(v >> 44 & 15), (int)(v >> 40 & 15), (int)(v >> 36 & 15)};
            int paint[4][3];
            tPaints(a, b, (int)((v >> 34 & 3) << 1 | (v >> 32 & 1)), paint);
            for (int p = 0; p < 16; ++p) {
                const int n = col(p);
                const int k = (int)((v >> (16 + n) & 1) << 1 | (v >> n & 1));
                for (int c = 0; c < 3; ++c) px[p * 4 + c] = (uint8_t)paint[k][c];
            }
            return;
        }
    }
    int base[2][3];
    if (diff) {
        for (int c = 0; c < 3; ++c) {
            const int sh = 59 - 8 * c;
            const int a = (int)(v >> sh & 31);
            int d = (int)(v >> (sh - 3) & 7);
            if (d >= 4) d -= 8;
            const int b = std::clamp(a + d, 0, 31);
            base[0][c] = (a << 3) | (a >> 2);
            base[1][c] = (b << 3) | (b >> 2);
        }
    } else {
        for (int c = 0; c < 3; ++c) {
            base[0][c] = (int)(v >> (60 - 8 * c) & 15) * 17;
            base[1][c] = (int)(v >> (56 - 8 * c) & 15) * 17;
        }
    }
    const int tables[2] = {(int)(v >> 37 & 7), (int)(v >> 34 & 7)};
    for (int p = 0; p < 16; ++p) {
        const int x = p & 3, y = p >> 2, n = col(p);
        const int half = flip ? (y >= 2) : (x >= 2);
        const int step = (int)((v >> (16 + n) & 1) << 1 | (v >> n & 1));
        for (int c = 0; c < 3; ++c) px[p * 4 + c] = (uint8_t)clamp255(base[half][c] + etcMod(tables[half], step));
    }
}

// EAC: a base, a table of 8 steps and a multiplier. `eleven`: the 11-bit kind (RG11),
// where values are base * 8 + 4 + step * mult * 8 out of 2047 (targets given in that range).
uint64_t encodeEac(const int target[16], bool eleven) {
    int lo = 1 << 30, hi = -1;
    for (int p = 0; p < 16; ++p) { lo = std::min(lo, target[p]); hi = std::max(hi, target[p]); }
    const int scale = eleven ? 8 : 1;
    const int top = eleven ? 2047 : 255;
    auto value = [&](int base, int mult, int step) {
        return std::clamp(eleven ? base * 8 + 4 + step * mult * 8 : base + step * mult, 0, top);
    };
    long bestErr = -1;
    uint64_t bestBits = 0;
    if (hi == lo && !eleven) {   // (one value: table 13 has a 0 step)
        uint64_t v = (uint64_t)hi << 56 | (uint64_t)1 << 52 | (uint64_t)13 << 48;
        for (int n = 0; n < 16; ++n) v |= (uint64_t)4 << (45 - 3 * n);
        return v;
    }
    const int mid = eleven ? std::clamp(((lo + hi) / 2 - 4) / 8, 0, 255) : (lo + hi + 1) / 2;
    for (int t = 0; t < 16; ++t) {
        const int span = kEac[t][3] < 0 ? kEac[t][7] - kEac[t][3] : 1;   // (from the lowest step to the highest)
        const int guess = std::max(1, (int)std::lround((double)(hi - lo) / (span * scale)));
        for (int mult = std::max(1, guess - 1); mult <= std::min(15, guess + 1); ++mult)
            for (int bd = -2; bd <= 2; ++bd) {
                const int base = std::clamp(mid + bd, 0, 255);
                long err = 0;
                uint64_t idx = 0;
                for (int p = 0; p < 16; ++p) {
                    long best = 1L << 40;
                    int bi = 0;
                    for (int s = 0; s < 8; ++s) {
                        const long d = value(base, mult, kEac[t][s]) - target[p];
                        if (d * d < best) { best = d * d; bi = s; }
                    }
                    err += best;
                    idx |= (uint64_t)bi << (45 - 3 * col(p));
                }
                if (bestErr < 0 || err < bestErr) {
                    bestErr = err;
                    bestBits = (uint64_t)base << 56 | (uint64_t)mult << 52 | (uint64_t)t << 48 | idx;
                }
            }
    }
    return bestBits;
}

void decodeEac(uint64_t v, bool eleven, int out[16]) {
    const int base = (int)(v >> 56 & 255), mult = (int)(v >> 52 & 15), t = (int)(v >> 48 & 15);
    for (int p = 0; p < 16; ++p) {
        const int s = kEac[t][v >> (45 - 3 * col(p)) & 7];
        if (eleven) out[p] = std::clamp(mult ? base * 8 + 4 + s * mult * 8 : base * 8 + 4 + s, 0, 2047);
        else out[p] = clamp255(base + s * mult);
    }
}

} // namespace

void encodeETC2(const uint8_t px[64], uint8_t out[16]) {
    int a[16];
    for (int p = 0; p < 16; ++p) a[p] = px[p * 4 + 3];
    putBE(encodeEac(a, false), out);
    putBE(encodeEtcColour(px), out + 8);
}

void decodeETC2(const uint8_t in[16], uint8_t px[64]) {
    int a[16];
    decodeEac(getBE(in), false, a);
    decodeEtcColour(getBE(in + 8), px);
    for (int p = 0; p < 16; ++p) px[p * 4 + 3] = (uint8_t)a[p];
}

void encodeEAC11(const uint8_t values[16], uint8_t out[8]) {
    int t[16];
    for (int p = 0; p < 16; ++p) t[p] = (values[p] * 2047 + 127) / 255;
    putBE(encodeEac(t, true), out);
}

void decodeEAC11(const uint8_t in[8], uint8_t values[16]) {
    int v[16];
    decodeEac(getBE(in), true, v);
    for (int p = 0; p < 16; ++p) values[p] = (uint8_t)((v[p] * 255 + 1023) / 2047);
}

// ---------------------------------------------------------------- whole pictures
bool packable(int w, int h) { return w >= 4 && h >= 4 && w <= 8192 && h <= 8192 && w % 4 == 0 && h % 4 == 0; }

bool looksLikeNormalMap(const std::string& path) {
    std::string name = path.substr(path.find_last_of("/\\") + 1);
    for (char& c : name) c = (char)std::tolower((unsigned char)c);
    const size_t dot = name.find_last_of('.');
    const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
    auto ends = [&](const char* s) { const size_t n = std::strlen(s); return stem.size() >= n && stem.compare(stem.size() - n, n, s) == 0; };
    return stem.find("normal") != std::string::npos || ends("_n") || ends("_nrm") || ends("-n");
}

size_t levelSize(Format, int w, int h) { return (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4) * 16; }

unsigned glFormat(Format f) {
    switch (f) {
        case BC7:     return 0x8E8C;   // GL_COMPRESSED_RGBA_BPTC_UNORM
        case BC5:     return 0x8DBD;   // GL_COMPRESSED_RG_RGTC2
        case ETC2:    return 0x9278;   // GL_COMPRESSED_RGBA8_ETC2_EAC
        case EACRG11: return 0x9272;   // GL_COMPRESSED_RG11_EAC
        default:      return 0;
    }
}

Format pick(bool normalMap, bool bc7, bool bc5, bool etc2) {
    if (normalMap) return bc5 ? BC5 : etc2 ? EACRG11 : None;
    return bc7 ? BC7 : etc2 ? ETC2 : None;
}

void buildMips(const uint8_t* rgba, int w, int h, std::vector<std::vector<uint8_t>>& out, std::vector<int>& ws,
               std::vector<int>& hs) {
    out.clear(); ws.clear(); hs.clear();
    out.emplace_back(rgba, rgba + (size_t)w * h * 4);
    ws.push_back(w); hs.push_back(h);
    while (w > 1 || h > 1) {
        const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        std::vector<uint8_t> next((size_t)nw * nh * 4);
        const std::vector<uint8_t>& prev = out.back();
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    int sum = 0, n = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(w - 1, x * 2 + dx), sy = std::min(h - 1, y * 2 + dy);
                            sum += prev[((size_t)sy * w + sx) * 4 + c];
                            ++n;
                        }
                    next[((size_t)y * nw + x) * 4 + c] = (uint8_t)((sum + n / 2) / n);
                }
        out.push_back(std::move(next));
        w = nw; h = nh;
        ws.push_back(w); hs.push_back(h);
    }
}

namespace {

void encodeLevel(Format f, const std::vector<uint8_t>& px, int w, int h, std::vector<uint8_t>& out) {
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            uint8_t block[64];
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {   // (a level under 4 pixels repeats its edge)
                    const int sx = std::min(w - 1, bx * 4 + x), sy = std::min(h - 1, by * 4 + y);
                    std::memcpy(block + (y * 4 + x) * 4, &px[((size_t)sy * w + sx) * 4], 4);
                }
            uint8_t enc[16];
            if (f == BC7) encodeBC7(block, enc);
            else if (f == ETC2) encodeETC2(block, enc);
            else {
                uint8_t r[16], g[16];
                for (int p = 0; p < 16; ++p) { r[p] = block[p * 4]; g[p] = block[p * 4 + 1]; }
                if (f == BC5) { encodeBC4(r, enc); encodeBC4(g, enc + 8); }
                else { encodeEAC11(r, enc); encodeEAC11(g, enc + 8); }
            }
            out.insert(out.end(), enc, enc + 16);
        }
}

void put32(std::vector<uint8_t>& out, uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (8 * i))); }
bool get32(const std::vector<uint8_t>& in, size_t& at, uint32_t& v) {
    if (at + 4 > in.size()) return false;
    v = (uint32_t)in[at] | (uint32_t)in[at + 1] << 8 | (uint32_t)in[at + 2] << 16 | (uint32_t)in[at + 3] << 24;
    at += 4;
    return true;
}

} // namespace

// The file: "GBTX", version, width, height, flags (1 = normal map), how many formats;
// then for each format: its number, how many levels, each level's size; then all the
// levels' bytes, format by format, biggest level first.
std::vector<uint8_t> pack(const uint8_t* rgba, int w, int h, bool normalMap) {
    if (!rgba || !packable(w, h)) return {};
    std::vector<std::vector<uint8_t>> mips;
    std::vector<int> ws, hs;
    buildMips(rgba, w, h, mips, ws, hs);
    const Format formats[2] = {normalMap ? BC5 : BC7, normalMap ? EACRG11 : ETC2};
    std::vector<uint8_t> out(kMagic, kMagic + 4);
    put32(out, kVersion);
    put32(out, (uint32_t)w);
    put32(out, (uint32_t)h);
    put32(out, normalMap ? 1u : 0u);
    put32(out, 2);
    for (Format f : formats) {
        put32(out, f);
        put32(out, (uint32_t)mips.size());
        for (size_t l = 0; l < mips.size(); ++l) put32(out, (uint32_t)levelSize(f, ws[l], hs[l]));
    }
    for (Format f : formats)
        for (size_t l = 0; l < mips.size(); ++l) encodeLevel(f, mips[l], ws[l], hs[l], out);
    return out;
}

bool read(const std::vector<uint8_t>& file, Format want, View& out) {
    out = View{};
    if (file.size() < 24 || std::memcmp(file.data(), kMagic, 4) != 0) return false;
    size_t at = 4;
    uint32_t version = 0, w = 0, h = 0, flags = 0, count = 0;
    if (!get32(file, at, version) || version != kVersion || !get32(file, at, w) || !get32(file, at, h) ||
        !get32(file, at, flags) || !get32(file, at, count) || !packable((int)w, (int)h) || count > 8)
        return false;
    // (First the table of every format, then the bytes in the same order.)
    struct Entry { uint32_t format, levels; size_t sizesAt; };
    Entry entries[8];
    for (uint32_t i = 0; i < count; ++i) {
        if (!get32(file, at, entries[i].format) || !get32(file, at, entries[i].levels) || entries[i].levels > 14) return false;
        entries[i].sizesAt = at;
        at += 4 * (size_t)entries[i].levels;
        if (at > file.size()) return false;
    }
    size_t data = at;
    for (uint32_t i = 0; i < count; ++i) {
        size_t s = entries[i].sizesAt;
        const bool mine = entries[i].format == want;
        int lw = (int)w, lh = (int)h;
        for (uint32_t l = 0; l < entries[i].levels; ++l) {
            uint32_t size = 0;
            get32(file, s, size);
            if (data + size > file.size() || size != levelSize((Format)entries[i].format, lw, lh)) return false;
            if (mine) out.levels.push_back({file.data() + data, size, lw, lh});
            data += size;
            lw = std::max(1, lw / 2);
            lh = std::max(1, lh / 2);
        }
        if (mine) {
            out.width = (int)w;
            out.height = (int)h;
            out.normalMap = flags & 1u;
            out.format = want;
            return !out.levels.empty();
        }
    }
    return false;
}

bool has(const std::vector<uint8_t>& file, Format f) {
    View v;
    return read(file, f, v);
}

} // namespace TexturePack
