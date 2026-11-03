#pragma once
#include <map>
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "Animation.h"

class SceneNode;

// Roblox animations (KeyframeSequence, from a .rbxm / .rbxmx file) made to play
// on Guts&Bolts characters.
//
// A Roblox pose is the turn of a Motor6D joint, written in the joint's own
// axes (which, on R6 rigs, are turned sideways). Our poses are a turn of the
// part in its own axes about where it bends (Anim::jointPivot). So each pose is:
//   1. moved from the joint's axes into the part's axes,
//   2. retargeted if the animation was made for the other body (R15 -> R6 or back),
//   3. flipped front to back (our dummies face +Z, Roblox's face -Z),
//   4. halved in size (Roblox characters are twice ours), and
//   5. shifted so it turns about our joint the way it turned about Roblox's.
namespace RobloxAnim {

// One pose as it is in the file: Pose.CFrame (the joint's Transform) and its settings.
struct Pose {
    glm::vec3 pos{0.0f};
    glm::mat3 rot{1.0f};    // columns = right, up, back (like RobloxFile's CFrames)
    float     weight = 1.0f;
    int       style = 0;    // Enum.PoseEasingStyle: Linear 0, Constant 1, Elastic 2, Cubic 3, Bounce 4, CubicV2 5
    int       dir = 0;      // Enum.PoseEasingDirection: In 0, Out 1, InOut 2
};

struct Keyframe {
    float                       time = 0.0f;
    std::string                 name;     // Keyframe.Name ("Keyframe" = none)
    std::map<std::string, Pose> poses;    // part name -> pose (every Pose under the Keyframe)
};

struct Clip {
    std::string           name = "Animation";
    bool                  loop = false;
    int                   priority = 2;  // Enum.AnimationPriority: Idle 0, Movement 1, Action 2..5, Core 1000
    std::vector<Keyframe> keys;
};

enum class RigKind { R6, R15, Other };
const char* rigKindName(RigKind k);

// R6 (Torso, Left Arm...) or R15 (UpperTorso, LeftUpperArm...), by the parts' names.
RigKind kindOfNames(const std::vector<std::string>& names);
RigKind kindOfClip(const Clip& clip);

// The body the animation goes on.
struct Target {
    struct Part { glm::vec3 pivot{0.0f}, size{1.0f}; };   // where it bends (its own space, studs) and its size
    RigKind                     kind = RigKind::R6;
    bool                        mirror = true;   // flip front / back (our dummies face +Z)
    std::map<std::string, Part> parts;
};
Target targetOf(SceneNode* rig);   // a rig in the scene
Target dummyTarget();              // a Guts&Bolts dummy (Insert > Rig), for when there's no rig to look at

// Turn the Roblox animation into ours. `notes` gets what couldn't come across exactly.
Anim::Clip convert(const Clip& src, const Target& target, std::vector<std::string>* notes = nullptr);

// Flip an animation front to back (for a character that faces the other way).
void turnAround(Anim::Clip& clip);

} // namespace RobloxAnim
