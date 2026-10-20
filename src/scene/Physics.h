#pragma once
#include <functional>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include "NavMesh.h"
#include "CollisionGroups.h"

class Scene;
class SceneNode;
class Terrain;
enum class Material;   // (SceneNode.h)

struct AABB {
    glm::vec3 min{0.0f}, max{0.0f};
    bool overlaps(const AABB& o, float eps = 1e-4f) const {
        return min.x < o.max.x - eps && max.x > o.min.x + eps &&
               min.y < o.max.y - eps && max.y > o.min.y + eps &&
               min.z < o.max.z - eps && max.z > o.min.z + eps;
    }
    AABB inflated(float d) const { return {min - glm::vec3(d), max + glm::vec3(d)}; }
};

// A box that can be rotated any way (used for tilted parts, ramps, etc.).
struct OBB {
    glm::vec3 center{0.0f};
    glm::vec3 axis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    glm::vec3 half{0.5f};
    static OBB fromAABB(const AABB& b) {
        OBB o;
        o.center = (b.min + b.max) * 0.5f;
        o.half = (b.max - b.min) * 0.5f;
        return o;
    }
};

// "part was touched by other" — fed to the Touched event in scripts.
struct TouchEvent {
    uint64_t partId;
    uint64_t otherId;
};


// Very small physics world used in Play mode:
//  * the character is a box that walks, climbs small steps and lands on parts
//  * unanchored parts fall with gravity and rest on whatever is below them
//  * overlaps are reported as Touched events
// Parts are treated as axis-aligned boxes (rotated parts use their bounding box).
class Physics {
public:
    struct MoveResult {
        glm::vec3 position;
        bool      grounded   = false;
        bool      hitCeiling = false;
        uint64_t  groundId   = 0;    // the part we are standing on
        float     stepped    = 0.0f; // how far we were lifted onto a step this move (to smooth it out on screen)
        float     dropped    = 0.0f; // ...and how far we were pulled down onto a lower step
        std::vector<std::pair<SceneNode*, glm::vec3>> pushed;   // loose parts we walked into
    };

    // The character's body, like Roblox's R6: 2 studs wide and 1 deep (the torso;
    // arms and legs don't collide), from the feet to the top of the head.
    static constexpr float kCharHalfWidth = 0.5f;
    static constexpr float kCharHalfDepth = 0.25f;
    static constexpr float kCharHeight    = 2.65f;
    static constexpr float kStepHeight    = 0.55f;

    static AABB worldBounds(const SceneNode* node);
    static OBB  worldOBB(const SceneNode* node);
    // Separating-axis test. On overlap, `normal` is the direction to push `a`
    // out of `b` and `depth` how far.
    static bool obbOverlap(const OBB& a, const OBB& b, glm::vec3& normal, float& depth);
    // First visible part hit by a ray (skipping `ignore` and everything inside it).
    // Hitting the terrain returns scene.terrainNode().
    static SceneNode* raycast(Scene& scene, const glm::vec3& origin, const glm::vec3& dir,
                              float* distance = nullptr, const SceneNode* ignore = nullptr);
    // The same, but only parts `counts` says yes to (and not the ones inside a skipped model).
    static SceneNode* raycastIf(Scene& scene, const glm::vec3& origin, const glm::vec3& dir, float* distance,
                                const std::function<bool(const SceneNode*)>& counts);
    static AABB characterBox(const glm::vec3& feet);

    void reset();                       // forget touch state (on Play / Stop)
    void gather(Scene& scene);          // collect this frame's parts

