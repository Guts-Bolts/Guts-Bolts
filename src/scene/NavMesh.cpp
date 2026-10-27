#include "NavMesh.h"
#include "Terrain.h"
#include "Physics.h"
#include "SceneNode.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <queue>
#include <unordered_map>

namespace {

constexpr int kDX[4] = {1, 0, -1, 0};
constexpr int kDZ[4] = {0, 1, 0, -1};
constexpr uint16_t kFar = 0xFFFF;
constexpr size_t kMaxColumns = 1'500'000;   // bigger worlds get wider columns
constexpr int kMaxSearch = 400'000;          // spans an A* search may look at

const Attribute* attr(const SceneNode* n, const char* name) {
    for (const auto& a : n->attributes) if (a.name == name) return &a;
    return nullptr;
}

// Where a straight up-and-down line at (x, z) goes through a turned box: lo..hi.
bool verticalThroughOBB(const OBB& o, float x, float z, float& lo, float& hi) {
    float t0 = -1e30f, t1 = 1e30f;
    glm::vec3 p(x, 0.0f, z);
    for (int i = 0; i < 3; ++i) {
        const glm::vec3& ax = o.axis[i];
        float d = ax.y;                                  // the line goes along +y
        float c = glm::dot(p - o.center, ax);            // where y = 0 is, along this axis
        if (std::abs(d) < 1e-6f) {
            if (std::abs(c) > o.half[i]) return false;   // parallel and outside this slab
            continue;
        }
        float a = (-o.half[i] - c) / d, b = (o.half[i] - c) / d;
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    lo = t0; hi = t1;
    return true;
}

} // namespace

const char* NavMesh::statusName(Status s) {
    switch (s) {
        case Status::Success: return "Success";
        case Status::ClosestNoPath: return "ClosestNoPath";
        case Status::NoPath: return "NoPath";
        case Status::FailStartNotEmpty: return "FailStartNotEmpty";
        case Status::FailFinishNotEmpty: return "FailFinishNotEmpty";
    }
    return "NoPath";
}

// ===========================================================================
// Baking
// ===========================================================================

void NavMesh::bake(const Physics& physics, const Settings& settings) {
    auto t0 = std::chrono::steady_clock::now();
    m_set = settings;
    m_spans.clear(); m_links.clear(); m_solids.clear(); m_colStart.clear(); m_solidStart.clear();
    m_areas.assign(1, std::string());
    m_baked = true;
    ++m_version;

    // 1. Which parts count: solid, anchored (loose parts move about), not "pass through".
    struct Src { const Physics::Collider* c; bool walkTop; uint16_t area; };
    std::vector<Src> src;
    std::unordered_map<std::string, uint16_t> areaIds;
    auto areaOf = [&](const std::string& name) -> uint16_t {
        if (name.empty()) return 0;
        auto [it, fresh] = areaIds.try_emplace(name, (uint16_t)m_areas.size());
        if (fresh) m_areas.push_back(name);
        return it->second;
    };
    AABB world{glm::vec3(1e30f), glm::vec3(-1e30f)};
    for (const auto& c : physics.m_colliders) {
        if (!c.solid || c.dynamic) continue;
        const SceneNode* n = c.node;
        if (const Attribute* a = attr(n, "PathfindingPassThrough"); a && a->type == Attribute::Bool && a->b) continue;
        glm::vec3 size = c.box.max - c.box.min;
        if (size.x > 1e5f || size.z > 1e5f) continue;   // nonsense sizes
        bool walkTop = true;
        if (c.rotated) {   // a tilted part: is its top gentle enough to walk on?
            float up = 0.0f;
            for (const auto& ax : c.obb.axis) up = std::max(up, std::abs(ax.y));
            walkTop = up >= std::cos(glm::radians(m_set.maxSlope));
        }
        std::string area;
        if (const Attribute* a = attr(n, "PathfindingLabel"); a && a->type == Attribute::String) area = a->s;
        else if ((int)n->material >= 0 && (int)n->material < kMaterialCount) area = kMaterialNames[(int)n->material];
        if (area == "Plastic" || area == "SmoothPlastic") area.clear();   // the everyday floors cost nothing extra
        src.push_back({&c, walkTop, areaOf(area)});
        world.min = glm::min(world.min, c.box.min);
        world.max = glm::max(world.max, c.box.max);
    }
    // The terrain is ground too.
    const Terrain* terrain = physics.m_terrain;
    if (terrain) {
        const float hw = terrain->halfWidth();
        world.min = glm::min(world.min, glm::vec3(-hw, Terrain::kMinHeight, -hw));
        world.max = glm::max(world.max, glm::vec3(hw, Terrain::kMaxHeight, hw));
    }
    if (src.empty() && !terrain) { m_w = m_h = 0; m_colStart.assign(1, 0); m_solidStart.assign(1, 0); return; }

    // 2. The columns. Huge worlds get wider columns so it stays quick.
    float cell = std::max(0.1f, m_set.cell);
    glm::vec2 ext(world.max.x - world.min.x, world.max.z - world.min.z);
    while ((size_t)(ext.x / cell + 2) * (size_t)(ext.y / cell + 2) > kMaxColumns) cell *= 1.25f;
    m_set.cell = cell;
    m_origin = glm::vec3(std::floor(world.min.x / cell) * cell, 0.0f, std::floor(world.min.z / cell) * cell);
    m_w = (int)std::ceil((world.max.x - m_origin.x) / cell) + 1;
    m_h = (int)std::ceil((world.max.z - m_origin.z) / cell) + 1;
    const size_t cols = (size_t)m_w * m_h;

    // 3. Each part fills in the columns it covers (a column counts as covered if the
    //    part reaches into it at all, so thin walls between column middles still count).
    struct Piece { float lo, hi; bool walkTop; uint16_t area; };
    std::vector<std::vector<Piece>> pieces(cols);
    for (const Src& s : src) {
        const auto& c = *s.c;
        int x0 = std::max(0, (int)std::floor((c.box.min.x - m_origin.x) / cell));
        int x1 = std::min(m_w - 1, (int)std::floor((c.box.max.x - m_origin.x) / cell - 1e-4f));
        int z0 = std::max(0, (int)std::floor((c.box.min.z - m_origin.z) / cell));
        int z1 = std::min(m_h - 1, (int)std::floor((c.box.max.z - m_origin.z) / cell - 1e-4f));
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                float lo = c.box.min.y, hi = c.box.max.y;
                if (c.rotated) {
                    // Look straight down through the turned box at a few spots in the column.
                    const float cx = m_origin.x + (x + 0.5f) * cell, cz = m_origin.z + (z + 0.5f) * cell, e = cell * 0.45f;
                    const glm::vec2 at[5] = {{cx, cz}, {cx - e, cz - e}, {cx + e, cz - e}, {cx - e, cz + e}, {cx + e, cz + e}};
                    bool any = false;
                    float mlo = 1e30f, mhi = -1e30f, top = -1e30f;
                    for (int k = 0; k < 5; ++k) {
                        float a, b;
                        if (!verticalThroughOBB(c.obb, at[k].x, at[k].y, a, b)) continue;
                        any = true;
                        mlo = std::min(mlo, a); mhi = std::max(mhi, b);
                        if (k == 0) top = b;
                    }
                    if (!any) continue;
                    lo = mlo;
                    hi = mhi;   // the highest corner: a hair high on a ramp, never inside it
                    (void)top;
                }
                pieces[(size_t)z * m_w + x].push_back({lo, hi, s.walkTop, s.area});
            }
    }

