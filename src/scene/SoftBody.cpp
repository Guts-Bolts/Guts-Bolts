#include "SoftBody.h"
#include "EditMesh.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Slice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

namespace SoftBody {

namespace {

// Push a point out of a box by its nearest face. True if it was inside.
bool pushOut(glm::vec3& p, const Collider& c) {
    glm::vec3 local, half, axis[3];
    for (int i = 0; i < 3; ++i) {
        half[i] = glm::length(c.axes[i]);
        axis[i] = half[i] > 1e-6f ? c.axes[i] / half[i] : glm::vec3(0.0f);
        local[i] = glm::dot(p - c.center, axis[i]);
    }
    float best = 1e30f;
    int bestAxis = -1;
    for (int i = 0; i < 3; ++i) {
        const float inside = half[i] - std::abs(local[i]);
        if (inside <= 0.0f) return false;
        if (inside < best) { best = inside; bestAxis = i; }
    }
    if (bestAxis < 0) return false;
    p += axis[bestAxis] * (local[bestAxis] >= 0.0f ? best : -best);
    return true;
}

float signedVolume(const std::vector<glm::vec3>& p, const std::vector<uint32_t>& tris) {
    double v = 0.0;
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        const glm::dvec3 a = p[tris[t]], b = p[tris[t + 1]], c = p[tris[t + 2]];
        v += glm::dot(a, glm::cross(b, c));
    }
    return (float)(v / 6.0);
}

bool sameFrame(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 3; ++r)
            if (std::abs(a[c][r] - b[c][r]) > 1e-5f) return false;
    return true;
}

// How hard the springs are, from Softness: the speed (turns a second) a corner on its
// spring would bob at. Firm rubber is quick, jelly is slow.
float bobsPerSecond(float softness) { return glm::mix(11.0f, 2.6f, std::clamp(softness, 0.0f, 1.0f)); }

} // namespace

void build(Body& body, const EditMesh& shape, const glm::mat4& frame, float softness) {
    body = Body{};
    body.softness = std::clamp(softness, 0.0f, 1.0f);
    body.frame = frame;
    // Corners in the same place become one (a block has three at every corner, one per
    // side, so its sides can have their own colours; they must still move together).
    std::map<std::array<int64_t, 3>, uint32_t> place;
    body.cornerOf.resize(shape.verts.size());
    for (size_t i = 0; i < shape.verts.size(); ++i) {
        const glm::vec3& v = shape.verts[i];
        auto q = [](float f) { return (int64_t)std::llround((double)f * 20000.0); };
        auto [it, fresh] = place.try_emplace({q(v.x), q(v.y), q(v.z)}, (uint32_t)body.rest.size());
        if (fresh) body.rest.push_back(v);
        body.cornerOf[i] = it->second;
    }
    const size_t n = body.rest.size();
    body.pos.resize(n);
    for (size_t i = 0; i < n; ++i) body.pos[i] = glm::vec3(frame * glm::vec4(body.rest[i], 1.0f));
    body.prev = body.pos;
    body.vel.assign(n, glm::vec3(0.0f));
    body.grad.assign(n, glm::vec3(0.0f));

    std::set<std::pair<uint32_t, uint32_t>> seen;
    std::map<std::pair<uint32_t, uint32_t>, int> sides;
    auto edge = [&](uint32_t a, uint32_t b) {
        if (a == b) return;
        if (a > b) std::swap(a, b);
        if (!seen.insert({a, b}).second) return;
        body.edges.push_back({a, b, glm::length(body.pos[a] - body.pos[b])});
    };
    for (const auto& f : shape.faces) {
        if (f.size() < 3) continue;
        for (size_t k = 0; k < f.size(); ++k) {
            const uint32_t a = body.cornerOf[f[k]], b = body.cornerOf[f[(k + 1) % f.size()]];
            edge(a, b);
            if (a != b) ++sides[{a, b}];
        }
        // (across four-sided faces too, so they can't fold flat)
        if (f.size() == 4) { edge(body.cornerOf[f[0]], body.cornerOf[f[2]]); edge(body.cornerOf[f[1]], body.cornerOf[f[3]]); }
        for (size_t k = 1; k + 1 < f.size(); ++k) {
            const uint32_t a = body.cornerOf[f[0]], b = body.cornerOf[f[k]], c = body.cornerOf[f[k + 1]];
            if (a == b || b == c || a == c) continue;
            body.tris.push_back(a); body.tris.push_back(b); body.tris.push_back(c);
        }
    }
    // Closed (every edge has a face on both sides)? Then it has a volume to keep.
    bool closed = !sides.empty();
    for (const auto& [e, count] : sides) {
        auto other = sides.find({e.second, e.first});
        if (other == sides.end() || other->second != count) { closed = false; break; }
    }
    body.restVolume = closed ? signedVolume(body.pos, body.tris) : 0.0f;
    if (body.restVolume < 1e-6f) body.restVolume = 0.0f;   // (inside out, or flat)
}

