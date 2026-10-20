// UV mapping for meshes made in Modeling mode: laying the surface out flat so
// a picture can wrap onto it (Blender's U menu), seams, packing the pieces into
// the square, keeping the layout through edits, and a picture of the layout.
//
// "Unwrap" uses least squares conformal maps (LSCM, like Blender's Conformal
// unwrap): each piece is flattened keeping its angles as close as it can.
#include "EditMesh.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <unordered_map>

namespace MeshEdit {

namespace {
constexpr float kPi = 3.14159265f;

uint64_t edgeKey(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return ((uint64_t)a << 32) | b;
}

bool inStep(const EditMesh& m) {
    if (m.uvs.size() != m.faces.size()) return false;
    for (size_t i = 0; i < m.faces.size(); ++i) if (m.uvs[i].size() != m.faces[i].size()) return false;
    return true;
}

// The box mapping refresh() draws a face with when it has no UVs of its own.
std::vector<glm::vec2> boxUV(const EditMesh& m, const std::vector<uint32_t>& f) {
    glm::vec3 an = glm::abs(faceNormal(m, f));
    int ax = an.x > an.y && an.x > an.z ? 0 : (an.y > an.z ? 1 : 2);
    int u = ax == 0 ? 2 : 0, v = ax == 1 ? 2 : 1;
    std::vector<glm::vec2> out;
    for (uint32_t i : f) out.push_back({m.verts[i][u] + 0.5f, m.verts[i][v] + 0.5f});
    return out;
}

// Two directions along a flat surface facing n.
void planeAxes(glm::vec3 n, glm::vec3& t1, glm::vec3& t2) {
    n = glm::length(n) > 1e-9f ? glm::normalize(n) : glm::vec3(0, 1, 0);
    glm::vec3 up = std::abs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
    t1 = glm::normalize(glm::cross(up, n));
    t2 = glm::cross(n, t1);
}

std::vector<int> pickedOrAll(const EditMesh& m, const Selection& sel) {
    std::vector<int> faces = selectedFaces(m, sel);
    if (faces.empty()) { faces.resize(m.faces.size()); std::iota(faces.begin(), faces.end(), 0); }
    return faces;
}

struct Groups {
    std::vector<uint32_t> up;
    explicit Groups(size_t n) : up(n) { std::iota(up.begin(), up.end(), 0u); }
    uint32_t root(uint32_t a) { while (up[a] != a) a = up[a] = up[up[a]]; return a; }
    void join(uint32_t a, uint32_t b) { a = root(a); b = root(b); if (a != b) up[b] = a; }
};

std::set<uint64_t> seamSet(const EditMesh& m) {
    std::set<uint64_t> s;
    for (auto [a, b] : m.seams) s.insert(edgeKey(a, b));
    return s;
}

// Split faces into pieces joined across edges that aren't seams (and, for
// Smart UV Project, only where the faces point roughly the same way).
std::vector<std::vector<int>> pieces(const EditMesh& m, const std::vector<int>& faces, bool bySeams, float minDot) {
    std::set<uint64_t> seams = bySeams ? seamSet(m) : std::set<uint64_t>();
    std::unordered_map<uint64_t, std::vector<int>> byEdge;
    for (size_t k = 0; k < faces.size(); ++k) {
        const auto& f = m.faces[faces[k]];
        for (size_t i = 0; i < f.size(); ++i) byEdge[edgeKey(f[i], f[(i + 1) % f.size()])].push_back((int)k);
    }
    std::vector<glm::vec3> n(faces.size());
    for (size_t k = 0; k < faces.size(); ++k) n[k] = faceNormal(m, m.faces[faces[k]]);
    if (minDot <= -1.0f) {
        Groups g(faces.size());
        for (auto& [key, list] : byEdge)
            if (!seams.count(key)) for (size_t i = 1; i < list.size(); ++i) g.join((uint32_t)list[0], (uint32_t)list[i]);
        std::map<uint32_t, std::vector<int>> out;
        for (size_t k = 0; k < faces.size(); ++k) out[g.root((uint32_t)k)].push_back(faces[k]);
        std::vector<std::vector<int>> v;
        for (auto& [r, list] : out) v.push_back(std::move(list));
        return v;
    }
    // Grow pieces from the biggest faces, taking neighbours that face the same way as the seed.
    std::vector<size_t> order(faces.size());
    std::iota(order.begin(), order.end(), 0);
    std::vector<float> area(faces.size());
    for (size_t k = 0; k < faces.size(); ++k) {
        glm::vec3 nn(0.0f);
        const auto& f = m.faces[faces[k]];
        for (size_t i = 1; i + 1 < f.size(); ++i) nn += glm::cross(m.verts[f[i]] - m.verts[f[0]], m.verts[f[i + 1]] - m.verts[f[0]]);
        area[k] = glm::length(nn);
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return area[a] > area[b]; });
    std::vector<int> owner(faces.size(), -1);
    std::vector<std::vector<int>> out;
    for (size_t seed : order) {
        if (owner[seed] >= 0) continue;
        int id = (int)out.size();
        out.push_back({});
        std::vector<size_t> stack{seed};
        owner[seed] = id;
        while (!stack.empty()) {
            size_t k = stack.back(); stack.pop_back();
            out[id].push_back(faces[k]);
            const auto& f = m.faces[faces[k]];
            for (size_t i = 0; i < f.size(); ++i)
                for (int o : byEdge[edgeKey(f[i], f[(i + 1) % f.size()])])
                    if (owner[o] < 0 && glm::dot(n[o], n[seed]) >= minDot) { owner[o] = id; stack.push_back((size_t)o); }
        }
    }
    return out;
}

// True if every edge of the piece has a face of the piece on both sides and no seam.
bool closed(const EditMesh& m, const std::vector<int>& faces) {
    const std::set<uint64_t> seams = seamSet(m);
    std::unordered_map<uint64_t, int> uses;
    for (int fi : faces) {
        const auto& f = m.faces[fi];
        for (size_t i = 0; i < f.size(); ++i) ++uses[edgeKey(f[i], f[(i + 1) % f.size()])];
    }
    for (auto& [key, count] : uses) if (count < 2 || seams.count(key)) return false;
    return true;
}

// Least squares conformal map of one piece. Corners of the same mesh corner
// share one UV unless a seam runs between them. Returns false if it can't.
bool conformal(EditMesh& m, const std::vector<int>& faces, glm::vec3 scale) {
    std::vector<int> base(faces.size() + 1, 0);
    for (size_t k = 0; k < faces.size(); ++k) base[k + 1] = base[k] + (int)m.faces[faces[k]].size();
    const int corners = base.back();
    Groups g((size_t)corners);
    const std::set<uint64_t> seams = seamSet(m);
    // Join the corners on both sides of every edge that isn't a seam.
    std::unordered_map<uint64_t, std::vector<std::pair<int, int>>> byEdge;   // edge -> (piece face, corner index)
    for (size_t k = 0; k < faces.size(); ++k) {
        const auto& f = m.faces[faces[k]];
        for (size_t i = 0; i < f.size(); ++i) byEdge[edgeKey(f[i], f[(i + 1) % f.size()])].push_back({(int)k, (int)i});
    }
    for (auto& [key, list] : byEdge) {
        if (seams.count(key) || list.size() != 2) continue;
        auto [k0, i0] = list[0];
        auto [k1, i1] = list[1];
        const auto& f0 = m.faces[faces[k0]];
        const auto& f1 = m.faces[faces[k1]];
        for (int s = 0; s < 2; ++s) {
            uint32_t v = f0[(i0 + s) % f0.size()];
            for (size_t c = 0; c < f1.size(); ++c)
                if (f1[c] == v) g.join((uint32_t)(base[k0] + (i0 + s) % (int)f0.size()), (uint32_t)(base[k1] + (int)c));
        }
    }
    std::vector<int> var(corners, -1);
    std::vector<glm::vec3> pos;
    for (size_t k = 0; k < faces.size(); ++k) {
        const auto& f = m.faces[faces[k]];
        for (size_t c = 0; c < f.size(); ++c) {
            uint32_t r = g.root((uint32_t)(base[k] + c));
            if (var[r] < 0) { var[r] = (int)pos.size(); pos.push_back(m.verts[f[c]] * scale); }
            var[base[k] + c] = var[r];
        }
    }
    const int n = (int)pos.size();
    if (n < 3) return false;
    // Two rows per triangle: sum over corners of W_j * U_j = 0 (complex numbers).
    struct Row { int idx[6]; float w[6]; };
    std::vector<Row> rows;
    for (size_t k = 0; k < faces.size(); ++k) {
        const auto& f = m.faces[faces[k]];
        for (size_t t = 1; t + 1 < f.size(); ++t) {
            int v[3] = {var[base[k]], var[base[k] + (int)t], var[base[k] + (int)t + 1]};
            glm::vec3 p0 = pos[v[0]], p1 = pos[v[1]], p2 = pos[v[2]];
            glm::vec3 nn = glm::cross(p1 - p0, p2 - p0);
            float a2 = glm::length(nn);
            if (a2 < 1e-12f || glm::length(p1 - p0) < 1e-9f) continue;
            glm::vec3 x = glm::normalize(p1 - p0), y = glm::cross(nn / a2, x);
            glm::vec2 z[3] = {{0, 0}, {glm::length(p1 - p0), 0}, {glm::dot(p2 - p0, x), glm::dot(p2 - p0, y)}};
            float s = 1.0f / std::sqrt(a2);
            Row r1, r2;
            for (int j = 0; j < 3; ++j) {
                glm::vec2 W = (z[(j + 2) % 3] - z[(j + 1) % 3]) * s;
                r1.idx[j * 2] = v[j] * 2;     r1.w[j * 2] = W.x;       // u
                r1.idx[j * 2 + 1] = v[j] * 2 + 1; r1.w[j * 2 + 1] = -W.y;  // v
                r2.idx[j * 2] = v[j] * 2;     r2.w[j * 2] = W.y;
                r2.idx[j * 2 + 1] = v[j] * 2 + 1; r2.w[j * 2 + 1] = W.x;
            }
            rows.push_back(r1);
            rows.push_back(r2);
        }
    }
    if (rows.empty()) return false;
    // Pin the two corners furthest apart.
    int p = 0, q = 0;
    for (int i = 0; i < n; ++i) if (glm::length(pos[i] - pos[0]) > glm::length(pos[p] - pos[0])) p = i;
    for (int i = 0; i < n; ++i) if (glm::length(pos[i] - pos[p]) > glm::length(pos[q] - pos[p])) q = i;
    if (p == q) return false;
    std::vector<double> x(n * 2, 0.0);
    std::vector<char> fixed(n * 2, 0);
    // Start from a flat projection (close already, so it settles quickly).
    {
        glm::vec3 avg(0.0f);
        for (size_t k = 0; k < faces.size(); ++k) avg += faceNormal(m, m.faces[faces[k]]);
        glm::vec3 t1, t2;
        planeAxes(avg, t1, t2);
        for (int i = 0; i < n; ++i) { x[i * 2] = glm::dot(pos[i], t1); x[i * 2 + 1] = glm::dot(pos[i], t2); }
        // Pins sit where the projection puts them, so the start is already lined up.
        float d = glm::length(pos[q] - pos[p]);
        glm::dvec2 a(x[p * 2], x[p * 2 + 1]), b(x[q * 2], x[q * 2 + 1]);
        glm::dvec2 dir = glm::length(b - a) > 1e-9 ? glm::normalize(b - a) : glm::dvec2(1, 0);
        x[q * 2] = a.x + dir.x * d;
        x[q * 2 + 1] = a.y + dir.y * d;
    }
    fixed[p * 2] = fixed[p * 2 + 1] = fixed[q * 2] = fixed[q * 2 + 1] = 1;
    // Conjugate gradients on the normal equations (A^T A x = A^T b), pins held still.
    auto mulA = [&](const std::vector<double>& in, std::vector<double>& out, bool withFixed) {
        out.assign(rows.size(), 0.0);
        for (size_t r = 0; r < rows.size(); ++r)
            for (int e = 0; e < 6; ++e) if (withFixed || !fixed[rows[r].idx[e]]) out[r] += rows[r].w[e] * in[rows[r].idx[e]];
    };
    auto mulAt = [&](const std::vector<double>& in, std::vector<double>& out) {
        out.assign(n * 2, 0.0);
        for (size_t r = 0; r < rows.size(); ++r)
            for (int e = 0; e < 6; ++e) if (!fixed[rows[r].idx[e]]) out[rows[r].idx[e]] += rows[r].w[e] * in[r];
    };
    std::vector<double> Ax, res(rows.size()), rvec, pvec, Ap, AtAp;
    mulA(x, Ax, true);
    for (size_t r = 0; r < rows.size(); ++r) res[r] = -Ax[r];
    mulAt(res, rvec);   // = A^T (b - A x)
    pvec = rvec;
    double rr = 0.0;
    for (double v : rvec) rr += v * v;
    const double start = rr;
    for (int it = 0; it < 4000 && rr > start * 1e-14 && rr > 1e-20; ++it) {
        mulA(pvec, Ap, false);
        mulAt(Ap, AtAp);
        double pAp = 0.0;
        for (int i = 0; i < n * 2; ++i) pAp += pvec[i] * AtAp[i];
        if (pAp <= 0.0) break;
        double alpha = rr / pAp, rr2 = 0.0;
        for (int i = 0; i < n * 2; ++i) {
            if (fixed[i]) continue;
            x[i] += alpha * pvec[i];
            rvec[i] -= alpha * AtAp[i];
            rr2 += rvec[i] * rvec[i];
        }
        double beta = rr2 / rr;
        rr = rr2;
        for (int i = 0; i < n * 2; ++i) pvec[i] = fixed[i] ? 0.0 : rvec[i] + beta * pvec[i];
    }
    for (double v : x) if (!std::isfinite(v)) return false;
    // Faces must come out the right way round (not mirrored).
    double area = 0.0;
    for (size_t k = 0; k < faces.size(); ++k) {
        const auto& f = m.faces[faces[k]];
        for (size_t t = 1; t + 1 < f.size(); ++t) {
            int a = var[base[k]], b = var[base[k] + (int)t], c = var[base[k] + (int)t + 1];
            area += (x[b * 2] - x[a * 2]) * (x[c * 2 + 1] - x[a * 2 + 1]) - (x[c * 2] - x[a * 2]) * (x[b * 2 + 1] - x[a * 2 + 1]);
        }
    }
    // Squashed flat (a piece that can't open up): no good, use a plain projection.
    double area3 = 0.0;
    for (size_t k = 0; k < faces.size(); ++k) {
        const auto& f = m.faces[faces[k]];
        for (size_t t = 1; t + 1 < f.size(); ++t)
            area3 += glm::length(glm::cross(pos[var[base[k] + (int)t]] - pos[var[base[k]]], pos[var[base[k] + (int)t + 1]] - pos[var[base[k]]]));
    }
    if (std::abs(area) < area3 * 0.05) return false;
    const double flipU = area < 0.0 ? -1.0 : 1.0;
    for (size_t k = 0; k < faces.size(); ++k) {
        auto& uv = m.uvs[faces[k]];
        for (size_t c = 0; c < uv.size(); ++c) {
            int v = var[base[k] + (int)c];
            uv[c] = glm::vec2((float)(x[v * 2] * flipU), (float)x[v * 2 + 1]);
        }
    }
    return true;
}

// Flatten a piece straight onto the plane it mostly faces.
void planar(EditMesh& m, const std::vector<int>& faces, glm::vec3 scale) {
    glm::vec3 avg(0.0f);
    for (int fi : faces) {
        const auto& f = m.faces[fi];
        std::vector<glm::vec3> p;
        for (uint32_t v : f) p.push_back(m.verts[v] * scale);
        for (size_t i = 1; i + 1 < p.size(); ++i) avg += glm::cross(p[i] - p[0], p[i + 1] - p[0]);
    }
    glm::vec3 t1, t2;
    planeAxes(avg, t1, t2);
    for (int fi : faces)
        for (size_t c = 0; c < m.faces[fi].size(); ++c) {
            glm::vec3 p = m.verts[m.faces[fi][c]] * scale;
            m.uvs[fi][c] = {glm::dot(p, t1), glm::dot(p, t2)};
        }
}

// Squash the picked faces' UVs into the 0..1 square, keeping their shape.
void fitSquare(EditMesh& m, const std::vector<int>& faces) {
    glm::vec2 lo(1e30f), hi(-1e30f);
    for (int fi : faces) for (auto& t : m.uvs[fi]) { lo = glm::min(lo, t); hi = glm::max(hi, t); }
    float s = std::max(hi.x - lo.x, hi.y - lo.y);
    if (s < 1e-9f) return;
    for (int fi : faces) for (auto& t : m.uvs[fi]) t = (t - lo) / s;
}
} // namespace

