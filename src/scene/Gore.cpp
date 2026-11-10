#include "Gore.h"
#include "EditMesh.h"
#include "Scene.h"
#include "SceneNode.h"
#include "../renderer/MeshLibrary.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Gore {

namespace {

const Attribute* attrUp(const SceneNode* n, const char* name) {
    // The character's Model (or any model above the part) can say how it bleeds.
    for (const SceneNode* p = n; p; p = p->parent)
        if (const Attribute* a = p->findAttribute(name)) return a;
    return nullptr;
}

glm::vec3 sideways(const glm::vec3& n) {
    return glm::normalize(glm::cross(n, std::abs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
}

} // namespace

Look lookFor(Scene& scene, const SceneNode* n) {
    Look l;
    l.on = scene.goreEnabled();
    l.kind = scene.goreKind();
    l.color = scene.world().bloodColor;
    l.amount = scene.world().bloodAmount;
    if (const Attribute* g = attrUp(n, "Gore"); g && g->type == Attribute::String) {
        if (g->s == "Off") l.on = false;
        else if (g->s == "Oil") l.kind = GoreKind::Oil;
        else if (g->s == "Blood") l.kind = GoreKind::Blood;
    }
    if (const Attribute* c = attrUp(n, "BloodColor"); c && (c->type == Attribute::Color3 || c->type == Attribute::Vector3))
        l.color = glm::clamp(c->v, glm::vec3(0.0f), glm::vec3(1.0f));
    if (const Attribute* a = attrUp(n, "BloodAmount"); a && a->type == Attribute::Number)
        l.amount = std::clamp((float)a->n, 0.0f, 5.0f) * scene.world().bloodAmount;
    return l;
}

Scope::Scope(Scene& scene, const SceneNode* n)
    : m_look(lookFor(scene, n)), m_keep(scene.particles(), m_look.color, m_look.amount) {}

bool exitPoint(const SceneNode& part, const glm::vec3& entry, const glm::vec3& dir, glm::vec3& out) {
    if (glm::length(dir) < 1e-6f) return false;
    const glm::mat4 M = part.worldMatrix();
    if (std::abs(glm::determinant(glm::mat3(M))) < 1e-9f) return false;
    const glm::mat4 inv = glm::inverse(M);
    // In the part's own -0.5..0.5 box: how far along `dir` until it leaves.
    glm::vec3 o = glm::vec3(inv * glm::vec4(entry, 1.0f));
    glm::vec3 d = glm::vec3(inv * glm::vec4(glm::normalize(dir), 0.0f));
    float tExit = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-9f) continue;
        const float wall = d[i] > 0.0f ? 0.5f : -0.5f;
        tExit = std::min(tExit, (wall - o[i]) / d[i]);
    }
    if (!(tExit > 0.05f) || tExit > 1e20f) return false;
    out = glm::vec3(M * glm::vec4(o + d * tExit, 1.0f));
    return glm::length(out - entry) > 0.1f;
}

void hit(Scene& scene, SceneNode* part, const glm::vec3& point, const glm::vec3& normal,
         const glm::vec3& velocity, float damage) {
    if (!part) return;
    Scope look(scene, part);
    if (!look.on()) return;
    ParticleSystem& ps = scene.particles();
    const glm::vec3 n = glm::length(normal) > 1e-5f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
    glm::vec3 tangent = velocity - n * glm::dot(velocity, n);
    if (glm::length(tangent) < 1e-3f) tangent = sideways(n);
    const float width = std::clamp(0.07f + damage * 0.0015f, 0.07f, 0.16f);
    const float length = std::clamp(0.22f + damage * 0.003f, 0.22f, 0.48f);
    ps.wound(look.kind(), part, point, n, tangent, width, length);
    const float speed = glm::length(velocity);
    if (speed > 4.0f) ps.spurt(look.kind(), point, n, velocity * 0.015f, std::clamp(speed * 0.025f, 0.15f, 1.5f));
    else              ps.spray(look.kind(), point, n, 5, 2.5f);
    if (look.kind() == GoreKind::Oil) ps.sparks(point, 3);

    // Fast enough to go right through: the exit wound is bigger and messier, and what
    // comes out of it keeps going (it splats on the wall behind).
    glm::vec3 out;
    if (speed >= kThroughSpeed && exitPoint(*part, point, velocity, out)) {
        const glm::vec3 dir = velocity / speed;
        glm::vec3 t2 = sideways(dir);
        ps.wound(look.kind(), part, out, dir, t2, width * 1.5f, length * 1.3f);
        ps.spurt(look.kind(), out, dir, velocity * 0.02f, 1.5f);
        ps.spray(look.kind(), out, dir, 14, std::clamp(speed * 0.05f, 6.0f, 16.0f));
        ps.stain(look.kind(), part, out, 0.35f);
        if (look.kind() == GoreKind::Oil) ps.sparks(out, 6);
    }
}

void tear(Scene& scene, SceneNode* part, const glm::vec3& point, const glm::vec3& normal, const glm::vec3& velocity) {
    if (!part) return;
    Scope look(scene, part);
    if (!look.on()) return;
    ParticleSystem& ps = scene.particles();
    const glm::vec3 n = glm::length(normal) > 1e-5f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
    const glm::vec3 t = sideways(n);
    ps.wound(look.kind(), part, point, n, t, 0.2f, 0.55f);
    ps.wound(look.kind(), part, point + t * 0.08f, n, glm::cross(n, t), 0.12f, 0.35f);   // ragged: torn two ways
    ps.gibs(look.kind(), point, n * 3.0f + velocity * 0.02f, 3);
    ps.spurt(look.kind(), point, n, velocity * 0.01f, 1.2f);
    ps.spray(look.kind(), point, n, 16, 4.0f);
    ps.stain(look.kind(), part, point, 0.6f);
    if (look.kind() == GoreKind::Oil) ps.sparks(point, 10);
}

