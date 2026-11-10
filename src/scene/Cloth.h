#pragma once
#include <glm/glm.hpp>
#include <vector>

class Scene;
class SceneNode;

// Soft things that move by themselves while the game runs:
//
//  * Cloth: a part with the attribute Cloth = true (or inside a model that has it, like
//    a cape accessory) flaps like fabric. Its top edge stays put on the part (attribute
//    ClothPin = how much of the top, 0..1, default 0.12) and the rest hangs, swings, is
//    pushed by the wind (workspace.GlobalWind) and by the body it's on (legs push a
//    skirt). Every corner of the mesh is a little weight joined to its neighbours by
//    sticks ("Verlet" cloth, like the ragdolls).
//
//  * Jiggle: a part with the attribute Jiggle = true (or inside a model that has it: a
//    ponytail, antennas, a chain) gets two springy bones from where it's held (the side
//    nearest the head when it's worn) to its far end, so it swings when the character
//    moves. JiggleStiffness (0..1, default 0.45) makes it stiffer.
//
// Each screen works this out itself: nothing extra is sent online.
namespace Cloth {

// Every frame while playing (the renderer calls it, before Skin::update).
void update(Scene& scene);

// Something cloth bumps into: a box (`axes` = the box's turned and sized axes, half of it).
struct Collider { glm::vec3 center; glm::mat3 axes; };

// Move one cloth part on by dt seconds (update does this). `gravity` in studs/s²
// (downwards), `wind` in studs/s. False if the part can't be cloth (no corners).
bool step(SceneNode& part, float dt, float gravity, const glm::vec3& wind, const std::vector<Collider>& colliders);
// Forget a part's cloth (it goes back to its own shape).
void reset(SceneNode& part);
// Forget every cloth (Play stopped).
void clearAll();
// How far (studs) a cloth corner hangs from where the part alone would put it: for tests.
float sag(const SceneNode& part);

// Give a part the Jiggle bones described above (true if it has bones now).
bool rigJiggle(SceneNode& part);
// Does this part (or the model it's in) ask for `attribute` = true?
bool wants(const SceneNode& part, const char* attribute);

} // namespace Cloth
