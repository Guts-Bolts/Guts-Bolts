#include "MeshLod.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace MeshLod {

Level cluster(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx, int cells) {
    Level out;
    if (verts.empty() || idx.size() < 3 || cells < 1) return out;
    glm::vec3 lo = verts[0].position, hi = lo;
    for (const Vertex& v : verts) { lo = glm::min(lo, v.position); hi = glm::max(hi, v.position); }
    const float longest = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-6f});
    const float cell = longest / (float)cells;

    // Each corner's cell, and which of the 6 ways it faces.
    auto cellOf = [&](const glm::vec3& p) -> uint32_t {
        const glm::ivec3 c = glm::min(glm::ivec3((p - lo) / cell), glm::ivec3(cells - 1));
        return (uint32_t)(c.x + cells * (c.y + cells * c.z));
    };
    auto facing = [](const glm::vec3& n) -> uint32_t {
        const glm::vec3 a = glm::abs(n);
        const int ax = a.x >= a.y && a.x >= a.z ? 0 : (a.y >= a.z ? 1 : 2);
        return (uint32_t)(ax * 2 + (n[ax] < 0.0f ? 1 : 0));
    };
    std::vector<uint32_t> vertCell(verts.size());
    // The middle of the corners in each cell: every new point of that cell sits there.
    struct Sum { glm::vec3 pos{0.0f}, normal{0.0f}; int n = 0; };
    std::unordered_map<uint32_t, Sum> middle;
    for (size_t i = 0; i < verts.size(); ++i) {
        vertCell[i] = cellOf(verts[i].position);
        Sum& s = middle[vertCell[i]];
        s.pos += verts[i].position;
        s.normal += verts[i].normal;
        ++s.n;
    }
    // One new point per (cell, facing): made the first time a kept triangle needs it.
    std::unordered_map<uint64_t, uint32_t> made;
    std::vector<glm::vec3> normalSum;
    auto pointFor = [&](uint32_t vi) -> uint32_t {
        const Vertex& v = verts[vi];
        const Sum& s = middle[vertCell[vi]];
        // A cell on a smooth, round bit (its corners face about the same way) is one
        // point; a cell on a hard edge keeps one point per side, so the edge stays sharp.
        const bool smooth = glm::length(s.normal) > 0.75f * (float)s.n;
        const uint64_t key = (uint64_t)vertCell[vi] * 6u + (smooth ? 0u : facing(v.normal));
        auto it = made.find(key);
        if (it != made.end()) { normalSum[it->second] += v.normal; return it->second; }
        Vertex nv = v;                       // (its picture spot, colour and tangent: the first corner's)
        nv.position = s.pos / (float)s.n;
        const uint32_t at = (uint32_t)out.verts.size();
        out.verts.push_back(nv);
        normalSum.push_back(v.normal);
        made.emplace(key, at);
        return at;
    };
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const uint32_t a = idx[t], b = idx[t + 1], c = idx[t + 2];
        if (a >= verts.size() || b >= verts.size() || c >= verts.size()) continue;
        // Two corners in one cell: the triangle shrank to a line. Gone.
        if (vertCell[a] == vertCell[b] || vertCell[b] == vertCell[c] || vertCell[a] == vertCell[c]) continue;
        const uint32_t pa = pointFor(a), pb = pointFor(b), pc = pointFor(c);
        out.idx.insert(out.idx.end(), {pa, pb, pc});
    }
    for (size_t i = 0; i < out.verts.size(); ++i)
        if (glm::length(normalSum[i]) > 1e-6f) out.verts[i].normal = glm::normalize(normalSum[i]);
    return out;
}

std::vector<Level> build(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx) {
    std::vector<Level> levels;
    if (idx.size() < (size_t)kMinTriangles * 3) return levels;
    size_t before = idx.size();
    for (int i = 0; i < kLevels; ++i) {
        Level l = cluster(verts, idx, kCells[i]);
        // Not a real saving (a mesh that is mostly big flat faces), or nothing left of
        // it: skip this one, the level before it is drawn at this distance too.
        if (l.idx.size() < 12 || l.idx.size() * 10 > before * 7) continue;
        l.pixels = kPixels[i];
        before = l.idx.size();
        levels.push_back(std::move(l));
    }
    return levels;
}

float pixelsAcross(float radius, float distance, float pixelsPerStud) {
    return 2.0f * radius * pixelsPerStud / std::max(distance, 1e-3f);
}

int pick(float pixels, float bias, const std::vector<float>& levelPixels) {
    if (bias <= 0.0f) return 0;
    int level = 0;
    for (size_t i = 0; i < levelPixels.size(); ++i)
        if (pixels < levelPixels[i] * bias) level = (int)i + 1;
    return level;
}

} // namespace MeshLod
