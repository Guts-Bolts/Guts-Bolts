// More Modeling-mode tools, the Blender-style ones: edge loops and rings,
// loop cuts, bridging, dissolving, triangles <-> quads, smoothing, mirror,
// spin and the shapes Shift+A adds. The basic tools are in EditMesh.cpp.
#include "EditMesh.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>

namespace MeshEdit {

namespace {
constexpr float kPi = 3.14159265f;

uint64_t edgeKey(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return ((uint64_t)a << 32) | b;
}
uint64_t halfKey(uint32_t a, uint32_t b) { return ((uint64_t)a << 32) | b; }   // a -> b (direction kept)

glm::vec3 newell(const EditMesh& m, const std::vector<uint32_t>& f) {
    glm::vec3 n(0.0f);
    for (size_t i = 0; i < f.size(); ++i) {
        const glm::vec3& a = m.verts[f[i]];
        const glm::vec3& b = m.verts[f[(i + 1) % f.size()]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

void cleanFaces(EditMesh& m) {
    std::vector<std::vector<uint32_t>> out;
    for (auto& f : m.faces) {
        std::vector<uint32_t> g;
        for (uint32_t v : f)
            if (g.empty() || g.back() != v) g.push_back(v);
        while (g.size() > 1 && g.front() == g.back()) g.pop_back();
        if (g.size() >= 3) out.push_back(std::move(g));
    }
    m.faces = std::move(out);
}

// Who touches whom: the faces on each edge and each corner's neighbours.
struct Adjacency {
    std::unordered_map<uint64_t, std::vector<int>> edgeFaces;
    std::vector<std::vector<uint32_t>>              nbr;
    explicit Adjacency(const EditMesh& m) : nbr(m.verts.size()) {
        for (size_t fi = 0; fi < m.faces.size(); ++fi) {
            const auto& f = m.faces[fi];
            for (size_t i = 0; i < f.size(); ++i) {
                uint32_t a = f[i], b = f[(i + 1) % f.size()];
                auto& list = edgeFaces[edgeKey(a, b)];
                if (list.empty()) { nbr[a].push_back(b); nbr[b].push_back(a); }
                list.push_back((int)fi);
            }
        }
    }
    const std::vector<int>& faces(uint32_t a, uint32_t b) const {
        static const std::vector<int> none;
        auto it = edgeFaces.find(edgeKey(a, b));
        return it == edgeFaces.end() ? none : it->second;
    }
};

// Union-find, for "joined together" groups.
struct Groups {
    std::vector<uint32_t> up;
    explicit Groups(size_t n) : up(n) { std::iota(up.begin(), up.end(), 0u); }
    uint32_t root(uint32_t a) { while (up[a] != a) a = up[a] = up[up[a]]; return a; }
    void join(uint32_t a, uint32_t b) { a = root(a); b = root(b); if (a != b) up[b] = a; }
};

// The corner of face `f` that comes after / before `v` (or -1).
int after(const std::vector<uint32_t>& f, uint32_t v) {
    for (size_t i = 0; i < f.size(); ++i) if (f[i] == v) return (int)f[(i + 1) % f.size()];
    return -1;
}
int before(const std::vector<uint32_t>& f, uint32_t v) {
    for (size_t i = 0; i < f.size(); ++i) if (f[i] == v) return (int)f[(i + f.size() - 1) % f.size()];
    return -1;
}

// Make every face of a freshly built shape point away from its middle.
void faceOut(EditMesh& m) {
    for (auto& f : m.faces)
        if (glm::dot(newell(m, f), faceCenter(m, f)) < 0.0f) std::reverse(f.begin(), f.end());
}

// Merge each group of faces into one face along its outline. Groups with a
// hole in the middle (or a pinch) are left as they were. Returns how many merged.
int mergeRegions(EditMesh& m, const std::vector<std::vector<int>>& groups) {
    std::vector<char> gone(m.faces.size(), 0);
    std::vector<std::vector<uint32_t>> added;
    int merged = 0;
    for (const auto& g : groups) {
        if (g.size() < 2) continue;
        std::set<uint64_t> halves;
        for (int fi : g) {
            const auto& f = m.faces[fi];
            for (size_t i = 0; i < f.size(); ++i) halves.insert(halfKey(f[i], f[(i + 1) % f.size()]));
        }
        // The outline: half-edges whose partner going the other way isn't in the group.
        std::map<uint32_t, uint32_t> next;
        bool ok = true;
        for (uint64_t h : halves) {
            uint32_t a = (uint32_t)(h >> 32), b = (uint32_t)(h & 0xffffffffu);
            if (halves.count(halfKey(b, a))) continue;
            if (next.count(a)) { ok = false; break; }   // a pinch: two ways out of one corner
            next[a] = b;
        }
        if (!ok || next.size() < 3) continue;
        std::vector<uint32_t> poly;
        uint32_t start = next.begin()->first, cur = start;
        do {
            poly.push_back(cur);
            auto it = next.find(cur);
            if (it == next.end() || poly.size() > next.size()) { ok = false; break; }
            cur = it->second;
        } while (cur != start);
        if (!ok || poly.size() != next.size()) continue;   // more than one outline: a hole
        for (int fi : g) gone[fi] = 1;
        added.push_back(std::move(poly));
        ++merged;
    }
    std::vector<std::vector<uint32_t>> out;
    for (size_t fi = 0; fi < m.faces.size(); ++fi) if (!gone[fi]) out.push_back(std::move(m.faces[fi]));
    for (auto& f : added) out.push_back(std::move(f));
    m.faces = std::move(out);
    return merged;
}

// Faces joined by sharing an edge, from a list of faces.
std::vector<std::vector<int>> faceGroups(const EditMesh& m, const std::vector<int>& faces,
                                         const std::function<bool(uint32_t, uint32_t)>& crossOk) {
    std::unordered_map<uint64_t, std::vector<int>> byEdge;
    for (size_t k = 0; k < faces.size(); ++k) {
        const auto& f = m.faces[faces[k]];
        for (size_t i = 0; i < f.size(); ++i) byEdge[edgeKey(f[i], f[(i + 1) % f.size()])].push_back((int)k);
    }
    Groups g(faces.size());
    for (auto& [key, list] : byEdge) {
        if (list.size() < 2) continue;
        if (crossOk && !crossOk((uint32_t)(key >> 32), (uint32_t)(key & 0xffffffffu))) continue;
        for (size_t i = 1; i < list.size(); ++i) g.join((uint32_t)list[0], (uint32_t)list[i]);
    }
    std::map<uint32_t, std::vector<int>> out;
    for (size_t k = 0; k < faces.size(); ++k) out[g.root((uint32_t)k)].push_back(faces[k]);
    std::vector<std::vector<int>> v;
    for (auto& [r, list] : out) v.push_back(std::move(list));
    return v;
}

// Split one face into triangles (ear clipping, so dents in the face work too).
void earClip(const EditMesh& m, const std::vector<uint32_t>& f, std::vector<std::vector<uint32_t>>& out) {
    if (f.size() <= 3) { out.push_back(f); return; }
    glm::vec3 n = faceNormal(m, f);
    glm::vec3 ux = glm::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    ux = glm::normalize(ux - n * glm::dot(ux, n));
    glm::vec3 uy = glm::cross(n, ux);
    std::vector<glm::vec2> p;
    for (uint32_t v : f) p.push_back({glm::dot(m.verts[v], ux), glm::dot(m.verts[v], uy)});
    std::vector<int> idx(f.size());
    std::iota(idx.begin(), idx.end(), 0);
    auto cross2 = [](glm::vec2 a, glm::vec2 b, glm::vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool cut = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
            if (cross2(p[a], p[b], p[c]) <= 1e-9f) continue;   // a dent, not an ear
            bool inside = false;
            for (int k : idx) {
                if (k == a || k == b || k == c) continue;
                if (cross2(p[a], p[b], p[k]) >= 0 && cross2(p[b], p[c], p[k]) >= 0 && cross2(p[c], p[a], p[k]) >= 0) { inside = true; break; }
            }
            if (inside) continue;
            out.push_back({f[a], f[b], f[c]});
            idx.erase(idx.begin() + i);
            cut = true;
            break;
        }
        if (!cut) break;   // a twisted face: fan the rest
    }
    for (size_t k = 1; k + 1 < idx.size(); ++k) out.push_back({f[idx[0]], f[idx[k]], f[idx[k + 1]]});
}
} // namespace

// ---------------------------------------------------------------------------
// Picking helpers
// ---------------------------------------------------------------------------

namespace {
// The next corner along an edge loop going prev -> cur (or -1 at the end).
int loopNext(const EditMesh& m, const Adjacency& adj, uint32_t prev, uint32_t cur) {
    const auto& F = adj.faces(prev, cur);
    if (F.size() == 1) {   // along the edge of a hole: follow the outline
        int found = -1;
        for (uint32_t n : adj.nbr[cur]) {
            if (n == prev || adj.faces(cur, n).size() != 1) continue;
            if (found >= 0) return -1;
            found = (int)n;
        }
        return found;
    }
    if (F.size() != 2 || adj.nbr[cur].size() != 4) return -1;
    std::set<uint32_t> skip{prev};
    for (int fi : F) {
        const auto& f = m.faces[fi];
        int a = after(f, cur), b = before(f, cur);
        if (a >= 0 && (uint32_t)a != prev) skip.insert((uint32_t)a);
        if (b >= 0 && (uint32_t)b != prev) skip.insert((uint32_t)b);
    }
    for (uint32_t n : adj.nbr[cur]) if (!skip.count(n)) return (int)n;
    return -1;
}
} // namespace

std::vector<uint32_t> edgeLoop(const EditMesh& m, uint32_t a, uint32_t b) {
    Adjacency adj(m);
    auto walk = [&](uint32_t from, uint32_t to, bool& closed) {
        std::vector<uint32_t> out{to};
        std::set<uint32_t> seen{from, to};
        uint32_t prev = from, cur = to;
        closed = false;
        while (out.size() <= m.verts.size()) {
            int n = loopNext(m, adj, prev, cur);
            if (n < 0) break;
            if ((uint32_t)n == from) { closed = true; break; }
            if (seen.count((uint32_t)n)) break;
            seen.insert((uint32_t)n);
            out.push_back((uint32_t)n);
            prev = cur;
            cur = (uint32_t)n;
        }
        return out;
    };
    bool closed = false;
    std::vector<uint32_t> fwd = walk(a, b, closed);
    std::vector<uint32_t> loop;
    if (closed) {
        loop.push_back(a);
        loop.insert(loop.end(), fwd.begin(), fwd.end());
        return loop;
    }
    bool c2 = false;
    std::vector<uint32_t> back = walk(b, a, c2);
    loop.assign(back.rbegin(), back.rend());
    loop.insert(loop.end(), fwd.begin(), fwd.end());
    return loop;
}

std::vector<std::pair<uint32_t, uint32_t>> edgeRing(const EditMesh& m, uint32_t a, uint32_t b) {
    Adjacency adj(m);
    std::vector<std::pair<uint32_t, uint32_t>> out{{a, b}};
    std::set<uint64_t> seenEdge{edgeKey(a, b)};
    std::set<int> seenFace;
    for (size_t q = 0; q < out.size(); ++q) {
        auto [u, v] = out[q];
        for (int fi : adj.faces(u, v)) {
            const auto& f = m.faces[fi];
            if (f.size() != 4 || seenFace.count(fi)) continue;
            seenFace.insert(fi);
            for (int i = 0; i < 4; ++i) {
                if (edgeKey(f[i], f[(i + 1) % 4]) != edgeKey(u, v)) continue;
                uint32_t c = f[(i + 2) % 4], d = f[(i + 3) % 4];
                if (seenEdge.insert(edgeKey(c, d)).second) out.push_back({c, d});
                break;
            }
        }
    }
    return out;
}

void selectLinked(const EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    Groups g(m.verts.size());
    for (auto& f : m.faces) for (size_t i = 1; i < f.size(); ++i) g.join(f[0], f[i]);
    std::set<uint32_t> roots;
    for (size_t i = 0; i < sel.size(); ++i) if (sel[i]) roots.insert(g.root((uint32_t)i));
    for (size_t i = 0; i < sel.size(); ++i) if (roots.count(g.root((uint32_t)i))) sel[i] = 1;
}

void grow(const EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    Selection out = sel;
    for (auto& f : m.faces) {
        bool any = false;
        for (uint32_t v : f) if (sel[v]) { any = true; break; }
        if (any) for (uint32_t v : f) out[v] = 1;
    }
    sel = std::move(out);
}

void shrink(const EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    Selection out = sel;
    for (auto& f : m.faces) {
        bool all = true;
        for (uint32_t v : f) if (!sel[v]) { all = false; break; }
        if (!all) for (uint32_t v : f) out[v] = 0;   // corners on the edge of the picked area
    }
    sel = std::move(out);
}

void selectBoundary(const EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    std::map<uint64_t, int> uses;
    for (int fi : selectedFaces(m, sel)) {
        const auto& f = m.faces[fi];
        for (size_t i = 0; i < f.size(); ++i) uses[edgeKey(f[i], f[(i + 1) % f.size()])]++;
    }
    std::fill(sel.begin(), sel.end(), 0);
    for (auto& [k, n] : uses)
        if (n == 1) { sel[(uint32_t)(k >> 32)] = 1; sel[(uint32_t)(k & 0xffffffffu)] = 1; }
}

void selectNonManifold(const EditMesh& m, Selection& sel) {
    sel.assign(m.verts.size(), 0);
    Adjacency adj(m);
    for (auto& [k, list] : adj.edgeFaces)
        if (list.size() != 2) { sel[(uint32_t)(k >> 32)] = 1; sel[(uint32_t)(k & 0xffffffffu)] = 1; }
}

// ---------------------------------------------------------------------------
// Shaping tools
// ---------------------------------------------------------------------------

bool duplicate(EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    std::vector<int> faces = selectedFaces(m, sel);
    if (faces.empty()) return false;
    std::map<uint32_t, uint32_t> copyOf;
    const bool uv = m.uvs.size() == m.faces.size();
    for (int fi : faces) {
        std::vector<uint32_t> g;
        for (uint32_t v : m.faces[fi]) {
            auto it = copyOf.find(v);
            if (it == copyOf.end()) {
                it = copyOf.emplace(v, (uint32_t)m.verts.size()).first;
                m.verts.push_back(m.verts[v]);
            }
            g.push_back(it->second);
        }
        m.faces.push_back(std::move(g));
        if (uv) m.uvs.push_back(m.uvs[fi]);
    }
    sel.assign(m.verts.size(), 0);
    for (auto [from, to] : copyOf) sel[to] = 1;
    return true;
}

bool loopCut(EditMesh& m, Selection& sel, uint32_t a, uint32_t b, int cuts) {
    cuts = std::clamp(cuts, 1, 64);
    auto ring = edgeRing(m, a, b);
    Adjacency adj(m);
    if (adj.faces(a, b).empty()) return false;
    // New points along every edge of the ring, in order from the lower-numbered end.
    std::unordered_map<uint64_t, std::vector<uint32_t>> pts;
    for (auto [u, v] : ring) {
        uint32_t lo = std::min(u, v), hi = std::max(u, v);
        auto& list = pts[edgeKey(u, v)];
        for (int k = 1; k <= cuts; ++k) {
            list.push_back((uint32_t)m.verts.size());
            m.verts.push_back(glm::mix(m.verts[lo], m.verts[hi], (float)k / (float)(cuts + 1)));
        }
    }
    auto along = [&](uint32_t from, uint32_t to) {   // the new points going from -> to
        std::vector<uint32_t> list = pts[edgeKey(from, to)];
        if (from > to) std::reverse(list.begin(), list.end());
        return list;
    };
    // Faces the ring goes through are cut into strips; their neighbours get the
    // new points on the shared edge (no cracks).
    std::set<int> ringFaces;
    for (auto [u, v] : ring)
        for (int fi : adj.faces(u, v)) {
            const auto& f = m.faces[fi];
            if (f.size() != 4) continue;
            int hits = 0;
            for (int i = 0; i < 4; ++i) hits += pts.count(edgeKey(f[i], f[(i + 1) % 4])) ? 1 : 0;
            if (hits == 2) ringFaces.insert(fi);
        }
    const bool uv = m.uvs.size() == m.faces.size();
    std::vector<std::vector<uint32_t>> out;
    std::vector<std::vector<glm::vec2>> outUV;
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        const auto& f = m.faces[fi];
        if (ringFaces.count((int)fi)) {
            int i = 0;
            while (!(pts.count(edgeKey(f[i], f[(i + 1) % 4])) && pts.count(edgeKey(f[(i + 2) % 4], f[(i + 3) % 4])))) ++i;
            uint32_t p0 = f[i], p1 = f[(i + 1) % 4], p2 = f[(i + 2) % 4], p3 = f[(i + 3) % 4];
            std::vector<uint32_t> A{p0}, B{p3};
            for (uint32_t x : along(p0, p1)) A.push_back(x);
            for (uint32_t x : along(p3, p2)) B.push_back(x);
            A.push_back(p1);
            B.push_back(p2);
            for (int k = 0; k <= cuts; ++k) {
                out.push_back({A[k], A[k + 1], B[k + 1], B[k]});
                if (uv) {
                    const auto& U = m.uvs[fi];
                    float t0 = (float)k / (cuts + 1), t1 = (float)(k + 1) / (cuts + 1);
                    glm::vec2 u0 = U[i], u1 = U[(i + 1) % 4], u2 = U[(i + 2) % 4], u3 = U[(i + 3) % 4];
                    outUV.push_back({glm::mix(u0, u1, t0), glm::mix(u0, u1, t1), glm::mix(u3, u2, t1), glm::mix(u3, u2, t0)});
                }
            }
            continue;
        }
        std::vector<uint32_t> g;
        std::vector<glm::vec2> gu;
        for (size_t i = 0; i < f.size(); ++i) {
            uint32_t x = f[i], y = f[(i + 1) % f.size()];
            g.push_back(x);
            if (uv) gu.push_back(m.uvs[fi][i]);
            if (pts.count(edgeKey(x, y))) {
                auto list = along(x, y);
                for (size_t k = 0; k < list.size(); ++k) {
                    g.push_back(list[k]);
                    if (uv) gu.push_back(glm::mix(m.uvs[fi][i], m.uvs[fi][(i + 1) % f.size()], (float)(k + 1) / (cuts + 1)));
                }
            }
        }
        out.push_back(std::move(g));
        if (uv) outUV.push_back(std::move(gu));
    }
    m.faces = std::move(out);
    if (uv) m.uvs = std::move(outUV);
    sel.assign(m.verts.size(), 0);
    for (auto& [k, list] : pts) for (uint32_t v : list) sel[v] = 1;
    return true;
}

bool bridge(EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    // The picked edges, split into joined-up chains.
    // Holes' outlines first (edges with a face on one side only): picking two rims
    // also picks the edges running between them, which aren't part of either loop.
    Adjacency adj(m);
    std::map<uint32_t, std::vector<uint32_t>> links;
    for (int pass = 0; pass < 2 && links.empty(); ++pass)
        for (auto& [k, list] : adj.edgeFaces) {
            uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xffffffffu);
            if (sel[a] && sel[b] && (pass == 1 || list.size() == 1)) { links[a].push_back(b); links[b].push_back(a); }
        }
    std::set<uint32_t> seen;
    std::vector<std::pair<std::vector<uint32_t>, bool>> loops;   // corners, closed?
    for (auto& [start, nb] : links) {
        if (seen.count(start)) continue;
        // Start an open chain at its end if it has one.
        uint32_t s = start;
        std::set<uint32_t> group;
        std::vector<uint32_t> stack{start};
        while (!stack.empty()) {
            uint32_t v = stack.back(); stack.pop_back();
            if (!group.insert(v).second) continue;
            for (uint32_t n : links[v]) stack.push_back(n);
        }
        for (uint32_t v : group) {
            if (links[v].size() > 2) return false;   // a branch: not a loop
            if (links[v].size() == 1) s = v;
        }
        std::vector<uint32_t> chain{s};
        uint32_t prev = s, cur = s;
        seen.insert(s);
        while (true) {
            int nx = -1;
            for (uint32_t n : links[cur]) if (n != prev && !seen.count(n)) { nx = (int)n; break; }
            if (nx < 0) break;
            prev = cur; cur = (uint32_t)nx;
            seen.insert(cur);
            chain.push_back(cur);
        }
        bool closed = chain.size() > 2 && std::find(links[cur].begin(), links[cur].end(), s) != links[cur].end();
        loops.push_back({chain, closed});
    }
    if (loops.size() != 2 || loops[0].first.size() != loops[1].first.size() || loops[0].second != loops[1].second) return false;
    std::vector<uint32_t> A = loops[0].first, B = loops[1].first;
    const bool closed = loops[0].second;
    const size_t n = A.size();
    // Line B up with A: the turn and direction that make the shortest joins.
    float best = 1e30f;
    std::vector<uint32_t> bestB = B;
    for (int dir = 0; dir < 2; ++dir) {
        std::vector<uint32_t> R = B;
        if (dir) std::reverse(R.begin(), R.end());
        for (size_t off = 0; off < (closed ? n : 1); ++off) {
            float d = 0.0f;
            for (size_t i = 0; i < n; ++i) d += glm::length(m.verts[A[i]] - m.verts[R[(i + off) % n]]);
            if (d < best) {
                best = d;
                bestB.clear();
                for (size_t i = 0; i < n; ++i) bestB.push_back(R[(i + off) % n]);
            }
        }
    }
    B = bestB;
    // Wind the new faces to match the faces already on loop A.
    bool flipIt = false;
    for (int fi : adj.faces(A[0], A[1])) if (after(m.faces[fi], A[0]) == (int)A[1]) flipIt = true;
    size_t edgesN = closed ? n : n - 1;
    const bool uv = m.uvs.size() == m.faces.size();
    for (size_t i = 0; i < edgesN; ++i) {
        size_t j = (i + 1) % n;
        std::vector<uint32_t> f{A[i], A[j], B[j], B[i]};
        if (flipIt) std::reverse(f.begin(), f.end());
        m.faces.push_back(f);
        if (uv) m.uvs.push_back({{0, 0}, {1, 0}, {1, 1}, {0, 1}});
    }
    return true;
}

bool dissolve(EditMesh& m, Selection& sel, int what) {
    sel.resize(m.verts.size(), 0);
    m.uvs.clear();
    if (what == 2) {   // faces: each joined patch becomes one face
        auto groups = faceGroups(m, selectedFaces(m, sel), nullptr);
        if (!mergeRegions(m, groups)) return false;
    } else if (what == 1) {   // edges: the two faces on each picked edge become one
        std::vector<int> all(m.faces.size());
        std::iota(all.begin(), all.end(), 0);
        auto groups = faceGroups(m, all, [&](uint32_t a, uint32_t b) { return sel[a] && sel[b]; });
        if (!mergeRegions(m, groups)) return false;
    } else {           // vertices: the faces round each corner become one, without it
        Groups g(m.faces.size());
        std::vector<int> firstFace(m.verts.size(), -1);
        for (size_t fi = 0; fi < m.faces.size(); ++fi)
            for (uint32_t v : m.faces[fi]) {
                if (!sel[v]) continue;
                if (firstFace[v] < 0) firstFace[v] = (int)fi;
                else g.join((uint32_t)firstFace[v], (uint32_t)fi);
            }
        std::map<uint32_t, std::vector<int>> by;
        for (size_t fi = 0; fi < m.faces.size(); ++fi) {
            bool touches = false;
            for (uint32_t v : m.faces[fi]) if (sel[v]) touches = true;
            if (touches) by[g.root((uint32_t)fi)].push_back((int)fi);
        }
        std::vector<std::vector<int>> groups;
        for (auto& [r, list] : by) groups.push_back(list);
        mergeRegions(m, groups);
        // Corners still on an outline (the edge of a hole) are just taken out of it.
        for (auto& f : m.faces) {
            std::vector<uint32_t> keep;
            for (uint32_t v : f) if (!sel[v]) keep.push_back(v);
            if (keep.size() >= 3) f = keep;
        }
    }
    cleanFaces(m);
    std::fill(sel.begin(), sel.end(), 0);
    compact(m, sel);
    return !m.faces.empty();
}

bool triangulate(EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    std::vector<int> faces = selectedFaces(m, sel);
    bool all = faces.empty();
    std::vector<char> pick(m.faces.size(), all ? 1 : 0);
    for (int fi : faces) pick[fi] = 1;
    std::vector<std::vector<uint32_t>> out;
    bool changed = false;
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        if (!pick[fi] || m.faces[fi].size() <= 3) { out.push_back(m.faces[fi]); continue; }
        earClip(m, m.faces[fi], out);
        changed = true;
    }
    if (!changed) return false;
    m.faces = std::move(out);
    m.uvs.clear();
    return true;
}

