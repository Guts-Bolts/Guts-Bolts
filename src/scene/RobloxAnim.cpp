#include "RobloxAnim.h"
#include "SceneNode.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <set>

namespace RobloxAnim {

namespace {

constexpr float kScale = 0.5f;   // Roblox characters are twice the size of ours

// A Roblox R6 joint: the turn of its C1 (columns) and where it bends on the part,
// as a fraction of the part's size (-0.5..0.5), from the standard R6 Motor6Ds.
struct R6Joint { glm::mat3 c1; glm::vec3 pivot; };

const R6Joint* r6Joint(const std::string& part) {
    // Columns of each C1's turn (Roblox writes CFrames as rows: R00 R01 R02 / R10 ...).
    static const glm::mat3 kNeck(glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));
    static const glm::mat3 kRight(glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), glm::vec3(1, 0, 0));
    static const glm::mat3 kLeft(glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), glm::vec3(-1, 0, 0));
    static const std::map<std::string, R6Joint> joints = {
        {"Torso",     {kNeck,  {0.0f, 0.0f, 0.0f}}},     // RootJoint, C1 (0, 0, 0) on a 2x2x1 torso
        {"Head",      {kNeck,  {0.0f, -0.5f, 0.0f}}},    // Neck, C1 (0, -0.5, 0) on a 2x1x1 head
        {"Right Arm", {kRight, {-0.5f, 0.25f, 0.0f}}},   // Right Shoulder, C1 (-0.5, 0.5, 0) on a 1x2x1 arm
        {"Left Arm",  {kLeft,  {0.5f, 0.25f, 0.0f}}},    // Left Shoulder, C1 (0.5, 0.5, 0)
        {"Right Leg", {kRight, {0.5f, 0.5f, 0.0f}}},     // Right Hip, C1 (0.5, 1, 0)
        {"Left Leg",  {kLeft,  {-0.5f, 0.5f, 0.0f}}},    // Left Hip, C1 (-0.5, 1, 0)
    };
    auto it = joints.find(part);
    return it == joints.end() ? nullptr : &it->second;
}

// What a pose does to its part, in the part's own axes (Roblox studs).
struct Motion {
    glm::mat3 rot{1.0f};
    glm::vec3 pos{0.0f};
    glm::vec3 pivot{0.0f};      // where Roblox turns it (fraction of the part's size)
    bool      hasPivot = false; // false: turn it about our own joint
    int       style = 0, dir = 0;
};

Motion motionOf(const std::string& name, const Pose& p, RigKind source) {
    Motion m;
    m.style = p.style;
    m.dir = p.dir;
    const R6Joint* j = source == RigKind::R6 ? r6Joint(name) : nullptr;
    if (j) {
        // Part1 = Part0 * C0 * T * C1:Inverse(), so on the part itself the pose is C1 * T * C1:Inverse().
        m.rot = j->c1 * p.rot * glm::transpose(j->c1);
        m.pos = j->c1 * p.pos;
        m.pivot = j->pivot;
        m.hasPivot = true;
    } else {
        // R15 joints are lined up with their parts, so the pose is already in the part's axes.
        m.rot = p.rot;
        m.pos = p.pos;
    }
    return m;
}

glm::mat3 orthonormal(const glm::mat3& m) {
    return glm::mat3_cast(glm::normalize(glm::quat_cast(m)));
}

// A part's pose at time t, between the keyframes that have it (still at rest if none do).
Pose sampleAt(const Clip& c, const std::string& part, float t) {
    const Keyframe* before = nullptr;
    const Keyframe* after = nullptr;
    for (const Keyframe& k : c.keys) {
        auto it = k.poses.find(part);
        if (it == k.poses.end() || it->second.weight <= 0.001f) continue;
        if (k.time <= t) before = &k;
        else if (!after) after = &k;
    }
    if (!before && !after) return Pose{};
    if (!before) return after->poses.at(part);
    if (!after || after->time - before->time < 1e-5f) return before->poses.at(part);
    const Pose& a = before->poses.at(part);
    const Pose& b = after->poses.at(part);
    const float w = (t - before->time) / (after->time - before->time);
    Pose out = a;
    out.pos = glm::mix(a.pos, b.pos, w);
    out.rot = glm::mat3_cast(glm::slerp(glm::normalize(glm::quat_cast(a.rot)), glm::normalize(glm::quat_cast(b.rot)), w));
    return out;
}

Anim::Easing easingOf(int style) {
    switch (style) {
        case 1:  return Anim::Easing::Constant;
        case 2:  return Anim::Easing::Elastic;
        case 3:
        case 5:  return Anim::Easing::Cubic;     // Cubic, CubicV2
        case 4:  return Anim::Easing::Bounce;
        default: return Anim::Easing::Linear;
    }
}

