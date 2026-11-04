#include "IkControls.h"
#include "Scene.h"
#include "SceneNode.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace IkControls {
namespace {

bool g_made = false;
// What each IKControl turned by last frame (for SmoothTime).
std::unordered_map<uint64_t, glm::quat> g_last;

// The part something is in (itself, if it's a part).
SceneNode* partOf(SceneNode* n) {
    while (n && !n->isPart()) n = n->parent;
    return n;
}

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Where a limb turns (in the limb's own unit-cube space): the top of an arm or leg (its
// shoulder / hip), the bottom of the head (its neck). Other parts don't bend.
bool jointOf(const SceneNode* limb, glm::vec3& pivot, glm::vec3& tip) {
    const std::string& n = limb->name;
    if (n == "Left Arm" || n == "Right Arm" || n == "Left Leg" || n == "Right Leg") {
        pivot = {0.0f, 0.5f, 0.0f};
        tip = {0.0f, -0.5f, 0.0f};
        return true;
    }
    if (n == "Head") {
        pivot = {0.0f, -0.5f, 0.0f};
        tip = {0.0f, 0.0f, 0.0f};
        return true;
    }
    return false;
}

glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int i = 0; i < 3; ++i) {
        const float l = glm::length(r[i]);
        if (l > 1e-6f) r[i] /= l;
    }
    return glm::quat_cast(r);
}

// Turn `limb` by q (a world rotation) about the world point `pivot`.
void turn(SceneNode* limb, const glm::quat& q, const glm::vec3& pivot) {
    const glm::mat4 world = limb->worldMatrix();
    const glm::mat4 turned = glm::translate(glm::mat4(1.0f), pivot) * glm::mat4_cast(q) *
                             glm::translate(glm::mat4(1.0f), -pivot) * world;
    const glm::mat4 parent = limb->parent ? limb->parent->worldMatrix() : glm::mat4(1.0f);
    glm::mat4 local = glm::inverse(parent) * turned;
    if (!finite(glm::vec3(local[3]))) return;
    for (int i = 0; i < 3; ++i) {
        const float l = glm::length(glm::vec3(local[i]));
        if (l > 1e-6f) local[i] /= l;
    }
    float z = 0, y = 0, x = 0;
    glm::extractEulerAngleZYX(local, z, y, x);
    const glm::vec3 deg = glm::degrees(glm::vec3(x, y, z));
    if (!finite(deg)) return;
    limb->transform.position = glm::vec3(local[3]);
    limb->transform.rotation = deg;
}

void solve(Scene& scene, SceneNode* control, float dt) {
    const IkProps& k = control->ik;
    SceneNode* effector = k.endEffector ? scene.findById(k.endEffector) : nullptr;
    SceneNode* target = k.target ? scene.findById(k.target) : nullptr;
    SceneNode* limb = partOf(effector);
    if (!effector || !target || !limb || target == effector) return;
    glm::vec3 pivotLocal, tipLocal;
    if (!jointOf(limb, pivotLocal, tipLocal)) return;

    const glm::mat4 lw = limb->worldMatrix();
    const glm::vec3 pivot(lw * glm::vec4(pivotLocal, 1.0f));
    const glm::mat4 ew = effector->worldMatrix();
    // The end that reaches: an attachment / bone's own spot, or the limb's far end (hand, foot).
    const glm::vec3 end = effector == limb ? glm::vec3(lw * glm::vec4(tipLocal, 1.0f)) : glm::vec3(ew[3]);
    const glm::vec3 aim(target->worldMatrix()[3]);
    if (!finite(pivot) || !finite(end) || !finite(aim)) return;

    glm::quat q(1.0f, 0.0f, 0.0f, 0.0f);
    switch (k.type) {
        case IkType::Position:
        case IkType::Transform: {
            const glm::vec3 from = end - pivot, to = aim - pivot;
            if (glm::length(from) < 1e-4f || glm::length(to) < 1e-4f) return;
            q = glm::rotation(glm::normalize(from), glm::normalize(to));
            break;
        }
        case IkType::LookAt: {
            const glm::vec3 front = -glm::vec3(ew[2]), to = aim - glm::vec3(ew[3]);
            if (glm::length(front) < 1e-6f || glm::length(to) < 1e-4f) return;
            q = glm::rotation(glm::normalize(front), glm::normalize(to));
            break;
        }
        case IkType::Rotation:
            q = rotationOf(target->worldMatrix()) * glm::inverse(rotationOf(ew));
            break;
    }
    // Catch up with a moving target over SmoothTime, then blend by Weight.
    auto [it, fresh] = g_last.try_emplace(control->id, q);
    if (!fresh && k.smoothTime > 0.0f && dt > 0.0f) {
        const float f = 1.0f - std::exp(-dt / std::max(k.smoothTime, 1e-3f) * 3.0f);
        q = glm::slerp(it->second, q, f);
    }
    it->second = q;
    q = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), q, std::clamp(k.weight, 0.0f, 1.0f));
    if (!std::isfinite(q.w)) return;
    turn(limb, q, pivot);
}

} // namespace

void noteMade() { g_made = true; }
bool anyMade() { return g_made; }

void apply(Scene& scene, float dt) {
    if (!g_made) return;
    std::vector<SceneNode*> controls;
    scene.forEach([&](SceneNode* n) {
        if (n->isIkControl() && n->enabled && n->ik.weight > 0.0f) controls.push_back(n);
    });
    if (controls.empty()) { g_last.clear(); return; }
    std::stable_sort(controls.begin(), controls.end(), [](const SceneNode* a, const SceneNode* b) { return a->ik.priority < b->ik.priority; });
    for (SceneNode* c : controls) solve(scene, c, dt);
    if (g_last.size() > controls.size() * 2 + 64) g_last.clear();   // (forget removed ones now and then)
}

} // namespace IkControls