    // 3b. The terrain: solid from far below up to the highest ground in each column;
    //     too steep to stand on is a wall. Its material is the area (for Costs).
    if (terrain) {
        const float minUp = std::cos(glm::radians(m_set.maxSlope));
        uint16_t areas[kTerrainMaterialCount];
        for (int m = 0; m < kTerrainMaterialCount; ++m) areas[m] = areaOf(kTerrainMaterialNames[m]);
        for (int z = 0; z < m_h; ++z)
            for (int x = 0; x < m_w; ++x) {
                const float x0 = m_origin.x + x * cell, z0 = m_origin.z + z * cell;
                float top;
                if (!terrain->highestUnder(x0, z0, x0 + cell, z0 + cell, top)) continue;
                const float cx = x0 + cell * 0.5f, cz = z0 + cell * 0.5f;
                const bool walk = terrain->normalAt(cx, cz).y >= minUp;
                pieces[(size_t)z * m_w + x].push_back({Terrain::kMinHeight, top, walk, areas[(int)terrain->materialAt(cx, cz)]});
            }
    }

    // 4. Per column: merge overlapping pieces, then every solid top with room above is a floor.
    m_colStart.resize(cols + 1);
    m_solidStart.resize(cols + 1);
    for (size_t ci = 0; ci < cols; ++ci) {
        m_colStart[ci] = (uint32_t)m_spans.size();
        m_solidStart[ci] = (uint32_t)m_solids.size();
        auto& p = pieces[ci];
        if (p.empty()) continue;
        std::sort(p.begin(), p.end(), [](const Piece& a, const Piece& b) { return a.lo < b.lo; });
        std::vector<Piece> merged;
        for (const Piece& q : p) {
            if (!merged.empty() && q.lo <= merged.back().hi + 0.05f) {
                Piece& m = merged.back();
                if (q.hi >= m.hi) { m.walkTop = q.walkTop; m.area = q.area; }
                m.hi = std::max(m.hi, q.hi);
            } else {
                merged.push_back(q);
            }
        }
        for (size_t i = 0; i < merged.size(); ++i) {
            m_solids.push_back({merged[i].lo, merged[i].hi});
            float ceil = i + 1 < merged.size() ? merged[i + 1].lo : 1e30f;
            if (!merged[i].walkTop || ceil - merged[i].hi < 1.0f) continue;   // too steep, or no room to stand
            Span s{};
            s.floor = merged[i].hi;
            s.ceil = ceil;
            s.col = (int32_t)ci;
            for (int d = 0; d < 4; ++d) s.next[d] = -1;
            s.area = merged[i].area;
            m_spans.push_back(s);
        }
    }
    m_colStart[cols] = (uint32_t)m_spans.size();
    m_solidStart[cols] = (uint32_t)m_solids.size();
    pieces.clear();
    pieces.shrink_to_fit();

