#pragma once
// A Bone's type and its settings (Jiggle / LookAt / IK), as Properties and the
// Armature window show them (EditorArmature.cpp).
class Scene;
class SceneNode;

namespace BoneUi {

// The Bone Type list and the sliders for that type, with a target picker and a
// "Make Target" button for LookAt / IK. True if anything changed.
bool edit(Scene& scene, SceneNode& bone);
// A small see-through ball at the bone's tip for it to reach for / look at (next to
// its part, so you can move it with the Move tool). Returns it.
SceneNode* makeTarget(Scene& scene, SceneNode& bone);

} // namespace BoneUi