// ---------------------------------------------------------------------------

void ensureUVs(EditMesh& m) {
    if (inStep(m)) return;
    std::vector<std::vector<glm::vec2>> uvs;
    for (size_t i = 0; i < m.faces.size(); ++i)
        uvs.push_back(i < m.uvs.size() && m.uvs[i].size() == m.faces[i].size() ? m.uvs[i] : boxUV(m, m.faces[i]));
    m.uvs = std::move(uvs);
}

bool unwrap(EditMesh& m, const Selection& sel, Unwrap how, glm::vec3 scale, const glm::mat4& view) {
    if (m.faces.empty()) return false;
    ensureUVs(m);
    std::vector<int> faces = pickedOrAll(m, sel);
    scale = glm::max(glm::abs(scale), glm::vec3(1e-4f));
    switch (how) {
    case Unwrap::Reset:
        for (int fi : faces) {
            auto& uv = m.uvs[fi];
            size_t n = uv.size();
            if (n == 4) { uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}}; continue; }
            for (size_t c = 0; c < n; ++c) {
                float a = 2.0f * kPi * c / n - kPi * 0.5f;
                uv[c] = {0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)};
            }
        }
        return true;
    case Unwrap::View: {
        for (int fi : faces)
            for (size_t c = 0; c < m.faces[fi].size(); ++c) {
                glm::vec4 p = view * glm::vec4(m.verts[m.faces[fi][c]], 1.0f);
                m.uvs[fi][c] = p.w > 1e-6f ? glm::vec2(p.x / p.w, p.y / p.w) : glm::vec2(p.x, p.y);
            }
        fitSquare(m, faces);
        return true;
    }
    case Unwrap::Cylinder:
    case Unwrap::Sphere: {
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (int fi : faces) for (uint32_t v : m.faces[fi]) { lo = glm::min(lo, m.verts[v] * scale); hi = glm::max(hi, m.verts[v] * scale); }
        glm::vec3 c = (lo + hi) * 0.5f;
        for (int fi : faces) {
            auto& uv = m.uvs[fi];
            for (size_t k = 0; k < uv.size(); ++k) {
                glm::vec3 p = m.verts[m.faces[fi][k]] * scale - c;
                float u = std::atan2(p.x, p.z) / (2.0f * kPi) + 0.5f;
                float v = how == Unwrap::Cylinder ? (hi.y > lo.y ? (p.y + c.y - lo.y) / (hi.y - lo.y) : 0.5f)
                                                  : std::asin(std::clamp(glm::length(p) > 1e-9f ? p.y / glm::length(p) : 0.0f, -1.0f, 1.0f)) / kPi + 0.5f;
                uv[k] = {u, v};
            }
            // A face across the back seam: keep it in one piece.
            float mn = 1e9f, mx = -1e9f;
            for (auto& t : uv) { mn = std::min(mn, t.x); mx = std::max(mx, t.x); }
            if (mx - mn > 0.5f) for (auto& t : uv) if (t.x < 0.5f) t.x += 1.0f;
        }
        return true;
    }
    case Unwrap::Cube: {
        // Each side gets the direction it faces most; the six sides are laid out apart.
        std::map<int, std::vector<int>> bySide;
        for (int fi : faces) {
            const auto& f = m.faces[fi];
            glm::vec3 nn(0.0f);
            for (size_t i = 1; i + 1 < f.size(); ++i)
                nn += glm::cross(m.verts[f[i]] * scale - m.verts[f[0]] * scale, m.verts[f[i + 1]] * scale - m.verts[f[0]] * scale);
            glm::vec3 an = glm::abs(nn);
            int ax = an.x > an.y && an.x > an.z ? 0 : (an.y > an.z ? 1 : 2);
            int sign = nn[ax] >= 0.0f ? 1 : -1;
            bySide[ax * 2 + (sign > 0)].push_back(fi);
            for (size_t c = 0; c < f.size(); ++c) {
                glm::vec3 p = m.verts[f[c]] * scale;
                m.uvs[fi][c] = ax == 0 ? glm::vec2(-p.z * sign, p.y) : ax == 1 ? glm::vec2(p.x, -p.z * sign) : glm::vec2(p.x * sign, p.y);
            }
        }
        packIslands(m, faces, scale);
        return true;
    }
    case Unwrap::Smart: {
        for (auto& piece : pieces(m, faces, false, std::cos(glm::radians(66.0f)))) planar(m, piece, scale);
        packIslands(m, faces, scale);
        return true;
    }
    case Unwrap::Unwrap:
    default: {
        for (auto& piece : pieces(m, faces, true, -2.0f)) {
            // A closed piece (no seams around it, like a whole ball) can't be laid flat
            // in one go: cut it where it bends the most, like Smart UV Project.
            if (closed(m, piece)) {
                for (auto& part : pieces(m, piece, false, std::cos(glm::radians(66.0f))))
                    if (!conformal(m, part, scale)) planar(m, part, scale);
            } else if (!conformal(m, piece, scale)) {
                planar(m, piece, scale);
            }
        }
        packIslands(m, faces, scale);
        return true;
    }
    }
}