Anim::Priority priorityOf(int p) {
    if (p == 0) return Anim::Priority::Idle;
    if (p == 1) return Anim::Priority::Movement;
    if (p >= 1000) return Anim::Priority::Core;
    return Anim::Priority::Action;   // Action, Action2, Action3, Action4
}

const glm::mat3 kFlip(glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, -1));

} // namespace

const char* rigKindName(RigKind k) {
    return k == RigKind::R6 ? "R6" : k == RigKind::R15 ? "R15" : "custom";
}

RigKind kindOfNames(const std::vector<std::string>& names) {
    int r6 = 0, r15 = 0;
    static const std::set<std::string> kR6 = {"Torso", "Left Arm", "Right Arm", "Left Leg", "Right Leg"};
    static const std::set<std::string> kR15 = {"LowerTorso", "UpperTorso", "LeftUpperArm", "RightUpperArm", "LeftLowerArm",
                                               "RightLowerArm", "LeftHand", "RightHand", "LeftUpperLeg", "RightUpperLeg",
                                               "LeftLowerLeg", "RightLowerLeg", "LeftFoot", "RightFoot"};
    for (const std::string& n : names) {
        if (kR6.count(n)) ++r6;
        if (kR15.count(n)) ++r15;
    }
    if (r6 == 0 && r15 == 0) return RigKind::Other;
    return r15 > r6 ? RigKind::R15 : RigKind::R6;
}

RigKind kindOfClip(const Clip& clip) {
    std::vector<std::string> names;
    for (const Keyframe& k : clip.keys)
        for (const auto& [n, p] : k.poses) names.push_back(n);
    return kindOfNames(names);
}

Target targetOf(SceneNode* rig) {
    Target t;
    std::vector<std::string> names;
    for (SceneNode* p : Anim::rigParts(rig)) {
        names.push_back(p->name);
        t.parts[p->name] = {Anim::jointPivot(p) * p->transform.scale, p->transform.scale};
    }
    t.kind = kindOfNames(names);
    // Our own dummies (R6) face +Z; R15 bodies here come from Roblox files and still face -Z.
    t.mirror = t.kind != RigKind::R15;
    return t;
}

Target dummyTarget() {
    // The sizes Player::buildRig gives a dummy, and where Anim::jointPivot bends them.
    Target t;
    t.kind = RigKind::R6;
    t.mirror = true;
    const glm::vec3 limb(0.5f, 1.0f, 0.5f);
    t.parts["HumanoidRootPart"] = {glm::vec3(0.0f), {1.0f, 1.0f, 0.5f}};
    t.parts["Torso"] = {glm::vec3(0.0f), {1.0f, 1.0f, 0.5f}};
    t.parts["Head"] = {{0.0f, -0.325f, 0.0f}, {0.72f, 0.65f, 0.72f}};
    for (const char* n : {"Left Arm", "Right Arm", "Left Leg", "Right Leg"}) t.parts[n] = {{0.0f, 0.5f, 0.0f}, limb};
    return t;
}