    // 5. Join each floor to the one next door you can step to.
    const float minRoom = 1.0f;
    std::vector<uint8_t> wallSide(m_spans.size(), 0), edgeSide(m_spans.size(), 0);
    for (size_t si = 0; si < m_spans.size(); ++si) {
        Span& s = m_spans[si];
        const int x = s.col % m_w, z = s.col / m_w;
        for (int d = 0; d < 4; ++d) {
            const int nx = x + kDX[d], nz = z + kDZ[d];
            if (nx < 0 || nz < 0 || nx >= m_w || nz >= m_h) { edgeSide[si] = 1; continue; }
            const int nc = nz * m_w + nx;
            int best = -1;
            float bestDy = 1e30f;
            for (uint32_t k = m_colStart[nc]; k < m_colStart[nc + 1]; ++k) {
                const Span& t = m_spans[k];
                float dy = std::abs(t.floor - s.floor);
                if (dy > m_set.maxClimb) continue;
                if (std::min(s.ceil, t.ceil) - std::max(s.floor, t.floor) < minRoom) continue;
                if (dy < bestDy) { bestDy = dy; best = (int)k; }
            }
            s.next[d] = best;
            if (best >= 0) continue;
            // No floor to step to that way: a wall (something solid at body height) or an edge.
            if (blockedAt(nc, s.floor + m_set.maxClimb, s.floor + m_set.bodyHeight)) wallSide[si] = 1;
            else edgeSide[si] = 1;
        }
    }

    // 6. How far each floor is from a wall and from an edge (counting columns).
    auto spread = [&](const std::vector<uint8_t>& seed, uint16_t Span::*field) {
        std::deque<int> q;
        for (size_t i = 0; i < m_spans.size(); ++i) {
            m_spans[i].*field = seed[i] ? 0 : kFar;
            if (seed[i]) q.push_back((int)i);
        }
        while (!q.empty()) {
            int i = q.front(); q.pop_front();
            uint16_t nd = (uint16_t)(m_spans[i].*field + 1);
            for (int d = 0; d < 4; ++d) {
                int j = m_spans[i].next[d];
                if (j >= 0 && m_spans[j].*field > nd) { m_spans[j].*field = nd; q.push_back(j); }
            }
        }
    };
    spread(wallSide, &Span::wallDist);
    spread(edgeSide, &Span::edgeDist);