std::vector<std::vector<int>> uvIslands(const EditMesh& m, const std::vector<int>& faces) {
    std::vector<std::vector<int>> out;
    if (!inStep(m)) return out;
    struct Side { int k; glm::vec2 a, b; };   // the piece face, and its UVs at the edge's lower / higher corner
    std::unordered_map<uint64_t, std::vector<Side>> byEdge;
    for (size_t k = 0; k < faces.size(); ++k) {
        const auto& f = m.faces[faces[k]];
        const auto& uv = m.uvs[faces[k]];
        for (size_t i = 0; i < f.size(); ++i) {
            size_t j = (i + 1) % f.size();
            bool lowFirst = f[i] < f[j];
            byEdge[edgeKey(f[i], f[j])].push_back({(int)k, lowFirst ? uv[i] : uv[j], lowFirst ? uv[j] : uv[i]});
        }
    }
    Groups g(faces.size());
    for (auto& [key, list] : byEdge)
        for (size_t i = 1; i < list.size(); ++i)
            if (glm::length(list[0].a - list[i].a) < 1e-5f && glm::length(list[0].b - list[i].b) < 1e-5f)
                g.join((uint32_t)list[0].k, (uint32_t)list[i].k);
    std::map<uint32_t, std::vector<int>> by;
    for (size_t k = 0; k < faces.size(); ++k) by[g.root((uint32_t)k)].push_back(faces[k]);
    for (auto& [r, list] : by) out.push_back(std::move(list));
    return out;
}

