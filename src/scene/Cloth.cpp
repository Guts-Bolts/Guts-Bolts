#include "Cloth.h"
#include "EditMesh.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Skin.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <unordered_map>

namespace Cloth {

namespace {

struct State {
    const EditMesh* mesh = nullptr;      // the shape it was made from (redone if it changes)
    std::vector<glm::vec3> pos, prev;    // world space
    std::vector<float> pin;              // 1 = held by the part, 0 = free (in between: partly)
    struct Stick { uint32_t a, b; float rest; };
    std::vector<Stick> sticks;
    float time = 0.0f;
};

std::unordered_map<uint64_t, State>& states() {
    static std::unordered_map<uint64_t, State> s;
    return s;
}

float attrNumber(const SceneNode& n, const char* name, float fallback) {
    for (const SceneNode* p = &n; p; p = p->parent) {
        if (const Attribute* a = p->findAttribute(name); a && a->type == Attribute::Number) return (float)a->n;
        if (p != &n && p->kind != NodeKind::Model) break;
    }
    return fallback;
}

// Push a point out of a box (the nearest face), a little padding so cloth sits on it.
void pushOut(glm::vec3& p, const Collider& c) {
    const float pad = 0.04f;
    glm::vec3 local;
    glm::vec3 half;
    glm::vec3 axis[3];
    for (int i = 0; i < 3; ++i) {
        half[i] = glm::length(c.axes[i]);
        axis[i] = half[i] > 1e-6f ? c.axes[i] / half[i] : glm::vec3(0.0f);
        local[i] = glm::dot(p - c.center, axis[i]);
    }
    float best = 1e30f;
    int bestAxis = -1;
    for (int i = 0; i < 3; ++i) {
        const float inside = half[i] + pad - std::abs(local[i]);
        if (inside <= 0.0f) return;   // outside on this axis: not touching
        if (inside < best) { best = inside; bestAxis = i; }
    }
    if (bestAxis < 0) return;
    p += axis[bestAxis] * (local[bestAxis] >= 0.0f ? best : -best);
}

State& build(SceneNode& part, const glm::mat4& M) {
    State& s = states()[part.id];
    const EditMesh& m = *part.editMesh;
    s = State{};
    s.mesh = part.editMesh.get();
    const size_t n = m.verts.size();
    s.pos.resize(n);
    s.pin.resize(n);
    // The top band of the part is held (the shoulders of a cape, the waist of a skirt).
    const float band = std::clamp(attrNumber(part, "ClothPin", 0.12f), 0.0f, 1.0f);
    float top = -1e30f;
    for (const glm::vec3& v : m.verts) top = std::max(top, v.y);
    for (size_t i = 0; i < n; ++i) {
        s.pos[i] = glm::vec3(M * glm::vec4(m.verts[i], 1.0f));
        const float below = top - m.verts[i].y;   // (corners are in -0.5..0.5)
        s.pin[i] = band <= 0.0f ? 0.0f : std::clamp(1.0f - (below - band) / 0.05f, 0.0f, 1.0f);
    }
    s.prev = s.pos;
    std::set<std::pair<uint32_t, uint32_t>> seen;
    for (const auto& f : m.faces)
        for (size_t k = 0; k < f.size(); ++k) {
            uint32_t a = f[k], b = f[(k + 1) % f.size()];
            if (a >= n || b >= n || a == b) continue;
            if (a > b) std::swap(a, b);
            if (!seen.insert({a, b}).second) continue;
            s.sticks.push_back({a, b, glm::length(s.pos[a] - s.pos[b])});
        }
    return s;
}

} // namespace

bool wants(const SceneNode& part, const char* attribute) {
    for (const SceneNode* p = &part; p; p = p->parent) {
        if (const Attribute* a = p->findAttribute(attribute)) return a->type == Attribute::Bool && a->b;
        if (p != &part && p->kind != NodeKind::Model) break;   // (the part, then the models it's in)
    }
    return false;
}

bool step(SceneNode& part, float dt, float gravity, const glm::vec3& wind, const std::vector<Collider>& colliders) {
    if (!part.editMesh && part.primitiveType != PrimitiveType::None && part.primitiveType != PrimitiveType::Mesh)
        MeshEdit::attach(part, MeshEdit::fromPrimitive(part.primitiveType));
    if (!part.editMesh || part.editMesh->verts.size() < 3 || part.editMesh->verts.size() > 6000) return false;
    const glm::mat4 M = part.worldMatrix();
    auto it = states().find(part.id);
    State& s = it != states().end() && it->second.mesh == part.editMesh.get() ? it->second : build(part, M);
    const EditMesh& m = *part.editMesh;
    if (s.pos.size() != m.verts.size()) build(part, M);
    dt = std::clamp(dt, 0.0f, 1.0f / 20.0f);
    if (dt <= 0.0f) return true;

    // A few small steps are steadier than one big one.
    const int sub = 3;
    const float h = dt / sub;
    const float drag = 0.985f;
    for (int k = 0; k < sub; ++k) {
        s.time += h;
        for (size_t i = 0; i < s.pos.size(); ++i) {
            const glm::vec3 home = glm::vec3(M * glm::vec4(m.verts[i], 1.0f));
            if (s.pin[i] >= 1.0f) { s.prev[i] = s.pos[i] = home; continue; }
            glm::vec3 v = (s.pos[i] - s.prev[i]) * drag;
            // Wind: a steady push plus a flutter that ripples across the cloth.
            glm::vec3 a(0.0f, -gravity * 0.35f, 0.0f);
            if (glm::length(wind) > 1e-3f) {
                const float flutter = 0.75f + 0.5f * std::sin(s.time * 7.0f + home.x * 1.7f + home.y * 2.3f + home.z * 1.1f);
                a += wind * 1.4f * flutter;
            }
            s.prev[i] = s.pos[i];
            s.pos[i] += v + a * h * h;
            if (s.pin[i] > 0.0f) s.pos[i] = glm::mix(s.pos[i], home, s.pin[i]);
        }
        // Sticks keep each edge its own length (several passes, so it doesn't stretch).
        for (int iter = 0; iter < 6; ++iter)
            for (const State::Stick& st : s.sticks) {
                glm::vec3 d = s.pos[st.b] - s.pos[st.a];
                const float len = glm::length(d);
                if (len < 1e-6f) continue;
                const float wa = 1.0f - s.pin[st.a], wb = 1.0f - s.pin[st.b];
                const float sum = wa + wb;
                if (sum < 1e-6f) continue;
                const glm::vec3 fix = d * ((len - st.rest) / len);
                s.pos[st.a] += fix * (wa / sum);
                s.pos[st.b] -= fix * (wb / sum);
            }
        for (size_t i = 0; i < s.pos.size(); ++i)
            if (s.pin[i] < 1.0f) for (const Collider& c : colliders) pushOut(s.pos[i], c);
    }
    // Something went wrong (teleported, flung far away): start again from the part.
    const float size = glm::length(part.transform.scale) * 4.0f + 4.0f;
    for (size_t i = 0; i < s.pos.size(); ++i)
        if (!std::isfinite(s.pos[i].x) || glm::length(s.pos[i] - glm::vec3(M[3])) > size) { build(part, M); break; }

    // Back into the part's own space, and bend the mesh there.
    const glm::mat4 inv = glm::inverse(M);
    std::vector<glm::vec3> posed(s.pos.size());
    for (size_t i = 0; i < s.pos.size(); ++i) posed[i] = glm::vec3(inv * glm::vec4(s.pos[i], 1.0f));
    MeshEdit::refresh(part, &posed);
    return true;
}

void reset(SceneNode& part) {
    if (states().erase(part.id) && part.editMesh) MeshEdit::refresh(part);
}

void clearAll() { states().clear(); }

float sag(const SceneNode& part) {
    auto it = states().find(part.id);
    if (it == states().end() || !part.editMesh) return 0.0f;
    const glm::mat4 M = part.worldMatrix();
    float most = 0.0f;
    for (size_t i = 0; i < it->second.pos.size() && i < part.editMesh->verts.size(); ++i)
        most = std::max(most, glm::length(it->second.pos[i] - glm::vec3(M * glm::vec4(part.editMesh->verts[i], 1.0f))));
    return most;
}

bool rigJiggle(SceneNode& part) {
    if (!Skin::bones(&part).empty()) return true;
    if (!part.editMesh) {
        if (part.primitiveType == PrimitiveType::None) return false;
        MeshEdit::attach(part, MeshEdit::fromPrimitive(part.primitiveType));
    }
    if (!part.editMesh || part.editMesh->verts.empty()) return false;
    // Where it's held: the side of it nearest the head of the character wearing it (a
    // ponytail hangs from the back of the head), or its top when it isn't worn.
    const glm::vec3 S = part.transform.scale;
    glm::vec3 from(0.0f, 0.5f * S.y, 0.0f);
    SceneNode* head = nullptr;
    for (SceneNode* p = part.parent; p && !head; p = p->parent) head = p->findChild("Head");
    if (head && head != &part) {
        const glm::mat4 toPart = glm::inverse(Skin::partSpace(&part));
        glm::vec3 h = glm::vec3(toPart * glm::vec4(glm::vec3(head->worldMatrix()[3]), 1.0f));
        from = glm::clamp(h, -0.5f * S, 0.5f * S);   // the nearest point of its box
        if (glm::length(from) < 1e-3f) from = {0.0f, 0.5f * S.y, 0.0f};
    }
    glm::vec3 dir = -from;
    const float reach = glm::length(dir) * 2.0f;
    if (reach < 1e-3f) return false;
    dir = glm::normalize(dir);
    // The bone's +Y points from where it's held to the far end (turns applied z, y, x like Skin).
    const glm::mat4 R = glm::mat4_cast(glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), dir));
    float z, y, x;
    glm::extractEulerAngleZYX(R, z, y, x);
    const float stiff = std::clamp(attrNumber(part, "JiggleStiffness", 0.45f), 0.0f, 1.0f);
    auto bone = [&](SceneNode& in, const char* name, glm::vec3 pos, glm::vec3 rot) {
        auto b = std::make_unique<SceneNode>(name, NodeKind::Attachment);
        b->bone = true;
        b->transform.position = pos;
        b->transform.rotation = rot;
        b->rig.type = BoneType::Jiggle;
        b->rig.stiffness = stiff;
        b->rig.damping = 0.3f;
        b->rig.gravity = 4.0f;
        b->rig.length = reach * 0.5f;
        b->tags.push_back("JiggleBone");
        return in.addChild(std::move(b));
    };
    SceneNode* root = bone(part, "JiggleRoot", from, glm::degrees(glm::vec3(x, y, z)));
    bone(*root, "JiggleTip", {0.0f, reach * 0.5f, 0.0f}, glm::vec3(0.0f));
    root->rig.stiffness = std::min(1.0f, stiff + 0.25f);   // (the base moves less than the tip)
    return Skin::bindAutomatic(part);
}

