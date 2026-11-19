#include "Terrain.h"

#include <algorithm>
#include <cmath>

using json = nlohmann::json;

namespace {

// --- base64 (the packed heights and materials in the saved file) --------------
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string toBase64(const std::string& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16;
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8);
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += '=';
    }
    return out;
}

std::string fromBase64(const std::string& in) {
    int val[256];
    std::fill(val, val + 256, -1);
    for (int i = 0; i < 64; ++i) val[(uint8_t)kB64[i]] = i;
    std::string out;
    out.reserve(in.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        int d = val[(uint8_t)c];
        if (d < 0) continue;   // '=' padding, line breaks
        acc = (acc << 6) | (uint32_t)d;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((acc >> bits) & 0xFF); }
    }
    return out;
}

// --- value noise for Generate --------------------------------------------------
float hash2(int x, int z, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF);
}
float noise2(float x, float z, uint32_t seed) {
    int xi = (int)std::floor(x), zi = (int)std::floor(z);
    float fx = x - xi, fz = z - zi;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    float a = hash2(xi, zi, seed), b = hash2(xi + 1, zi, seed), c = hash2(xi, zi + 1, seed), d = hash2(xi + 1, zi + 1, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fz;
}
float fbm(float x, float z, uint32_t seed) {
    float v = 0, amp = 0.5f;
    for (int o = 0; o < 5; ++o) { v += amp * noise2(x, z, seed + o * 101u); x *= 2.03f; z *= 2.03f; amp *= 0.5f; }
    return v;   // about 0..1
}

} // namespace

void Terrain::touched() {
    ++m_version;
    if (m_h.empty()) { m_lo = m_hi = 0.0f; return; }
    auto mm = std::minmax_element(m_h.begin(), m_h.end());
    m_lo = *mm.first;
    m_hi = *mm.second;
}

void Terrain::create(int cells, float cellSize, float height, TerrainMaterial m) {
    m_cells = std::clamp(cells, kMinCells, kMaxCells);
    m_cell  = std::clamp(cellSize, 1.0f, 16.0f);
    const size_t n = (size_t)points() * points();
    m_h.assign(n, std::clamp(height, kMinHeight, kMaxHeight));
    m_mat.assign(n, (uint8_t)m);
    touched();
}

void Terrain::clear() {
    m_cells = 0;
    m_h.clear();
    m_mat.clear();
    touched();
}

void Terrain::copyFrom(const Terrain& other) {
    if (&other == this) return;
    m_cells = other.m_cells;
    m_cell  = other.m_cell;
    m_h     = other.m_h;
    m_mat   = other.m_mat;
    m_version = std::max(m_version, other.m_version);   // (never back to a number the renderer already saw)
    touched();
}

bool Terrain::sameAs(const Terrain& other) const {
    return m_cells == other.m_cells && m_cell == other.m_cell && m_h == other.m_h && m_mat == other.m_mat;
}

void Terrain::generate(int cells, float cellSize, uint32_t seed, float hills) {
    create(cells, cellSize, 0.0f, TerrainMaterial::Grass);
    hills = std::clamp(hills, 0.0f, 1.0f);
    const float amp = 6.0f + hills * 70.0f;     // studs from the lowest valley to the highest hill
    const float scale = 1.0f / 160.0f;          // how wide the hills are
    const int np = points();
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < np; ++i) {
            glm::vec3 p = pointPos(i, k);
            float n = fbm(p.x * scale + 50.0f, p.z * scale + 50.0f, seed);
            // Flatter in the middle, so there's somewhere to put the spawn.
            float mid = std::min(1.0f, std::sqrt(p.x * p.x + p.z * p.z) / (halfWidth() * 0.35f));
            m_h[(size_t)k * np + i] = (n - 0.35f) * amp * (0.25f + 0.75f * mid);
        }
    // Paint it: sand down low, rock on steep slopes, snow on high tops, grass elsewhere.
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < np; ++i) {
            float h = height(i, k);
            float up = pointNormal(i, k).y;
            TerrainMaterial m = TerrainMaterial::Grass;
            if (h < 1.0f) m = TerrainMaterial::Sand;
            if (h > amp * 0.45f) m = TerrainMaterial::Snow;
            if (up < 0.72f) m = TerrainMaterial::Rock;
            else if (up < 0.85f && m == TerrainMaterial::Grass) m = TerrainMaterial::Dirt;
            m_mat[(size_t)k * np + i] = (uint8_t)m;
        }
    touched();
}

glm::vec3 Terrain::pointPos(int i, int k) const {
    const float hw = halfWidth();
    return {-hw + i * m_cell, height(i, k), -hw + k * m_cell};
}

