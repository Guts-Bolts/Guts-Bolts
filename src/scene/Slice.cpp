#include "Slice.h"
#include "EditMesh.h"
#include "Particles.h"
#include "Scene.h"
#include "SceneNode.h"
#include "../renderer/MeshLibrary.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>

namespace Slice {

namespace {

constexpr float kOnCut = 1e-5f;   // closer to the cut than this is on it

struct Corner { uint32_t v; bool onCut; glm::vec2 uv; };

// The outline of a cut end, flattened: is corner b of a-b-c an "ear" (a corner that can
// be clipped off without cutting through the rest of the outline)?
bool isEar(const std::vector<glm::vec2>& p, const std::vector<int>& left, size_t at) {
    const size_t n = left.size();
    const glm::vec2 a = p[left[(at + n - 1) % n]], b = p[left[at]], c = p[left[(at + 1) % n]];
    const float turn = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (turn <= 1e-12f) return false;   // a dent, or flat
    for (size_t i = 0; i < n; ++i) {
        if (i == at || i == (at + n - 1) % n || i == (at + 1) % n) continue;
        const glm::vec2 q = p[left[i]];
        const float d0 = (b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x);
        const float d1 = (c.x - b.x) * (q.y - b.y) - (c.y - b.y) * (q.x - b.x);
        const float d2 = (a.x - c.x) * (q.y - c.y) - (a.y - c.y) * (q.x - c.x);
        if (d0 > 0.0f && d1 > 0.0f && d2 > 0.0f) return false;   // another corner is inside it
    }
    return true;
}

// Fill an outline (counter-clockwise in `flat`) with triangles: clip ears until three
// corners are left. Works for outlines with dents (an L-shaped beam), which a simple fan
// from one corner would fill wrongly.
void fill(const std::vector<glm::vec2>& flat, const std::vector<uint32_t>& ids, std::vector<std::vector<uint32_t>>& faces) {
    std::vector<int> left(flat.size());
    for (size_t i = 0; i < left.size(); ++i) left[i] = (int)i;
    while (left.size() > 3) {
        size_t ear = left.size();
        for (size_t i = 0; i < left.size() && ear == left.size(); ++i)
            if (isEar(flat, left, i)) ear = i;
        if (ear == left.size()) ear = 0;   // (a broken outline: clip anything, so it still ends)
        const size_t n = left.size();
        faces.push_back({ids[left[(ear + n - 1) % n]], ids[left[ear]], ids[left[(ear + 1) % n]]});
        left.erase(left.begin() + (long)ear);
    }
    if (left.size() == 3) faces.push_back({ids[left[0]], ids[left[1]], ids[left[2]]});
}

// One side of the cut: the faces (and bits of faces) where side * (distance from the
// cut) >= 0, then new faces closing the hole.
bool half(const EditMesh& in, const glm::vec3& point, const glm::vec3& n, float side, const Inside& inside, EditMesh& out) {
    out = EditMesh{};
    out.smooth = in.smooth;
    const bool uvs = in.uvs.size() == in.faces.size();
    const bool colors = in.colors.size() == in.verts.size();

    std::vector<float> dist(in.verts.size());
    for (size_t i = 0; i < in.verts.size(); ++i) {
        const float d = side * glm::dot(n, in.verts[i] - point);
        dist[i] = std::abs(d) < kOnCut ? 0.0f : d;
    }
    auto addVert = [&](const glm::vec3& p, const glm::vec4& color) {
        out.verts.push_back(p);
        out.colors.push_back(color);
        return (uint32_t)out.verts.size() - 1;
    };
    // Corners that are kept, and the new corners where an edge crosses the cut (one per
    // edge, shared by the faces either side of it, so the half stays joined up).
    std::vector<int> kept(in.verts.size(), -1);
    auto keep = [&](uint32_t v) {
        if (kept[v] < 0) kept[v] = (int)addVert(in.verts[v], colors ? in.colors[v] : glm::vec4(1.0f));
        return (uint32_t)kept[v];
    };
    std::unordered_map<uint64_t, uint32_t> crossed;
    auto cross = [&](uint32_t a, uint32_t b, float t) {
        const uint64_t key = ((uint64_t)std::min(a, b) << 32) | std::max(a, b);
        auto it = crossed.find(key);
        if (it != crossed.end()) return it->second;
        const glm::vec4 ca = colors ? in.colors[a] : glm::vec4(1.0f), cb = colors ? in.colors[b] : glm::vec4(1.0f);
        const uint32_t v = addVert(glm::mix(in.verts[a], in.verts[b], t), glm::mix(ca, cb, t));
        crossed[key] = v;
        return v;
    };

    // The edges of the hole: from -> to, running the way the faces beside them run.
    std::map<std::pair<uint32_t, uint32_t>, int> rim;
    std::vector<Corner> poly;
    for (size_t fi = 0; fi < in.faces.size(); ++fi) {
        const auto& f = in.faces[fi];
        if (f.size() < 3) continue;
        const bool faceUVs = uvs && in.uvs[fi].size() == f.size();
        poly.clear();
        bool anyIn = false;
        for (size_t c = 0; c < f.size(); ++c) {
            const uint32_t a = f[c], b = f[(c + 1) % f.size()];
            const float da = dist[a], db = dist[b];
            const glm::vec2 ua = faceUVs ? in.uvs[fi][c] : glm::vec2(0.0f), ub = faceUVs ? in.uvs[fi][(c + 1) % f.size()] : glm::vec2(0.0f);
            if (da >= 0.0f) {
                poly.push_back({keep(a), da == 0.0f, ua});
                if (da > 0.0f) anyIn = true;
            }
            if ((da > 0.0f && db < 0.0f) || (da < 0.0f && db > 0.0f)) {
                const float t = da / (da - db);
                poly.push_back({cross(a, b, t), true, glm::mix(ua, ub, t)});
            }
        }
        // (A face lying flat in the cut is dropped: the new end takes its place.)
        if (poly.size() < 3 || !anyIn) continue;
        std::vector<uint32_t> face;
        std::vector<glm::vec2> faceUV;
        for (size_t c = 0; c < poly.size(); ++c) {
            if (!face.empty() && face.back() == poly[c].v) continue;   // (two corners that ended up the same)
            face.push_back(poly[c].v);
            faceUV.push_back(poly[c].uv);
        }
        if (face.size() > 1 && face.front() == face.back()) { face.pop_back(); faceUV.pop_back(); }
        if (face.size() < 3) continue;
        for (size_t c = 0; c < poly.size(); ++c) {
            const Corner& a = poly[c];
            const Corner& b = poly[(c + 1) % poly.size()];
            if (a.onCut && b.onCut && a.v != b.v) ++rim[{a.v, b.v}];
        }
        out.faces.push_back(std::move(face));
        if (uvs) out.uvs.push_back(std::move(faceUV));
    }
    if (out.faces.empty() || rim.empty()) return false;

    // An edge two kept faces share (a ridge that only touches the cut) isn't a hole.
    std::unordered_map<uint32_t, uint32_t> next;
    for (const auto& [edge, count] : rim) {
        if (rim.count({edge.second, edge.first})) continue;
        next[edge.first] = edge.second;
    }
    if (next.empty()) return false;

    // Join the edges up into outlines, and close each one.
    glm::vec3 x = glm::normalize(glm::cross(std::abs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), n));
    glm::vec3 y = glm::cross(n, x);
    bool closedAny = false;
    while (!next.empty()) {
        std::vector<uint32_t> loop;
        uint32_t at = next.begin()->first;
        const uint32_t start = at;
        bool whole = false;
        for (size_t guard = 0; guard <= out.verts.size(); ++guard) {
            auto it = next.find(at);
            if (it == next.end()) break;
            loop.push_back(at);
            at = it->second;
            next.erase(it);
            if (at == start) { whole = true; break; }
        }
        if (!whole || loop.size() < 3) continue;   // (an outline that doesn't close: leave it open)
        // The faces beside the hole run one way round it; the new end runs the other.
        std::reverse(loop.begin(), loop.end());
        std::vector<glm::vec2> flat(loop.size());
        glm::vec3 middle(0.0f);
        for (uint32_t v : loop) middle += out.verts[v];
        middle /= (float)loop.size();
        // Seen from outside (looking against capNormal) the outline must go counter-clockwise.
        const glm::vec3 fx = side > 0.0f ? y : x, fy = side > 0.0f ? x : y;   // fx x fy = capNormal
        float area = 0.0f;
        for (size_t i = 0; i < loop.size(); ++i) {
            const glm::vec3 d = out.verts[loop[i]] - middle;
            flat[i] = glm::vec2(glm::dot(d, fx), glm::dot(d, fy));
        }
        for (size_t i = 0; i < loop.size(); ++i) {
            const glm::vec2 a = flat[i], b = flat[(i + 1) % loop.size()];
            area += a.x * b.y - a.y * b.x;
        }
        if (std::abs(area) < 1e-10f) continue;
        if (area < 0.0f) { std::reverse(loop.begin(), loop.end()); std::reverse(flat.begin(), flat.end()); }

        // New corners for the end (its own, so it can have its own colours and a hard edge).
        auto ring = [&](float reach, const glm::vec4& color) {
            std::vector<uint32_t> ids(loop.size());
            for (size_t i = 0; i < loop.size(); ++i)
                ids[i] = addVert(glm::mix(middle, out.verts[loop[i]], reach), color);
            return ids;
        };
        const size_t firstFace = out.faces.size();
        const int layers = std::clamp(inside.layers, 0, 3);
        if (layers == 0) {
            fill(flat, ring(1.0f, inside.cap), out.faces);
        } else {
            // Bands from the outline in (skin, fat, muscle), then the middle (bone).
            for (int k = 0; k < layers; ++k) {
                const std::vector<uint32_t> outer = ring(inside.reach[k], inside.band[k]);
                const std::vector<uint32_t> inner = ring(inside.reach[k + 1], inside.band[k]);
                for (size_t i = 0; i < loop.size(); ++i) {
                    const size_t j = (i + 1) % loop.size();
                    out.faces.push_back({outer[i], outer[j], inner[j], inner[i]});
                }
            }
            fill(flat, ring(inside.reach[layers], inside.core), out.faces);
        }
        // Texture coordinates for the new faces: looking straight at the end.
        if (uvs)
            for (size_t f = firstFace; f < out.faces.size(); ++f) {
                std::vector<glm::vec2> uv;
                for (uint32_t v : out.faces[f]) {
                    const glm::vec3 d = out.verts[v] - middle;
                    uv.push_back(glm::vec2(glm::dot(d, fx), glm::dot(d, fy)) + 0.5f);
                }
                out.uvs.push_back(std::move(uv));
            }
        closedAny = true;
    }
    return closedAny;
}

} // namespace