    // 7. Links: jump up onto ledges, walk off drops, and jump across gaps. They start
    //    from floors up to a few columns back from the edge, so a character that has
    //    to keep clear of the wall still has somewhere to take off from.
    std::vector<std::vector<Link>> links(m_spans.size());
    const int gapCols = std::max(2, (int)std::floor(m_set.jumpGap / cell));
    const int back = std::max(1, (int)std::ceil(1.25f / cell));   // how far back a take-off can be
    static const int kD8X[8] = {1, 0, -1, 0, 1, -1, 1, -1};
    static const int kD8Z[8] = {0, 1, 0, -1, 1, 1, -1, -1};
    for (size_t si = 0; si < m_spans.size(); ++si) {
        const Span& s = m_spans[si];
        if (s.edgeDist > back && s.wallDist > back) continue;
        const float flyLo = s.floor + m_set.maxClimb, flyHi = s.floor + m_set.maxClimb + m_set.bodyHeight;
        for (int d = 0; d < 8; ++d) {
            const float stepLen = (d < 4 ? 1.0f : 1.41421f) * cell;
            // Walk along the floor (straight ways only) to where it stops.
            int cur = (int)si, j = 0;
            if (d < 4)
                while (j < back && m_spans[cur].next[d] >= 0) { cur = m_spans[cur].next[d]; ++j; }
            if (d < 4 && m_spans[cur].next[d] >= 0) continue;   // the floor just keeps going
            const Span& e = m_spans[cur];                       // the last floor before the edge / wall
            if (std::abs(e.floor - s.floor) > m_set.maxClimb) continue;
            const int ex = e.col % m_w, ez = e.col / m_w;
            const int x1 = ex + kD8X[d], z1 = ez + kD8Z[d];
            if (x1 < 0 || z1 < 0 || x1 >= m_w || z1 >= m_h) continue;
            const int c1 = z1 * m_w + x1;
            if (d < 4) {
                for (uint32_t t = m_colStart[c1]; t < m_colStart[c1 + 1]; ++t) {
                    const Span& o = m_spans[t];
                    const float dy = o.floor - e.floor;
                    const float len = stepLen * (j + 1);
                    if (dy > m_set.maxClimb && dy <= m_set.jumpHeight && e.ceil >= o.floor + m_set.bodyHeight * 0.8f)
                        links[si].push_back({(int32_t)t, true, len});          // jump up onto it
                    else if (dy < -m_set.maxClimb && dy >= -m_set.maxDrop && o.ceil >= e.floor + m_set.bodyHeight * 0.8f) {
                        // Walk off the edge. Right at the bottom may be too close to the wall
                        // you just came off for a wide character, so a few steps further out too.
                        int land = (int)t;
                        for (int k = 0; k <= back && land >= 0; ++k) {
                            links[si].push_back({(int32_t)land, false, len + stepLen * k});
                            land = m_spans[land].next[d];
                        }
                    }
                }
            } else if (cur != (int)si) {
                continue;
            }
            // A gap: nothing to stand on next door, and nothing in the way of the jump.
            bool floorNext = false;
            for (uint32_t t = m_colStart[c1]; t < m_colStart[c1 + 1]; ++t)
                if (std::abs(m_spans[t].floor - e.floor) <= m_set.maxDrop * 0.5f) floorNext = true;
            if (floorNext || blockedAt(c1, flyLo, flyHi)) continue;
            for (int k = 2; k <= gapCols; ++k) {
                const int nx = ex + kD8X[d] * k, nz = ez + kD8Z[d] * k;
                if (nx < 0 || nz < 0 || nx >= m_w || nz >= m_h) break;
                const int nc = nz * m_w + nx;
                if (blockedAt(nc, flyLo, flyHi)) break;
                bool landed = false;
                for (uint32_t t = m_colStart[nc]; t < m_colStart[nc + 1]; ++t) {
                    const float dy = m_spans[t].floor - e.floor;
                    if (dy <= m_set.maxClimb && dy >= -m_set.maxDrop * 0.5f) {
                        links[si].push_back({(int32_t)t, true, stepLen * (k + j)});
                        landed = true;
                    }
                }
                if (landed) break;
            }
        }
    }
    for (size_t si = 0; si < m_spans.size(); ++si) {
        m_spans[si].firstLink = (uint32_t)m_links.size();
        m_spans[si].linkCount = (uint32_t)links[si].size();
        m_links.insert(m_links.end(), links[si].begin(), links[si].end());
    }