Anim::Clip convert(const Clip& src, const Target& target, std::vector<std::string>* notes) {
    Anim::Clip out;
    out.loop = src.loop;
    out.priority = priorityOf(src.priority);
    RigKind from = kindOfClip(src);
    RigKind to = target.kind;
    if (from == RigKind::Other) from = to;   // only a head (or a custom rig's parts): made for this kind of body
    if (to == RigKind::Other) to = from;     // a custom rig: keep the names as they are
    auto note = [&](const std::string& s) {
        if (notes && std::find(notes->begin(), notes->end(), s) == notes->end()) notes->push_back(s);
    };
    if (from != to && from != RigKind::Other)
        note(std::string("Made for ") + rigKindName(from) + ", fitted to this " + rigKindName(to) + " body.");
    if (from == RigKind::R15 && to == RigKind::R6)
        note("R6 arms and legs are one piece each, so elbow, knee, hand and foot bends are left out.");

    for (const Keyframe& k : src.keys) {
        std::map<std::string, std::pair<Motion, bool>> moved;   // our part name -> (motion, retargeted)
        auto has = [&](const char* n) {
            auto it = k.poses.find(n);
            return it != k.poses.end() && it->second.weight > 0.001f;
        };
        auto at = [&](const char* n) { return has(n) ? k.poses.at(n) : sampleAt(src, n, k.time); };

        if (from == RigKind::R15 && to == RigKind::R6) {
            // The R6 torso is both R15 torso halves; the R6 limbs are the upper limbs.
            if (has("LowerTorso") || has("UpperTorso")) {
                Motion lower = motionOf("LowerTorso", at("LowerTorso"), from);
                Motion upper = motionOf("UpperTorso", at("UpperTorso"), from);
                Motion m = lower;
                m.rot = lower.rot * upper.rot;
                const Pose& keyed = has("LowerTorso") ? k.poses.at("LowerTorso") : k.poses.at("UpperTorso");
                m.style = keyed.style;
                m.dir = keyed.dir;
                moved["Torso"] = {m, true};
            }
            const glm::mat3 waist = has("UpperTorso") || has("LowerTorso") ? at("UpperTorso").rot : glm::mat3(1.0f);
            const std::pair<const char*, const char*> limbs[] = {
                {"Head", "Head"}, {"LeftUpperArm", "Left Arm"}, {"RightUpperArm", "Right Arm"},
                {"LeftUpperLeg", "Left Leg"}, {"RightUpperLeg", "Right Leg"}};
            for (auto [r15, r6] : limbs) {
                if (!has(r15)) continue;
                Motion m = motionOf(r15, k.poses.at(r15), from);
                // R15 legs hang off the lower torso, R6 ones off the whole torso (which has the waist's turn in it).
                if (std::string(r6).find("Leg") != std::string::npos) m.rot = glm::transpose(waist) * m.rot;
                moved[r6] = {m, true};
            }
        } else if (from == RigKind::R6 && to == RigKind::R15) {
            const std::pair<const char*, const char*> parts[] = {
                {"Torso", "LowerTorso"}, {"Head", "Head"}, {"Left Arm", "LeftUpperArm"}, {"Right Arm", "RightUpperArm"},
                {"Left Leg", "LeftUpperLeg"}, {"Right Leg", "RightUpperLeg"}};
            for (auto [r6, r15] : parts) {
                if (!has(r6)) continue;
                Motion m = motionOf(r6, k.poses.at(r6), from);
                m.hasPivot = false;   // (R6 joints sit elsewhere on the part)
                moved[r15] = {m, true};
            }
        } else {
            for (const auto& [name, p] : k.poses) {
                if (p.weight <= 0.001f) continue;
                moved[name] = {motionOf(name, p, from), false};
            }
        }

        Anim::Keyframe& key = out.addKey(std::max(0.0f, k.time));
        if (k.name != "Keyframe") key.name = k.name;
        for (auto& [name, mv] : moved) {
            if (name == "HumanoidRootPart") continue;   // nothing turns the root part (it's what the rest hangs off)
            Motion m = mv.first;
            if (target.mirror) {   // Roblox bodies face -Z, ours +Z: flip front and back
                m.rot = kFlip * m.rot * kFlip;
                m.pos = kFlip * m.pos;
                m.pivot = kFlip * m.pivot;
            }
            m.rot = orthonormal(m.rot);
            m.pos *= kScale;
            Target::Part tp;
            auto tpIt = target.parts.find(name);
            if (tpIt != target.parts.end()) tp = tpIt->second;
            else if (!target.parts.empty()) note("This body has no \"" + name + "\" for some of the poses.");
            glm::vec3 move = m.pos;
            if (m.hasPivot && !mv.second) {
                // Roblox turns it about its own joint; we turn it about ours. Make up the difference
                // so it ends up exactly where Roblox puts it.
                const glm::vec3 robloxPivot = m.pivot * tp.size;
                move += (glm::mat3(1.0f) - m.rot) * (robloxPivot - tp.pivot);
            }
            Anim::Pose pose;
            pose.pos = move;
            pose.rot = Anim::quatToEuler(glm::quat_cast(m.rot));
            for (float* f : {&pose.pos.x, &pose.pos.y, &pose.pos.z, &pose.rot.x, &pose.rot.y, &pose.rot.z})
                if (std::fabs(*f) < 1e-4f) *f = 0.0f;
            pose.easing = easingOf(m.style);
            pose.dir = (Anim::EaseDir)std::clamp(m.dir, 0, 2);
            key.poses[name] = pose;
        }
    }
    // Keyframes left with nothing (only the root part was in them).
    out.keys.erase(std::remove_if(out.keys.begin(), out.keys.end(),
                                  [](const Anim::Keyframe& k) { return k.poses.empty() && k.name.empty(); }),
                   out.keys.end());
    out.sort();
    return out;
}

void turnAround(Anim::Clip& clip) {
    // Flipping Z: a turn of (x, y, z) degrees (Z * Y * X) becomes (-x, -y, -z); moves go the other way along Z.
    for (Anim::Keyframe& k : clip.keys)
        for (auto& [name, p] : k.poses) {
            p.rot = -p.rot;
            p.pos.z = -p.pos.z;
        }
}

} // namespace RobloxAnim