Inside flesh(const glm::vec3& skin, const glm::vec3& blood) {
    // Colours are multiplied by the part's own (its skin), so divide that back out.
    const glm::vec3 s = glm::max(skin, glm::vec3(0.04f));
    auto over = [&](const glm::vec3& c) { return glm::vec4(c / s, 1.0f); };
    Inside in;
    in.layers = 3;
    in.reach[0] = 1.0f; in.reach[1] = 0.94f; in.reach[2] = 0.86f; in.reach[3] = 0.3f;
    in.band[0] = glm::vec4(0.86f, 0.86f, 0.86f, 1.0f);                                   // skin, a little darker on the cut
    in.band[1] = over(glm::vec3(0.93f, 0.82f, 0.52f));                                   // fat
    in.band[2] = over(glm::mix(glm::vec3(0.56f, 0.07f, 0.08f), blood, 0.4f));            // muscle
    in.core    = over(glm::vec3(0.93f, 0.89f, 0.8f));                                    // bone
    in.cap     = in.band[2];
    return in;
}

bool cut(const EditMesh& mesh, const glm::vec3& point, const glm::vec3& normal, const Inside& inside,
         EditMesh& front, EditMesh& back) {
    front = EditMesh{};
    back = EditMesh{};
    if (mesh.verts.empty() || mesh.faces.empty() || mesh.skinned()) return false;
    const float len = glm::length(normal);
    if (!(len > 1e-8f)) return false;
    const glm::vec3 n = normal / len;
    return half(mesh, point, n, 1.0f, inside, front) && half(mesh, point, n, -1.0f, inside, back);
}

