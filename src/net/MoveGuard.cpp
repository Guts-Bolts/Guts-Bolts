#include "MoveGuard.h"

#include <algorithm>
#include <cmath>

namespace {
float flat(const glm::vec3& a, const glm::vec3& b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }
} // namespace

const char* MoveGuard::words(Verdict v) {
    switch (v) {
        case Verdict::TooFast:    return "moving too fast";
        case Verdict::Teleported: return "teleporting";
        case Verdict::Flying:     return "flying";
        default:                  return "nothing";
    }
}

glm::vec3 MoveGuard::lastGood(uint64_t who) const {
    auto it = m_people.find(who);
    return it == m_people.end() ? glm::vec3(0.0f) : it->second.good;
}

void MoveGuard::excuse(uint64_t who, double until) {
    Track& k = m_people[who];
    k.excusedUntil = std::max(k.excusedUntil, until);
}

void MoveGuard::reset(uint64_t who) {
    auto it = m_people.find(who);
    if (it != m_people.end()) it->second.started = false;
}

MoveGuard::Verdict MoveGuard::check(uint64_t who, const glm::vec3& pos, double t, float walkSpeed, float jumpSpeed,
                                    float gravity, bool free) {
    Track& k = m_people[who];
    auto restart = [&]() {
        k.started = true;
        k.good = k.last = k.windowFrom = pos;
        k.lastT = k.windowT = t;
        k.airSince = -1.0;
        k.nextAirCheck = t;
    };
    if (!m_rules.on || !k.started || free || t < k.excusedUntil) { restart(); return Verdict::Ok; }
    const double dt = t - k.lastT;
    if (dt <= 0.0) return Verdict::Ok;
    if (dt > 2.0) { restart(); return Verdict::Ok; }   // a long gap (lag, a hitch): nothing to judge

    const float slack = std::max(0.1f, m_rules.slack);
    walkSpeed = std::max(walkSpeed, 0.0f);
    jumpSpeed = std::max(jumpSpeed, 0.0f);
    gravity = std::max(gravity, 1.0f);
    // How fast is fine, sideways: a good bit over WalkSpeed (knockback, slopes, lag).
    const float maxSpeed = (walkSpeed * 1.5f + 6.0f) * slack;

    // One big jump in a single report: teleported.
    const float rise = pos.y - k.last.y;
    if (flat(pos, k.last) > maxSpeed * (float)dt + 25.0f * slack || rise > jumpSpeed * (float)dt + 25.0f * slack) {
        k.last = k.windowFrom = k.good;
        k.lastT = k.windowT = t;
        k.airSince = -1.0;
        return Verdict::Teleported;
    }
    k.last = pos;
    k.lastT = t;

    // Hanging in the air: nothing under or beside them and not falling for longer than a
    // jump takes. (Checked a few times a second; falling or touching anything resets it.)
    if (nearSomething && t >= k.nextAirCheck) {
        k.nextAirCheck = t + 0.25;
        if (nearSomething(pos)) {
            k.airSince = -1.0;
        } else if (k.airSince < 0.0 || pos.y < k.airLowest - 1.0f) {
            k.airSince = t;          // just left the ground, or still falling: fine
            k.airLowest = pos.y;
        } else {
            k.airLowest = std::min(k.airLowest, pos.y);
            const double jumpTime = 2.0 * jumpSpeed / gravity;   // up and back down
            if (t - k.airSince > std::max(3.0, jumpTime + 1.5) * slack) {
                k.last = k.windowFrom = k.good;
                k.lastT = k.windowT = t;
                k.airSince = -1.0;
                return Verdict::Flying;
            }
        }
    }

    // Too fast: how far they went over the last second or so.
    const double windowLen = t - k.windowT;
    if (windowLen >= 1.0) {
        if (flat(pos, k.windowFrom) > maxSpeed * (float)windowLen + 4.0f * slack) {
            k.last = k.windowFrom = k.good;
            k.lastT = k.windowT = t;
            k.airSince = -1.0;
            return Verdict::TooFast;
        }
        k.windowFrom = pos;
        k.windowT = t;
        if (k.airSince < 0.0) k.good = pos;   // (not while up in the air: that's where flyers go)
    }
    return Verdict::Ok;
}