void packIslands(EditMesh& m, const std::vector<int>& facesIn, glm::vec3 scale) {
    ensureUVs(m);
    std::vector<int> faces = facesIn;
    if (faces.empty()) { faces.resize(m.faces.size()); std::iota(faces.begin(), faces.end(), 0); }
    auto islands = uvIslands(m, faces);
    if (islands.empty()) return;
    // Every piece the same "texels per stud": scale each to its real size first.
    struct Box { glm::vec2 lo, hi; size_t i; };
    std::vector<Box> boxes;
    double total = 0.0;
    for (size_t i = 0; i < islands.size(); ++i) {
        double a3 = 0.0, a2 = 0.0;
        for (int fi : islands[i]) {
            const auto& f = m.faces[fi];
            const auto& uv = m.uvs[fi];
            for (size_t t = 1; t + 1 < f.size(); ++t) {
                a3 += glm::length(glm::cross(m.verts[f[t]] * scale - m.verts[f[0]] * scale, m.verts[f[t + 1]] * scale - m.verts[f[0]] * scale));
                glm::vec2 e1 = uv[t] - uv[0], e2 = uv[t + 1] - uv[0];
                a2 += std::abs(e1.x * e2.y - e1.y * e2.x);
            }
        }
        float k = a2 > 1e-12 && a3 > 1e-12 ? (float)std::sqrt(a3 / a2) : 1.0f;
        for (int fi : islands[i]) for (auto& t : m.uvs[fi]) t *= k;
        // Turn the piece so it sits square (its smallest box), lined up with one of its own edges.
        {
            std::vector<glm::vec2> pts;
            for (int fi : islands[i]) for (auto& t : m.uvs[fi]) pts.push_back(t);
            float bestArea = 1e30f, bestAngle = 0.0f;
            size_t tried = 0;
            for (int fi : islands[i]) {
                const auto& uv = m.uvs[fi];
                for (size_t c = 0; c < uv.size() && tried < 256; ++c, ++tried) {
                    glm::vec2 e = uv[(c + 1) % uv.size()] - uv[c];
                    if (glm::length(e) < 1e-9f) continue;
                    float ang = -std::atan2(e.y, e.x), cs = std::cos(ang), sn = std::sin(ang);
                    glm::vec2 lo(1e30f), hi(-1e30f);
                    for (auto& p : pts) { glm::vec2 r(p.x * cs - p.y * sn, p.x * sn + p.y * cs); lo = glm::min(lo, r); hi = glm::max(hi, r); }
                    float area = (hi.x - lo.x) * (hi.y - lo.y);
                    if (area < bestArea * 0.999f) { bestArea = area; bestAngle = ang; }
                }
            }
            // Wider than tall reads best on the shelves.
            float cs = std::cos(bestAngle), sn = std::sin(bestAngle);
            glm::vec2 lo(1e30f), hi(-1e30f);
            for (auto& p : pts) { glm::vec2 r(p.x * cs - p.y * sn, p.x * sn + p.y * cs); lo = glm::min(lo, r); hi = glm::max(hi, r); }
            if (hi.y - lo.y > (hi.x - lo.x) * 1.001f) { bestAngle += 1.5707963f; cs = std::cos(bestAngle); sn = std::sin(bestAngle); }
            if (std::abs(bestAngle) > 1e-6f)
                for (int fi : islands[i]) for (auto& t : m.uvs[fi]) t = glm::vec2(t.x * cs - t.y * sn, t.x * sn + t.y * cs);
        }
        Box b{glm::vec2(1e30f), glm::vec2(-1e30f), i};
        for (int fi : islands[i]) for (auto& t : m.uvs[fi]) { b.lo = glm::min(b.lo, t); b.hi = glm::max(b.hi, t); }
        boxes.push_back(b);
        total += (double)(b.hi.x - b.lo.x) * (b.hi.y - b.lo.y);
    }
    // Shelves: tallest pieces first, left to right, then a new row.
    const float gap = (float)std::sqrt(std::max(total, 1e-12)) * 0.02f;
    float width = (float)std::sqrt(std::max(total, 1e-12)) * 1.15f;
    for (auto& b : boxes) width = std::max(width, b.hi.x - b.lo.x + gap * 2.0f);
    std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return a.hi.y - a.lo.y > b.hi.y - b.lo.y; });
    float x = gap, y = gap, rowH = 0.0f, usedW = 0.0f;
    std::vector<glm::vec2> moveBy(islands.size());
    for (auto& b : boxes) {
        glm::vec2 sz = b.hi - b.lo;
        if (x + sz.x + gap > width && x > gap) { x = gap; y += rowH + gap; rowH = 0.0f; }
        moveBy[b.i] = glm::vec2(x, y) - b.lo;
        x += sz.x + gap;
        rowH = std::max(rowH, sz.y);
        usedW = std::max(usedW, x);
    }
    float side = std::max(usedW, y + rowH + gap);
    if (side < 1e-9f) return;
    for (size_t i = 0; i < islands.size(); ++i)
        for (int fi : islands[i]) for (auto& t : m.uvs[fi]) t = (t + moveBy[i]) / side;
}