bool solve(Body& b, const glm::mat4& frame, float dt, float gravity, const Collider* colliders, size_t colliderCount) {
    const size_t n = b.pos.size();
    if (n == 0) return false;
    dt = std::clamp(dt, 0.0f, 1.0f / 20.0f);
    if (dt <= 0.0f) return false;
    const bool moved = !sameFrame(frame, b.frame);
    if (b.asleep && !moved) return false;
    b.asleep = false;

    // The part was put somewhere else entirely (teleported): go with it.
    const glm::vec3 jump = glm::vec3(frame[3]) - glm::vec3(b.frame[3]);
    const float size = glm::length(glm::vec3(frame[0])) + glm::length(glm::vec3(frame[1])) + glm::length(glm::vec3(frame[2]));
    if (glm::length(jump) > size * 2.0f + 8.0f)
        for (size_t i = 0; i < n; ++i) { b.pos[i] += jump; b.prev[i] += jump; }

    const float bob = bobsPerSecond(b.softness);
    const float k = (6.2832f * bob) * (6.2832f * bob);       // spring to its own place, per unit of weight
    const float shapeGive = 1.0f / k;                         // ("compliance": how much it gives)
    const float edgeGive = shapeGive * 0.3f;                  // neighbours hold on tighter than that
    const float volumeGive = shapeGive * 0.02f * std::max(b.restVolume, 1e-3f);
    const float drag = glm::mix(5.5f, 2.2f, b.softness);      // how fast the wobble dies away (1/s)

    const int steps = 4;
    const float h = dt / steps;
    float fastest = 0.0f;
    for (int s = 0; s < steps; ++s) {
        // The part moves from where it was to where it is across the steps.
        const float t = (float)(s + 1) / steps;
        b.time += h;
        const float keep = std::exp(-drag * h);
        for (size_t i = 0; i < n; ++i) {
            b.vel[i].y -= gravity * h;
            b.prev[i] = b.pos[i];
            b.pos[i] += b.vel[i] * h;
        }
        // 1. Each corner is pulled to its own place on the part.
        {
            const float a = shapeGive / (h * h);
            const float pull = 1.0f / (1.0f + a);
            for (size_t i = 0; i < n; ++i) {
                const glm::vec3 local = b.rest[i];
                const glm::vec3 was = glm::vec3(b.frame * glm::vec4(local, 1.0f)), now = glm::vec3(frame * glm::vec4(local, 1.0f));
                const glm::vec3 home = glm::mix(was, now, t);
                b.pos[i] += (home - b.pos[i]) * pull;
            }
        }
        // 2. Edges keep their length.
        {
            const float a = edgeGive / (h * h);
            for (const Body::Edge& e : b.edges) {
                const glm::vec3 d = b.pos[e.b] - b.pos[e.a];
                const float len = glm::length(d);
                if (len < 1e-7f) continue;
                const float lambda = (len - e.rest) / (2.0f + a);
                const glm::vec3 fix = d * (lambda / len);
                b.pos[e.a] += fix;
                b.pos[e.b] -= fix;
            }
        }
        // 3. The whole thing keeps its volume: squashed flat, it bulges at the sides.
        if (b.restVolume > 0.0f) {
            std::fill(b.grad.begin(), b.grad.end(), glm::vec3(0.0f));
            double six = 0.0;
            for (size_t ti = 0; ti + 2 < b.tris.size(); ti += 3) {
                const uint32_t i0 = b.tris[ti], i1 = b.tris[ti + 1], i2 = b.tris[ti + 2];
                const glm::vec3& p0 = b.pos[i0];
                const glm::vec3& p1 = b.pos[i1];
                const glm::vec3& p2 = b.pos[i2];
                b.grad[i0] += glm::cross(p1, p2);
                b.grad[i1] += glm::cross(p2, p0);
                b.grad[i2] += glm::cross(p0, p1);
                six += glm::dot(glm::dvec3(p0), glm::cross(glm::dvec3(p1), glm::dvec3(p2)));
            }
            const float C = (float)(six / 6.0) - b.restVolume;
            float sum = 0.0f;
            for (size_t i = 0; i < n; ++i) sum += glm::dot(b.grad[i], b.grad[i]);
            sum /= 36.0f;
            const float a = volumeGive / (h * h);
            if (sum > 1e-12f) {
                // (never more than a little at a time: a shape turned inside out by a wild
                // hit must not be blown apart by its own volume)
                const float lambda = std::clamp(-C / (sum + a), -0.05f, 0.05f) / 6.0f;
                for (size_t i = 0; i < n; ++i) b.pos[i] += b.grad[i] * lambda;
            }
        }
        // 4. It can't go into things.
        for (size_t c = 0; c < colliderCount; ++c)
            for (size_t i = 0; i < n; ++i) pushOut(b.pos[i], colliders[c]);

        fastest = 0.0f;
        const glm::vec3 carried = moved ? jump / dt : glm::vec3(0.0f);   // (the part's own speed isn't wobble)
        for (size_t i = 0; i < n; ++i) {
            b.vel[i] = (b.pos[i] - b.prev[i]) / h;
            // The wobble dies away (the part's own motion doesn't).
            b.vel[i] = carried + (b.vel[i] - carried) * keep;
            fastest = std::max(fastest, glm::length(b.vel[i] - carried));
        }
    }
    b.frame = frame;

    // Something went badly wrong (a wild number): start again from the part.
    bool sane = true;
    for (size_t i = 0; i < n && sane; ++i)
        sane = std::isfinite(b.pos[i].x) && std::isfinite(b.pos[i].y) && std::isfinite(b.pos[i].z) &&
               glm::length(b.pos[i] - glm::vec3(frame[3])) < size * 3.0f + 12.0f;
    if (!sane) {
        for (size_t i = 0; i < n; ++i) b.pos[i] = b.prev[i] = glm::vec3(frame * glm::vec4(b.rest[i], 1.0f));
        std::fill(b.vel.begin(), b.vel.end(), glm::vec3(0.0f));
    }
    // Still for a while, and the part isn't moving: nothing more to do until it's
    // touched again.
    b.calm = (fastest < kStill && !moved) ? b.calm + dt : 0.0f;
    if (b.calm > 0.6f) {
        b.asleep = true;
        std::fill(b.vel.begin(), b.vel.end(), glm::vec3(0.0f));
    }
    return true;
}

