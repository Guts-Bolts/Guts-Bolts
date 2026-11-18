// Terrain shaping: the kinds of land Generate makes, rain erosion and painting the
// ground by its shape (see Terrain.h).
#include "Terrain.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

float hash(int x, int z, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFF) / (float)0xFFFFFF;
}

float noise(float x, float z, uint32_t seed) {
    const float fx = std::floor(x), fz = std::floor(z);
    const int ix = (int)fx, iz = (int)fz;
    float tx = x - fx, tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    const float a = hash(ix, iz, seed), b = hash(ix + 1, iz, seed);
    const float c = hash(ix, iz + 1, seed), d = hash(ix + 1, iz + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}

// Several sizes of bumps on top of each other, 0..1.
float fbm(float x, float z, uint32_t seed, int octaves = 5) {
    float v = 0.0f, amp = 0.5f, total = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        v += amp * noise(x, z, seed + (uint32_t)o * 101u);
        total += amp;
        x = x * 2.03f + 17.1f;
        z = z * 2.03f + 9.7f;
        amp *= 0.5f;
    }
    return v / total;
}

// Sharp ridges (mountain crests), 0..1: the valleys of the noise turned upside down.
// Each finer layer is stronger where the ridge already is high, so peaks get craggy and
// valleys stay smooth, like real mountains.
float ridged(float x, float z, uint32_t seed, int octaves = 6) {
    float v = 0.0f, amp = 0.5f, weight = 1.0f, total = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        float n = 1.0f - std::fabs(noise(x, z, seed + (uint32_t)o * 131u) * 2.0f - 1.0f);
        n *= n;
        n *= weight;
        weight = std::clamp(n * 1.6f, 0.0f, 1.0f);
        v += n * amp;
        total += amp;
        x = x * 2.1f + 5.3f;
        z = z * 2.1f + 1.9f;
        amp *= 0.5f;
    }
    return v / total;
}

