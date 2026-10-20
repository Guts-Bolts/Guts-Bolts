#include "EditMesh.h"
#include "../renderer/MeshLibrary.h"
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace MeshEdit {

namespace {
constexpr float kPi = 3.14159265f;

uint64_t edgeKey(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return ((uint64_t)a << 32) | b;
}

// Newell's method: works for any polygon, and its length is twice the area.
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

// Faces must keep at least three different corners; drop repeats next to each other.
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
} // namespace

// ---------------------------------------------------------------------------
// Making meshes
// ---------------------------------------------------------------------------

std::shared_ptr<EditMesh> fromPrimitive(PrimitiveType type) {
    auto m = std::make_shared<EditMesh>();
    auto ring = [&](int segs, float y, float r) {
        uint32_t base = (uint32_t)m->verts.size();
        for (int i = 0; i < segs; ++i) {
            float a = 2.0f * kPi * i / segs;
            m->verts.push_back({std::cos(a) * r, y, std::sin(a) * r});
        }
        return base;
    };
    switch (type) {
        case PrimitiveType::Plane:
            m->verts = {{-0.5f, 0, -0.5f}, {0.5f, 0, -0.5f}, {0.5f, 0, 0.5f}, {-0.5f, 0, 0.5f}};
            m->faces = {{0, 3, 2, 1}};   // facing up
            break;
        case PrimitiveType::Cylinder: {
            const int segs = 16;
            uint32_t bot = ring(segs, -0.5f, 0.5f), top = ring(segs, 0.5f, 0.5f);
            std::vector<uint32_t> capTop, capBot;
            for (int i = 0; i < segs; ++i) {
                uint32_t j = (uint32_t)((i + 1) % segs);
                m->faces.push_back({bot + i, top + i, top + j, bot + j});
                capTop.push_back(top + (segs - 1 - i));
                capBot.push_back(bot + i);
            }
            m->faces.push_back(capTop);
            m->faces.push_back(capBot);
            break;
        }
        case PrimitiveType::Sphere: {
            const int segs = 16, rings = 8;
            m->verts.push_back({0, -0.5f, 0});                        // bottom pole
            std::vector<uint32_t> rowStart;
            for (int r = 1; r < rings; ++r) {
                float phi = kPi * r / rings - kPi * 0.5f;
                rowStart.push_back(ring(segs, 0.5f * std::sin(phi), 0.5f * std::cos(phi)));
            }
            uint32_t topPole = (uint32_t)m->verts.size();
            m->verts.push_back({0, 0.5f, 0});
            for (int i = 0; i < segs; ++i) {
                uint32_t j = (uint32_t)((i + 1) % segs);
                m->faces.push_back({0, rowStart.front() + i, rowStart.front() + j});
                for (size_t r = 0; r + 1 < rowStart.size(); ++r)
                    m->faces.push_back({rowStart[r] + i, rowStart[r + 1] + i, rowStart[r + 1] + j, rowStart[r] + j});
                m->faces.push_back({rowStart.back() + i, topPole, rowStart.back() + j});
            }
            break;
        }
        default: {   // Cube
            m->verts = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
                        {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},  {0.5f, 0.5f, 0.5f},  {-0.5f, 0.5f, 0.5f}};
            m->faces = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {3, 7, 6, 2}, {0, 1, 5, 4}};
            break;
        }
    }
    // Make every face point outwards (the rings above are built by hand).
    for (auto& f : m->faces)
        if (glm::dot(newell(*m, f), faceCenter(*m, f)) < 0.0f) std::reverse(f.begin(), f.end());
    return m;
}

void attach(SceneNode& node, std::shared_ptr<EditMesh> mesh) {
    node.primitiveType = PrimitiveType::Mesh;
    node.editMesh = std::move(mesh);
    refresh(node);
}

EditMesh& own(SceneNode& node) {
    if (!node.editMesh) node.editMesh = fromPrimitive(PrimitiveType::Cube);
    else if (node.editMesh.use_count() > 1) node.editMesh = std::make_shared<EditMesh>(*node.editMesh);
    return *node.editMesh;
}