void poke(Body& b, const glm::vec3& point, const glm::vec3& velocity, float radius) {
    radius = std::max(radius, 1e-3f);
    bool any = false;
    for (size_t i = 0; i < b.pos.size(); ++i) {
        const float d = glm::length(b.pos[i] - point) / radius;
        if (d >= 1.0f) continue;
        const float w = (1.0f - d * d) * (1.0f - d * d);
        b.vel[i] += velocity * w;
        any = true;
    }
    if (any) { b.asleep = false; b.calm = 0.0f; }
}

float volume(const Body& b) { return signedVolume(b.pos, b.tris); }

float stretch(const Body& b, uint32_t* edge) {
    float most = 1.0f;
    for (size_t i = 0; i < b.edges.size(); ++i) {
        const Body::Edge& e = b.edges[i];
        if (e.rest < 1e-6f) continue;
        const float r = glm::length(b.pos[e.b] - b.pos[e.a]) / e.rest;
        if (r > most) { most = r; if (edge) *edge = (uint32_t)i; }
    }
    return most;
}

float tearStretch(const Body& b, uint32_t* edge) {
    // Like stretch, but tiny edges (the thin rings round a cut end) count as if they were
    // a fair length: a short edge pulled a little isn't a tear.
    double total = 0.0;
    for (const Body::Edge& e : b.edges) total += e.rest;
    const float typical = b.edges.empty() ? 0.0f : (float)(total / (double)b.edges.size());
    float most = 1.0f;
    for (size_t i = 0; i < b.edges.size(); ++i) {
        const Body::Edge& e = b.edges[i];
        const float len = glm::length(b.pos[e.b] - b.pos[e.a]);
        const float r = 1.0f + (len - e.rest) / std::max(e.rest, typical * 0.75f);
        if (r > most) { most = r; if (edge) *edge = (uint32_t)i; }
    }
    return most;
}

float offShape(const Body& b) {
    float most = 0.0f;
    for (size_t i = 0; i < b.pos.size(); ++i)
        most = std::max(most, glm::length(b.pos[i] - glm::vec3(b.frame * glm::vec4(b.rest[i], 1.0f))));
    return most;
}

