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
class Ragdoll {
public:
    // `force` 0..1 = how violent the death was (decides dismemberment).
    void start(Scene& scene, SceneNode* root, const glm::vec3& velocity,
               const glm::vec3& impulse, float force);
    void update(float dt, Scene& scene, const Physics& physics);
    void stop() { m_active = false; m_points.clear(); m_sticks.clear(); m_bones.clear(); m_stumps.clear(); }
    bool active() const { return m_active; }
    glm::vec3 center() const;

    // Cut a limb loose: 0 L arm, 1 R arm, 2 L leg, 3 R leg, 4 head.
    void dismember(Scene& scene, int limb);
    // Blown to pieces: every body part bursts into gibs and is hidden.
    void explode(Scene& scene, const glm::vec3& velocity, float force);
    // Show the parts explode() hid again (respawning).
    void unhide(Scene& scene);

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
        int       kind;            // 0 torso, 1 head, 2 limb, 3 attached-to-head
        int       a, b;            // points (limb: top, bottom)
        glm::vec3 right;           // remembered sideways axis (for detached parts)
        glm::mat4 offset{1.0f};    // attached-to-head: transform relative to head
    };
    // A bleeding wound: blood comes out at the middle of a..a2, pointing away from the middle
    // of f..f2 (f -1 = the torso's centre). `pumped` ones are still joined to the heart:
    // they spurt with every beat. The others just drain.
    struct Stump { int a, a2, f, f2; float bleed; bool pumped; };

    void solve(const Physics& physics);
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
    GoreKind  m_goreKind = GoreKind::Blood;
    float     m_gravity = 22.0f;
};
