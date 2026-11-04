#include "DragDetectors.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace DragDetectors {
namespace {

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// The middle of what moves: a part's own middle, or the middle of a Model's parts.
glm::vec3 middleOf(const SceneNode* n) {
    if (n->isPart()) return glm::vec3(n->worldMatrix()[3]);
    glm::vec3 lo(1e30f), hi(-1e30f);
    bool any = false;
    std::vector<const SceneNode*> stack{n};
    while (!stack.empty()) {
        const SceneNode* s = stack.back();
        stack.pop_back();
        if (s->isPart()) {
            const glm::vec3 c(s->worldMatrix()[3]);
            lo = glm::min(lo, c);
            hi = glm::max(hi, c);
            any = true;
        }
        for (const auto& k : s->children) stack.push_back(k.get());
    }
    return any ? 0.5f * (lo + hi) : glm::vec3(n->worldMatrix()[3]);
}

// Where a ray meets a flat surface (false if it runs along it or points away).
bool hitPlane(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& origin, const glm::vec3& normal, glm::vec3& out) {
    const float d = glm::dot(rd, normal);
    if (std::fabs(d) < 1e-4f) return false;
    const float t = glm::dot(origin - ro, normal) / d;
    if (t < 0.0f || t > 5000.0f) return false;
    out = ro + rd * t;
    return true;
}

// Put a node at a world matrix (keeping its size).
void placeAt(SceneNode* n, const glm::mat4& world) {
    const glm::mat4 parent = n->parent ? n->parent->worldMatrix() : glm::mat4(1.0f);
    glm::mat4 local = glm::inverse(parent) * world;
    if (!finite(glm::vec3(local[3]))) return;
    for (int i = 0; i < 3; ++i) {
        const float l = glm::length(glm::vec3(local[i]));
        if (l > 1e-6f) local[i] /= l;
    }
    float z = 0, y = 0, x = 0;
    glm::extractEulerAngleZYX(local, z, y, x);
    const glm::vec3 deg = glm::degrees(glm::vec3(x, y, z));
    if (!finite(deg)) return;
    n->transform.position = glm::vec3(local[3]);
    n->transform.rotation = deg;
}

// Angle of p round the axis through `pivot` (radians), measured from a fixed side.
float angleOf(const glm::vec3& p, const glm::vec3& pivot, const glm::vec3& axis) {
    glm::vec3 side = std::fabs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 u = glm::normalize(glm::cross(axis, side)), v = glm::cross(axis, u);
    const glm::vec3 r = p - pivot;
    return std::atan2(glm::dot(r, v), glm::dot(r, u));
}

// Loose parts in what moves (they're carried, then keep their speed).
void eachLoosePart(SceneNode* n, const std::function<void(SceneNode*)>& f) {
    std::vector<SceneNode*> stack{n};
    while (!stack.empty()) {
        SceneNode* s = stack.back();
        stack.pop_back();
        if (s->isPart() && !s->anchored) f(s);
        for (auto& k : s->children) stack.push_back(k.get());
    }
}

} // namespace

bool isDrag(const SceneNode* n) {
    return n && n->isClickDetector() && n->click.drag && n->enabled && n->click.range > 0.0f && n->parent &&
           (n->parent->isPart() || n->parent->kind == NodeKind::Model);
}

bool begin(Scene& scene, SceneNode* detector, const glm::vec3& hit, const glm::vec3& camForward, Drag& out) {
    if (!isDrag(detector) || !finite(hit)) return false;
    SceneNode* thing = detector->parent;
    if (thing == scene.root() || scene.isCharacterPart(thing)) return false;
    Drag d;
    d.detector = detector->id;
    d.thing = thing->id;
    d.style = std::clamp(detector->click.dragStyle, 0, 3);
    d.grab = hit;
    d.pivot = middleOf(thing);
    d.startWorld = thing->worldMatrix();
    d.lastPos = d.pivot;
    // The Axis turns with the thing (a door's hinge stays its hinge however it's placed).
    glm::mat3 r(d.startWorld);
    for (int i = 0; i < 3; ++i) { const float l = glm::length(r[i]); if (l > 1e-6f) r[i] /= l; }
    glm::vec3 axis = r * detector->click.axis;
    if (!(glm::length(axis) > 1e-4f) || !finite(axis)) axis = {0, 1, 0};
    d.axis = glm::normalize(axis);
    glm::vec3 look = glm::length(camForward) > 1e-4f ? glm::normalize(camForward) : glm::vec3(0, 0, -1);
    switch (d.style) {
        case TranslatePlane: d.normal = d.axis; break;
        case TranslateViewPlane: d.normal = -look; break;
        case TranslateLine: {
            // The pointer moves on the surface along the line that faces the camera best.
            glm::vec3 n = glm::cross(d.axis, glm::cross(look, d.axis));
            d.normal = glm::length(n) > 1e-4f ? glm::normalize(n) : -look;
            break;
        }
        case RotateAxis:
            d.normal = d.axis;
            d.grab = hit - d.axis * glm::dot(hit - d.pivot, d.axis);   // (on the spin's flat surface through the middle)
            d.startAngle = angleOf(d.grab, d.pivot, d.axis);
            break;
    }
    out = d;
    return true;
}

bool move(Scene& scene, Drag& d, const glm::vec3& ro, const glm::vec3& rd, float dt) {
    SceneNode* detector = scene.findById(d.detector);
    SceneNode* thing = scene.findById(d.thing);
    if (!isDrag(detector) || !thing || detector->parent != thing || !finite(ro) || !finite(rd)) return false;
    const ClickProps& c = detector->click;
    glm::vec3 p;
    if (!hitPlane(ro, glm::normalize(rd), d.style == RotateAxis ? d.pivot : d.grab, d.normal, p)) return true;   // (pointing off: stay put)

    glm::mat4 world = d.startWorld;
    if (d.style == RotateAxis) {
        float turn = angleOf(p, d.pivot, d.axis) - d.startAngle;
        while (turn > glm::pi<float>()) turn -= glm::two_pi<float>();
        while (turn < -glm::pi<float>()) turn += glm::two_pi<float>();
        turn = glm::radians(std::clamp(glm::degrees(turn), c.minAngle, c.maxAngle));
        world = glm::translate(glm::mat4(1.0f), d.pivot) * glm::rotate(glm::mat4(1.0f), turn, d.axis) *
                glm::translate(glm::mat4(1.0f), -d.pivot) * d.startWorld;
    } else {
        glm::vec3 shift = p - d.grab;
        if (d.style == TranslateLine) shift = d.axis * std::clamp(glm::dot(shift, d.axis), c.minMove, c.maxMove);
        if (glm::length(shift) > 2000.0f) return true;
        world = glm::translate(glm::mat4(1.0f), shift) * d.startWorld;
    }
    placeAt(thing, world);
    // Loose parts are carried: no falling or drifting while held.
    const glm::vec3 now = middleOf(thing);
    const glm::vec3 v = dt > 0.0f ? (now - d.lastPos) / dt : glm::vec3(0.0f);
    d.lastPos = now;
    eachLoosePart(thing, [&](SceneNode* s) {
        s->velocity = finite(v) && glm::length(v) < 200.0f ? v : glm::vec3(0.0f);
        s->angularVelocity = glm::vec3(0.0f);
        s->sleepTime = 0.0f;
    });
    return true;
}

void end(Scene& scene, Drag& d, float) {
    if (SceneNode* thing = scene.findById(d.thing))
        eachLoosePart(thing, [](SceneNode* s) { s->sleepTime = 0.0f; });   // (and off it goes, with its speed)
    d = Drag{};
}

} // namespace DragDetectors
