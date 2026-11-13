#pragma once
// Shapecasts for scripts: slide a ball, a box or a part's own shape along a line and
// find the first thing it bumps into (workspace:Spherecast, :Blockcast, :Shapecast).
// Like Roblox's, with margins (a rounded, fatter shape), size gradients (it grows or
// shrinks as it goes), CFrame / Size overrides and drawing in Studio's Physics View.
#include "../scene/Physics.h"

#include <glm/glm.hpp>
#include <functional>
#include <vector>

struct lua_State;
class Scene;
class SceneNode;

namespace Shapecasts {

// What is being slid along: a ball or a turned box, made fatter (and rounded) by
// `margin`, and growing by `grow` studs on every side for each stud it moves.
struct Shape {
    bool  ball = false;
    OBB   box;               // (a ball: box.center is its middle)
    float radius = 0.0f;     // (balls)
    float margin = 0.0f;
    float grow = 0.0f;
};

struct Hit {
    const SceneNode* part = nullptr;   // the terrain node when it's the ground
    glm::vec3 position{0.0f};          // where they touch
    glm::vec3 normal{0.0f, 1.0f, 0.0f};// out of what was hit, towards the shape
    float distance = 0.0f;             // how far the shape got
};

// The shape moved `t` studs along `dir` (a unit vector).
Shape at(const Shape& s, const glm::vec3& dir, float t);

// The gap between the shape (margin included) and a part's shape. Below 0: they overlap.
// `onShape` / `onPart` get the nearest points.
float gap(const Shape& s, const SceneNode* part, glm::vec3* onShape = nullptr, glm::vec3* onPart = nullptr);

// Slide `s` up to `range` studs along `dir` (a unit vector). Parts the shape already
// overlaps at the start are left out (like Roblox). `counts` says which parts (and the
// terrain node) count.
bool cast(Scene& scene, const Shape& s, const glm::vec3& dir, float range,
          const std::function<bool(const SceneNode*)>& counts, Hit& hit);

// Studio's Physics View draws the last casts (kept while Physics::debugOn() is set).
struct Drawn {
    Shape     from, to;
    bool      hit = false;
    glm::vec3 position{0.0f}, normal{0.0f};
    double    when = 0.0;   // (seconds, the clock used by now())
};
const std::vector<Drawn>& recent();
double now();

// Adds workspace:Spherecast / :Blockcast / :Shapecast. Runs after SpatialQueries::install.
void install(lua_State* L);

} // namespace Shapecasts
