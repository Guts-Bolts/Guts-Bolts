#include "Player.h"
#include "Animation.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>

// VR body tracking: after the walk cycle (and any animation) has posed the
// character, the head turns the way the player's real head faces and each arm
// swings round its shoulder to point at the controller in that hand. The legs
// and torso keep doing what they were doing (walking, jumping, sitting), and
// the Player app turns the whole body to follow the head.
//
// The arms are single blocks (R6), so "reaching" is aiming the arm from the
// shoulder at the hand. Every part's pose is sent over the network as usual,
// so other players see your head and arms move.

float Player::eyeHeight() const {
    SceneNode* r = root();
    SceneNode* h = part("Head");
    if (!r || !h) return 2.3f;
    auto it = m_rest.find(h->id);
    const float y = it != m_rest.end() ? it->second.position.y : h->transform.position.y;
    return std::max(0.5f, y * r->transform.scale.y);
}

void Player::applyVrBody() {
    if (!m_vr.on || m_dead) return;
    SceneNode* r = root();
    if (!r) return;
    const glm::mat4 toLocal = glm::inverse(r->worldMatrix());
    const glm::quat rootRot = Anim::eulerToQuat(r->transform.rotation);
    // The headset looks along -Z, the character faces +Z: turn one into the other.
    const glm::quat flip = glm::angleAxis(glm::pi<float>(), glm::vec3(0, 1, 0));

    if (SceneNode* head = part("Head")) {
        auto it = m_rest.find(head->id);
        if (it != m_rest.end()) {
            // How the head turns on the neck, kept to what a neck can do.
            glm::vec3 turn = Anim::quatToEuler(glm::inverse(rootRot) * m_vr.head * flip);
            turn.x = std::clamp(turn.x, -70.0f, 70.0f);   // nod
            turn.y = std::clamp(turn.y, -80.0f, 80.0f);   // shake
            turn.z = std::clamp(turn.z, -35.0f, 35.0f);   // tilt
            head->transform.rotation = Anim::quatToEuler(Anim::eulerToQuat(turn) * Anim::eulerToQuat(it->second.rotation));
        }
    }

    const glm::vec3 headAt = focusPoint();
    const char* arms[2] = {"Left Arm", "Right Arm"};
    for (int i = 0; i < 2; ++i) {
        if (!m_vr.hand[i]) continue;   // not tracked: the arm keeps its walk / idle pose
        SceneNode* arm = part(arms[i]);
        if (!arm) continue;
        auto it = m_rest.find(arm->id);
        if (it == m_rest.end()) continue;
        const Transform& rest = it->second;
        const float h = rest.scale.y * 0.5f;
        const glm::vec3 shoulder = rest.position + glm::vec3(0.0f, h, 0.0f);
        const glm::vec3 target = glm::vec3(toLocal * glm::vec4(headAt + m_vr.handAt[i], 1.0f));
        const glm::vec3 to = target - shoulder;
        if (glm::length(to) < 1e-3f) continue;
        // Swing the arm (which hangs straight down at rest) to point at the hand.
        const glm::quat swing = glm::rotation(glm::vec3(0.0f, -1.0f, 0.0f), glm::normalize(to));
        arm->transform.position = shoulder + swing * glm::vec3(0.0f, -h, 0.0f);
        arm->transform.rotation = Anim::quatToEuler(swing * Anim::eulerToQuat(rest.rotation));
    }
}
