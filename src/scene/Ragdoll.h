#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "Particles.h"

class Scene;
class SceneNode;
class Physics;

// A floppy R6 ragdoll. The body is a skeleton of points joined by fixed-length
// sticks ("Verlet" physics): gravity pulls the points, the sticks keep the body
// together, and the points bounce off parts. Each body part is then placed
// between its points. Sticks can be cut to knock limbs off (dismemberment).
//
// It's an *active* ragdoll: the body has muscles (bottom of Ragdoll.cpp). They pull the
// limbs towards a pose picked from what's happening (bracing for a fall with the
// arms, covering the head from a hit, clutching a wound, writhing, pushing up off
// the floor), and joint limits stop limbs bending the wrong way or through the body.
// A living character knocked over keeps full strength and gets back up afterwards;
// a dying one fights for a moment, then goes limp.
class Ragdoll {
public:
    // `force` 0..1 = how violent the death was (decides dismemberment).
    // `alive`: knocked over, not killed (no gore, muscles stay strong).
    void start(Scene& scene, SceneNode* root, const glm::vec3& velocity,
               const glm::vec3& impulse, float force, bool alive = false);
    void update(float dt, Scene& scene, const Physics& physics);
    // Put parts inside accessory models back where they were in them (call before the
    // character is put back together: respawning, getting up). Then stop().
    void release(Scene& scene);
    void stop() { m_active = false; m_held = false; m_points.clear(); m_sticks.clear(); m_bones.clear(); m_stumps.clear(); }
    bool active() const { return m_active; }
    glm::vec3 center() const;
    glm::mat3 bodyFrame() const { return torsoFrame(); }   // right, up (shoulders), front
    // Still enough to stand back up from: lying down, hardly moving.
    bool      settled() const;
    bool      lying() const { return m_onGround >= 3; }        // down on the floor (maybe still sliding)
    bool      alive() const { return m_alive; }
    // Held down by a script (PlatformStand, the Ragdoll state): the body goes floppy and
    // doesn't try to get up; it stays down until the script lets go.
    void      setHeld(bool on) { m_held = on; }
    bool      held() const { return m_held; }
    glm::vec3 velocity() const;                             // of the body as a whole (studs / second)

    // Cut a limb loose: 0 L arm, 1 R arm, 2 L leg, 3 R leg, 4 head.
    void dismember(Scene& scene, int limb);
    // Blown to pieces: every body part bursts into gibs and is hidden.
    void explode(Scene& scene, const glm::vec3& velocity, float force);
    // Show the parts explode() hid again (respawning).
    void unhide(Scene& scene);
    static constexpr float kStep = 1.0f / 120.0f;   // physics step

private:
    struct Point {
        glm::vec3 pos, prev; float radius;
        bool      touching = false;     // resting on / sliding along something this frame
        glm::vec3 contactN{0.0f, 1.0f, 0.0f};
        float     blood = 0.0f;         // how bloody this bit of the body is (it smears what it slides on)
        float     slid = 0.0f;          // distance slid since its last smear
    };
    struct Stick { int a, b; float length; int limb; bool alive; };   // limb -1 = body
    struct Bone  {
        uint64_t  id;
        int       kind;            // 0 torso, 1 head, 2 limb, 3 attached to the head, 4 attached to the torso
        int       a, b;            // points (limb: top, bottom)
        glm::vec3 right;           // remembered sideways axis (for detached parts)
        glm::mat4 offset{1.0f};    // attached: transform relative to the head / torso
        // A part inside an accessory model (not straight in the character): where it
        // was in that model, to put it back (release).
        bool      nested = false;
        glm::vec3 restPos{0.0f}, restRot{0.0f};
    };
    // A bleeding wound: blood comes out at the middle of a..a2, pointing away from the middle
    // of f..f2 (f -1 = the torso's centre). `pumped` ones are still joined to the heart:
    // they spurt with every beat. The others just drain.
    struct Stump { int a, a2, f, f2; float bleed; bool pumped; };

    void solve(const Physics& physics);
    void step(float h, const Physics& physics);
    void muscles(float dt, const Physics& physics);   // (bottom of Ragdoll.cpp)
    void jointLimits();                     // (inside solve)
    glm::mat3 torsoFrame() const;
    void place(Scene& scene);

    bool m_active = false;
    std::vector<Point> m_points;
    std::vector<Stick> m_sticks;
    std::vector<Bone>  m_bones;
    std::vector<Stump> m_stumps;
    std::vector<uint64_t> m_hidden;     // parts blown to pieces (shown again by unhide)
    bool      m_cut[5] = {};
    float     m_stumpTimer = 0.0f;
    float     m_heartTime = 0.0f;       // since death: the heart slows and weakens, then stops
    float     m_beat = 0.0f;            // until the next heartbeat
    void      bleed(Scene& scene, float dt);
    void      smearAll(Scene& scene, float dt);
    glm::vec3 stumpPos(const Stump& s) const;
    glm::vec3 stumpDir(const Stump& s) const;
    bool      m_gore = false;
    // Muscles
    bool      m_alive = false;
    bool      m_held = false;
    float     m_time = 0.0f;                // since it went down
    float     m_throes = 3.0f;              // dying: how long it fights before going limp
    float     m_protect = 0.0f;             // covering the head from the hit (seconds left)
    glm::vec3 m_wound{0.0f};                // where it was hit, in the torso's own frame (to clutch it)
    bool      m_hasWound = false;
    float     m_phase = 0.0f;               // (so writhing bodies don't all move in step)
    float     m_accum = 0.0f;               // time not yet stepped
    int       m_onGround = 0;               // how many points are resting on something
    GoreKind  m_goreKind = GoreKind::Blood;
    float     m_gravity = 22.0f;
};