void update(Scene& scene) {
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (!Skin::live()) { if (!states().empty()) clearAll(); return; }
    const WorldSettings& w = scene.world();
    std::vector<SceneNode*> cloth;
    scene.forEach([&](SceneNode* n) {
        if (!n->isPart() || (n->attributes.empty() && (!n->parent || n->parent->attributes.empty()))) return;
        if (wants(*n, "Cloth")) cloth.push_back(n);
        else if (wants(*n, "Jiggle") && Skin::bones(n).empty()) rigJiggle(*n);
    });
    for (SceneNode* part : cloth) {
        // It bumps into the body it's on: the other parts of the character (or model).
        std::vector<Collider> colliders;
        SceneNode* model = part->parent;
        while (model && model->parent && model->parent->kind == NodeKind::Model && model->parent->parent) model = model->parent;
        if (model && model->kind == NodeKind::Model)
            for (auto& c : model->children) {
                if (!c->isPart() || c.get() == part || !c->visible || c->name == "HumanoidRootPart") continue;
                const glm::mat4 m = c->worldMatrix();
                colliders.push_back({glm::vec3(m[3]), glm::mat3(glm::vec3(m[0]) * 0.5f, glm::vec3(m[1]) * 0.5f, glm::vec3(m[2]) * 0.5f)});
            }
        step(*part, dt, w.gravity, w.globalWind, colliders);
    }
    // Forget cloth that's gone.
    for (auto it = states().begin(); it != states().end();)
        it = scene.findById(it->first) ? std::next(it) : states().erase(it);
}

} // namespace Cloth