void refresh(SceneNode& node, const std::vector<glm::vec3>* posed) {
    if (!node.editMesh) return;
    // Posed by its bones: the same faces, the corners where the bones put them.
    EditMesh bent;
    if (posed && posed->size() == node.editMesh->verts.size()) {
        bent.verts = *posed;
        bent.faces = node.editMesh->faces;
        bent.smooth = node.editMesh->smooth;
        bent.uvs = node.editMesh->uvs;
    }
    const EditMesh& m = bent.verts.empty() ? *node.editMesh : bent;
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;

    std::vector<glm::vec3> smoothN;
    if (m.smooth) {
        smoothN.assign(m.verts.size(), glm::vec3(0.0f));
        for (auto& f : m.faces) {
            glm::vec3 n = newell(m, f);          // bigger faces count more
            for (uint32_t v : f) smoothN[v] += n;
        }
        for (auto& n : smoothN) n = glm::length(n) > 1e-12f ? glm::normalize(n) : glm::vec3(0, 1, 0);
    }
    const bool ownUVs = m.uvs.size() == m.faces.size();
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        const auto& f = m.faces[fi];
        if (f.size() < 3) continue;
        glm::vec3 n = faceNormal(m, f);
        if (ownUVs && m.uvs[fi].size() == f.size()) {   // laid out by hand (clothing templates)
            uint32_t base = (uint32_t)verts.size();
            for (size_t c = 0; c < f.size(); ++c)
                verts.push_back({m.verts[f[c]], m.smooth ? smoothN[f[c]] : n, m.uvs[fi][c]});
            for (uint32_t k = 1; k + 1 < (uint32_t)f.size(); ++k)
                idx.insert(idx.end(), {base, base + k, base + k + 1});
            continue;
        }
        // Box-style texture coordinates: project along the face's main direction.
        // (From the unbent shape, so a picture doesn't slide over a bending mesh.)
        const EditMesh& rest = *node.editMesh;
        glm::vec3 an = glm::abs(&m == &rest ? n : faceNormal(rest, f));
        int ax = an.x > an.y && an.x > an.z ? 0 : (an.y > an.z ? 1 : 2);
        int u = ax == 0 ? 2 : 0, v = ax == 1 ? 2 : 1;
        uint32_t base = (uint32_t)verts.size();
        for (uint32_t i : f) {
            const glm::vec3& p = rest.verts[i];
            verts.push_back({m.verts[i], m.smooth ? smoothN[i] : n, {p[u] + 0.5f, p[v] + 0.5f}});
        }
        for (uint32_t k = 1; k + 1 < (uint32_t)f.size(); ++k)
            idx.insert(idx.end(), {base, base + k, base + k + 1});
    }
    node.mesh = verts.empty() ? MeshLibrary::get(PrimitiveType::Cube) : std::make_shared<Mesh>(verts, idx);
}

void fit(SceneNode& node) {
    if (!node.editMesh || node.editMesh->verts.empty()) return;
    EditMesh& m = own(node);
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (auto& p : m.verts) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
    glm::vec3 c = (lo + hi) * 0.5f, e = hi - lo;
    for (int i = 0; i < 3; ++i) if (e[i] < 1e-4f) e[i] = 1.0f;   // flat on this axis: leave it
    if (glm::length(c) < 1e-6f && glm::length(e - glm::vec3(1.0f)) < 1e-6f) return;   // already fits

    glm::vec3 newPos = glm::vec3(node.transform.matrix() * glm::vec4(c, 1.0f));
    for (auto& p : m.verts) p = (p - c) / e;
    node.transform.position = newPos;
    node.transform.scale *= e;
    // Things inside the part (attachments, lights, ...) stay where they were in the world.
    const glm::vec3 shift = node.transform.scale / e * c;   // the middle moved this far (studs, before the resize)
    for (auto& ch : node.children) {
        if (ch->isBone()) { ch->transform.position -= shift; continue; }   // (bones are in studs)
        ch->transform.position = (ch->transform.position - c) / e;
        if (ch->isPart()) ch->transform.scale /= e;
    }
    for (auto& b : m.bind) b = b * glm::translate(glm::mat4(1.0f), shift);
    refresh(node);
}