EditMesh softBox(int cuts) {
    cuts = std::clamp(cuts, 1, 16);
    EditMesh m;
    std::map<std::array<int, 3>, uint32_t> at;
    auto corner = [&](int i, int j, int k) {
        auto [it, fresh] = at.try_emplace({i, j, k}, (uint32_t)m.verts.size());
        if (fresh) m.verts.push_back(glm::vec3((float)i, (float)j, (float)k) / (float)cuts - 0.5f);
        return it->second;
    };
    // Each side is a grid; u x v points out of the side (for the +, and into it for the -).
    const int U[3] = {1, 2, 0}, V[3] = {2, 0, 1};
    for (int axis = 0; axis < 3; ++axis)
        for (int side = 0; side < 2; ++side)
            for (int u = 0; u < cuts; ++u)
                for (int v = 0; v < cuts; ++v) {
                    auto p = [&](int du, int dv) {
                        int c[3];
                        c[axis] = side ? cuts : 0;
                        c[U[axis]] = u + du;
                        c[V[axis]] = v + dv;
                        return corner(c[0], c[1], c[2]);
                    };
                    if (side) m.faces.push_back({p(0, 0), p(1, 0), p(1, 1), p(0, 1)});
                    else      m.faces.push_back({p(0, 0), p(0, 1), p(1, 1), p(1, 0)});
                }
    return m;
}

// ---------------------------------------------------------------------------
// Parts in the world
// ---------------------------------------------------------------------------

namespace {

struct State {
    const EditMesh* mesh = nullptr;   // the shape it was made from (redone if it changes)
    Body body;
    std::vector<glm::vec3> posed;     // the shape's corners, bent (the part's own space)
    float tearWait = 0.0f;            // seconds until it may try to tear again
};

std::unordered_map<uint64_t, State>& states() {
    static std::unordered_map<uint64_t, State> s;
    return s;
}

float number(const SceneNode& n, const char* name, float fallback) {
    const Attribute* a = n.findAttribute(name);
    return a && a->type == Attribute::Number ? (float)a->n : fallback;
}

// The part's body: made the first time, and again if its shape was changed.
State* stateOf(SceneNode& part) {
    if (!part.editMesh) {
        if (part.primitiveType == PrimitiveType::Cube) MeshEdit::attach(part, std::make_shared<EditMesh>(softBox()));
        else if (part.primitiveType == PrimitiveType::Sphere || part.primitiveType == PrimitiveType::Cylinder)
            MeshEdit::attach(part, MeshEdit::fromPrimitive(part.primitiveType));
        else return nullptr;
    }
    const EditMesh& m = *part.editMesh;
    if (m.verts.size() < 4 || m.verts.size() > (size_t)kMaxCorners || m.skinned()) return nullptr;
    auto it = states().find(part.id);
    if (it != states().end() && it->second.mesh == part.editMesh.get() && it->second.body.cornerOf.size() == m.verts.size())
        return &it->second;
    State& s = states()[part.id];
    s = State{};
    s.mesh = part.editMesh.get();
    build(s.body, m, part.worldMatrix(), number(part, "Softness", 0.5f));
    s.posed.resize(m.verts.size());
    return &s;
}

// Show the bent shape.
void show(SceneNode& part, State& s, const glm::mat4& frame) {
    const glm::mat4 inv = glm::inverse(frame);
    for (size_t i = 0; i < s.posed.size(); ++i)
        s.posed[i] = glm::vec3(inv * glm::vec4(s.body.pos[s.body.cornerOf[i]], 1.0f));
    MeshEdit::refresh(part, &s.posed);
}

} // namespace

bool wants(const SceneNode& part) {
    const Attribute* a = part.findAttribute("Soft");
    return a && a->type == Attribute::Bool && a->b;
}

const Body* bodyOf(const SceneNode& part) {
    auto it = states().find(part.id);
    return it == states().end() ? nullptr : &it->second.body;
}

bool poke(Scene& scene, SceneNode& part, const glm::vec3& point, const glm::vec3& velocity, float radius) {
    if (!part.isPart() || !wants(part) || scene.isCharacterPart(&part)) return false;
    State* s = stateOf(part);
    if (!s) return false;
    poke(s->body, point, velocity, radius);
    return true;
}

void reset(SceneNode& part) {
    if (states().erase(part.id) && part.editMesh) MeshEdit::refresh(part);
}

void clearAll() { states().clear(); }