glm::vec3 Terrain::pointNormal(int i, int k) const {
    const int n = points() - 1;
    float l = height(std::max(i - 1, 0), k), r = height(std::min(i + 1, n), k);
    float b = height(i, std::max(k - 1, 0)), f = height(i, std::min(k + 1, n));
    float dx = (r - l) / (m_cell * (std::min(i + 1, n) - std::max(i - 1, 0)));
    float dz = (f - b) / (m_cell * (std::min(k + 1, n) - std::max(k - 1, 0)));
    return glm::normalize(glm::vec3(-dx, 1.0f, -dz));
}

bool Terrain::cellAt(float x, float z, int& i, int& k, float& fx, float& fz) const {
    if (empty()) return false;
    const float hw = halfWidth();
    float gx = (x + hw) / m_cell, gz = (z + hw) / m_cell;
    if (!(gx >= 0.0f && gz >= 0.0f && gx <= (float)m_cells && gz <= (float)m_cells)) return false;
    i = std::min((int)gx, m_cells - 1);
    k = std::min((int)gz, m_cells - 1);
    fx = gx - i;
    fz = gz - k;
    return true;
}

// Each square is two triangles, split from corner (i, k) to corner (i+1, k+1).
bool Terrain::heightAt(float x, float z, float& h) const {
    int i, k;
    float fx, fz;
    if (!cellAt(x, z, i, k, fx, fz)) return false;
    float a = height(i, k), b = height(i + 1, k), c = height(i, k + 1), d = height(i + 1, k + 1);
    h = fx >= fz ? a + (b - a) * fx + (d - b) * fz
                 : a + (d - c) * fx + (c - a) * fz;
    return true;
}

glm::vec3 Terrain::normalAt(float x, float z) const {
    int i, k;
    float fx, fz;
    if (!cellAt(x, z, i, k, fx, fz)) return {0, 1, 0};
    float a = height(i, k), b = height(i + 1, k), c = height(i, k + 1), d = height(i + 1, k + 1);
    float dx = fx >= fz ? (b - a) : (d - c);
    float dz = fx >= fz ? (d - b) : (c - a);
    return glm::normalize(glm::vec3(-dx / m_cell, 1.0f, -dz / m_cell));
}

TerrainMaterial Terrain::materialAt(float x, float z) const {
    int i, k;
    float fx, fz;
    if (!cellAt(x, z, i, k, fx, fz)) return TerrainMaterial::Grass;
    return material(i + (fx >= 0.5f), k + (fz >= 0.5f));
}

bool Terrain::solidAt(const glm::vec3& p) const {
    float h;
    return heightAt(p.x, p.z, h) && p.y < h;
}

bool Terrain::highestUnder(float x0, float z0, float x1, float z1, float& h) const {
    bool any = false;
    float best = -1e30f;
    auto take = [&](float x, float z) {
        float y;
        if (heightAt(x, z, y)) { best = std::max(best, y); any = true; }
    };
    // The corners and middle of the footprint, plus every grid point inside it
    // (a peak smaller than the footprint still holds it up).
    take(x0, z0); take(x1, z0); take(x0, z1); take(x1, z1); take((x0 + x1) * 0.5f, (z0 + z1) * 0.5f);
    if (!empty()) {
        const float hw = halfWidth();
        int i0 = std::max(0, (int)std::ceil((x0 + hw) / m_cell)), i1 = std::min(m_cells, (int)std::floor((x1 + hw) / m_cell));
        int k0 = std::max(0, (int)std::ceil((z0 + hw) / m_cell)), k1 = std::min(m_cells, (int)std::floor((z1 + hw) / m_cell));
        for (int k = k0; k <= k1; ++k)
            for (int i = i0; i <= i1; ++i) { best = std::max(best, height(i, k)); any = true; }
    }
    h = best;
    return any;
}