// ---------------------------------------------------------------------------
// Looking at meshes
// ---------------------------------------------------------------------------

glm::vec3 faceNormal(const EditMesh& m, const std::vector<uint32_t>& f) {
    glm::vec3 n = newell(m, f);
    float len = glm::length(n);
    return len > 1e-12f ? n / len : glm::vec3(0, 1, 0);
}

glm::vec3 faceCenter(const EditMesh& m, const std::vector<uint32_t>& f) {
    glm::vec3 c(0.0f);
    for (uint32_t v : f) c += m.verts[v];
    return f.empty() ? c : c / (float)f.size();
}

std::vector<std::pair<uint32_t, uint32_t>> edges(const EditMesh& m) {
    std::vector<uint64_t> keys;
    for (auto& f : m.faces)
        for (size_t i = 0; i < f.size(); ++i) keys.push_back(edgeKey(f[i], f[(i + 1) % f.size()]));
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    std::vector<std::pair<uint32_t, uint32_t>> out;
    out.reserve(keys.size());
    for (uint64_t k : keys) out.push_back({(uint32_t)(k >> 32), (uint32_t)(k & 0xffffffffu)});
    return out;
}

std::vector<int> selectedFaces(const EditMesh& m, const Selection& sel) {
    std::vector<int> out;
    for (size_t i = 0; i < m.faces.size(); ++i) {
        bool all = !m.faces[i].empty();
        for (uint32_t v : m.faces[i]) if (v >= sel.size() || !sel[v]) { all = false; break; }
        if (all) out.push_back((int)i);
    }
    return out;
}

int countSelected(const Selection& sel) { return (int)std::count(sel.begin(), sel.end(), 1); }

void compact(EditMesh& m, Selection& sel) {
    std::vector<char> used(m.verts.size(), 0);
    for (auto& f : m.faces) for (uint32_t v : f) used[v] = 1;
    std::vector<uint32_t> remap(m.verts.size(), 0);
    std::vector<glm::vec3> verts;
    Selection nsel;
    for (size_t i = 0; i < m.verts.size(); ++i) {
        if (!used[i]) continue;
        remap[i] = (uint32_t)verts.size();
        verts.push_back(m.verts[i]);
        nsel.push_back(i < sel.size() ? sel[i] : 0);
    }
    for (auto& f : m.faces) for (uint32_t& v : f) v = remap[v];
    m.verts = std::move(verts);
    sel = std::move(nsel);
}

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

bool extrude(EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    std::vector<int> faces = selectedFaces(m, sel);
    std::map<uint32_t, uint32_t> copyOf;
    auto dup = [&](uint32_t v) {
        auto it = copyOf.find(v);
        if (it != copyOf.end()) return it->second;
        uint32_t n = (uint32_t)m.verts.size();
        m.verts.push_back(m.verts[v]);
        copyOf[v] = n;
        return n;
    };

    if (!faces.empty()) {
        // Region extrude: the selected faces move out as one piece and new
        // side walls fill the gap along the region's outline.
        std::map<uint64_t, int> uses;
        for (int fi : faces) {
            auto& f = m.faces[fi];
            for (size_t i = 0; i < f.size(); ++i) uses[edgeKey(f[i], f[(i + 1) % f.size()])]++;
        }
        std::vector<std::vector<uint32_t>> walls;
        for (int fi : faces) {
            auto& f = m.faces[fi];
            for (size_t i = 0; i < f.size(); ++i) {
                uint32_t a = f[i], b = f[(i + 1) % f.size()];
                if (uses[edgeKey(a, b)] == 1) walls.push_back({a, b, dup(b), dup(a)});
            }
        }
        for (int fi : faces) for (uint32_t& v : m.faces[fi]) v = dup(v);
        m.faces.insert(m.faces.end(), walls.begin(), walls.end());
    } else {
        // Edge extrude: each selected edge grows a new face.
        std::vector<std::pair<uint32_t, uint32_t>> picked;
        for (auto& f : m.faces)
            for (size_t i = 0; i < f.size(); ++i) {
                uint32_t a = f[i], b = f[(i + 1) % f.size()];
                if (sel[a] && sel[b]) picked.push_back({a, b});
            }
        std::sort(picked.begin(), picked.end(), [](auto x, auto y) { return edgeKey(x.first, x.second) < edgeKey(y.first, y.second); });
        picked.erase(std::unique(picked.begin(), picked.end(), [](auto x, auto y) {
            return edgeKey(x.first, x.second) == edgeKey(y.first, y.second); }), picked.end());
        if (picked.empty()) return false;
        // Wind opposite to the face the edge came from, so the new face continues the surface.
        for (auto [a, b] : picked) m.faces.push_back({b, a, dup(a), dup(b)});
    }
    std::fill(sel.begin(), sel.end(), 0);
    sel.resize(m.verts.size(), 0);
    for (auto [from, to] : copyOf) sel[to] = 1;
    compact(m, sel);   // corners inside the region were left behind unused
    return true;
}

