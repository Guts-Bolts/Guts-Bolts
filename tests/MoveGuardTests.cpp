// Basic anti-cheat (src/net/MoveGuard.h): walking, jumping, falling, seats and launches
// are fine; zooming, teleporting and hanging in the air get pulled back.
#include "net/MoveGuard.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
using V = MoveGuard::Verdict;
constexpr float kWalk = 8.0f, kJump = 12.6f, kGravity = 22.0f;
constexpr double kStep = 0.05;   // 20 reports a second, like the real game

// Move player `who` along `path(t)` for `seconds`; the first verdict that isn't Ok.
template <class F>
V run(MoveGuard& g, uint64_t who, double& t, double seconds, F path, bool free = false) {
    V first = V::Ok;
    for (double end = t + seconds; t < end; t += kStep) {
        V v = g.check(who, path(t), t, kWalk, kJump, kGravity, free);
        if (first == V::Ok) first = v;
    }
    return first;
}
} // namespace

int main() {
    // The world: a floor at y = 0 everywhere, plus a tall ladder at x = 100.
    MoveGuard g;
    g.nearSomething = [](const glm::vec3& p) { return p.y < 9.0f || (p.x > 98.0f && p.x < 102.0f); };
    double t = 0.0;

    check(run(g, 1, t, 3.0, [](double t) { return glm::vec3((float)t * kWalk, 3.0f, 0.0f); }) == V::Ok, "walking at WalkSpeed is fine");
    check(run(g, 1, t, 3.0, [](double t) { return glm::vec3((float)t * kWalk * 1.3f, 3.0f, 0.0f); }) == V::Ok, "a little faster (lag, slopes) is fine");
    check(run(g, 9, t, 0.5, [](double) { return glm::vec3(30, 3, 0); }) == V::Ok, "(player 9 arrives)");
    {   // jumping: up and back down
        const double t0 = t;
        check(run(g, 9, t, 1.2, [t0](double t) {
            const float s = (float)(t - t0);
            return glm::vec3(30.0f, 3.0f + std::max(0.0f, kJump * s - 0.5f * kGravity * s * s), 0.0f);
        }) == V::Ok, "jumping is fine");
    }
    check(run(g, 10, t, 0.5, [](double) { return glm::vec3(30, 3, 0); }) == V::Ok, "(player 10 arrives)");
    check(run(g, 10, t, 1.0, [](double t) { return glm::vec3(30.0f, 3.0f - 20.0f * (float)std::fmod(t, 1.0), 0.0f); }) == V::Ok,
          "falling fast is fine");

    // Zooming at five times the speed.
    t += 1.0;
    {
        const double t0 = t;
        check(run(g, 2, t, 0.1, [](double) { return glm::vec3(0, 3, 0); }) == V::Ok, "(player 2 arrives)");
        check(run(g, 2, t, 3.0, [t0](double t) { return glm::vec3((float)(t - t0) * kWalk * 5.0f, 3.0f, 0.0f); }) == V::TooFast,
              "speed hacking is caught");
        check(g.lastGood(2).x < 10.0f, "and they go back to the last good spot");
    }

    // One big jump across the map.
    check(run(g, 3, t, 0.5, [](double) { return glm::vec3(0, 3, 0); }) == V::Ok, "(player 3 arrives)");
    check(g.check(3, glm::vec3(500.0f, 3.0f, 0.0f), t, kWalk, kJump, kGravity, false) == V::Teleported, "teleporting is caught");
    t += kStep;

    // Hanging high in the air with nothing around.
    check(run(g, 4, t, 0.5, [](double) { return glm::vec3(0, 3, 0); }) == V::Ok, "(player 4 arrives)");
    {
        const double t0 = t;
        check(run(g, 4, t, 6.0, [t0](double t) { return glm::vec3(0.0f, 3.0f + std::min(30.0f, (float)(t - t0) * 10.0f), 0.0f); }) == V::Flying,
              "flying is caught");
    }

    // Climbing the ladder (something right next to them all the way up) is fine.
    check(run(g, 5, t, 0.5, [](double) { return glm::vec3(100, 3, 0); }) == V::Ok, "(player 5 arrives)");
    {
        const double t0 = t;
        check(run(g, 5, t, 8.0, [t0](double t) { return glm::vec3(100.0f, 3.0f + (float)(t - t0) * 4.0f, 0.0f); }) == V::Ok,
              "climbing a ladder is fine");
    }

    // In a fast car: not judged.
    {
        const double t0 = t;
        check(run(g, 6, t, 3.0, [t0](double t) { return glm::vec3((float)(t - t0) * 80.0f, 3.0f, 0.0f); }, true) == V::Ok,
              "riding in a seat is fine");
    }
    // Launched by a jump pad (the host excuses them for a moment).
    check(run(g, 7, t, 0.5, [](double) { return glm::vec3(0, 3, 0); }) == V::Ok, "(player 7 arrives)");
    g.excuse(7, t + 3.0);
    {
        const double t0 = t;
        check(run(g, 7, t, 2.5, [t0](double t) { return glm::vec3((float)(t - t0) * 60.0f, 3.0f, 0.0f); }) == V::Ok,
              "being launched is fine");
    }

    // Games can turn it off.
    MoveGuard::Rules off; off.on = false;
    g.setRules(off);
    check(run(g, 8, t, 0.5, [](double) { return glm::vec3(0, 3, 0); }) == V::Ok &&
          g.check(8, glm::vec3(900.0f, 3.0f, 0.0f), t, kWalk, kJump, kGravity, false) == V::Ok,
          "Workspace AntiCheat = false turns it off");

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
