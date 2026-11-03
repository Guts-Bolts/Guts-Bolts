#pragma once
#include <glm/glm.hpp>

class Scene;
class SceneNode;

// Roblox's older joints (Weld, ManualWeld, Snap, Motor, Motor6D: SceneNode::legacyJoint) and
// the joints built into characters (Neck, Right Shoulder...). Every frame, while playing:
//   Part1.CFrame = Part0.CFrame * C0 * (Motor: a turn of CurrentAngle around Z) * Transform * C1:Inverse()
// Two loose (unanchored) parts are held together by the physics instead (RigidBodies).
// A character's joints leave the limb where its animation put it unless a script changed
// C0, C1 or Transform (then the limb moves by that much, like aiming a gun with the arm).
namespace LegacyJoints {
void update(Scene& scene, float dt);
// The joints every character gets (Roblox's R6 names), made by Player::buildRig.
void addCharacterJoints(Scene& scene, SceneNode* rig);
// A part's place in the world without its size: rotation + position.
glm::mat4 frameOf(const SceneNode* n);
void setFrame(SceneNode* n, const glm::mat4& world);
// Where Part1 goes relative to Part0 (C0 * turn * Transform * C1^-1).
glm::mat4 offset(const SceneNode* joint);
}