bool trisToQuads(EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    std::vector<int> faces = selectedFaces(m, sel);
    if (faces.empty()) { faces.resize(m.faces.size()); std::iota(faces.begin(), faces.end(), 0); }
    std::vector<char> tri(m.faces.size(), 0);
    for (int fi : faces) if (m.faces[fi].size() == 3) tri[fi] = 1;
    Adjacency adj(m);
    struct Pair { float score; int f, g; std::vector<uint32_t> quad; };
    std::vector<Pair> pairs;
    for (auto& [k, list] : adj.edgeFaces) {
        if (list.size() != 2 || !tri[list[0]] || !tri[list[1]]) continue;
        uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xffffffffu);
        const auto& f = m.faces[list[0]];
        const auto& g = m.faces[list[1]];
        // f goes ... x -> y -> (f's other); the quad is x, fo, y, go in f's direction.
        uint32_t x = after(f, a) == (int)b ? a : b, y = x == a ? b : a;
        uint32_t fo = (uint32_t)after(f, y), go = (uint32_t)after(g, x);
        if (after(g, y) != (int)x) continue;   // the two point different ways
        std::vector<uint32_t> q{x, go, y, fo};
        glm::vec3 n1 = faceNormal(m, f), n2 = faceNormal(m, g);
        float flat = glm::dot(n1, n2);
        if (flat < 0.7f) continue;   // too bent to be one face
        // Both diagonals must cross inside (a convex quad).
        glm::vec3 n = glm::normalize(n1 + n2);
        bool convex = true;
        for (int i = 0; i < 4; ++i) {
            glm::vec3 e1 = m.verts[q[(i + 1) % 4]] - m.verts[q[i]], e2 = m.verts[q[(i + 2) % 4]] - m.verts[q[(i + 1) % 4]];
            if (glm::dot(glm::cross(e1, e2), n) <= 0.0f) convex = false;
        }
        if (!convex) continue;
        // Squarer corners score better.
        float sq = 0.0f;
        for (int i = 0; i < 4; ++i) {
            glm::vec3 e1 = glm::normalize(m.verts[q[(i + 1) % 4]] - m.verts[q[i]]);
            glm::vec3 e2 = glm::normalize(m.verts[q[(i + 3) % 4]] - m.verts[q[i]]);
            sq += std::abs(glm::dot(e1, e2));
        }
        pairs.push_back({flat * 2.0f - sq, list[0], list[1], q});
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& x, const Pair& y) { return x.score > y.score; });
    std::vector<char> used(m.faces.size(), 0);
    std::vector<std::vector<uint32_t>> added;
    for (auto& p : pairs) {
        if (used[p.f] || used[p.g]) continue;
        used[p.f] = used[p.g] = 1;
        added.push_back(p.quad);
    }
    if (added.empty()) return false;
    std::vector<std::vector<uint32_t>> out;
    for (size_t fi = 0; fi < m.faces.size(); ++fi) if (!used[fi]) out.push_back(m.faces[fi]);
    for (auto& q : added) out.push_back(q);
    m.faces = std::move(out);
    m.uvs.clear();
    return true;
}