float volume(const EditMesh& mesh) {
    double v = 0.0;
    for (const auto& f : mesh.faces)
        for (size_t k = 1; k + 1 < f.size(); ++k) {
            const glm::dvec3 a = mesh.verts[f[0]], b = mesh.verts[f[k]], c = mesh.verts[f[k + 1]];
            v += glm::dot(a, glm::cross(b, c)) / 6.0;
        }
    return (float)v;
}

bool closed(const EditMesh& mesh) {
    // By where the corners are, not their numbers: a cut end has corners of its own in
    // the same places as the outline it closes.
    auto key = [](const glm::vec3& p) {
        auto q = [](float f) { return (int64_t)std::llround((double)f * 20000.0); };
        return std::array<int64_t, 3>{q(p.x), q(p.y), q(p.z)};
    };
    std::map<std::pair<std::array<int64_t, 3>, std::array<int64_t, 3>>, int> edges;
    for (const auto& f : mesh.faces)
        for (size_t c = 0; c < f.size(); ++c) {
            const auto a = key(mesh.verts[f[c]]), b = key(mesh.verts[f[(c + 1) % f.size()]]);
            if (a == b) continue;
            ++edges[{a, b}];
        }
    if (edges.empty()) return false;
    for (const auto& [edge, count] : edges) {
        auto other = edges.find({edge.second, edge.first});
        if (other == edges.end() || other->second != count) return false;
    }
    return true;
}