    m_bakeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

bool NavMesh::blockedAt(int col, float lo, float hi) const {
    for (uint32_t k = m_solidStart[col]; k < m_solidStart[col + 1]; ++k)
        if (m_solids[k].lo < hi && m_solids[k].hi > lo) return true;
    return false;
}

// ===========================================================================
// Questions
// ===========================================================================

glm::vec3 NavMesh::center(const Span& s) const {
    const int x = s.col % m_w, z = s.col / m_w;
    return {m_origin.x + (x + 0.5f) * m_set.cell, s.floor, m_origin.z + (z + 0.5f) * m_set.cell};
}

bool NavMesh::fits(const Span& s, const Agent& agent) const {
    if (s.ceil - s.floor < agent.height) return false;
    // Keep the body clear of walls; at drop-offs the middle of the body just has to be on the floor.
    const float c = m_set.cell;
    int wall = (int)std::floor((agent.radius - 0.5f * c) / c + 0.5f);
    int edge = (int)std::floor((agent.radius * 0.5f - 0.5f * c) / c + 0.5f);
    return s.wallDist >= std::max(0, wall) && s.edgeDist >= std::max(0, edge);
}

float NavMesh::areaCost(const Span& s, const Agent& agent) const {
    if (s.area == 0 || agent.costs.empty()) return 1.0f;
    auto it = agent.costs.find(m_areas[s.area]);
    if (it == agent.costs.end()) return 1.0f;
    return it->second;
}

int NavMesh::spanAt(const glm::vec3& p, const Agent& agent, float range) const {
    if (!m_baked || m_w == 0) return -1;
    const float c = m_set.cell;
    const int cx = (int)std::floor((p.x - m_origin.x) / c), cz = (int)std::floor((p.z - m_origin.z) / c);
    const int rings = std::max(0, (int)std::ceil(range / c));
    int best = -1;
    float bestD = 1e30f;
    for (int r = 0; r <= rings; ++r) {
        for (int z = cz - r; z <= cz + r; ++z)
            for (int x = cx - r; x <= cx + r; ++x) {
                if (std::max(std::abs(x - cx), std::abs(z - cz)) != r) continue;   // just this ring
                if (x < 0 || z < 0 || x >= m_w || z >= m_h) continue;
                const int col = z * m_w + x;
                for (uint32_t k = m_colStart[col]; k < m_colStart[col + 1]; ++k) {
                    const Span& s = m_spans[k];
                    if (!fits(s, agent) || areaCost(s, agent) > 1e8f) continue;
                    // Feet a little under the floor still count (standing on it); far above
                    // is fine within the drop height (falling onto it).
                    float dy = p.y - s.floor;
                    if (dy < -1.0f || dy > m_set.maxDrop) continue;
                    glm::vec3 cpos = center(s);
                    float d = glm::length(glm::vec2(cpos.x - p.x, cpos.z - p.z)) + std::abs(dy) * 0.5f;
                    if (d < bestD) { bestD = d; best = (int)k; }
                }
            }
        // Found one in this ring: a further ring can't be much closer.
        if (best >= 0 && bestD < (r + 1) * c) break;
    }
    return best;
}

bool NavMesh::closestPoint(const glm::vec3& p, const Agent& agent, float range, glm::vec3& out) const {
    int s = spanAt(p, agent, range);
    if (s < 0) return false;
    out = center(m_spans[s]);
    return true;
}

bool NavMesh::walkable(const glm::vec3& p, const Agent& agent) const {
    return spanAt(p, agent, 0.0f) >= 0;
}

// The joined floor one column over (dx, dz each -1..1), keeping the agent's needs.
int NavMesh::stepToward(int from, int dx, int dz, const Agent& agent) const {
    auto ortho = [&](int i, int ddx, int ddz) -> int {
        for (int d = 0; d < 4; ++d)
            if (kDX[d] == ddx && kDZ[d] == ddz) {
                int j = m_spans[i].next[d];
                return j >= 0 && fits(m_spans[j], agent) ? j : -1;
            }
        return -1;
    };
    if (dx == 0 || dz == 0) return ortho(from, dx, dz);
    // Diagonal: both ways round the corner must be open and arrive at the same floor.
    int a = ortho(from, dx, 0), b = ortho(from, 0, dz);
    if (a < 0 || b < 0) return -1;
    int a2 = ortho(a, 0, dz), b2 = ortho(b, dx, 0);
    return a2 >= 0 && a2 == b2 ? a2 : -1;
}

// Straight walk from floor a to floor b on the mesh (no jumps), same cost all the way.
bool NavMesh::lineClear(int a, int b, const Agent& agent) const {
    const float c = m_set.cell;
    const glm::vec3 pa = center(m_spans[a]), pb = center(m_spans[b]);
    const float cost = areaCost(m_spans[a], agent);
    const glm::vec2 d(pb.x - pa.x, pb.z - pa.z);
    const float len = glm::length(d);
    if (len < 1e-4f) return a == b;
    int cur = a;
    int cx = m_spans[a].col % m_w, cz = m_spans[a].col / m_w;
    const int steps = (int)std::ceil(len / (c * 0.25f));
    for (int i = 1; i <= steps; ++i) {
        const float t = (float)i / steps;
        const int nx = (int)std::floor((pa.x + d.x * t - m_origin.x) / c);
        const int nz = (int)std::floor((pa.z + d.y * t - m_origin.z) / c);
        for (int guard = 0; (cx != nx || cz != nz) && guard < 4; ++guard) {
            const int ddx = (nx > cx) - (nx < cx), ddz = (nz > cz) - (nz < cz);
            const int nxt = stepToward(cur, ddx, ddz, agent);
            if (nxt < 0 || std::abs(areaCost(m_spans[nxt], agent) - cost) > 1e-3f) return false;
            cur = nxt;
            cx += ddx;
            cz += ddz;
        }
        if (cx != nx || cz != nz) return false;
    }
    return cur == b;
}

bool NavMesh::straightWalk(const glm::vec3& a, const glm::vec3& b, const Agent& agent, glm::vec3* hit) const {
    int sa = spanAt(a, agent, m_set.cell * 2.0f), sb = spanAt(b, agent, m_set.cell * 2.0f);
    if (sa < 0) { if (hit) *hit = a; return false; }
    if (sb >= 0 && lineClear(sa, sb, agent)) return true;
    if (hit) {   // walk it until it stops, for "how far can I get"
        const float c = m_set.cell;
        int cur = sa;
        glm::vec3 last = center(m_spans[sa]);
        glm::vec2 d(b.x - a.x, b.z - a.z);
        float len = glm::length(d);
        int steps = (int)std::ceil(len / (c * 0.25f));
        for (int i = 1; i <= steps; ++i) {
            glm::vec2 p = glm::vec2(a.x, a.z) + d * ((float)i / steps);
            int nx = (int)std::floor((p.x - m_origin.x) / c), nz = (int)std::floor((p.y - m_origin.z) / c);
            int cx = m_spans[cur].col % m_w, cz = m_spans[cur].col / m_w;
            if (nx == cx && nz == cz) continue;
            int nxt = stepToward(cur, std::clamp(nx - cx, -1, 1), std::clamp(nz - cz, -1, 1), agent);
            if (nxt < 0) break;
            cur = nxt;
            last = center(m_spans[cur]);
        }
        *hit = last;
    }
    return false;
}

NavMesh::Status NavMesh::findPath(const glm::vec3& start, const glm::vec3& goal, const Agent& agent,
                                  std::vector<Waypoint>& out) const {
    out.clear();
    if (!m_baked) return Status::NoPath;
    const int s0 = spanAt(start, agent, 2.0f);
    if (s0 < 0) return Status::FailStartNotEmpty;
    int g0 = spanAt(goal, agent, 2.0f);
    const bool goalOnMesh = g0 >= 0;
    const glm::vec3 goalC = goal;

    // A* over the floors: plain steps (8 ways) plus the jump / drop links.
    struct Rec { float g; int parent; bool jump; bool closed; };
    std::unordered_map<int, Rec> rec;
    rec.reserve(4096);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    auto h = [&](int i) { return glm::length(center(m_spans[i]) - goalC); };
    rec[s0] = {0.0f, -1, false, false};
    open.push({h(s0), s0});
    int found = -1, closest = s0;
    float closestH = h(s0);
    int visits = 0;
    while (!open.empty() && visits < kMaxSearch) {
        auto [f, i] = open.top();
        open.pop();
        Rec& r = rec[i];
        if (r.closed) continue;
        r.closed = true;
        ++visits;
        if (i == g0) { found = i; break; }
        float hi = h(i);
        if (hi < closestH) { closestH = hi; closest = i; }
        const float gi = r.g;
        auto relax = [&, i = i](int j, float len, bool jump) {
            const Span& t = m_spans[j];
            if (!fits(t, agent)) return;
            float cost = areaCost(t, agent);
            if (cost > 1e8f) return;
            float ng = gi + len * cost + (jump ? 1.0f : 0.0f);
            auto it = rec.find(j);
            if (it != rec.end() && (it->second.closed || it->second.g <= ng)) return;
            rec[j] = {ng, i, jump, false};
            open.push({ng + h(j), j});
        };
        const float c = m_set.cell;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dz) continue;
                int j = stepToward(i, dx, dz, agent);
                if (j >= 0) relax(j, (dx && dz ? 1.41421f : 1.0f) * c, false);
            }
        const Span& s = m_spans[i];
        for (uint32_t k = 0; k < s.linkCount; ++k) {
            const Link& l = m_links[s.firstLink + k];
            if (l.jump && !agent.canJump) continue;
            relax(l.to, l.length, l.jump);
        }
    }

    Status status = Status::Success;
    if (found < 0) {
        if (closest == s0) return goalOnMesh ? Status::NoPath : Status::FailFinishNotEmpty;
        found = closest;
        status = Status::ClosestNoPath;
    }

    // The floors along the way, start first.
    std::vector<int> chain;
    std::vector<bool> jumpTo;
    for (int i = found; i >= 0; i = rec[i].parent) { chain.push_back(i); jumpTo.push_back(rec[i].jump); }
    std::reverse(chain.begin(), chain.end());
    std::reverse(jumpTo.begin(), jumpTo.end());

    // Pull the string tight: skip floors while a straight walk gets there anyway.
    std::vector<int> keep{0};
    size_t a = 0;
    while (a + 1 < chain.size()) {
        size_t best = a + 1;
        const size_t limit = std::min(chain.size() - 1, a + 96);
        for (size_t b = limit; b > a + 1; --b) {
            bool jumps = false;
            for (size_t k = a + 1; k <= b; ++k) if (jumpTo[k]) { jumps = true; break; }
            if (jumps) continue;
            // A drop link (walking off an edge) also has to stay a corner.
            bool linked = false;
            for (size_t k = a + 1; k <= b && !linked; ++k) {
                const Span& p = m_spans[chain[k - 1]];
                const Span& q = m_spans[chain[k]];
                if (std::abs(p.floor - q.floor) > m_set.maxClimb) linked = true;
            }
            if (linked) continue;
            if (lineClear(chain[a], chain[b], agent)) { best = b; break; }
        }
        // Before a jump or drop, keep the floor at its edge (where to take off from).
        keep.push_back((int)best);
        a = best;
    }

    // Waypoints, with extra ones so none are further apart than WaypointSpacing.
    auto labelOf = [&](int i) { return m_areas[m_spans[i].area]; };
    out.push_back({start, Action::Walk, labelOf(chain[0])});
    for (size_t k = 1; k < keep.size(); ++k) {
        const int i = chain[keep[k]];
        glm::vec3 p = center(m_spans[i]);
        if (k + 1 == keep.size() && status == Status::Success) p = glm::vec3(goal.x, m_spans[i].floor, goal.z);
        const glm::vec3 prev = out.back().pos;
        const bool jump = jumpTo[keep[k]];
        if (!jump && agent.spacing > 0.1f) {
            float len = glm::length(glm::vec2(p.x - prev.x, p.z - prev.z));
            int extra = (int)std::floor(len / agent.spacing);
            if (len - extra * agent.spacing < 0.25f * agent.spacing) --extra;
            for (int e = 1; e <= extra; ++e) {
                glm::vec3 q = glm::mix(prev, p, (e * agent.spacing) / len);
                int si = spanAt(q, agent, m_set.cell);
                if (si >= 0) q.y = m_spans[si].floor;
                out.push_back({q, Action::Walk, si >= 0 ? labelOf(si) : labelOf(i)});
            }
        }
        out.push_back({p, jump ? Action::Jump : Action::Walk, labelOf(i)});
    }
    return status;
}

