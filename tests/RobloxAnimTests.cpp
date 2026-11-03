// Roblox animations (KeyframeSequence in .rbxmx / .rbxm) on Guts&Bolts characters:
// R6 and R15 animations both go on our R6 dummy the right way round (an arm Roblox
// raises forwards goes forwards here too), at our size, and importing a Roblox model
// with an animation in it gives an Animation object.
//
//   RobloxAnimTests
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "scene/Animation.h"
#include "scene/Player.h"
#include "scene/RobloxAnim.h"
#include "scene/RobloxFile.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
bool near(glm::vec3 a, glm::vec3 b, float tol = 0.03f) { return glm::length(a - b) < tol; }
std::string str(glm::vec3 v) {
    char b[96];
    std::snprintf(b, sizeof b, "(%.3f, %.3f, %.3f)", v.x, v.y, v.z);
    return b;
}

// Roblox XML for one Pose: CFrame = a turn of `deg` degrees about `axis` (0 X, 1 Y, 2 Z).
std::string poseXml(const std::string& name, int axis, float deg, const std::string& inside = "", int style = 0) {
    const float a = glm::radians(deg), c = std::cos(a), s = std::sin(a);
    glm::mat3 r(1.0f);   // columns
    if (axis == 0) r = glm::mat3(glm::vec3(1, 0, 0), glm::vec3(0, c, s), glm::vec3(0, -s, c));
    if (axis == 1) r = glm::mat3(glm::vec3(c, 0, -s), glm::vec3(0, 1, 0), glm::vec3(s, 0, c));
    if (axis == 2) r = glm::mat3(glm::vec3(c, s, 0), glm::vec3(-s, c, 0), glm::vec3(0, 0, 1));
    std::ostringstream o;
    o << "<Item class=\"Pose\"><Properties><string name=\"Name\">" << name << "</string>"
      << "<CoordinateFrame name=\"CFrame\"><X>0</X><Y>0</Y><Z>0</Z>";
    const char* names[9] = {"R00", "R01", "R02", "R10", "R11", "R12", "R20", "R21", "R22"};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) o << "<" << names[row * 3 + col] << ">" << r[col][row] << "</" << names[row * 3 + col] << ">";
    o << "</CoordinateFrame><float name=\"Weight\">1</float><token name=\"EasingStyle\">" << style
      << "</token><token name=\"EasingDirection\">0</token></Properties>" << inside << "</Item>";
    return o.str();
}

std::string keyframeXml(float time, const std::string& poses) {
    std::ostringstream o;
    o << "<Item class=\"Keyframe\"><Properties><string name=\"Name\">Keyframe</string><float name=\"Time\">" << time
      << "</float></Properties>" << poses << "</Item>";
    return o.str();
}

std::string fileXml(const std::string& name, const std::string& keys, const std::string& wrapper = "") {
    std::ostringstream o;
    o << "<roblox version=\"4\">";
    if (!wrapper.empty()) o << "<Item class=\"Model\"><Properties><string name=\"Name\">" << wrapper << "</string></Properties>";
    o << "<Item class=\"KeyframeSequence\"><Properties><string name=\"Name\">" << name << "</string>"
      << "<bool name=\"Loop\">true</bool><token name=\"Priority\">1</token></Properties>" << keys << "</Item>";
    if (!wrapper.empty()) o << "</Item>";
    o << "</roblox>";
    return o.str();
}

std::string writeFile(const std::string& name, const std::string& text) {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    std::ofstream(p, std::ios::binary) << text;
    return p.string();
}

// Play the clip on the dummy at time t; returns where the Right Arm is and which way its hand points.
void poseAt(SceneNode* rig, const Anim::Clip& clip, float t, glm::vec3& armCenter, glm::vec3& handDir, const char* arm = "Right Arm") {
    Anim::RestPose rest;
    Anim::captureRest(rig, rest);
    std::map<std::string, Anim::Sample> poses;
    Anim::sample(clip, t, poses);
    Anim::applyPoses(rig, rest, poses);
    SceneNode* a = Anim::findPart(rig, arm);
    const glm::mat4 m = a->worldMatrix();
    armCenter = glm::vec3(m[3]) - glm::vec3(rig->findChild("Torso")->worldMatrix()[3]);
    handDir = -glm::normalize(glm::vec3(m[1]));
    Anim::restore(rig, rest);
}
} // namespace

