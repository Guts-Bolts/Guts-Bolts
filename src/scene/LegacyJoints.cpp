#include "LegacyJoints.h"
#include "Scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace LegacyJoints {

namespace {
glm::mat4 orthonormal(glm::mat4 m) {
    for (int i = 0; i < 3; ++i) {
        glm::vec3 a(m[i]);
        float len = glm::length(a);
        m[i] = glm::vec4(len > 1e-9f ? a / len : glm::vec3(i == 0, i == 1, i == 2), 0.0f);
    }
    m[3].w = 1.0f;
    return m;
}
glm::vec3 eulerOf(const glm::mat4& m) {
    float z, y, x;
    glm::extractEulerAngleZYX(orthonormal(m), z, y, x);
    return glm::degrees(glm::vec3(x, y, z));
}
glm::mat4 localFrame(const glm::vec3& pos, const glm::vec3& rot) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
    m = glm::rotate(m, glm::radians(rot.z), {0, 0, 1});
    m = glm::rotate(m, glm::radians(rot.y), {0, 1, 0});
    m = glm::rotate(m, glm::radians(rot.x), {1, 0, 0});
    return m;
}
bool near(const glm::vec3& a, const glm::vec3& b) { return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(1e-4f))); }
bool same(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::fabs(a[c][r] - b[c][r]) > 1e-5f) return false;
    return true;
}
} // namespace

glm::mat4 frameOf(const SceneNode* n) { return orthonormal(n->worldMatrix()); }

void setFrame(SceneNode* n, const glm::mat4& world) {
    glm::mat4 local = n->parent ? glm::inverse(n->parentFrame()) * world : world;
    n->transform.position = glm::vec3(local[3]);
    n->transform.rotation = eulerOf(local);
}

glm::mat4 offset(const SceneNode* j) {
    glm::mat4 turn(1.0f);
    if (j->legacyJoint >= 2 && j->currentAngle != 0.0f) turn = glm::rotate(glm::mat4(1.0f), j->currentAngle, {0, 0, 1});
    return j->jointC0 * turn * j->jointXform * glm::inverse(j->jointC1);
}

void update(Scene& scene, float dt) {
    std::vector<SceneNode*> joints;
    scene.forEach([&](SceneNode* n) {
        if (n->isConstraint() && n->legacyJoint && n->enabled && n->ref0 && n->ref1) joints.push_back(n);
    });
    if (joints.empty()) return;
    // Motors turn towards where they're told (MaxVelocity is per 1/60 s, like Roblox).
    for (SceneNode* j : joints)
        if (j->legacyJoint >= 2 && j->maxVelocity != 0.0f && j->currentAngle != j->desiredAngle) {
            const float step = std::fabs(j->maxVelocity) * dt * 60.0f;
            const float d = j->desiredAngle - j->currentAngle;
            j->currentAngle = std::fabs(d) <= step ? j->desiredAngle : j->currentAngle + (d > 0 ? step : -step);
        }
    // (A chain of welds settles from its root out: go over them a few times.)
    for (int pass = 0; pass < 3; ++pass) {
        bool moved = false;
        for (SceneNode* j : joints) {
            SceneNode* p0 = scene.findById(j->ref0);
            SceneNode* p1 = scene.findById(j->ref1);
            if (!p0 || !p1 || p0 == p1 || !p0->isPart() || !p1->isPart()) continue;
            if (j->characterJoint) {
                // The pose the animation gave the limb this frame (if it didn't touch it,
                // the one it had before we moved it).
                glm::vec3 animPos = p1->transform.position, animRot = p1->transform.rotation;
                if (j->jointWrote && near(animPos, j->jointWrotePos) && near(animRot, j->jointWroteRot)) {
                    animPos = j->jointAnimPos;
                    animRot = j->jointAnimRot;
                }
                const bool changed = !same(j->jointC0, j->jointC0Rest) || !same(j->jointC1, j->jointC1Rest) ||
                                     !same(j->jointXform, glm::mat4(1.0f)) || j->currentAngle != 0.0f;
                if (!changed) {
                    if (j->jointWrote) { p1->transform.position = animPos; p1->transform.rotation = animRot; moved = true; }
                    j->jointWrote = false;
                    continue;
                }
                if (pass > 0 && j->jointWrote) continue;   // (once a frame)
                // Animated limb: Part0 * C0rest * A * C1rest^-1, so A = C0rest^-1 * Part0^-1 * limb * C1rest.
                const glm::mat4 P0 = frameOf(p0);
                const glm::mat4 parent = p1->parent ? orthonormal(p1->parentFrame()) : glm::mat4(1.0f);
                const glm::mat4 limb = parent * localFrame(animPos, animRot);
                glm::mat4 anim = glm::inverse(j->jointC0Rest) * glm::inverse(P0) * limb * j->jointC1Rest;
                if (!same(j->jointXform, glm::mat4(1.0f))) anim = j->jointXform;   // a script posing it itself
                glm::mat4 turn(1.0f);
                if (j->currentAngle != 0.0f) turn = glm::rotate(glm::mat4(1.0f), j->currentAngle, {0, 0, 1});
                setFrame(p1, P0 * j->jointC0 * turn * anim * glm::inverse(j->jointC1));
                j->jointWrote = true;
                j->jointWrotePos = p1->transform.position; j->jointWroteRot = p1->transform.rotation;
                j->jointAnimPos = animPos; j->jointAnimRot = animRot;
                moved = true;
                continue;
            }
            // Two loose parts: the physics holds them together (it knows about forces).
            const bool c0 = scene.isCharacterPart(p0), c1 = scene.isCharacterPart(p1);
            if (!p0->anchored && !p1->anchored && !c0 && !c1) continue;
            const glm::mat4 rel = offset(j);
            if (c1 && !c0) continue;   // (never drag a character's limb to a loose part)
            if (p1->anchored && !p0->anchored && !c0) {
                setFrame(p0, frameOf(p1) * glm::inverse(rel));   // the anchored end stays put
                p0->velocity = glm::vec3(0.0f);
            } else {
                const glm::mat4 want = frameOf(p0) * rel;
                if (same(want, frameOf(p1))) continue;
                setFrame(p1, want);
                p1->velocity = p0->velocity;
            }
            moved = true;
        }
        if (!moved) break;
    }
    scene.markDirty();
}

