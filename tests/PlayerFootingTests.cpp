// The character's footing (src/scene/CharacterFooting.h): it speeds up to
// WalkSpeed, brakes quickly on the ground, and keeps its momentum in the air,
// only steering a little, like a Roblox Humanoid.
#include "scene/CharacterFooting.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
constexpr float kWalk = 6.0f;
constexpr float kDt = 1.0f / 60.0f;

// Run `seconds` of one fixed input, from `v`.
glm::vec2 run(glm::vec2 v, float mx, float mz, bool grounded, float seconds) {
    for (float t = 0.0f; t < seconds; t += kDt)
        v = CharacterFooting::step(v, {mx, mz}, kWalk, grounded, kDt);
    return v;
}
float len(const glm::vec2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
} // namespace

int main() {
    // Standing start: it picks up speed over a moment, not all at once ...
    glm::vec2 v = CharacterFooting::step({0.0f, 0.0f}, {1.0f, 0.0f}, kWalk, true, kDt);
    check(v.x > 0.0f && v.x < kWalk, "walking starts below WalkSpeed");
    // ... and reaches WalkSpeed in about a tenth of a second.
    v = run({0.0f, 0.0f}, 1.0f, 0.0f, true, 0.12f);
    check(std::abs(v.x - kWalk) < 0.01f, "reaches WalkSpeed quickly");
    // A faster character still ends at its own WalkSpeed.
    glm::vec2 fast{0.0f, 0.0f};
    for (float t = 0.0f; t < 0.12f; t += kDt) fast = CharacterFooting::step(fast, {1.0f, 0.0f}, 16.0f, true, kDt);
    check(std::abs(fast.x - 16.0f) < 0.01f, "a faster WalkSpeed reaches its own speed");

    // Letting go on the ground stops quickly (a short slide, no skating).
    v = run({kWalk, 0.0f}, 0.0f, 0.0f, true, 0.1f);
    check(len(v) < 0.01f, "stopping on the ground takes a moment, not forever");

    // A half-pushed stick walks at half speed.
    v = run({0.0f, 0.0f}, 0.5f, 0.0f, true, 0.2f);
    check(std::abs(v.x - kWalk * 0.5f) < 0.01f, "a half-pushed stick walks at half WalkSpeed");

    // Jumping while running keeps the whole running speed in the air ...
    v = run({kWalk, 0.0f}, 0.0f, 0.0f, false, 0.5f);
    check(std::abs(v.x - kWalk) < 0.01f, "letting go in the air keeps your momentum");
    // ... and steering in the air takes a little while, it doesn't snap around.
    v = CharacterFooting::step({kWalk, 0.0f}, {0.0f, 1.0f}, kWalk, false, kDt);
    check(v.x > 0.0f && v.y > 0.0f && v.y < kWalk * 0.5f, "air steering turns the jump gradually");
    v = run({kWalk, 0.0f}, 0.0f, 1.0f, false, 0.5f);
    check(v.y > 0.0f && std::abs(len(v) - kWalk) < 0.01f, "air steering keeps the speed you jumped with");

    // Jumping from a standstill and holding forward: the air ramps you up, so a
    // jump can't turn into instant full-speed movement.
    v = CharacterFooting::step({0.0f, 0.0f}, {1.0f, 0.0f}, kWalk, false, kDt);
    check(v.x > 0.0f && v.x < kWalk * 0.2f, "a standstill jump doesn't snap to WalkSpeed");

    // WalkSpeed = 0 (a frozen character) goes nowhere, and stops one that was moving.
    v = CharacterFooting::step({0.0f, 0.0f}, {1.0f, 0.0f}, 0.0f, true, kDt);
    check(len(v) < 1e-6f, "WalkSpeed 0 stays put");
    v = CharacterFooting::step({kWalk, 0.0f}, {1.0f, 0.0f}, 0.0f, true, 0.5f);
    check(len(v) < 1e-6f, "WalkSpeed 0 stops a moving character");

    // A big frame can't overshoot the speed we asked for.
    v = CharacterFooting::step({0.0f, 0.0f}, {1.0f, 0.0f}, kWalk, true, 10.0f);
    check(std::abs(v.x - kWalk) < 0.01f, "a long frame still stops at WalkSpeed");

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
