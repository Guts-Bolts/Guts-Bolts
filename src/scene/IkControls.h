#pragma once
// IKControl, like Roblox's: put one in a character and every frame, on top of whatever
// animation is playing, a limb reaches for its Target (or the head looks at it).
//
//   local ik = Instance.new("IKControl")
//   ik.Type = Enum.IKControlType.Position      -- reach for it (LookAt: point at it)
//   ik.EndEffector = character["Right Arm"]    -- or an Attachment / Bone in it
//   ik.ChainRoot = character["Right Arm"]
//   ik.Target = workspace.Button
//   ik.Parent = character.Humanoid
//
// On the normal (R6) character each arm and leg turns at its shoulder or hip, and the
// head at its neck. Types:
//   Position / Transform  the end of the limb (or the EndEffector attachment) swings
//                         towards the Target
//   LookAt                the EndEffector's front (-Z) points at the Target
//   Rotation              the EndEffector turns to face the same way as the Target
// Weight blends between the animation (0) and the target (1); SmoothTime is how long it
// takes to catch up with a moving target. (Rigs made of Bones have their own IK bones.)
//
// It runs wherever that character is animated (GameSession::update): put it in your
// own character from a LocalScript, or in an NPC from a Script; everyone sees the pose.
class Scene;
class SceneNode;

namespace IkControls {

// Has an IKControl ever been made? (Until then the per-frame pass is skipped.)
void noteMade();
bool anyMade();

// Apply every enabled IKControl in the scene (after the animations have posed things).
void apply(Scene& scene, float dt);

} // namespace IkControls