bool inset(EditMesh& m, Selection& sel, float t) {
    sel.resize(m.verts.size(), 0);
    std::vector<int> faces = selectedFaces(m, sel);
    if (faces.empty()) return false;
    std::fill(sel.begin(), sel.end(), 0);
    for (int fi : faces) {
        std::vector<uint32_t> outer = m.faces[fi];
        glm::vec3 c = faceCenter(m, outer);
        std::vector<uint32_t> inner;
        for (uint32_t v : outer) {
            inner.push_back((uint32_t)m.verts.size());
            m.verts.push_back(glm::mix(m.verts[v], c, t));
        }
        m.faces[fi] = inner;
        for (size_t i = 0; i < outer.size(); ++i) {
            size_t j = (i + 1) % outer.size();
            m.faces.push_back({outer[i], outer[j], inner[j], inner[i]});
        }
    }
    sel.resize(m.verts.size(), 0);
    for (int fi : faces) for (uint32_t v : m.faces[fi]) sel[v] = 1;
    return true;
}

bool subdivide(EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    std::vector<int> faces = selectedFaces(m, sel);
    bool all = faces.empty();
    if (all) { faces.resize(m.faces.size()); std::iota(faces.begin(), faces.end(), 0); }
    std::map<uint64_t, uint32_t> mid;
    auto midpoint = [&](uint32_t a, uint32_t b) {
        uint64_t k = edgeKey(a, b);
        auto it = mid.find(k);
        if (it != mid.end()) return it->second;
        uint32_t n = (uint32_t)m.verts.size();
        m.verts.push_back((m.verts[a] + m.verts[b]) * 0.5f);
        mid[k] = n;
        return n;
    };
    std::vector<char> isPicked(m.faces.size(), 0);
    for (int fi : faces) isPicked[fi] = 1;
    std::vector<std::vector<uint32_t>> out;
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        if (!isPicked[fi]) continue;
        const auto f = m.faces[fi];
        size_t n = f.size();
        if (n == 3) {   // triangles split into four triangles
            uint32_t a = midpoint(f[0], f[1]), b = midpoint(f[1], f[2]), c = midpoint(f[2], f[0]);
            out.push_back({f[0], a, c}); out.push_back({a, f[1], b});
            out.push_back({c, b, f[2]}); out.push_back({a, b, c});
            continue;
        }
        uint32_t center = (uint32_t)m.verts.size();
        m.verts.push_back(faceCenter(m, f));
        for (size_t i = 0; i < n; ++i) {
            uint32_t prev = midpoint(f[(i + n - 1) % n], f[i]), next = midpoint(f[i], f[(i + 1) % n]);
            out.push_back({f[i], next, center, prev});
        }
    }
    size_t splitFaces = out.size();   // the split faces come first
    // Faces next to the split ones get the new edge points too, so there are no cracks.
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        if (isPicked[fi]) continue;
        std::vector<uint32_t> g;
        const auto& f = m.faces[fi];
        for (size_t i = 0; i < f.size(); ++i) {
            g.push_back(f[i]);
            auto it = mid.find(edgeKey(f[i], f[(i + 1) % f.size()]));
            if (it != mid.end()) g.push_back(it->second);
        }
        out.push_back(g);
    }
    m.faces = std::move(out);
    // Keep the whole split area selected.
    sel.assign(m.verts.size(), 0);
    if (!all) for (size_t i = 0; i < splitFaces; ++i) for (uint32_t v : m.faces[i]) sel[v] = 1;
    return true;
}