std::vector<SceneNode*> part(Scene& scene, SceneNode* p, const glm::vec3& point, const glm::vec3& normal, float push) {
    std::vector<SceneNode*> out;
    if (!p || !p->isPart() || !p->parent || scene.isCharacterPart(p)) return out;
    if (!(glm::length(normal) > 1e-6f)) return out;
    // The part's shape: the one built in Modeling mode, or its block / ball / cylinder.
    std::shared_ptr<EditMesh> shape = p->editMesh;
    if (!shape) {
        if (p->primitiveType != PrimitiveType::Cube && p->primitiveType != PrimitiveType::Sphere &&
            p->primitiveType != PrimitiveType::Cylinder) return out;
        shape = MeshEdit::fromPrimitive(p->primitiveType);
    }
    if (!shape || shape->skinned()) return out;

    // The cut in the part's own space (where its shape fits a 1 x 1 x 1 box).
    const glm::mat4 M = p->worldMatrix();
    const glm::vec3 localPoint = glm::vec3(glm::inverse(M) * glm::vec4(point, 1.0f));
    const glm::vec3 localNormal = glm::transpose(glm::mat3(M)) * normal;
    const glm::vec3 n = glm::normalize(normal);

    // What's inside.
    Inside inside;
    const Attribute* fleshAttr = p->findAttribute("Flesh");
    const bool meat = fleshAttr && fleshAttr->type == Attribute::Bool && fleshAttr->b && scene.goreEnabled();
    if (meat) inside = flesh(p->color, scene.world().bloodColor);
    else if (const Attribute* a = p->findAttribute("InsideColor"); a && a->type == Attribute::Color3)
        inside.cap = glm::vec4(a->v / glm::max(p->color, glm::vec3(0.04f)), 1.0f);

    EditMesh halves[2];
    if (!cut(*shape, localPoint, localNormal, inside, halves[0], halves[1])) return out;
    const float whole = std::abs(volume(*shape));
    if (whole <= 0.0f) return out;
    for (const EditMesh& h : halves)
        if (std::abs(volume(h)) < whole * kSliver) return out;

    for (int i = 0; i < 2; ++i) {
        auto node = std::make_unique<SceneNode>(p->name);
        node->transform.position = glm::vec3(M[3]);
        node->transform.rotation = p->transform.rotation;
        node->transform.scale = p->transform.scale;
        node->color = p->color;
        node->material = p->material;
        node->materialVariant = p->materialVariant;
        node->transparency = p->transparency;
        node->density = p->density;
        node->friction = p->friction;
        node->elasticity = p->elasticity;
        node->canCollide = p->canCollide;
        node->castShadow = p->castShadow;
        node->collisionGroup = p->collisionGroup;
        node->attributes = p->attributes;   // (so a half can be cut again the same way)
        node->tags = p->tags;
        // The half the blade pushed off comes loose; the other stays put if it was fixed.
        node->anchored = i == 0 ? false : p->anchored;
        const float dir = i == 0 ? 1.0f : -1.0f;
        node->velocity = p->velocity + (node->anchored ? glm::vec3(0.0f) : n * dir * push * (p->anchored ? 1.0f : 0.5f));
        node->angularVelocity = p->angularVelocity;
        node->mesh = MeshLibrary::get(PrimitiveType::Cube);
        SceneNode* made = p->parent->addChild(std::move(node));
        MeshEdit::attach(*made, std::make_shared<EditMesh>(std::move(halves[i])));
        MeshEdit::fit(*made);        // its box wraps its own half now
        MeshEdit::refresh(*made);
        out.push_back(made);
    }
    if (meat) {
        scene.particles().spray(GoreKind::Blood, point, n, 12, 3.0f);
        scene.particles().spray(GoreKind::Blood, point, -n, 12, 3.0f);
    }
    scene.removeNode(p);
    scene.markDirty();
    return out;
}

} // namespace Slice
