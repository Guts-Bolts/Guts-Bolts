#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "Particles.h"

class Scene;
class SceneNode;

// Gore that more than one place needs: where a character is hit, how it bleeds, wounds
// that go all the way through, tears, and the cut ends of a limb that came off.
namespace Gore {

// What one character bleeds. The game's settings (workspace.Gore, BloodColor,
// BloodAmount) unless the character's Model has its own attributes:
//   Gore        "Blood" / "Oil" / "Off"   (a robot NPC in a game full of people)
//   BloodColor  Color3                    (green aliens, blue slime)
//   BloodAmount number, 0..5              (how messy, times the game's amount)
struct Look {
    bool      on = false;
    GoreKind  kind = GoreKind::Blood;
    glm::vec3 color{0.5f, 0.02f, 0.03f};
    float     amount = 1.0f;
};
Look lookFor(Scene& scene, const SceneNode* partOrCharacter);

// While one of these lives, sprays and splats use that character's look.
class Scope {
public:
    Scope(Scene& scene, const SceneNode* partOrCharacter);
    const Look& look() const { return m_look; }
    bool on() const { return m_look.on; }
    GoreKind kind() const { return m_look.kind; }
private:
    Look m_look;
    ParticleSystem::Look m_keep;
};

// A hit on a body part at `point` from something moving at `velocity`: the wound and
// the blood coming out of it. Fast things (bullets) go all the way through: a second,
// bigger wound on the far side, and a spray out of it that lands on whatever is behind.
// `damage` makes the wound bigger.
void hit(Scene& scene, SceneNode* part, const glm::vec3& point, const glm::vec3& normal,
         const glm::vec3& velocity, float damage);

// Where something moving along `dir` from `entry` comes out of `part` (its box). False if
// it doesn't (it only grazed it).
bool exitPoint(const SceneNode& part, const glm::vec3& entry, const glm::vec3& dir, glm::vec3& out);
// Faster than this (studs / second), a hit goes through the body.
constexpr float kThroughSpeed = 60.0f;

// A tear: soft tissue pushed too far (see SoftTissue). A bigger, ragged wound, bits of
// meat and a gush of blood.
void tear(Scene& scene, SceneNode* part, const glm::vec3& point, const glm::vec3& normal, const glm::vec3& velocity);

// The cut end of a limb or a neck (dismemberment): a flat cap of meat with a ring of
// bone in it (or metal and wires for oil), put on `part` where it was cut. `end` is the
// side of the part, in its own -0.5..0.5 space ((0, 0.5, 0) = the top). The cap fits the
// part's real shape there (bundles aren't boxes). Returns the cap (a part inside `part`).
SceneNode* cutEnd(Scene& scene, SceneNode* part, const glm::vec3& end, float size, GoreKind kind, const glm::vec3& color);
// Take every cut end off (respawning).
void removeCutEnds(Scene& scene, SceneNode* root);
constexpr const char* kCutEndName = "CutEnd";

// A random point inside a part's real shape (its mesh, when it has one), world space.
// For gibs that come from where the body actually is.
glm::vec3 insidePoint(const SceneNode& part, float t0, float t1, float t2);

} // namespace Gore