bool recalcNormals(EditMesh& m, const Selection& sel, bool inside) {
    std::vector<int> faces = selectedFaces(m, sel);
    if (faces.empty()) { faces.resize(m.faces.size()); std::iota(faces.begin(), faces.end(), 0); }
    std::vector<char> pick(m.faces.size(), 0), done(m.faces.size(), 0);
    for (int fi : faces) pick[fi] = 1;
    Adjacency adj(m);
    const bool uv = m.uvs.size() == m.faces.size();
    auto flipFace = [&](int fi) {
        std::reverse(m.faces[fi].begin(), m.faces[fi].end());
        if (uv) std::reverse(m.uvs[fi].begin(), m.uvs[fi].end());
    };
    for (int seed : faces) {
        if (done[seed]) continue;
        // Spread from one face: each neighbour must cross the shared edge the other way.
        std::vector<int> comp{seed};
        done[seed] = 1;
        for (size_t q = 0; q < comp.size(); ++q) {
            const auto f = m.faces[comp[q]];
            for (size_t i = 0; i < f.size(); ++i) {
                uint32_t a = f[i], b = f[(i + 1) % f.size()];
                const auto& list = adj.faces(a, b);
                if (list.size() != 2) continue;
                for (int g : list) {
                    if (g == comp[q] || done[g] || !pick[g]) continue;
                    if (after(m.faces[g], a) == (int)b) flipFace(g);
                    done[g] = 1;
                    comp.push_back(g);
                }
            }
        }
        // Then the whole piece points away from its middle (or towards it).
        glm::vec3 c(0.0f);
        float area = 0.0f;
        for (int fi : comp) { float w = glm::length(newell(m, m.faces[fi])); c += faceCenter(m, m.faces[fi]) * w; area += w; }
        if (area <= 0.0f) continue;
        c /= area;
        float out = 0.0f;
        for (int fi : comp) out += glm::dot(newell(m, m.faces[fi]), faceCenter(m, m.faces[fi]) - c);
        if ((out < 0.0f) != inside) for (int fi : comp) flipFace(fi);
    }
    return true;
}

