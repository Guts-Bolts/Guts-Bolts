#pragma once
#include <glm/glm.hpp>

class Scene;
class SceneNode;

// Dents in bodies that bounce back. A hit pushes the surface in where it lands (Player::
// deformPartAtImpact); with workspace.SoftTissue on (the default), the dent then slowly
// springs back out. Different body parts take hits differently: a head is hard (small
// dents that come back slowly), a torso is soft (deep dents that bounce back quickly),
// arms and legs are in between. Hit the same spot hard enough and it tears: a ragged
// wound that never quite heals before you respawn.
//
// Only the screen that sees the hit works this out (nothing is sent).
namespace SoftTissue {

struct Feel { float depth = 1.0f, recover = 0.5f; };   // dent size, and how fast (1/s) it springs back
Feel feelOf(const SceneNode& part);

// Push the dent in. Returns true if it tore (Gore::tear should follow).
bool dent(SceneNode& part, const glm::vec3& point, const glm::vec3& normal, const glm::vec3& velocity, bool springBack);

// Every frame while playing: dents spring back a little.
void update(Scene& scene, float dt);

// How deep (studs) the deepest dent in a part is now (0 = none, or healed).
float depth(const SceneNode& part);
void clear();   // forget every dent (Play stopped)

} // namespace SoftTissue