    // `yaw` (degrees) = which way the character faces: its box turns with it.
    // `self`: the moving character's root id, so it doesn't bump into itself
    // (other characters are solid when the game has Player Collisions on).
    // `group`: the character's collision group slot (groupOf): it walks through parts,
    // characters and the terrain its group doesn't collide with.
    MoveResult moveCharacter(const glm::vec3& feet, const glm::vec3& delta, bool wasGrounded, float yaw = 0.0f,
                             uint64_t self = 0, int group = 0) const;
    // Collision groups (PhysicsService). A character is in its HumanoidRootPart's group
    // (or its Torso's); this frame's table is a copy of the Workspace's.
    static const std::string& characterGroup(const SceneNode* root);
    int  groupOf(const SceneNode* characterRoot) const { return m_groups.slotOf(characterGroup(characterRoot)); }
    bool groupsCollide(int a, int b) const { return m_groups.collides(a, b); }
    const CollisionGroups& groups() const { return m_groups; }
    static OBB  charOBB(const glm::vec3& feet, float yaw);   // the character's body box
    // Loose parts moving into the character: which way each one shoves it and how
    // hard (the speed it gives), heavier and faster parts shoving harder.
    struct Shove { glm::vec3 dir; float speed; };
    std::vector<Shove> shoves(const glm::vec3& feet, float yaw, float characterMass, int group = 0) const;
    static AABB bounds(const OBB& o);                       // a box around a turned box
    // Unanchored parts: rigid-body physics (tumbling, stacking, bouncing) and
    // constraints (ropes, rods, springs, welds, hinges). Ids of parts that fell
    // out of the world are appended to `fallen`. See RigidBodies.cpp.
    void stepParts(Scene& scene, float dt, std::vector<uint64_t>& fallen);
    static void wake(SceneNode* n);   // make a sleeping part move again
    // How heavy a part is for its size: its Density, or its material's. Water is
    // kWaterDensity: anything lighter floats, anything heavier sinks.
    static float densityOf(const SceneNode* n);
    // A material's own density, friction and bounciness (what a part without
    // CustomPhysicalProperties uses). Friction and bounciness of a part: its own, else its material's.
    // (The material's own values: materialPhysics in SceneNode.h.)
    static float frictionOf(const SceneNode* n);
    static float elasticityOf(const SceneNode* n);
    static constexpr float kWaterDensity = 1.3f;
    // How far `box` must move up to rest on top of a solid part (0 = free).
    float pushUp(const AABB& box) const;
    // Is this point inside any solid part?
    bool  solidAt(const glm::vec3& p) const;
    // Push a sphere out of solid parts. Returns true (and the push normal) on contact.
    bool  resolveSphere(glm::vec3& center, float radius, glm::vec3* normal = nullptr) const;
    // Everything the physics world knows about (for explosions etc.).
    template <typename F> void forEachCollider(F&& f) const {
        for (const auto& c : m_colliders) f(c.node, c.box, c.dynamic);
    }
    // The navigation mesh (PathfindingService). Baked the first time it's asked for,
    // and again (at most about once a second) after anchored parts move or change.
    const NavMesh& navMesh() const;
    void setNavSettings(const NavMesh::Settings& s) const { m_navSettings = s; m_navHash = ~0ull; }
    void rebakeNavMesh() const;   // right now (Studio's Bake button, scripts)
    // New touches since the last call (parts vs character, unanchored vs others).
    void collectTouches(Scene& scene, std::vector<TouchEvent>& out);

private:
    friend class NavMesh;
    struct Collider {
        SceneNode* node;
        AABB       box;
        bool       solid;     // CanCollide
        bool       dynamic;   // unanchored
        bool       rotated;   // not lined up with the world axes: use `obb`
        OBB        obb;
        int        group = 0;  // collision group slot
    };
    bool blocked(const AABB& box) const;

    // The ground under a character standing at `feet` (false = no terrain there).
    bool terrainGround(const glm::vec3& feet, float yaw, float& y) const;

    std::vector<Collider>                  m_colliders;
    const Terrain*                         m_terrain = nullptr;   // this frame's terrain (null = none)
    CollisionGroups                        m_groups;              // this frame's collision groups
    std::vector<Collider>                  m_bodies;   // characters' body boxes (players bumping into players)
    std::set<std::pair<uint64_t, uint64_t>> m_touching;
    uint64_t                               m_staticHash = 0;     // the anchored solid parts, as a number
    mutable NavMesh::Settings              m_navSettings;
    mutable NavMesh                        m_nav;
    mutable uint64_t                       m_navHash = ~0ull;    // m_staticHash when it was baked
    mutable double                         m_navTime = -1e9;
    // How hard each contact pushed last step, so the solver starts from there
    // next step instead of from nothing (stacks stay still). See RigidBodies.cpp.
    // (Per pair of touching parts: where each contact was on the first part, and its pushes.)
    struct ContactMemory { glm::vec3 local, n; float jn, jt1, jt2; };
    std::unordered_map<uint64_t, std::vector<ContactMemory>> m_contactMemory;
};