int main() {
    Mesh::headless = true;
    Scene scene;
    scene.buildDefault();
    SceneNode* rig = Player::buildRig(scene, "Rig", glm::vec3(0.0f));
    check(rig && Anim::findPart(rig, "Right Arm"), "a dummy to animate");
    const RobloxAnim::Target target = RobloxAnim::targetOf(rig);
    check(target.kind == RobloxAnim::RigKind::R6 && target.mirror, "the dummy is seen as R6 (facing +Z)");

    // --- R6: Right Shoulder turned +90 degrees about its Z (Roblox's tool-holding arm, straight forward) ---
    const std::string r6 = fileXml("Point", keyframeXml(0.0f, poseXml("HumanoidRootPart", 0, 0, poseXml("Torso", 0, 0, poseXml("Right Arm", 2, 0)))) +
                                                keyframeXml(1.0f, poseXml("HumanoidRootPart", 0, 0, poseXml("Torso", 0, 0, poseXml("Right Arm", 2, 90, "", 3)))));
    std::vector<RobloxAnim::Clip> clips;
    std::string error;
    check(RobloxFile::readAnimations(writeFile("gb_point_r6.rbxmx", r6), clips, error) && clips.size() == 1, "reads an R6 KeyframeSequence " + error);
    if (clips.empty()) return 1;
    check(clips[0].keys.size() == 2 && clips[0].keys[1].poses.count("Right Arm"), "its keyframes and nested poses");
    check(RobloxAnim::kindOfClip(clips[0]) == RobloxAnim::RigKind::R6, "the animation is seen as R6");
    std::vector<std::string> notes;
    Anim::Clip clip = RobloxAnim::convert(clips[0], target, &notes);
    check(clip.loop && clip.priority == Anim::Priority::Movement, "Loop and Priority come across");
    check(clip.keys.size() == 2 && clip.keys[1].poses.count("Right Arm") && !clip.keys[1].poses.count("HumanoidRootPart"),
          "poses for the parts (not the root part)");
    check(clip.keys[1].poses["Right Arm"].easing == Anim::Easing::Cubic, "easing style comes across (Cubic)");

    glm::vec3 center, hand;
    poseAt(rig, clip, 1.0f, center, hand);
    // Roblox: the arm swings forward about the shoulder (torso's top right edge); ours is half size and faces +Z.
    check(near(hand, glm::vec3(0, 0, 1)), "R6: the arm points forward (+Z, where our dummy faces): " + str(hand));
    check(near(center, glm::vec3(0.75f, 0.25f, 0.25f)), "R6: and sits where Roblox puts it, at our size: " + str(center));
    poseAt(rig, clip, 0.0f, center, hand);
    check(near(hand, glm::vec3(0, -1, 0)) && near(center, glm::vec3(0.75f, 0.0f, 0.0f)), "R6: at the start it hangs down: " + str(center));

    // The left arm the same way (its joint is turned the other way round in Roblox).
    const std::string left = fileXml("Wave", keyframeXml(1.0f, poseXml("Left Arm", 2, -90)));
    clips.clear();
    RobloxFile::readAnimations(writeFile("gb_left_r6.rbxmx", left), clips, error);
    clip = RobloxAnim::convert(clips.at(0), target);
    poseAt(rig, clip, 1.0f, center, hand, "Left Arm");
    check(near(hand, glm::vec3(0, 0, 1)), "R6: Left Shoulder -90 points the left arm forward too: " + str(hand));

    // Head turned 30 degrees: Roblox's neck turns it to the character's left; so should ours.
    const std::string look = fileXml("Look", keyframeXml(1.0f, poseXml("Head", 2, 30)));
    clips.clear();
    RobloxFile::readAnimations(writeFile("gb_look_r6.rbxmx", look), clips, error);
    clip = RobloxAnim::convert(clips.at(0), target);
    {
        Anim::RestPose rest;
        Anim::captureRest(rig, rest);
        std::map<std::string, Anim::Sample> poses;
        Anim::sample(clip, 1.0f, poses);
        Anim::applyPoses(rig, rest, poses);
        const glm::vec3 face = glm::normalize(glm::vec3(Anim::findPart(rig, "Head")->worldMatrix()[2]));   // our face is +Z
        Anim::restore(rig, rest);
        // Roblox: Neck Transform Rz(+30) turns the face (-Z) toward -X... (the character's left is -X in Roblox).
        check(face.x < -0.4f && face.z > 0.8f, "R6: a head turn goes the same way: " + str(face));
    }

    // --- R15: RightUpperArm turned +90 about X (forward), fitted to our R6 dummy ---
    const std::string r15 = fileXml("R15 Point", keyframeXml(0.0f, poseXml("LowerTorso", 0, 0, poseXml("UpperTorso", 0, 0))) +
                                                     keyframeXml(1.0f, poseXml("RightUpperArm", 0, 90) + poseXml("RightLowerArm", 0, 45)));
    clips.clear();
    check(RobloxFile::readAnimations(writeFile("gb_point_r15.rbxmx", r15), clips, error), "reads an R15 KeyframeSequence");
    check(RobloxAnim::kindOfClip(clips.at(0)) == RobloxAnim::RigKind::R15, "the animation is seen as R15");
    notes.clear();
    clip = RobloxAnim::convert(clips.at(0), target, &notes);
    check(!notes.empty() && notes[0].find("R15") != std::string::npos, "it says it was fitted from R15");
    check(clip.keys.back().poses.count("Right Arm") && !clip.keys.back().poses.count("RightLowerArm"), "R15 upper arm -> R6 arm");
    poseAt(rig, clip, 1.0f, center, hand);
    check(near(hand, glm::vec3(0, 0, 1)), "R15 on R6: the arm points forward: " + str(hand));

    // --- Turn Around flips front and back ---
    RobloxAnim::turnAround(clip);
    poseAt(rig, clip, 1.0f, center, hand);
    check(near(hand, glm::vec3(0, 0, -1)), "Turn Around: now it points backwards: " + str(hand));

    // --- Importing a Roblox model with an animation in it: an Animation object, fitted ---
    RobloxFile::Report report;
    const std::string withModel = fileXml("Dance", keyframeXml(0.5f, poseXml("Right Arm", 2, 90)), "Dancer");
    auto made = RobloxFile::importModel(scene, scene.root(), writeFile("gb_model_anim.rbxmx", withModel), report, error);
    SceneNode* anim = nullptr;
    for (SceneNode* m : made) for (auto& c : m->children) if (c->isAnimation()) anim = c.get();
    check(anim && anim->name == "Dance", "importing the model gives an Animation object");
    if (anim) {
        Anim::Clip in = Anim::parse(anim->source);
        check(in.keys.size() == 1 && in.keys[0].poses.count("Right Arm") && in.loop, "with the keyframes in it");
    }

    // --- Files with no animation say why ---
    clips.clear();
    const std::string onlyLink = "<roblox version=\"4\"><Item class=\"Animation\"><Properties><string name=\"Name\">Run</string>"
                                 "<Content name=\"AnimationId\"><url>rbxassetid://123</url></Content></Properties></Item></roblox>";
    check(!RobloxFile::readAnimations(writeFile("gb_link.rbxmx", onlyLink), clips, error) &&
          error.find("link") != std::string::npos, "an Animation that's only a link says so: " + error);

    std::cout << (failures ? "FAILED: " : "All passed") << (failures ? std::to_string(failures) : "") << "\n";
    return failures ? 1 : 0;
}