bool smoothVerts(EditMesh& m, const Selection& sel, float amount) {
    if (countSelected(sel) == 0) return false;
    Adjacency adj(m);
    std::vector<glm::vec3> next = m.verts;
    for (size_t i = 0; i < m.verts.size() && i < sel.size(); ++i) {
        if (!sel[i] || adj.nbr[i].empty()) continue;
        // Corners on the edge of a hole only follow the outline (so it doesn't shrink inwards).
        std::vector<uint32_t> use;
        bool edgeOfHole = false;
        for (uint32_t n : adj.nbr[i]) if (adj.faces((uint32_t)i, n).size() == 1) { edgeOfHole = true; use.push_back(n); }
        if (!edgeOfHole) use = adj.nbr[i];
        glm::vec3 avg(0.0f);
        for (uint32_t n : use) avg += m.verts[n];
        avg /= (float)use.size();
        next[i] = glm::mix(m.verts[i], avg, amount);
    }
    m.verts = std::move(next);
    return true;
}

bool mergeByDistance(EditMesh& m, Selection& sel, float dist, int* removed) {
    sel.resize(m.verts.size(), 0);
    bool any = countSelected(sel) > 0;
    std::vector<uint32_t> order;
    for (uint32_t i = 0; i < m.verts.size(); ++i) if (!any || sel[i]) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return m.verts[a].x < m.verts[b].x; });
    Groups g(m.verts.size());
    int count = 0;
    for (size_t i = 0; i < order.size(); ++i)
        for (size_t j = i + 1; j < order.size() && m.verts[order[j]].x - m.verts[order[i]].x <= dist; ++j)
            if (glm::length(m.verts[order[i]] - m.verts[order[j]]) <= dist && g.root(order[i]) != g.root(order[j])) {
                g.join(order[i], order[j]);
                ++count;
            }
    if (removed) *removed = count;
    if (!count) return false;
    for (auto& f : m.faces) for (uint32_t& v : f) v = g.root(v);
    m.uvs.clear();
    cleanFaces(m);
    // Two faces on exactly the same corners (both sides of a welded seam): keep one.
    std::set<std::vector<uint32_t>> seen;
    std::vector<std::vector<uint32_t>> out;
    for (auto& f : m.faces) {
        std::vector<uint32_t> k = f;
        std::sort(k.begin(), k.end());
        if (seen.insert(k).second) out.push_back(f);
    }
    m.faces = std::move(out);
    for (size_t i = 0; i < sel.size(); ++i) if (sel[i]) sel[g.root((uint32_t)i)] = 1;
    compact(m, sel);
    return true;
}