float smooth01(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void Terrain::generateStyle(TerrainStyle style, int cells, float cellSize, uint32_t seed, float hills) {
    if (style == TerrainStyle::Hills) { generate(cells, cellSize, seed, hills); return; }
    create(cells, cellSize, 0.0f, TerrainMaterial::Grass);
    hills = std::clamp(hills, 0.0f, 1.0f);
    const int np = points();
    const float hw = halfWidth();
    // The way the wind blew the dunes (each seed its own).
    const float windAngle = hash(3, 7, seed) * 6.2831853f;
    const float wx = std::cos(windAngle), wz = std::sin(windAngle);
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < np; ++i) {
            const glm::vec3 p = pointPos(i, k);
            const float r = std::sqrt(p.x * p.x + p.z * p.z) / hw;   // 0 middle .. 1 edge (more in the corners)
            // Flatter in the middle, so there's somewhere to put the spawn.
            const float mid = 0.2f + 0.8f * smooth01(0.06f, 0.3f, r);
            float h = 0.0f;
            switch (style) {
                case TerrainStyle::Mountains: {
                    const float amp = 40.0f + hills * 160.0f;
                    float n = ridged(p.x / 420.0f + 30.0f, p.z / 420.0f + 30.0f, seed) * 0.8f +
                              fbm(p.x / 230.0f, p.z / 230.0f, seed + 5u) * 0.2f;
                    n = std::pow(std::clamp(n, 0.0f, 1.0f), 1.5f);
                    h = (n - 0.08f) * amp * mid;
                    break;
                }
                case TerrainStyle::Islands: {
                    const float amp = 25.0f + hills * 55.0f;
                    // Bendy coastlines: the noise is looked up a little off to one side.
                    const float warp = fbm(p.x / 300.0f, p.z / 300.0f, seed + 11u) * 2.0f;
                    float land = fbm(p.x / 240.0f + warp, p.z / 240.0f - warp, seed) - r * 0.75f + 0.12f;
                    land += 0.35f * (1.0f - smooth01(0.0f, 0.22f, r));   // (always an island in the middle)
                    h = land > 0.0f ? std::pow(land, 1.25f) * amp * 1.8f : std::max(land * 60.0f, -24.0f);
                    if (land > 0.0f) h += 0.6f;   // (beaches stand just out of the water)
                    break;
                }
                case TerrainStyle::Canyons: {
                    const float amp = 25.0f + hills * 55.0f;
                    // Mesas: the land steps up in flat layers, with steep cliffs between.
                    const float base = fbm(p.x / 320.0f, p.z / 320.0f, seed);
                    const float t = base * 5.0f;
                    const float stepped = std::floor(t) + smooth01(0.2f, 0.8f, t - std::floor(t));
                    // Winding canyons: where a second noise crosses its middle.
                    const float river = std::fabs(fbm(p.x / 380.0f, p.z / 380.0f, seed + 9u, 4) * 2.0f - 1.0f);
                    const float bank = smooth01(0.02f, 0.11f, river);
                    h = (stepped / 5.0f * amp) * bank - (1.0f - bank) * amp * 0.12f;
                    h *= mid;
                    break;
                }
                case TerrainStyle::Dunes: {
                    const float amp = 6.0f + hills * 26.0f;
                    const float along = (p.x * wx + p.z * wz) / (55.0f + 40.0f * hills) +
                                        fbm(p.x / 220.0f, p.z / 220.0f, seed + 3u) * 4.0f;
                    const float s = along - std::floor(along);
                    // A long gentle side up from the wind and a steep slip face down the other.
                    float crest = s < 0.78f ? s / 0.78f : (1.0f - s) / 0.22f;
                    crest = crest * crest * (3.0f - 2.0f * crest);
                    const float size = 0.5f + 0.7f * fbm(p.x / 420.0f, p.z / 420.0f, seed + 4u);
                    h = (crest * amp * size + fbm(p.x / 90.0f, p.z / 90.0f, seed + 6u) * 2.0f) * mid;
                    break;
                }
                case TerrainStyle::Plains:
                default: {
                    const float amp = 2.0f + hills * 9.0f;
                    h = (fbm(p.x / 520.0f, p.z / 520.0f, seed) - 0.45f) * amp * 2.0f +
                        (fbm(p.x / 95.0f, p.z / 95.0f, seed + 2u) - 0.5f) * amp * 0.5f;
                    h *= mid;
                    break;
                }
            }
            m_h[(size_t)k * np + i] = std::clamp(h, kMinHeight, kMaxHeight);
        }
    touched();
    // Weather it a little (or a lot, for mountains), then paint it by its shape.
    switch (style) {
        case TerrainStyle::Mountains: erode(0.7f, seed); break;
        case TerrainStyle::Islands:   erode(0.35f, seed); break;
        case TerrainStyle::Canyons:   erode(0.15f, seed); break;
        case TerrainStyle::Plains:    erode(0.1f, seed); break;
        default: break;
    }
    autoPaint(seed);
    if (style == TerrainStyle::Dunes || style == TerrainStyle::Canyons) {
        // A desert: the green goes, dunes are all sand, canyon tops are dry dirt and sand.
        for (int k = 0; k < np; ++k)
            for (int i = 0; i < np; ++i) {
                uint8_t& m = m_mat[(size_t)k * np + i];
                if (m == (uint8_t)TerrainMaterial::Rock || m == (uint8_t)TerrainMaterial::Snow) continue;
                const glm::vec3 p = pointPos(i, k);
                if (style == TerrainStyle::Dunes) m = (uint8_t)TerrainMaterial::Sand;
                else m = (uint8_t)(fbm(p.x / 70.0f, p.z / 70.0f, seed + 8u, 3) > 0.6f ? TerrainMaterial::Dirt : TerrainMaterial::Sand);
            }
        touched();
    }
}

