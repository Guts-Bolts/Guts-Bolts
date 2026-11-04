// The character's footing (src/scene/CharacterFooting.h): it speeds up to
// WalkSpeed, brakes quickly on the ground, steers almost fully in the air, and
// keeps speed over WalkSpeed (a fling) as momentum, like a Roblox Humanoid.
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

    // In the air you can still steer almost fully, like Roblox (obbies need it) ...
    v = run({kWalk, 0.0f}, 0.0f, 1.0f, false, 0.2f);
    check(std::abs(v.x) < 0.01f && std::abs(v.y - kWalk) < 0.01f, "air steering turns a jump around quickly");
    v = CharacterFooting::step({kWalk, 0.0f}, {0.0f, 1.0f}, kWalk, false, kDt);
    check(v.x > 0.0f && v.y > 0.0f && v.y < kWalk, "air steering still takes a few frames, it doesn't snap");
    // ... and letting go mid-jump drops you nearly straight down.
    v = run({kWalk, 0.0f}, 0.0f, 0.0f, false, 0.2f);
    check(len(v) < 0.01f, "letting go in the air stops you");
    // Holding forward through a jump keeps the whole running speed.
    v = run({kWalk, 0.0f}, 1.0f, 0.0f, false, 0.5f);
    check(std::abs(v.x - kWalk) < 0.01f, "a running jump keeps its speed");

    // Speed over WalkSpeed (a fling, jumping off a train) is momentum: it keeps
    // going through the air even when you let go ...
    v = run({kWalk * 3.0f, 0.0f}, 0.0f, 0.0f, false, 0.5f);
    check(v.x > kWalk * 2.5f, "a fling carries on through the air");
    // ... you can still steer it a little ...
    v = run({kWalk * 3.0f, 0.0f}, 0.0f, 1.0f, false, 0.3f);
    check(v.x > kWalk && v.y > kWalk * 0.5f, "you can steer while flung, but not cancel it");
    // ... and on the ground it fades out in a short slide.
    v = run({kWalk * 3.0f, 0.0f}, 0.0f, 0.0f, true, 0.15f);
    check(len(v) > 0.5f && len(v) < kWalk * 3.0f, "a shove on the ground slides a little way");
    v = run({kWalk * 3.0f, 0.0f}, 0.0f, 0.0f, true, 1.0f);
    check(len(v) < 0.01f, "...and then stops");

    // WalkSpeed = 0 (a frozen character) goes nowhere, and stops one that was moving.
    v = CharacterFooting::step({0.0f, 0.0f}, {1.0f, 0.0f}, 0.0f, true, kDt);
    check(len(v) < 1e-6f, "WalkSpeed 0 stays put");
    {
        glm::vec2 frozen{kWalk, 0.0f};
        for (float t = 0.0f; t < 1.0f; t += kDt) frozen = CharacterFooting::step(frozen, {1.0f, 0.0f}, 0.0f, true, kDt);
        check(len(frozen) < 1e-4f, "WalkSpeed 0 stops a moving character");
    }

    // A big frame can't overshoot the speed we asked for.
    v = CharacterFooting::step({0.0f, 0.0f}, {1.0f, 0.0f}, kWalk, true, 10.0f);
    check(std::abs(v.x - kWalk) < 0.01f, "a long frame still stops at WalkSpeed");

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