bool collapse(EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    if (countSelected(sel) < 2) return false;
    Groups g(m.verts.size());
    for (auto& f : m.faces)
        for (size_t i = 0; i < f.size(); ++i) {
            uint32_t a = f[i], b = f[(i + 1) % f.size()];
            if (sel[a] && sel[b]) g.join(a, b);
        }
    std::map<uint32_t, std::pair<glm::vec3, int>> sum;
    for (uint32_t i = 0; i < m.verts.size(); ++i)
        if (sel[i]) { auto& s = sum[g.root(i)]; s.first += m.verts[i]; s.second++; }
    for (auto& [r, s] : sum) m.verts[r] = s.first / (float)s.second;
    for (auto& f : m.faces) for (uint32_t& v : f) if (sel[v]) v = g.root(v);
    m.uvs.clear();
    cleanFaces(m);
    std::fill(sel.begin(), sel.end(), 0);
    for (auto& [r, s] : sum) sel[r] = 1;
    compact(m, sel);
    return true;
}

bool subdivideSmooth(EditMesh& m, Selection& sel) {
    if (m.faces.empty()) return false;
    Adjacency adj(m);
    const size_t nv = m.verts.size();
    std::vector<glm::vec3> facePt(m.faces.size());
    for (size_t fi = 0; fi < m.faces.size(); ++fi) facePt[fi] = faceCenter(m, m.faces[fi]);
    // Edge points.
    std::unordered_map<uint64_t, uint32_t> edgeIdx;
    std::vector<glm::vec3> verts = m.verts;
    for (auto& [k, list] : adj.edgeFaces) {
        uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xffffffffu);
        glm::vec3 p = (m.verts[a] + m.verts[b]) * 0.5f;
        if (list.size() == 2) p = (m.verts[a] + m.verts[b] + facePt[list[0]] + facePt[list[1]]) * 0.25f;
        edgeIdx[k] = (uint32_t)verts.size();
        verts.push_back(p);
    }
    // Move the old corners.
    std::vector<std::vector<int>> vertFaces(nv);
    for (size_t fi = 0; fi < m.faces.size(); ++fi) for (uint32_t v : m.faces[fi]) vertFaces[v].push_back((int)fi);
    for (uint32_t v = 0; v < nv; ++v) {
        const auto& nb = adj.nbr[v];
        if (nb.empty()) continue;
        std::vector<uint32_t> rim;
        bool odd = false;
        for (uint32_t n : nb) {
            size_t c = adj.faces(v, n).size();
            if (c == 1) rim.push_back(n);
            else if (c != 2) odd = true;
        }
        if (odd) continue;   // a strange edge: leave the corner where it is
        if (!rim.empty()) {   // on the edge of a hole: smooth along the outline only
            if (rim.size() == 2) verts[v] = m.verts[v] * 0.75f + (m.verts[rim[0]] + m.verts[rim[1]]) * 0.125f;
            continue;
        }
        float n = (float)nb.size();
        glm::vec3 F(0.0f), R(0.0f);
        for (int fi : vertFaces[v]) F += facePt[fi];
        F /= (float)vertFaces[v].size();
        for (uint32_t k : nb) R += (m.verts[v] + m.verts[k]) * 0.5f;
        R /= n;
        verts[v] = (F + 2.0f * R + (n - 3.0f) * m.verts[v]) / n;
    }
    std::vector<std::vector<uint32_t>> faces;
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        const auto& f = m.faces[fi];
        uint32_t c = (uint32_t)verts.size();
        verts.push_back(facePt[fi]);
        size_t n = f.size();
        for (size_t i = 0; i < n; ++i) {
            uint32_t prev = edgeIdx[edgeKey(f[(i + n - 1) % n], f[i])], next = edgeIdx[edgeKey(f[i], f[(i + 1) % n])];
            faces.push_back({f[i], next, c, prev});
        }
    }
    m.verts = std::move(verts);
    m.faces = std::move(faces);
    m.uvs.clear();
    sel.assign(m.verts.size(), 0);
    return true;
}