bool NavMesh::randomPoint(const glm::vec3& around, float radius, const Agent& agent, uint32_t seed, glm::vec3& out) const {
    if (!m_baked || m_spans.empty()) return false;
    uint32_t x = seed * 2654435761u + 12345u;
    auto rnd = [&]() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; };
    int from = radius > 0.0f ? spanAt(around, agent, 4.0f) : -1;
    if (radius > 0.0f && from < 0) return false;
    if (radius <= 0.0f) {   // anywhere at all
        for (int tries = 0; tries < 64; ++tries) {
            const Span& s = m_spans[rnd() % m_spans.size()];
            if (fits(s, agent) && areaCost(s, agent) < 1e8f) { out = center(s); return true; }
        }
        return false;
    }
    // Somewhere you can walk to from `around`, within `radius`: wander the joined floors.
    std::vector<int> seen{from};
    std::unordered_map<int, bool> visited{{from, true}};
    for (size_t q = 0; q < seen.size() && seen.size() < 20000; ++q) {
        int i = seen[q];
        for (int d = 0; d < 4; ++d) {
            int j = m_spans[i].next[d];
            if (j < 0 || visited.count(j) || !fits(m_spans[j], agent)) continue;
            if (glm::length(center(m_spans[j]) - around) > radius) continue;
            visited[j] = true;
            seen.push_back(j);
        }
    }
    out = center(m_spans[seen[rnd() % seen.size()]]);
    return true;
}