bool Terrain::raycast(const glm::vec3& ro, const glm::vec3& rd, float maxDist, float& tHit, glm::vec3* normal) const {
    if (empty()) return false;
    // Only the part of the ray inside the terrain's box needs checking.
    const float hw = halfWidth();
    glm::vec3 bmin(-hw, m_lo - 0.5f, -hw), bmax(hw, m_hi + 0.5f, hw);
    float t0 = 0.0f, t1 = maxDist;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(rd[a]) < 1e-9f) {
            if (ro[a] < bmin[a] || ro[a] > bmax[a]) return false;
            continue;
        }
        float ta = (bmin[a] - ro[a]) / rd[a], tb = (bmax[a] - ro[a]) / rd[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    // March in small steps until the ray goes under the ground, then narrow it down.
    auto below = [&](float t, bool& known) {
        glm::vec3 p = ro + rd * t;
        float h;
        known = heightAt(p.x, p.z, h);
        return known && p.y <= h;
    };
    const float step = m_cell * 0.25f;
    bool known;
    float prev = t0;
    if (below(t0, known)) return false;   // starting underground: nothing to hit
    for (float t = t0 + step;; t += step) {
        const float tc = std::min(t, t1);
        if (below(tc, known)) {
            float lo = prev, hi = tc;
            for (int it = 0; it < 20; ++it) {
                float m = (lo + hi) * 0.5f;
                bool kn;
                (below(m, kn) ? hi : lo) = m;
            }
            tHit = hi;
            if (normal) { glm::vec3 p = ro + rd * hi; *normal = normalAt(p.x, p.z); }
            return true;
        }
        prev = tc;
        if (tc >= t1) return false;
    }
}

float Terrain::friction(TerrainMaterial m) const {
    switch (m) {
        case TerrainMaterial::Rock: return 0.7f;
        case TerrainMaterial::Sand: return 0.5f;
        case TerrainMaterial::Snow: return 0.3f;
        case TerrainMaterial::Mud:  return 0.35f;
        default:                    return 0.6f;   // grass, dirt
    }
}

void Terrain::setHeight(int i, int k, float h) {
    if (empty() || i < 0 || k < 0 || i >= points() || k >= points()) return;
    m_h[(size_t)k * points() + i] = std::clamp(h, kMinHeight, kMaxHeight);
    touched();
}

void Terrain::setMaterial(int i, int k, TerrainMaterial m) {
    if (empty() || i < 0 || k < 0 || i >= points() || k >= points()) return;
    m_mat[(size_t)k * points() + i] = (uint8_t)m;
    touched();
}

void Terrain::brush(Brush b, const glm::vec3& c, float radius, float strength, TerrainMaterial m) {
    if (empty() || radius <= 0.0f) return;
    if (b == Brush::Erode) {   // (rain where the brush is: a few hundred drops a dab)
        static uint32_t seed = 1;
        erode(std::clamp(strength, 0.0f, 1.0f), ++seed, glm::vec2(c.x, c.z), radius);
        return;
    }
    const float hw = halfWidth();
    const int np = points();
    int i0 = std::max(0, (int)std::floor((c.x - radius + hw) / m_cell)), i1 = std::min(np - 1, (int)std::ceil((c.x + radius + hw) / m_cell));
    int k0 = std::max(0, (int)std::floor((c.z - radius + hw) / m_cell)), k1 = std::min(np - 1, (int)std::ceil((c.z + radius + hw) / m_cell));
    if (i0 > i1 || k0 > k1) return;
    const std::vector<float> before = b == Brush::Smooth ? m_h : std::vector<float>();
    for (int k = k0; k <= k1; ++k)
        for (int i = i0; i <= i1; ++i) {
            glm::vec3 p = pointPos(i, k);
            float d = std::sqrt((p.x - c.x) * (p.x - c.x) + (p.z - c.z) * (p.z - c.z)) / radius;
            if (d > 1.0f) continue;
            float w = (1.0f - d * d);
            w *= w;   // soft edge
            float& h = m_h[(size_t)k * np + i];
            switch (b) {
                case Brush::Raise:   h += strength * w; break;
                case Brush::Lower:   h -= strength * w; break;
                case Brush::Flatten: h += (c.y - h) * std::clamp(strength * w, 0.0f, 1.0f); break;
                case Brush::Smooth: {
                    float sum = 0.0f;
                    int n = 0;
                    for (int dk = -1; dk <= 1; ++dk)
                        for (int di = -1; di <= 1; ++di) {
                            int ii = i + di, kk = k + dk;
                            if (ii < 0 || kk < 0 || ii >= np || kk >= np) continue;
                            sum += before[(size_t)kk * np + ii];
                            ++n;
                        }
                    h += (sum / n - h) * std::clamp(strength * w, 0.0f, 1.0f);
                    break;
                }
                case Brush::Paint: m_mat[(size_t)k * np + i] = (uint8_t)m; break;
                case Brush::Erode: break;
            }
            h = std::clamp(h, kMinHeight, kMaxHeight);
        }
    touched();
}

void Terrain::fillBox(const glm::vec3& mn, const glm::vec3& mx, TerrainMaterial m) {
    if (empty()) return;
    const int np = points();
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < np; ++i) {
            glm::vec3 p = pointPos(i, k);
            if (p.x < mn.x || p.x > mx.x || p.z < mn.z || p.z > mx.z) continue;
            float& h = m_h[(size_t)k * np + i];
            h = std::clamp(std::max(h, mx.y), kMinHeight, kMaxHeight);
            m_mat[(size_t)k * np + i] = (uint8_t)m;
        }
    touched();
}