bool mirror(EditMesh& m, Selection& sel, int axis) {
    sel.resize(m.verts.size(), 0);
    bool any = countSelected(sel) > 0;
    glm::vec3 c(0.0f);
    int n = 0;
    for (size_t i = 0; i < m.verts.size(); ++i) if (!any || sel[i]) { c += m.verts[i]; ++n; }
    if (!n) return false;
    c /= (float)n;
    for (size_t i = 0; i < m.verts.size(); ++i) if (!any || sel[i]) m.verts[i][axis] = 2.0f * c[axis] - m.verts[i][axis];
    // Mirrored faces would face inwards: turn them round.
    const bool uv = m.uvs.size() == m.faces.size();
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        bool all = true;
        for (uint32_t v : m.faces[fi]) if (any && !sel[v]) all = false;
        if (!all) continue;
        std::reverse(m.faces[fi].begin(), m.faces[fi].end());
        if (uv) std::reverse(m.uvs[fi].begin(), m.uvs[fi].end());
    }
    return true;
}

bool spin(EditMesh& m, Selection& sel, glm::vec3 axis, glm::vec3 center, float degrees, int steps) {
    steps = std::clamp(steps, 1, 128);
    if (glm::length(axis) < 1e-6f || countSelected(sel) == 0) return false;
    glm::mat4 turn = glm::translate(glm::mat4(1.0f), center) *
                     glm::rotate(glm::mat4(1.0f), glm::radians(degrees / (float)steps), glm::normalize(axis)) *
                     glm::translate(glm::mat4(1.0f), -center);
    for (int s = 0; s < steps; ++s) {
        if (!extrude(m, sel)) return false;
        for (size_t i = 0; i < m.verts.size() && i < sel.size(); ++i)
            if (sel[i]) m.verts[i] = glm::vec3(turn * glm::vec4(m.verts[i], 1.0f));
    }
    if (std::abs(std::abs(degrees) - 360.0f) < 0.01f) {
        // The ends meet: join them, and a face that went all the way round is now inside.
        const bool hadFaces = !selectedFaces(m, sel).empty();
        const EditMesh before = m;
        Selection all(m.verts.size(), 1);
        mergeByDistance(m, all, 1e-4f);
        sel = carryFlags(before, sel, m);
        if (hadFaces) remove(m, sel, 2);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Shapes (Shift+A)
// ---------------------------------------------------------------------------

std::shared_ptr<EditMesh> shape(AddShape s) {
    switch (s) {
        case AddShape::Cube:     return fromPrimitive(PrimitiveType::Cube);
        case AddShape::Plane:    return fromPrimitive(PrimitiveType::Plane);
        case AddShape::Cylinder: return fromPrimitive(PrimitiveType::Cylinder);
        case AddShape::Sphere:   return fromPrimitive(PrimitiveType::Sphere);
        default: break;
    }
    auto m = std::make_shared<EditMesh>();
    auto ring = [&](int segs, float y, float r) {
        uint32_t base = (uint32_t)m->verts.size();
        for (int i = 0; i < segs; ++i) {
            float a = 2.0f * kPi * i / segs;
            m->verts.push_back({std::cos(a) * r, y, std::sin(a) * r});
        }
        return base;
    };
    switch (s) {
    case AddShape::Circle: {
        uint32_t b = ring(16, 0.0f, 0.5f);
        std::vector<uint32_t> f;
        for (int i = 15; i >= 0; --i) f.push_back(b + i);
        m->faces.push_back(f);
        break;
    }
    case AddShape::Cone: {
        const int segs = 16;
        uint32_t b = ring(segs, -0.5f, 0.5f);
        uint32_t tip = (uint32_t)m->verts.size();
        m->verts.push_back({0.0f, 0.5f, 0.0f});
        std::vector<uint32_t> cap;
        for (int i = 0; i < segs; ++i) {
            m->faces.push_back({b + i, tip, b + (uint32_t)((i + 1) % segs)});
            cap.push_back(b + i);
        }
        m->faces.push_back(cap);
        faceOut(*m);
        break;
    }
    case AddShape::IcoSphere: {
        const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
        m->verts = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                    {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
        m->faces = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                    {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
        Selection none;
        subdivide(*m, none);
        for (auto& p : m->verts) p = glm::normalize(p) * 0.5f;
        faceOut(*m);
        break;
    }
    case AddShape::Torus: {
        const int major = 24, minor = 12;
        const float R = 0.375f, r = 0.125f;
        for (int i = 0; i < major; ++i) {
            float a = 2.0f * kPi * i / major;
            glm::vec3 c(std::cos(a) * R, 0.0f, std::sin(a) * R), out(std::cos(a), 0.0f, std::sin(a));
            for (int j = 0; j < minor; ++j) {
                float b = 2.0f * kPi * j / minor;
                m->verts.push_back(c + out * (std::cos(b) * r) + glm::vec3(0, std::sin(b) * r, 0));
            }
        }
        for (int i = 0; i < major; ++i)
            for (int j = 0; j < minor; ++j) {
                uint32_t a = i * minor + j, b = ((i + 1) % major) * minor + j;
                uint32_t c = ((i + 1) % major) * minor + (j + 1) % minor, d = i * minor + (j + 1) % minor;
                std::vector<uint32_t> f{a, b, c, d};
                // Out = away from the middle of the tube, not the middle of the ring.
                float ang = 2.0f * kPi * (i + 0.5f) / major;
                glm::vec3 tube(std::cos(ang) * R, 0.0f, std::sin(ang) * R);
                if (glm::dot(newell(*m, f), faceCenter(*m, f) - tube) < 0.0f) std::reverse(f.begin(), f.end());
                m->faces.push_back(f);
            }
        break;
    }
    case AddShape::Grid: {
        const int n = 10;
        for (int z = 0; z <= n; ++z)
            for (int x = 0; x <= n; ++x) m->verts.push_back({(float)x / n - 0.5f, 0.0f, (float)z / n - 0.5f});
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x) {
                uint32_t a = z * (n + 1) + x;
                m->faces.push_back({a, a + n + 1, a + n + 2, a + 1});   // facing up
            }
        break;
    }
    case AddShape::Capsule: {
        const int segs = 16, half = 4;
        const float r = 0.25f, mid = 0.25f;
        m->verts.push_back({0, -0.5f, 0});
        std::vector<uint32_t> rows;
        for (int k = 1; k <= half; ++k) {   // bottom half-ball
            float phi = -kPi * 0.5f + kPi * 0.5f * k / half;
            rows.push_back(ring(segs, -mid + r * std::sin(phi), r * std::cos(phi)));
        }
        for (int k = 0; k < half; ++k) {    // top half-ball
            float phi = kPi * 0.5f * k / half;
            rows.push_back(ring(segs, mid + r * std::sin(phi), r * std::cos(phi)));
        }
        uint32_t top = (uint32_t)m->verts.size();
        m->verts.push_back({0, 0.5f, 0});
        for (int i = 0; i < segs; ++i) {
            uint32_t j = (uint32_t)((i + 1) % segs);
            m->faces.push_back({0, rows.front() + i, rows.front() + j});
            for (size_t k = 0; k + 1 < rows.size(); ++k)
                m->faces.push_back({rows[k] + i, rows[k + 1] + i, rows[k + 1] + j, rows[k] + j});
            m->faces.push_back({rows.back() + i, top, rows.back() + j});
        }
        faceOut(*m);
        break;
    }
    default: break;
    }
    return m;
}

void append(EditMesh& dst, Selection& sel, const EditMesh& srcIn, const glm::mat4& place) {
    const bool dstUV = dst.uvs.size() == dst.faces.size() && !dst.faces.empty();
    // Laid out already: the new faces get the box layout they'd be drawn with (not all squashed to a point).
    EditMesh boxed;
    if (dstUV && srcIn.uvs.size() != srcIn.faces.size()) { boxed = srcIn; ensureUVs(boxed); }
    const EditMesh& src = boxed.faces.empty() ? srcIn : boxed;
    const bool srcUV = src.uvs.size() == src.faces.size();
    const uint32_t base = (uint32_t)dst.verts.size();
    for (auto& p : src.verts) dst.verts.push_back(glm::vec3(place * glm::vec4(p, 1.0f)));
    // A mirrored placement would turn the faces inside out.
    const bool flipIt = glm::determinant(glm::mat3(place)) < 0.0f;
    for (size_t fi = 0; fi < src.faces.size(); ++fi) {
        std::vector<uint32_t> f;
        for (uint32_t v : src.faces[fi]) f.push_back(v + base);
        std::vector<glm::vec2> u = srcUV ? src.uvs[fi] : std::vector<glm::vec2>();
        if (flipIt) { std::reverse(f.begin(), f.end()); std::reverse(u.begin(), u.end()); }
        dst.faces.push_back(std::move(f));
        if (dstUV || (dst.faces.size() == src.faces.size() && srcUV)) dst.uvs.push_back(srcUV ? u : std::vector<glm::vec2>(src.faces[fi].size(), glm::vec2(0.0f)));
    }
    if (dst.uvs.size() != dst.faces.size()) dst.uvs.clear();
    sel.assign(dst.verts.size(), 0);
    for (size_t i = base; i < dst.verts.size(); ++i) sel[i] = 1;
}

EditMesh extract(const EditMesh& m, const Selection& sel) {
    EditMesh out;
    out.smooth = m.smooth;
    const bool uv = m.uvs.size() == m.faces.size();
    std::map<uint32_t, uint32_t> remap;
    for (int fi : selectedFaces(m, sel)) {
        std::vector<uint32_t> f;
        for (uint32_t v : m.faces[fi]) {
            auto it = remap.find(v);
            if (it == remap.end()) {
                it = remap.emplace(v, (uint32_t)out.verts.size()).first;
                out.verts.push_back(m.verts[v]);
            }
            f.push_back(it->second);
        }
        out.faces.push_back(std::move(f));
        if (uv) out.uvs.push_back(m.uvs[fi]);
    }
    return out;
}

std::vector<char> carryFlags(const EditMesh& before, const std::vector<char>& flags, const EditMesh& after) {
    std::vector<char> out(after.verts.size(), 0);
    if (std::find(flags.begin(), flags.end(), 1) == flags.end()) return out;
    std::map<std::tuple<float, float, float>, char> at;
    for (size_t i = 0; i < before.verts.size() && i < flags.size(); ++i)
        if (flags[i]) at[{before.verts[i].x, before.verts[i].y, before.verts[i].z}] = 1;
    for (size_t i = 0; i < after.verts.size(); ++i)
        if (at.count({after.verts[i].x, after.verts[i].y, after.verts[i].z})) out[i] = 1;
    return out;
}

} // namespace MeshEdit