// ===========================================================================
// Drawing (Studio)
// ===========================================================================

void NavMesh::buildDrawing(std::vector<DrawVertex>& tris, std::vector<DrawVertex>& lines, const Agent& agent) const {
    tris.clear();
    lines.clear();
    if (!m_baked) return;
    const float c = m_set.cell, lift = 0.04f;
    // Rows of joined floors at the same height become one long strip (far fewer triangles).
    std::vector<uint8_t> done(m_spans.size(), 0);
    auto colorOf = [&](const Span& s) {
        if (s.area != 0) {   // labelled / special material floors get their own colour
            uint32_t hsh = 2166136261u;
            for (char ch : m_areas[s.area]) hsh = (hsh ^ (uint8_t)ch) * 16777619u;
            return glm::vec4(0.35f + 0.5f * ((hsh & 255) / 255.0f), 0.3f + 0.4f * (((hsh >> 8) & 255) / 255.0f),
                             0.3f + 0.5f * (((hsh >> 16) & 255) / 255.0f), 0.45f);
        }
        return glm::vec4(0.15f, 0.55f, 1.0f, 0.38f);
    };
    for (size_t si = 0; si < m_spans.size(); ++si) {
        if (done[si] || !fits(m_spans[si], agent)) continue;
        // Grow to the right (+x) while the next floor is joined, fits and is the same kind.
        int end = (int)si;
        done[si] = 1;
        while (true) {
            int j = m_spans[end].next[0];
            if (j < 0 || done[j] || !fits(m_spans[j], agent) || m_spans[j].area != m_spans[si].area ||
                std::abs(m_spans[j].floor - m_spans[si].floor) > 1e-3f) break;
            done[j] = 1;
            end = j;
        }
        const glm::vec3 a = center(m_spans[si]), b = center(m_spans[end]);
        const float h = c * 0.5f - c * 0.06f;   // a hairline gap between strips shows the cells
        glm::vec3 p0(a.x - h, a.y + lift, a.z - h), p1(b.x + h, b.y + lift, b.z - h), p2(b.x + h, b.y + lift, b.z + h),
            p3(a.x - h, a.y + lift, a.z + h);
        glm::vec4 col = colorOf(m_spans[si]);
        tris.insert(tris.end(), {{p0, col}, {p1, col}, {p2, col}, {p0, col}, {p2, col}, {p3, col}});
    }
    // Links: yellow arcs for jumps, orange lines for drops.
    for (size_t si = 0; si < m_spans.size(); ++si) {
        const Span& s = m_spans[si];
        if (!fits(s, agent)) continue;
        // Only from the floor right at an edge, and only the shortest jump and the
        // shortest drop from each (there are many more, but they'd be a hairball).
        if (s.edgeDist > 1 && s.wallDist > 1) continue;
        int best[2] = {-1, -1};
        for (uint32_t k = 0; k < s.linkCount; ++k) {
            const Link& l = m_links[s.firstLink + k];
            if ((l.jump && !agent.canJump) || !fits(m_spans[l.to], agent)) continue;
            int& b = best[l.jump ? 1 : 0];
            if (b < 0 || l.length < m_links[s.firstLink + b].length) b = (int)k;
        }
        for (int k : best) {
            if (k < 0) continue;
            const Link& l = m_links[s.firstLink + k];
            if (((si * 2654435761u) >> 7) % 3 != 0) continue;   // and a third of those: enough to see where
            glm::vec3 p = center(s) + glm::vec3(0, lift, 0), q = center(m_spans[l.to]) + glm::vec3(0, lift, 0);
            glm::vec4 col = l.jump ? glm::vec4(1.0f, 0.9f, 0.1f, 0.9f) : glm::vec4(1.0f, 0.5f, 0.1f, 0.9f);
            const int segs = l.jump ? 6 : 1;
            glm::vec3 prev = p;
            for (int i = 1; i <= segs; ++i) {
                float t = (float)i / segs;
                glm::vec3 m = glm::mix(p, q, t);
                if (l.jump) m.y += std::sin(t * 3.14159f) * 0.6f;
                lines.push_back({prev, col});
                lines.push_back({m, col});
                prev = m;
            }
        }
    }
}