void update(Scene& scene, float dt, const std::vector<Physics::Knock>& knocks, bool host) {
    // (Lists kept between frames, so a frame with soft bodies asks for no memory.)
    static std::vector<SceneNode*> soft;
    static std::vector<Collider> boxes, near;
    static std::vector<float> reach;
    static std::vector<uint64_t> owner;
    struct Tear { uint64_t id; glm::vec3 at, way; };
    static std::vector<Tear> tears;
    soft.clear();
    scene.forEach([&](SceneNode* n) {
        if (!n->attributes.empty() && n->isPart() && wants(*n) && !scene.isCharacterPart(n)) soft.push_back(n);
    });
    if (soft.empty()) { if (!states().empty()) clearAll(); return; }

    // Hits: a dent and a ripple where something knocked into it.
    for (const Physics::Knock& k : knocks)
        for (uint64_t id : {k.a, k.b}) {
            SceneNode* n = id ? scene.findById(id) : nullptr;
            if (!n || !n->isPart() || !wants(*n)) continue;
            State* s = stateOf(*n);
            if (!s) continue;
            const glm::vec3 centre = glm::vec3(n->worldMatrix()[3]);
            glm::vec3 in = centre - k.at;
            const float len = glm::length(in);
            in = len > 1e-4f ? in / len : glm::vec3(0.0f, 1.0f, 0.0f);
            const float size = glm::length(n->transform.scale) * 0.5f;
            poke(s->body, k.at, in * std::min(k.speed, 60.0f) * 0.7f, std::max(0.6f, size * 0.7f));
        }

    // Is anything awake? Only then is it worth finding what's around them.
    bool work = false;
    for (SceneNode* n : soft) {
        State* s = stateOf(*n);
        if (s && !(s->body.asleep && sameFrame(s->body.frame, n->worldMatrix()))) work = true;
    }
    if (work) {
        boxes.clear(); reach.clear(); owner.clear();
        scene.forEach([&](SceneNode* n) {
            if (!n->isPart() || !n->canCollide || !n->visible) return;
            const glm::mat4 m = n->worldMatrix();
            const glm::mat3 axes(glm::vec3(m[0]) * 0.5f, glm::vec3(m[1]) * 0.5f, glm::vec3(m[2]) * 0.5f);
            boxes.push_back({glm::vec3(m[3]), axes});
            reach.push_back(glm::length(axes[0] + axes[1] + axes[2]) + glm::length(axes[0] - axes[1] + axes[2]));   // (generous)
            owner.push_back(n->id);
        });
    }

    tears.clear();
    const float gravity = scene.world().gravity;
    for (SceneNode* n : soft) {
        State* s = stateOf(*n);
        if (!s) continue;
        const glm::mat4 frame = n->worldMatrix();
        near.clear();
        if (work) {
            const glm::vec3 centre = glm::vec3(frame[3]);
            const float mine = glm::length(n->transform.scale) * 0.5f + 1.0f;
            for (size_t i = 0; i < boxes.size(); ++i)
                if (owner[i] != n->id && glm::length(boxes[i].center - centre) < reach[i] + mine) near.push_back(boxes[i]);
        }
        if (!solve(s->body, frame, dt, gravity, near.data(), near.size())) continue;
        show(*n, *s, frame);
        // Stretched too far: it tears, across the edge that gave way.
        s->tearWait = std::max(0.0f, s->tearWait - dt);
        const float tearAt = number(*n, "TearAt", 0.0f);
        // (Not a piece that was only just made, and not one that's already small:
        // otherwise a tear would keep tearing into crumbs.)
        const glm::vec3 S = n->transform.scale;
        const bool bigEnough = std::min(S.x, std::min(S.y, S.z)) >= 0.6f;
        if (host && tearAt > 1.0f && s->tearWait <= 0.0f && s->body.time > 0.5f && bigEnough) {
            uint32_t edge = 0;
            if (tearStretch(s->body, &edge) > tearAt) {
                const Body::Edge& e = s->body.edges[edge];
                const glm::vec3 a = s->body.pos[e.a], b = s->body.pos[e.b];
                // (cut the part where that edge really is: its own place, not the bent one)
                const glm::vec3 ra = glm::vec3(frame * glm::vec4(s->body.rest[e.a], 1.0f)), rb = glm::vec3(frame * glm::vec4(s->body.rest[e.b], 1.0f));
                tears.push_back({n->id, (ra + rb) * 0.5f, glm::length(b - a) > 1e-5f ? b - a : rb - ra});
                s->tearWait = 0.75f;
            }
        }
    }
    for (const Tear& t : tears)
        if (SceneNode* n = scene.findById(t.id)) {
            const std::vector<SceneNode*> halves = Slice::part(scene, n, t.at, t.way, 1.5f);
            if (!halves.empty()) states().erase(t.id);
        }
    // Forget bodies whose part is gone, or isn't soft any more.
    if (states().size() > soft.size())
        for (auto it = states().begin(); it != states().end();) {
            SceneNode* n = scene.findById(it->first);
            if (n && wants(*n)) { ++it; continue; }
            if (n && n->editMesh) MeshEdit::refresh(*n);
            it = states().erase(it);
        }
}

} // namespace SoftBody