bool markSeams(EditMesh& m, const Selection& sel, bool on) {
    std::set<uint64_t> s = seamSet(m);
    size_t before = s.size();
    for (auto [a, b] : edges(m)) {
        if (a >= sel.size() || b >= sel.size() || !sel[a] || !sel[b]) continue;
        if (on) s.insert(edgeKey(a, b)); else s.erase(edgeKey(a, b));
    }
    if (s.size() == before && on) return false;
    m.seams.clear();
    for (uint64_t k : s) m.seams.push_back({(uint32_t)(k >> 32), (uint32_t)(k & 0xffffffffu)});
    return true;
}

bool isSeam(const EditMesh& m, uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return std::binary_search(m.seams.begin(), m.seams.end(), std::make_pair(a, b));   // kept sorted
}

void carryUVs(const EditMesh& before, EditMesh& after) {
    using Key = std::tuple<float, float, float>;
    auto key = [](const glm::vec3& p) { return Key{p.x, p.y, p.z}; };
    // Seams: an edge whose two ends sit exactly where a seam's ends were.
    if (!before.seams.empty()) {
        std::set<std::pair<Key, Key>> was;
        for (auto [a, b] : before.seams) {
            if (a >= before.verts.size() || b >= before.verts.size()) continue;
            Key ka = key(before.verts[a]), kb = key(before.verts[b]);
            was.insert({std::min(ka, kb), std::max(ka, kb)});
        }
        std::set<uint64_t> now;
        for (auto [a, b] : edges(after)) {
            Key ka = key(after.verts[a]), kb = key(after.verts[b]);
            if (was.count({std::min(ka, kb), std::max(ka, kb)})) now.insert(edgeKey(a, b));
        }
        after.seams.clear();
        for (uint64_t k : now) after.seams.push_back({(uint32_t)(k >> 32), (uint32_t)(k & 0xffffffffu)});
    }
    if (!inStep(before) || inStep(after)) return;   // nothing to carry, or the tool kept them itself
    // Every corner of the old mesh: where it was, which way its face pointed, its UV.
    struct Corner { glm::vec3 n; glm::vec2 uv; };
    std::map<Key, std::vector<Corner>> at;
    for (size_t fi = 0; fi < before.faces.size(); ++fi) {
        glm::vec3 n = faceNormal(before, before.faces[fi]);
        for (size_t c = 0; c < before.faces[fi].size(); ++c) at[key(before.verts[before.faces[fi][c]])].push_back({n, before.uvs[fi][c]});
    }
    // Each old face's own flat mapping (3D -> UV), for points that are new.
    struct Map { glm::vec3 c, n, t1, t2; glm::vec2 uc; glm::mat2 M; };
    std::vector<Map> maps(before.faces.size());
    for (size_t fi = 0; fi < before.faces.size(); ++fi) {
        const auto& f = before.faces[fi];
        Map& mp = maps[fi];
        mp.c = faceCenter(before, f);
        mp.n = faceNormal(before, f);
        planeAxes(mp.n, mp.t1, mp.t2);
        mp.uc = glm::vec2(0.0f);
        for (auto& t : before.uvs[fi]) mp.uc += t;
        mp.uc /= (float)f.size();
        // Least squares: uv - uc = M * (q - qc), q = the corner on the face's plane.
        glm::mat2 QQ(0.0f), UQ(0.0f);
        for (size_t c = 0; c < f.size(); ++c) {
            glm::vec3 d = before.verts[f[c]] - mp.c;
            glm::vec2 q(glm::dot(d, mp.t1), glm::dot(d, mp.t2)), u = before.uvs[fi][c] - mp.uc;
            QQ += glm::outerProduct(q, q);
            UQ += glm::outerProduct(u, q);
        }
        mp.M = std::abs(glm::determinant(QQ)) > 1e-14f ? UQ * glm::inverse(QQ) : glm::mat2(0.0f);
    }
    after.uvs.assign(after.faces.size(), {});
    for (size_t fi = 0; fi < after.faces.size(); ++fi) {
        const auto& f = after.faces[fi];
        glm::vec3 n = faceNormal(after, f), c = faceCenter(after, f);
        int nearest = -1;   // (found only if needed)
        auto& out = after.uvs[fi];
        for (uint32_t v : f) {
            auto it = at.find(key(after.verts[v]));
            const Corner* best = nullptr;
            if (it != at.end())
                for (const Corner& k : it->second) if (!best || glm::dot(k.n, n) > glm::dot(best->n, n)) best = &k;
            if (best && glm::dot(best->n, n) > 0.5f) { out.push_back(best->uv); continue; }
            if (nearest < 0) {
                float bestScore = 1e30f;
                for (size_t g = 0; g < maps.size(); ++g) {
                    float score = glm::length(maps[g].c - c) * (1.5f - glm::dot(maps[g].n, n));
                    if (score < bestScore) { bestScore = score; nearest = (int)g; }
                }
            }
            if (nearest < 0) { out.push_back(glm::vec2(0.0f)); continue; }
            const Map& mp = maps[nearest];
            glm::vec3 d = after.verts[v] - mp.c;
            out.push_back(mp.uc + mp.M * glm::vec2(glm::dot(d, mp.t1), glm::dot(d, mp.t2)));
        }
    }
}

