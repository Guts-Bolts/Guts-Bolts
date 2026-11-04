#pragma once

#include <algorithm>
#include <glm/glm.hpp>

// How a character gets going and stops, like a Roblox Humanoid: WalkSpeed is the
// speed it aims for, it reaches it in a fraction of a second on the ground, and in
// the air it only steers a little, so a jump keeps the momentum you jumped with.
//
// Only the horizontal velocity is worked out here; gravity, jumping, water and
// ladders stay in Player::update.
namespace CharacterFooting {

// How hard to push, as a multiple of WalkSpeed, so a faster character gets a
// faster start and a shorter stop instead of feeling sluggish.
constexpr float kGroundAccel = 12.0f;   // 12 x WalkSpeed per second: about 0.08s from standstill
constexpr float kGroundBrake = 16.0f;   // 16 x WalkSpeed per second: about 0.06s to a stop
constexpr float kAirAccel    = 3.5f;    // 3.5 x WalkSpeed per second: about 0.29s from standstill

// Move `v` towards `want` by at most `accel * dt`, so a big dt can't overshoot.
inline glm::vec2 approach(const glm::vec2& v, const glm::vec2& want, float accel, float dt) {
    const glm::vec2 d = want - v;
    const float len = glm::length(d);
    if (len < 1e-6f) return want;
    const float step = std::max(0.0f, accel) * std::max(0.0f, dt);
    return len <= step ? want : v + d * (step / len);
}

// One step of horizontal velocity. `move` is the direction to walk in, its length
// scaled by how far the stick or key is pushed (0 stops, 1 is full speed).
// `grounded` picks the air behaviour: letting go in the air keeps your momentum.
inline glm::vec2 step(const glm::vec2& v, const glm::vec2& move, float walkSpeed, bool grounded, float dt) {
    const float speed = std::max(0.0f, walkSpeed);
    const glm::vec2 want = move * speed;
    const bool pushing = glm::dot(want, want) > 1e-8f;
    if (!grounded && !pushing) return v;   // airborne and letting go: keep flying
    // (At least 1, so WalkSpeed = 0 still brings a moving character to a stop.)
    const float accel = std::max(speed, 1.0f) * (grounded ? (pushing ? kGroundAccel : kGroundBrake) : kAirAccel);
    return approach(v, want, accel, dt);
}

} // namespace CharacterFooting
