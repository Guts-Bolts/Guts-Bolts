#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <glm/glm.hpp>

// Basic anti-cheat. In a multiplayer game each player moves their own character and
// tells the host where it is, so a hacked Player could say "I'm over there now" and
// zoom, fly or teleport around. The host checks every player's moves with this:
//
//   too fast     they went much further than their WalkSpeed allows (over about a second)
//   teleported   one big jump in one go
//   flying       they stayed up in the air for a while with nothing under or next to them
//
// and pulls them back to the last spot that looked fine (NetGame.cpp sends "moveback").
// It's careful not to catch normal play: seats, being launched by a jump pad or an
// explosion, respawning, ladders and lag are all left alone.
//
// A game can change it with Workspace attributes (scripts or Studio's Properties):
//   AntiCheat       false turns it off (on by default)
//   AntiCheatSpeed  how much faster than WalkSpeed is still fine (default 1; 2 = twice as lenient)
class MoveGuard {
public:
    enum class Verdict { Ok, TooFast, Teleported, Flying };
    static const char* words(Verdict v);   // "moving too fast" ...

    struct Rules {
        bool  on = true;
        float slack = 1.0f;   // AntiCheatSpeed
    };
    void setRules(const Rules& r) { m_rules = r; }
    const Rules& rules() const { return m_rules; }

    // Is there any part close to this spot (a floor under it, a wall or ladder beside it)?
    // Only asked when someone seems to hang in the air. Null = never flying.
    std::function<bool(const glm::vec3& at)> nearSomething;

    // One report of where a character is. `free` = don't judge this one (in a seat,
    // dead, ragdolled): just remember it. gravity in studs/s^2, jumpSpeed = how fast a
    // jump launches them (Humanoid::launchSpeed).
    Verdict check(uint64_t who, const glm::vec3& pos, double t, float walkSpeed, float jumpSpeed, float gravity, bool free);

    // Where to put them back (the last spot that looked fine).
    glm::vec3 lastGood(uint64_t who) const;
    // Something real moved them fast (a jump pad, an explosion, a script): don't judge for a bit.
    void excuse(uint64_t who, double until);
    // They respawned or were moved on purpose: start fresh from wherever they are next.
    void reset(uint64_t who);
    void forget(uint64_t who) { m_people.erase(who); }

private:
    struct Track {
        bool      started = false;
        glm::vec3 good{0.0f};        // the last spot that looked fine
        glm::vec3 last{0.0f};
        double    lastT = 0.0;
        glm::vec3 windowFrom{0.0f};  // where the speed window started
        double    windowT = 0.0;
        double    airSince = -1.0;   // when they started hanging in the air (-1 = not)
        float     airLowest = 0.0f;  // lowest height while hanging there
        double    excusedUntil = 0.0;
        double    nextAirCheck = 0.0;
    };
    Rules m_rules;
    std::unordered_map<uint64_t, Track> m_people;
};