bool remove(EditMesh& m, Selection& sel, int what) {
    sel.resize(m.verts.size(), 0);
    size_t before = m.faces.size();
    std::vector<std::vector<uint32_t>> keep;
    for (auto& f : m.faces) {
        bool drop = false;
        if (what == 0) {                      // any selected corner
            for (uint32_t v : f) if (sel[v]) drop = true;
        } else if (what == 1) {               // any selected edge
            for (size_t i = 0; i < f.size(); ++i) if (sel[f[i]] && sel[f[(i + 1) % f.size()]]) drop = true;
        } else {                              // the whole face is selected
            drop = true;
            for (uint32_t v : f) if (!sel[v]) drop = false;
        }
        if (!drop) keep.push_back(f);
    }
    if (keep.size() == before && what != 0) return false;
    m.faces = std::move(keep);
    std::fill(sel.begin(), sel.end(), 0);
    compact(m, sel);
    return true;
}

bool merge(EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    if (countSelected(sel) < 2) return false;
    glm::vec3 c(0.0f);
    int n = 0;
    uint32_t keepV = 0;
    for (size_t i = 0; i < sel.size(); ++i) if (sel[i]) { if (!n) keepV = (uint32_t)i; c += m.verts[i]; ++n; }
    m.verts[keepV] = c / (float)n;
    for (auto& f : m.faces) for (uint32_t& v : f) if (sel[v]) v = keepV;
    cleanFaces(m);
    std::fill(sel.begin(), sel.end(), 0);
    sel[keepV] = 1;
    compact(m, sel);
    return true;
}

bool fill(EditMesh& m, Selection& sel) {
    sel.resize(m.verts.size(), 0);
    std::vector<uint32_t> pts;
    for (size_t i = 0; i < sel.size(); ++i) if (sel[i]) pts.push_back((uint32_t)i);
    if (pts.size() < 3) return false;
    // Order the points around their middle, in the plane they (roughly) share.
    glm::vec3 c(0.0f);
    for (uint32_t v : pts) c += m.verts[v];
    c /= (float)pts.size();
    glm::vec3 n(0.0f);
    for (size_t i = 0; i < pts.size(); ++i)
        n += glm::cross(m.verts[pts[i]] - c, m.verts[pts[(i + 1) % pts.size()]] - c);
    if (glm::length(n) < 1e-9f) {                     // try every pair for a direction
        for (uint32_t a : pts) for (uint32_t b : pts) {
            glm::vec3 k = glm::cross(m.verts[a] - c, m.verts[b] - c);
            if (glm::length(k) > glm::length(n)) n = k;
        }
    }
    if (glm::length(n) < 1e-9f) return false;
    n = glm::normalize(n);
    glm::vec3 ux = m.verts[pts[0]] - c;
    ux = glm::normalize(ux - n * glm::dot(ux, n));
    glm::vec3 uy = glm::cross(n, ux);
    std::sort(pts.begin(), pts.end(), [&](uint32_t a, uint32_t b) {
        glm::vec3 da = m.verts[a] - c, db = m.verts[b] - c;
        return std::atan2(glm::dot(da, uy), glm::dot(da, ux)) < std::atan2(glm::dot(db, uy), glm::dot(db, ux));
    });
    // Face outwards: away from the middle of the whole mesh.
    glm::vec3 meshC(0.0f);
    for (auto& p : m.verts) meshC += p;
    meshC /= (float)m.verts.size();
    if (glm::dot(n, c - meshC) < 0.0f) std::reverse(pts.begin(), pts.end());
    m.faces.push_back(pts);
    return true;
}

bool flip(EditMesh& m, const Selection& sel) {
    std::vector<int> faces = selectedFaces(m, sel);
    if (faces.empty()) for (auto& f : m.faces) std::reverse(f.begin(), f.end());
    else for (int fi : faces) std::reverse(m.faces[fi].begin(), m.faces[fi].end());
    return true;
}

} // namespace MeshEdit