void addCharacterJoints(Scene& scene, SceneNode* rig) {
    auto part = [&](const char* name) { return rig->findChild(name); };
    SceneNode *root = part("HumanoidRootPart"), *torso = part("Torso"), *head = part("Head");
    if (!root || !torso) return;
    // Roblox's R6 joints, turned the same way (so C0 * CFrame.Angles(0, 0, a) swings a limb the
    // way Roblox scripts expect), with the joint points where this character's limbs meet.
    const glm::mat4 side = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), {0, 1, 0});    // right limbs
    const glm::mat4 sideL = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), {0, 1, 0});  // left limbs
    const glm::mat4 up = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), {1, 0, 0}) *
                         glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), {0, 1, 0});     // neck / root
    const glm::vec3 t = torso->transform.position;
    const glm::vec3 half = torso->transform.scale * 0.5f;
    auto add = [&](SceneNode* p0, SceneNode* p1, const char* name, const glm::vec3& point, const glm::mat4& turn) {
        if (!p0 || !p1) return;
        auto j = std::make_unique<SceneNode>(name, NodeKind::Constraint);
        j->constraintType = ConstraintType::Weld;
        j->legacyJoint = 2;
        j->characterJoint = true;
        j->visible = false;
        j->ref0 = p0->id;
        j->ref1 = p1->id;
        j->jointC0 = j->jointC0Rest = glm::translate(glm::mat4(1.0f), point - p0->transform.position) * turn;
        j->jointC1 = j->jointC1Rest = glm::translate(glm::mat4(1.0f), point - p1->transform.position) * turn;
        scene.insert(std::move(j), p0);
    };
    add(root, torso, "RootJoint", root->transform.position, up);
    add(torso, head, "Neck", t + glm::vec3(0, half.y, 0), up);
    add(torso, part("Right Arm"), "Right Shoulder", t + glm::vec3(half.x, half.y * 0.5f, 0), side);
    add(torso, part("Left Arm"), "Left Shoulder", t + glm::vec3(-half.x, half.y * 0.5f, 0), sideL);
    add(torso, part("Right Leg"), "Right Hip", t + glm::vec3(half.x, -half.y, 0), side);
    add(torso, part("Left Leg"), "Left Hip", t + glm::vec3(-half.x, -half.y, 0), sideL);
}

} // namespace LegacyJoints
