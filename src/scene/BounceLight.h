#pragma once
#include <glm/glm.hpp>
#include <vector>

class Scene;
struct Environment;

// Bouncy light (Lighting.BounceLight). Real light bounces: sun on a red floor makes
// the wall beside it a little red. Drawing every bounce is far too slow for a game,
// so this cheats the way many games do: a few rays go down from the sun (and from
// the nearest lamps) round the camera, and where each one lands a small, soft light
// is put just above the surface, in the colour of that surface. The lit shader then
// treats them like any other lamp. They are worked out again a few times a second.
namespace BounceLight {

struct Point {
    glm::vec3 pos;      // just off the surface
    glm::vec3 color;    // linear colour * strength (like a lamp's)
    glm::vec3 normal;   // the way out of the surface it came from (it shines that way)
    float     range;
};

struct Lamp { glm::vec3 pos; glm::vec3 color; float range; };   // color: linear * brightness

// The bounce lights round `around` (usually what the camera looks at). `lamps`: the
// nearest real lamps (each sends one ray straight down). Empty when env.bounceLight is 0.
std::vector<Point> gather(Scene& scene, const Environment& env, const glm::vec3& around,
                          const std::vector<Lamp>& lamps);

constexpr int   kSunRays   = 4;     // a 4 x 4 grid of sun rays
constexpr float kSpacing   = 7.0f;  // studs between them
constexpr int   kMostLamps = 4;
constexpr int   kMost      = 8;     // bounce lights kept (the brightest)

} // namespace BounceLight
