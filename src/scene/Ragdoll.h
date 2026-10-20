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
    void stop() { m_active = false; m_points.clear(); m_sticks.clear(); m_bones.clear(); m_stumps.clear(); }
    bool active() const { return m_active; }
    glm::vec3 center() const;
    glm::mat3 bodyFrame() const { return torsoFrame(); }   // right, up (shoulders), front
    // Still enough to stand back up from: lying down, hardly moving.
    bool      settled() const;
    bool      lying() const { return m_onGround >= 3; }        // down on the floor (maybe still sliding)
    bool      alive() const { return m_alive; }
    glm::vec3 velocity() const;                             // of the body as a whole (studs / second)

    // Cut a limb loose: 0 L arm, 1 R arm, 2 L leg, 3 R leg, 4 head.
    void dismember(Scene& scene, int limb);
    static constexpr float kStep = 1.0f / 120.0f;   // physics step

private:
    struct Point { glm::vec3 pos, prev; float radius; };
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
    struct Stump { int point; float time; glm::vec3 dir; };

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
    bool      m_cut[5] = {};
    float     m_stumpTimer = 0.0f;
    bool      m_gore = false;
    // Muscles
    bool      m_alive = false;
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
