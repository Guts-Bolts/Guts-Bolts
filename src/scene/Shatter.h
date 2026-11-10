#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

class Scene;
class SceneNode;

// Breaking things into pieces where they're hit: walls, crates, windows, props.
//
// A part breaks when a script calls part:Shatter(point, force), or by itself when it's
// Breakable (the tag "Breakable", or the attribute Breakable = true) and something hits
// it harder than its BreakSpeed attribute (studs / second, default 45), or an explosion
// reaches it. It's swapped for loose pieces of the same colour and material: small ones
// round where it was hit, bigger ones further away, all thrown away from the hit. Little
// pieces fade away after a while (big ones stay, so a broken wall leaves rubble).
//
// Only the host breaks things; the pieces are ordinary parts, so everyone sees them.
namespace Shatter {

// Can this part break by itself?
bool breakable(const SceneNode& part);
float breakSpeed(const SceneNode& part);

// Break `part` at `point` (world space). `force` 0..1+ = how hard: more and smaller
// pieces, thrown faster. Returns the pieces (empty if it couldn't break: not a part, a
// character's body, too small).
std::vector<SceneNode*> breakPart(Scene& scene, SceneNode* part, const glm::vec3& point, float force);

// Pieces too small to keep fade out and go after kPieceLife seconds.
void update(Scene& scene, float dt);
void clear();
constexpr float kPieceLife = 12.0f;
constexpr int   kMostPieces = 400;     // alive at once (the oldest small ones go first)

// Splitting a box: the pieces' boxes (centre + size, in the part's own unit space -0.5..0.5),
// smaller near `hit`. (The rest of breakPart is placing them; tests use this directly.)
struct Box { glm::vec3 center, size; };
std::vector<Box> split(const glm::vec3& hit, int count, uint32_t seed);

} // namespace Shatter