std::vector<unsigned char> layoutPicture(const EditMesh& m, int size) {
    std::vector<unsigned char> px((size_t)size * size * 4, 0);
    if (!inStep(m)) return px;
    auto put = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
        if (x < 0 || y < 0 || x >= size || y >= size) return;
        unsigned char* p = &px[((size_t)y * size + x) * 4];
        // Blend over what's there.
        float fa = a / 255.0f, oa = p[3] / 255.0f, na = fa + oa * (1.0f - fa);
        if (na <= 0.0f) return;
        p[0] = (unsigned char)((r * fa + p[0] * oa * (1.0f - fa)) / na);
        p[1] = (unsigned char)((g * fa + p[1] * oa * (1.0f - fa)) / na);
        p[2] = (unsigned char)((b * fa + p[2] * oa * (1.0f - fa)) / na);
        p[3] = (unsigned char)(na * 255.0f);
    };
    // Row 0 of the picture is the top, which is v = 1 (pictures load bottom row first).
    auto toPx = [&](glm::vec2 t) { return glm::vec2(t.x * size, (1.0f - t.y) * size); };
    std::vector<char> covered((size_t)size * size, 0);
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        const auto& uv = m.uvs[fi];
        // A light fill, so you can see which parts are covered.
        for (size_t t = 1; t + 1 < uv.size(); ++t) {
            glm::vec2 a = toPx(uv[0]), b = toPx(uv[t]), c = toPx(uv[t + 1]);
            int x0 = (int)std::floor(std::min({a.x, b.x, c.x})), x1 = (int)std::ceil(std::max({a.x, b.x, c.x}));
            int y0 = (int)std::floor(std::min({a.y, b.y, c.y})), y1 = (int)std::ceil(std::max({a.y, b.y, c.y}));
            float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            if (std::abs(area) < 1e-6f) continue;
            for (int y = std::max(0, y0); y <= std::min(size - 1, y1); ++y)
                for (int x = std::max(0, x0); x <= std::min(size - 1, x1); ++x) {
                    glm::vec2 p(x + 0.5f, y + 0.5f);
                    float w0 = ((b.x - p.x) * (c.y - p.y) - (c.x - p.x) * (b.y - p.y)) / area;
                    float w1 = ((c.x - p.x) * (a.y - p.y) - (a.x - p.x) * (c.y - p.y)) / area;
                    if (w0 >= 0 && w1 >= 0 && w0 + w1 <= 1.0f) covered[(size_t)y * size + x] = 1;
                }
        }
    }
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (covered[(size_t)y * size + x]) put(x, y, 200, 200, 200, 70);
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        const auto& uv = m.uvs[fi];
        for (size_t i = 0; i < uv.size(); ++i) {
            glm::vec2 a = toPx(uv[i]), b = toPx(uv[(i + 1) % uv.size()]);
            int steps = (int)std::ceil(std::max(std::abs(b.x - a.x), std::abs(b.y - a.y))) + 1;
            for (int s = 0; s <= steps; ++s) {
                glm::vec2 p = glm::mix(a, b, (float)s / steps);
                put((int)p.x, (int)p.y, 20, 20, 20, 255);
            }
        }
    }
    return px;
}

} // namespace MeshEdit