void Terrain::digBox(const glm::vec3& mn, const glm::vec3& mx) {
    if (empty()) return;
    const int np = points();
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < np; ++i) {
            glm::vec3 p = pointPos(i, k);
            if (p.x < mn.x || p.x > mx.x || p.z < mn.z || p.z > mx.z) continue;
            float& h = m_h[(size_t)k * np + i];
            if (h > mn.y) h = std::clamp(mn.y, kMinHeight, kMaxHeight);
        }
    touched();
}

void Terrain::fillBall(const glm::vec3& c, float r, TerrainMaterial m) {
    if (empty() || r <= 0.0f) return;
    const int np = points();
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < np; ++i) {
            glm::vec3 p = pointPos(i, k);
            float d2 = (p.x - c.x) * (p.x - c.x) + (p.z - c.z) * (p.z - c.z);
            if (d2 > r * r) continue;
            float top = c.y + std::sqrt(r * r - d2);
            float& h = m_h[(size_t)k * np + i];
            if (h >= top) continue;
            h = std::clamp(top, kMinHeight, kMaxHeight);
            m_mat[(size_t)k * np + i] = (uint8_t)m;
        }
    touched();
}

void Terrain::digBall(const glm::vec3& c, float r) {
    if (empty() || r <= 0.0f) return;
    const int np = points();
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < np; ++i) {
            glm::vec3 p = pointPos(i, k);
            float d2 = (p.x - c.x) * (p.x - c.x) + (p.z - c.z) * (p.z - c.z);
            if (d2 > r * r) continue;
            float bottom = c.y - std::sqrt(r * r - d2);
            float& h = m_h[(size_t)k * np + i];
            if (h > bottom) h = std::clamp(bottom, kMinHeight, kMaxHeight);
        }
    touched();
}

bool Terrain::parseMaterial(const std::string& s, TerrainMaterial& out) {
    for (int i = 0; i < kTerrainMaterialCount; ++i)
        if (s == kTerrainMaterialNames[i]) { out = (TerrainMaterial)i; return true; }
    // Roblox's other terrain materials: the closest one we have.
    if (s == "LeafyGrass")                                   { out = TerrainMaterial::Grass; return true; }
    if (s == "Ground" || s == "Pavement" || s == "Cobblestone") { out = TerrainMaterial::Dirt; return true; }
    if (s == "Sandstone" || s == "Salt")                     { out = TerrainMaterial::Sand; return true; }
    if (s == "Slate" || s == "Basalt" || s == "Granite" || s == "Limestone" || s == "CrackedLava" || s == "Asphalt" || s == "Concrete")
        { out = TerrainMaterial::Rock; return true; }
    if (s == "Glacier" || s == "Ice")                        { out = TerrainMaterial::Snow; return true; }
    return false;
}

json Terrain::toJson() const {
    if (empty()) return nullptr;
    // Heights to a sixteenth of a stud, as little-endian 16-bit numbers.
    std::string hb;
    hb.reserve(m_h.size() * 2);
    for (float h : m_h) {
        int v = (int)std::lround(std::clamp(h, -2047.0f, 2047.0f) * 16.0f);
        uint16_t u = (uint16_t)(int16_t)v;
        hb += (char)(u & 0xFF);
        hb += (char)(u >> 8);
    }
    return {{"cells", m_cells}, {"cell", m_cell},
            {"heights", toBase64(hb)},
            {"materials", toBase64(std::string(m_mat.begin(), m_mat.end()))}};
}

void Terrain::fromJson(const json& j) {
    if (!j.is_object() || !j.contains("cells")) { clear(); return; }
    create(j.value("cells", 0), j.value("cell", kDefaultCell), 0.0f, TerrainMaterial::Grass);
    const size_t n = m_h.size();
    std::string hb = fromBase64(j.value("heights", std::string()));
    std::string mb = fromBase64(j.value("materials", std::string()));
    for (size_t p = 0; p < n && p * 2 + 1 < hb.size(); ++p) {
        int16_t v = (int16_t)((uint8_t)hb[p * 2] | ((uint8_t)hb[p * 2 + 1] << 8));
        m_h[p] = std::clamp(v / 16.0f, kMinHeight, kMaxHeight);
    }
    for (size_t p = 0; p < n && p < mb.size(); ++p)
        m_mat[p] = (uint8_t)std::min<int>((uint8_t)mb[p], kTerrainMaterialCount - 1);
    touched();
}
