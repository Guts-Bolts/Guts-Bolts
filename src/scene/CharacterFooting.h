#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// How a character gets going and stops, like a Roblox Humanoid.
//
// Roblox characters are snappy: on the ground they reach WalkSpeed in a blink and
// stop just as fast, and in the air you can still steer almost fully (an obby
// depends on it), and letting go of the keys mid-jump drops you nearly straight
// down. Speed *above* WalkSpeed is different: a fling, a jump pad or jumping off a
// moving train carries on through the air and only fades once you land.
//
// So the velocity is looked at in two parts: up to WalkSpeed is "walking", which
// the keys control; anything over WalkSpeed is "momentum", which the keys can't
// cancel and which fades with friction (quickly on the ground, barely in the air).
//
// Only the horizontal velocity is worked out here; gravity, jumping, water and
// ladders stay in Player::update.
namespace CharacterFooting {

// How hard to push, as a multiple of WalkSpeed, so a faster character gets a
// faster start and a shorter stop instead of feeling sluggish.
constexpr float kGroundAccel = 12.0f;   // 12 x WalkSpeed per second: about 0.08s from standstill
constexpr float kGroundBrake = 16.0f;   // 16 x WalkSpeed per second: about 0.06s to a stop
constexpr float kAirAccel    = 9.0f;    // in the air: about 0.11s, nearly full control like Roblox
constexpr float kAirBrake    = 7.0f;    // letting go in the air stops you in about 0.14s
// How fast extra speed (over WalkSpeed) fades, per second.
constexpr float kGroundFriction = 8.0f; // on the ground: a shove slides you a little way
constexpr float kAirDrag        = 0.25f;// in the air: a fling carries on

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
inline glm::vec2 step(const glm::vec2& v, const glm::vec2& move, float walkSpeed, bool grounded, float dt) {
    const float speed = std::max(0.0f, walkSpeed);
    dt = std::max(0.0f, dt);
    const glm::vec2 want = move * speed;
    const bool pushing = glm::dot(want, want) > 1e-8f;
    // (At least 1, so WalkSpeed = 0 still brings a moving character to a stop.)
    const float scale = std::max(speed, 1.0f);
    const float accel = scale * (grounded ? (pushing ? kGroundAccel : kGroundBrake) : (pushing ? kAirAccel : kAirBrake));

    // Normal walking: steer straight towards what the keys ask for.
    const float have = glm::length(v);
    if (have <= speed + 1e-4f) return approach(v, want, accel, dt);

    // Going faster than WalkSpeed: the extra is momentum. It fades with friction
    // (fast on the ground, slowly in the air, plus a little steady friction so
    // it really does run out) ...
    const glm::vec2 dir = v / have;
    float left = (have - speed) * std::exp(-(grounded ? kGroundFriction : kAirDrag) * dt);
    left -= scale * (grounded ? 1.0f : 0.25f) * dt;
    glm::vec2 out = dir * (speed + left);
    if (left < 0.0f) {   // (run out this step: back to normal walking for the rest)
        out = dir * speed;
        return approach(out, want, accel, dt);
    }
    // ... and the keys can add speed in a new direction (steer) or push against
    // it, but letting go doesn't cancel it.
    if (pushing) {
        const float wishSpeed = glm::length(want);
        const glm::vec2 wishDir = want / wishSpeed;
        const float room = wishSpeed - glm::dot(out, wishDir);
        if (room > 0.0f) out += wishDir * std::min(accel * dt, room);
    }
    return out;
}

// Standing on something the liquid ran over lately (Liquid::isWet): is it slippery
// right here? A water slide's floor pieces are: the stream thins out but the slide
// stays slick all the way down. But "wet" is kept for a whole part, and one splash
// that lands on a big flat floor (a 240-stud baseplate under a fountain) used to make
// the whole map slippery for 20 seconds: you slid about everywhere, even standing
// still. So a big flat floor is only slippery where there's water on it right now.
//   groundSize   the part's size (its box in the world)
//   upright      how level it is (1 = flat; the up axis's y)
//   dropsHere    how many drops of liquid are around your feet (Liquid::sample)
constexpr float kWetWholePart = 24.0f;   // parts up to this many studs across are wet all over
inline bool wetUnderfoot(const glm::vec3& groundSize, float upright, int dropsHere) {
    if (dropsHere > 0) return true;                       // standing in it
    if (upright < 0.9995f) return true;                   // a slope (a slide): wet all the way down
    return std::max(groundSize.x, groundSize.z) <= kWetWholePart;   // a small piece: wet all over
}

// Riding something: where a point that moved along with the part we stand on is now.
// `before` and `now` are the part's world matrix last frame and this frame.
//
// A part that didn't move leaves the point exactly where it was. (Working it out
// anyway, as now * inverse(before), is never quite exact in floats: far from the
// middle of the map, on a big squashed baseplate, it came out a hair off every
// frame, and a player standing still slowly slid across the map, faster the higher
// the frame rate.) A moving part is worked out in doubles, for the same reason.
// `turnDegrees` (if given) gets how far the part turned about the up axis.
inline glm::vec3 carry(const glm::mat4& before, const glm::mat4& now, const glm::vec3& point, float* turnDegrees = nullptr) {
    if (turnDegrees) *turnDegrees = 0.0f;
    if (before == now) return point;
    const glm::dmat4 change = glm::dmat4(now) * glm::inverse(glm::dmat4(before));
    if (turnDegrees) *turnDegrees = (float)glm::degrees(std::atan2(change[2][0], change[2][2]));
    return glm::vec3(change * glm::dvec4(glm::dvec3(point), 1.0));
}

} // namespace CharacterFooting
