#pragma once
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

class Scene;
class SceneNode;
struct Humanoid;

// Arterial bleeding ("Open Artery"): a bullet or blade through the neck, the inside of
// the thigh or the inside of the upper arm opens an artery. Blood spurts out of the
// wound with every heartbeat (fast and strong at first, weaker as the blood runs out),
// the character staggers about clutching it, collapses, and bleeds to death.
//
// Who decides what: the host (or a game server, or a solo game). It works out which
// shots hit an artery (Humanoid:TakeDamage with a hit result), makes the spurts (its
// blood is sent to everyone, see ParticleSystem::Role) and drains the health. The wound
// itself rides along with the Humanoid (sent to a joined player's computer like the rest
// of their Humanoid), so whoever moves the body (that player's own computer, or the host
// for NPCs) makes it stumble the same way.
//
// Game makers: workspace.ArterialBleeding (on by default), Humanoid:CutArtery(part
// [, seconds]), Humanoid:StopBleeding() (a tourniquet / medkit), Humanoid.BleedingOut,
// and an "Arteries" attribute on a character's Model (false = this one never gets one).
struct ArteryWound {
    bool        on = false;
    std::string part;                    // the body part it's in (by name: ids differ per computer)
    std::string name;                    // "Neck", "Left Thigh", "Right Arm"...
    glm::vec3   local{0.0f};             // where on the part, in its own -0.5..0.5 space
    glm::vec3   out{0.0f, 0.0f, 1.0f};   // which way it points out of the part (its own space)
    float       time = 0.0f;             // seconds since it was cut
    float       bleedOut = 10.0f;        // seconds from cut to dead (unless it's stopped)
    // (Host only, never sent.)
    float       sinceDeath = -1.0f;      // the heart keeps going a few seconds after death
    float       beat = 0.0f;             // until the next heartbeat
    int         pulse = 0;               // spurts left in this beat
    float       pulseTimer = 0.0f;
    float       drained = 0.0f;          // health taken so far (it can't heal while it's open)
    void clear() { *this = ArteryWound{}; }
};

namespace Artery {

constexpr float kDefaultBleedOut = 10.0f;
// Slower than this (studs / second), a hit is blunt (a bat, a fall): it bruises, it
// doesn't open an artery.
constexpr float kSharpSpeed = 40.0f;

// Which artery a hit at `worldPoint` on `part` opens ("Neck", "Left Thigh"...), or
// nullptr. R6 and R15 body parts.
const char* at(const SceneNode& part, const glm::vec3& worldPoint);

// Open an artery in `part` at `worldPoint` (bleeding to death in `seconds`). The
// wound's name comes from at() when it is one, otherwise the part's name.
void cut(Humanoid& h, const SceneNode& part, const glm::vec3& worldPoint, float seconds = kDefaultBleedOut);

// A hit (Humanoid:TakeDamage with a hit result): if the game has arterial bleeding on,
// this character can get one, it was sharp and it landed on an artery, open it. True
// if it did.
bool onHit(Scene& scene, Humanoid& h, const SceneNode* character, const SceneNode& part,
           const glm::vec3& worldPoint, const glm::vec3& velocity);

// How far gone: 0 just cut, 1 about to die.
float severity(const ArteryWound& w, float health, float maxHealth);
// How fast they can still walk (times WalkSpeed).
inline float speedScale(float severity) { return 0.8f - 0.5f * severity; }
// Past this, the legs go: they drop and die on the floor.
constexpr float kCollapse = 0.72f;

// Where the wound is now (world space) and which way the blood comes out, on the body
// part `part` (the one named in the wound).
glm::vec3 worldPoint(const ArteryWound& w, const SceneNode& part);
glm::vec3 worldDir(const ArteryWound& w, const SceneNode& part);

// Staggering: the body lurches about while the legs try to keep up. `drift` is a push to
// add to where they're trying to walk (xz, up to about 1); `roll` / `pitch` lean the body
// (degrees: sideways the way it's falling, and hunched over the wound).
struct Stagger {
    float t = 0.0f;
    float seed = 0.0f;
    glm::vec3 drift{0.0f};
    float roll = 0.0f, pitch = 0.0f;
    void step(float dt, float severity, const glm::vec3& facing);
    void reset() { t = 0.0f; drift = glm::vec3(0.0f); roll = pitch = 0.0f; }
};

// The host's side, once a frame: heartbeats (spurts), the health draining away, and the
// last few beats after death. Not on a joined player's computer (the host sends it all).
void update(Scene& scene, float dt);

// Tests: one Humanoid's heart (spurts its part, drains it). `alive` false: after death.
void beat(Scene& scene, Humanoid& h, const SceneNode* character, bool alive, float dt);

} // namespace Artery
