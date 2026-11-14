#pragma once
// Real bullets (workspace:FireBullet): instead of a straight, instant laser, a bullet
//   * falls as it flies (the game's gravity) and is pushed by the wind (workspace.GlobalWind),
//   * goes through thin wood, glass, plastic and bodies (slowing down and doing less harm),
//     but stops in metal, concrete and stone,
//   * bounces off hard surfaces it only glances (a ricochet, with a whizz),
//   * whizzes past your ears when it misses you closely.
// trace() is the pure maths (tested on its own); fire() runs it in a Scene with the
// sounds, sparks, bullet holes and the harm to whoever it hits.
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <vector>

enum class Material;
class Scene;
class SceneNode;

namespace Ballistics {

struct Shot {
    glm::vec3 origin{0.0f};
    glm::vec3 velocity{0.0f, 0.0f, -900.0f};   // studs a second (a rifle: 900-1200)
    float     damage = 25.0f;                  // at the muzzle; less after going through things
    float     range = 1500.0f;                 // studs of flight before it's spent
    bool      pierce = true;                   // can go through thin things
    bool      ricochet = true;                 // can bounce off hard ones
    int       maxBounces = 2;
    glm::vec3 gravity{0.0f, -60.0f, 0.0f};
    glm::vec3 wind{0.0f};
    float     drag = 0.15f;                    // how much the air (and wind) tugs at it
    uint32_t  seed = 1u;
};

struct Hit {
    enum What { Stopped, Pierced, Ricochet };
    uint64_t  part = 0;
    bool      terrain = false;
    bool      flesh = false;           // part of a character
    Material  material{};
    glm::vec3 point{0.0f}, normal{0.0f, 1.0f, 0.0f};
    glm::vec3 velocity{0.0f};          // how it was moving when it hit
    What      what = Stopped;
    float     damage = 0.0f;           // what it does to a character here
};

struct Result {
    std::vector<glm::vec3> path;       // where it went (start, bends, end)
    std::vector<Hit>       hits;
    glm::vec3              end{0.0f};
    float                  time = 0.0f;   // seconds in the air
};

// What a straight piece of the flight hits: fills part/terrain/flesh/material/point/normal.
using CastFn = std::function<bool(const glm::vec3& from, const glm::vec3& dir, float length, Hit& out)>;
// How much of the thing just hit is in the way, along `dir` from the hit (studs).
using ThicknessFn = std::function<float(const Hit& hit, const glm::vec3& dir)>;

Result trace(const Shot& shot, const CastFn& cast, const ThicknessFn& thickness);

// How deep into a body a bullet goes before it stops (studs): through a limb, mostly.
constexpr float kFleshDepth = 2.5f;

// In a scene: casts against its parts and terrain (skipping `ignore` and what's inside
// them), harms characters it hits, plays the sounds and leaves the marks.
// `listener` = where the ears are (for the whizz), or null.
Result fire(Scene& scene, const Shot& shot, const std::vector<const SceneNode*>& ignore,
            const glm::vec3* listener);

} // namespace Ballistics
