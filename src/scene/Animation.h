#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "SceneNode.h"

class Scene;

// Animations, like Roblox's: an Animation object holds keyframes; each
// keyframe poses some of a rig's parts (turn and move them away from how
// they were built). Played on a rig (a Model of parts, like a character),
// the parts move smoothly from one keyframe to the next.
//
// The keyframes are kept as JSON text in the Animation object's `source`
// (so saving, copying, undo and Team Create need nothing special).
namespace Anim {

// How a part moves from its pose in one keyframe to the next (Roblox's
// PoseEasingStyle / PoseEasingDirection).
enum class Easing { Linear, Constant, Cubic, Elastic, Bounce };
enum class EaseDir { In, Out, InOut };
inline constexpr int kEasingCount = 5;
inline const char* const kEasingNames[kEasingCount] = {"Linear", "Constant", "Cubic", "Elastic", "Bounce"};
inline const char* const kEaseDirNames[3] = {"In", "Out", "InOut"};

// Which animation wins when several play on the same part (Roblox's AnimationPriority).
enum class Priority { Core, Idle, Movement, Action };
inline const char* const kPriorityNames[4] = {"Core", "Idle", "Movement", "Action"};

struct Pose {
    glm::vec3 pos{0.0f};   // moved this far (along the part's own axes)
    glm::vec3 rot{0.0f};   // turned this much (degrees, the part's own X / Y / Z)
    Easing    easing = Easing::Linear;
    EaseDir   dir    = EaseDir::In;
};

struct Keyframe {
    float                       time = 0.0f;   // seconds
    std::string                 name;          // optional: fires KeyframeReached
    std::map<std::string, Pose> poses;         // part name -> pose
};

struct Clip {
    bool                  loop     = false;
    Priority              priority = Priority::Action;
    std::vector<Keyframe> keys;                // sorted by time
    float length() const { return keys.empty() ? 0.0f : keys.back().time; }
    Keyframe* keyAt(float t, float tolerance = 1e-3f);
    Keyframe& addKey(float t);                 // finds or makes the keyframe at t
    void      sort();
};

Clip        parse(const std::string& text);
std::string dump(const Clip& clip);
// Make an empty Animation object's text.
std::string emptyClipText();

float ease(Easing e, EaseDir d, float t);   // 0..1 -> 0..1

// A part's pose at one moment: how far it's moved and turned.
struct Sample {
    glm::vec3 pos{0.0f};
    glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
};
// Where each part is at time t (only parts the clip poses).
void sample(const Clip& clip, float t, std::map<std::string, Sample>& out);

glm::quat eulerToQuat(const glm::vec3& deg);   // same order as Transform (Z * Y * X)
glm::vec3 quatToEuler(const glm::quat& q);

// ---- Putting poses on a rig ------------------------------------------------

// How the rig's parts were before any animation touched them (local transforms).
using RestPose = std::unordered_map<uint64_t, Transform>;
void captureRest(const SceneNode* rig, RestPose& rest);

// Pose the rig: every part named in `poses` is turned about its joint and
// moved; parts attached to it (arms on the torso, parts inside a part) follow.
// weight 0..1 blends from where the parts are now to the pose.
void applyPoses(SceneNode* rig, const RestPose& rest, const std::map<std::string, Sample>& poses, float weight = 1.0f);
// Put every part back how it was.
void restore(SceneNode* rig, const RestPose& rest);

// The inverse, for the editor: the part `part` was dragged to `posedLocal`;
// what pose gives that (given the other poses already on the rig)?
bool poseFromTransform(SceneNode* rig, const RestPose& rest, const std::map<std::string, Sample>& poses,
                       const SceneNode* part, const Transform& posedLocal, Pose& out);

// Where a part bends (its joint), in its own -0.5..0.5 space: an Attachment
// named "Pivot" inside it, else shoulders / hips / neck by name, else its middle.
glm::vec3 jointPivot(const SceneNode* part);

// The parts of a rig that can be posed (in order, for the editor's list).
std::vector<SceneNode*> rigParts(SceneNode* rig);
// Hats and other accessories on a rig: they turn with the head (or body) it wears them on.
std::vector<SceneNode*> wornParts(SceneNode* rig);
// The part a pose for `name` moves on this rig (null if none).
SceneNode* findPart(SceneNode* rig, const std::string& name);
// The model a part belongs to, for animating (the nearest Model above it).
SceneNode* rigOf(SceneNode* node);

// ---- Playing animations in a game (scripts' AnimationTracks) -----------------

class Animator {
public:
    // Scripts: humanoid:LoadAnimation(anim) -> a track id (0 = no).
    int  load(uint64_t rigId, const SceneNode& animation);
    // The same for keyframes that aren't in an Animation object (emotes): `name` is the track's Name.
    int  loadClip(uint64_t rigId, const std::string& name, const Clip& clip);
    void forget(int track) { m_tracks.erase(track); }   // done with it (it must have stopped)
    void play(int track, float fade, float weight, float speed);
    void stop(int track, float fade);
    void adjustSpeed(int track, float speed);
    void adjustWeight(int track, float weight, float fade);
    bool valid(int track) const { return m_tracks.count(track) != 0; }
    std::vector<int> playingOn(uint64_t rigId) const;

    struct Track {
        int         id = 0;
        uint64_t    rig = 0, animation = 0;
        std::string name;
        Clip        clip;
        bool        looped = false;
        Priority    priority = Priority::Action;
        bool        playing = false;
        float       time = 0.0f, speed = 1.0f;
        float       weight = 0.0f, target = 1.0f, fadeRate = 0.0f;   // fading in / out
    };
    Track* track(int id);

    // Events for scripts (filled by update, read by whoever runs the scripts).
    struct Event { enum Kind { Stopped, Ended, DidLoop, Keyframe } kind; int track; std::string name; };
    std::vector<Event> events;

    // Once a frame in Play mode, after the characters have moved.
    void update(float dt, Scene& scene);
    // A character died / went away: its tracks stop at once (no putting parts back).
    void stopRig(uint64_t rigId, bool restoreParts, Scene& scene);
    void clear();   // Play ended

    // How the local player's parts were when Play started (so walking and
    // animations don't fight over what "rest" is), and which parts something
    // else moves every frame (its walking limbs). Set by the game.
    std::function<const RestPose*(uint64_t rigId)>                restFor;
    std::function<bool(uint64_t rigId, const SceneNode* part)>    drivenElsewhere;

private:
    std::map<int, Track>              m_tracks;
    std::map<uint64_t, RestPose>      m_rests;     // rigs we've posed
    int                               m_next = 1;
};

} // namespace Anim