void Terrain::erode(float amount, uint32_t seed, glm::vec2 center, float radius) {
    if (empty() || amount <= 0.0f) return;
    amount = std::min(amount, 1.0f);
    const int np = points();
    if (np < 4) return;
    // Work in grid squares (a height of 1 = one square's width), so the same rain does
    // the same thing on any size of square.
    std::vector<float> H(m_h.size());
    for (size_t p = 0; p < H.size(); ++p) H[p] = m_h[p] / m_cell;
    const float hw = halfWidth();
    const bool local = radius > 0.0f;
    const float cx = (center.x + hw) / m_cell, cz = (center.y + hw) / m_cell;   // in grid points
    const float rr = radius / m_cell;
    // How much it rains: about one drop per point for the whole map at amount 1.
    const float area = local ? 3.14159f * rr * rr : (float)(np * np);
    const int drops = std::max(1, (int)(area * amount * (local ? 6.0f : 1.2f)));
    const float inertia = 0.05f, capacity = 4.0f, minCapacity = 0.01f;
    const float depositRate = 0.3f, erodeRate = 0.3f, evaporate = 0.02f, gravity = 4.0f;
    const int lifetime = 45;
    // Each drop wears away a little round patch, not one point (no pits).
    const int brushR = 2;
    std::vector<std::pair<int, float>> brushOffsets;   // (dk * np + di, weight)
    {
        float total = 0.0f;
        for (int dk = -brushR; dk <= brushR; ++dk)
            for (int di = -brushR; di <= brushR; ++di) {
                const float d = std::sqrt((float)(di * di + dk * dk));
                if (d > brushR) continue;
                total += 1.0f - d / (brushR + 0.001f);
            }
        for (int dk = -brushR; dk <= brushR; ++dk)
            for (int di = -brushR; di <= brushR; ++di) {
                const float d = std::sqrt((float)(di * di + dk * dk));
                if (d > brushR) continue;
                brushOffsets.push_back({dk * np + di, (1.0f - d / (brushR + 0.001f)) / total});
            }
    }
    uint32_t rng = seed * 747796405u + 2891336453u;
    auto rnd = [&]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float)(rng & 0xFFFFFF) / (float)0x1000000;
    };
    auto sample = [&](float x, float z, float& gx, float& gz) {
        const int i = std::clamp((int)x, 0, np - 2), k = std::clamp((int)z, 0, np - 2);
        const float u = x - i, v = z - k;
        const size_t p = (size_t)k * np + i;
        const float a = H[p], b = H[p + 1], c = H[p + np], d = H[p + np + 1];
        gx = (b - a) * (1.0f - v) + (d - c) * v;
        gz = (c - a) * (1.0f - u) + (d - b) * u;
        return a * (1.0f - u) * (1.0f - v) + b * u * (1.0f - v) + c * (1.0f - u) * v + d * u * v;
    };
    // The brush's edge fades out (local rain blends into the ground around it).
    auto fade = [&](float x, float z) {
        if (!local) return 1.0f;
        const float d = std::sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz)) / std::max(rr, 0.5f);
        return d >= 1.0f ? 0.0f : (1.0f - d * d);
    };
    for (int n = 0; n < drops; ++n) {
        float x, z;
        if (local) {
            const float a = rnd() * 6.2831853f, d = std::sqrt(rnd()) * rr;
            x = cx + std::cos(a) * d;
            z = cz + std::sin(a) * d;
        } else {
            x = 1.0f + rnd() * (np - 3);
            z = 1.0f + rnd() * (np - 3);
        }
        if (x < 1.0f || z < 1.0f || x > np - 2.0f || z > np - 2.0f) continue;
        float dx = 0.0f, dz = 0.0f, speed = 1.0f, water = 1.0f, sediment = 0.0f;
        for (int life = 0; life < lifetime; ++life) {
            const int i = (int)x, k = (int)z;
            const float u = x - i, v = z - k;
            float gx, gz;
            const float h0 = sample(x, z, gx, gz);
            // Roll downhill, keeping a little of the way it was going.
            dx = dx * inertia - gx * (1.0f - inertia);
            dz = dz * inertia - gz * (1.0f - inertia);
            const float len = std::sqrt(dx * dx + dz * dz);
            if (len < 1e-6f) break;   // (a perfectly flat spot: it soaks in)
            dx /= len; dz /= len;
            x += dx; z += dz;
            if (x < 1.0f || z < 1.0f || x > np - 2.0f || z > np - 2.0f) break;
            float ngx, ngz;
            const float dh = sample(x, z, ngx, ngz) - h0;
            const float f = fade((float)i + u, (float)k + v);
            // Fast, full drops going steeply down can carry a lot of soil.
            const float cap = std::max(-dh * speed * water * capacity, minCapacity);
            const size_t p = (size_t)k * np + i;
            if (sediment > cap || dh > 0.0f) {
                // Too much to carry (or going uphill, into a hollow): drop some here.
                const float drop = dh > 0.0f ? std::min(dh, sediment) : (sediment - cap) * depositRate;
                sediment -= drop;
                const float put = drop * f;
                H[p]          += put * (1.0f - u) * (1.0f - v);
                H[p + 1]      += put * u * (1.0f - v);
                H[p + np]     += put * (1.0f - u) * v;
                H[p + np + 1] += put * u * v;
            } else {
                // Room for more: wear the ground away around here (never more than the drop fell).
                const float take = std::min((cap - sediment) * erodeRate, -dh) * f;
                for (const auto& [off, wgt] : brushOffsets) {
                    const long q = (long)p + off;
                    if (q < 0 || q >= (long)H.size()) continue;
                    H[(size_t)q] -= take * wgt;
                }
                sediment += take;
            }
            speed = std::sqrt(std::max(0.0f, speed * speed - dh * gravity));
            water *= 1.0f - evaporate;
        }
    }
    // Loose ground can't stand steeper than about 40 degrees: it slides down (scree).
    // (Rock cliffs from Generate are kept as cliffs by doing only a few passes.)
    const float talus = 0.85f;   // height difference between neighbours a slope keeps
    const int passes = std::max(1, (int)(amount * 6.0f));
    for (int pass = 0; pass < passes; ++pass)
        for (int k = 1; k < np - 1; ++k)
            for (int i = 1; i < np - 1; ++i) {
                const size_t p = (size_t)k * np + i;
                const float f = fade((float)i, (float)k);
                if (f <= 0.0f) continue;
                const size_t nb[4] = {p - 1, p + 1, p - np, p + np};
                for (size_t q : nb) {
                    const float diff = H[p] - H[q];
                    if (diff > talus) {
                        const float move = (diff - talus) * 0.25f * f;
                        H[p] -= move;
                        H[q] += move;
                    }
                }
            }
    for (size_t p = 0; p < H.size(); ++p)
        if (std::isfinite(H[p])) m_h[p] = std::clamp(H[p] * m_cell, kMinHeight, kMaxHeight);
    touched();
}