SceneNode* cutEnd(Scene& scene, SceneNode* part, const glm::vec3& end, float size, GoreKind kind, const glm::vec3& color) {
    if (!part || !part->isPart()) return nullptr;
    // Which way the end faces (its biggest axis), and how wide the part really is there.
    int axis = 1;
    for (int i = 0; i < 3; ++i) if (std::abs(end[i]) > std::abs(end[axis])) axis = i;
    const float side = end[axis] >= 0.0f ? 1.0f : -1.0f;
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    glm::vec2 lo(-0.46f), hi(0.46f);
    if (part->editMesh && !part->editMesh->verts.empty()) {
        // Only the corners near that end count: a bundle's arm can be thin at the
        // shoulder and wide at the hand.
        float far = -1e30f;
        for (const glm::vec3& v : part->editMesh->verts) far = std::max(far, v[axis] * side);
        lo = glm::vec2(1e30f);
        hi = glm::vec2(-1e30f);
        for (const glm::vec3& v : part->editMesh->verts)
            if (v[axis] * side > far - 0.15f) {
                lo = glm::min(lo, glm::vec2(v[a1], v[a2]));
                hi = glm::max(hi, glm::vec2(v[a1], v[a2]));
            }
        if (lo.x > hi.x) { lo = glm::vec2(-0.46f); hi = glm::vec2(0.46f); }
        lo *= 0.92f;
        hi *= 0.92f;
    }
    const glm::vec2 mid = (lo + hi) * 0.5f, span = glm::max(hi - lo, glm::vec2(0.05f)) * std::clamp(size, 0.2f, 1.0f);
    const glm::vec3 S = part->transform.scale;
    const float thick = 0.03f / std::max(0.05f, S[axis]);   // about 0.03 studs, whatever the part's size

    auto disc = [&](const char* name, float shrink, float lift, const glm::vec3& col, Material mat) {
        auto c = std::make_unique<SceneNode>(name);
        c->primitiveType = PrimitiveType::Cylinder;
        c->mesh = MeshLibrary::get(PrimitiveType::Cylinder);
        glm::vec3 pos(0.0f), scale(1.0f), rot(0.0f);
        pos[axis] = side * (0.5f + lift);
        pos[a1] = mid.x;
        pos[a2] = mid.y;
        scale[axis] = thick;
        scale[a1] = span.x * shrink;
        scale[a2] = span.y * shrink;
        // A cylinder stands along its own Y: lay it along `axis`.
        if (axis == 0) { rot = {0.0f, 0.0f, 90.0f}; std::swap(scale.x, scale.y); }
        if (axis == 2) { rot = {90.0f, 0.0f, 0.0f}; std::swap(scale.y, scale.z); }
        c->transform.position = pos;
        c->transform.rotation = rot;
        c->transform.scale = scale;
        c->color = col;
        c->material = mat;
        c->anchored = true;
        c->canCollide = false;
        c->castShadow = false;
        c->internal = true;
        return part->addChild(std::move(c));
    };
    SceneNode* cap;
    if (kind == GoreKind::Oil) {   // a robot: a metal plate, with dark wires in the middle
        cap = disc(kCutEndName, 1.0f, 0.0f, {0.32f, 0.33f, 0.36f}, Material::Metal);
        disc("CutEndWires", 0.35f, thick * 0.6f, {0.08f, 0.08f, 0.09f}, Material::Plastic);
    } else {
        cap = disc(kCutEndName, 1.0f, 0.0f, glm::mix(glm::vec3(0.55f, 0.08f, 0.08f), color, 0.4f), Material::SmoothPlastic);
        disc("CutEndBone", 0.32f, thick * 0.6f, {0.93f, 0.89f, 0.8f}, Material::Plastic);
    }
    scene.markDirty();
    return cap;
}

void removeCutEnds(Scene& scene, SceneNode* root) {
    if (!root) return;
    std::vector<SceneNode*> cut;
    scene.forEach([&](SceneNode* n) {
        if (n->internal && n->parent && n->parent->isPart() && root->isAncestorOf(n) && n->name.rfind(kCutEndName, 0) == 0)
            cut.push_back(n);
    });
    for (SceneNode* n : cut) scene.removeNode(n);
}

glm::vec3 insidePoint(const SceneNode& part, float t0, float t1, float t2) {
    const glm::mat4 M = part.worldMatrix();
    // A mesh (a bundle's body part): between its middle and one of its corners, so the
    // point is in the real shape, not the empty corners of its box.
    if (part.editMesh && !part.editMesh->verts.empty()) {
        const auto& v = part.editMesh->verts;
        glm::vec3 c(0.0f);
        for (const glm::vec3& p : v) c += p;
        c /= (float)v.size();
        const glm::vec3 corner = v[std::min(v.size() - 1, (size_t)(std::clamp(t0, 0.0f, 0.9999f) * (float)v.size()))];
        return glm::vec3(M * glm::vec4(glm::mix(c, corner, 0.15f + 0.7f * std::clamp(t1, 0.0f, 1.0f)), 1.0f));
    }
    return glm::vec3(M * glm::vec4(glm::vec3(t0, t1, t2) * 0.8f - 0.4f, 1.0f));
}

} // namespace Gore