void Terrain::autoPaint(uint32_t seed) {
    if (empty()) return;
    const int np = points();
    const float lo = m_lo, hi = m_hi;
    // Snow only on real mountains: the top part of land that rises well above the rest.
    const float rise = hi - std::max(lo, 0.0f);
    const float snowLine = rise > 45.0f ? std::max(lo, 0.0f) + rise * 0.7f : 1e9f;
    const bool sea = lo < -1.0f;   // ground under y = 0: a sea (or lake) to put beaches round
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < np; ++i) {
            const glm::vec3 p = pointPos(i, k);
            const float h = p.y;
            const float up = pointNormal(i, k).y;
            // How much of a hollow this is: lower than the ground around it.
            float around = 0.0f;
            int n = 0;
            for (int dk = -2; dk <= 2; dk += 2)
                for (int di = -2; di <= 2; di += 2) {
                    if (!di && !dk) continue;
                    const int ii = i + di, kk = k + dk;
                    if (ii < 0 || kk < 0 || ii >= np || kk >= np) continue;
                    around += height(ii, kk);
                    ++n;
                }
            const float hollow = n ? around / n - h : 0.0f;
            const float j = noise(p.x / 30.0f, p.z / 30.0f, seed) - 0.5f;   // (ragged edges between grounds)
            TerrainMaterial m;
            if (up < 0.70f + j * 0.08f)                                   m = TerrainMaterial::Rock;
            else if (h > snowLine + j * 10.0f && up > 0.74f)              m = TerrainMaterial::Snow;
            else if (sea && h < 1.6f + j * 1.5f)                          m = TerrainMaterial::Sand;   // beaches and the sea floor
            else if (hollow > 2.5f && up > 0.95f && j < 0.0f)  m = TerrainMaterial::Mud;    // wet hollows
            else if (up < 0.86f + j * 0.04f)                              m = TerrainMaterial::Dirt;
            else m = noise(p.x / 45.0f, p.z / 45.0f, seed + 1u) > 0.82f ? TerrainMaterial::Dirt : TerrainMaterial::Grass;
            m_mat[(size_t)k * np + i] = (uint8_t)m;
        }
    touched();
}
