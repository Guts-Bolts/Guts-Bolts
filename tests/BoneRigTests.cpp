// Jiggle, LookAt and IK bones (Skin.h): they move by themselves, are saved and
// copied, scripts can set them up, and the weight brush paints.
//
//   BoneRigTests <a game file with a baseplate>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "scene/EditMesh.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"
#include "scene/Skin.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

bool run(GameSession& session, const std::string& code, std::string& err) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}

SceneNode* addPart(Scene& scene, const std::string& name, glm::vec3 at, glm::vec3 size) {
    SceneNode* p = scene.addNode(name, PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    p->transform.position = at;
    p->transform.scale = size;
    return p;
}

// A bone at `pos` (studs from the part's middle, or from the bone it's in), pointing up (+Y).
SceneNode* addBone(Scene& scene, SceneNode* in, const std::string& name, glm::vec3 pos, float length) {
    auto b = std::make_unique<SceneNode>(name, NodeKind::Attachment);
    b->bone = true;
    b->transform.position = pos;
    b->rig.length = length;
    return scene.insert(std::move(b), in);
}

glm::vec3 worldTip(SceneNode* bone) {
    SceneNode* part = Skin::partOf(bone);
    return glm::vec3(Skin::partSpace(part) * glm::vec4(Skin::tip(bone, part), 1.0f));
}
glm::vec3 worldHead(SceneNode* bone) {
    SceneNode* part = Skin::partOf(bone);
    return glm::vec3(Skin::partSpace(part) * Skin::frame(bone, part)[3]);
}
float turned(SceneNode* b) { return glm::degrees(glm::angle(b->rig.live)); }
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: BoneRigTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);
    session.start();

    // --- Jiggle: the tip lags behind when the part moves, then settles back ---
    {
        SceneNode* body = addPart(scene, "JiggleBody", {20, 5, 0}, {1, 4, 1});
        SceneNode* tail = addBone(scene, body, "Tail", {0, 2, 0}, 2.0f);
        tail->rig.type = BoneType::Jiggle;
        Skin::solve(*body, 1.0f / 60.0f);
        check(turned(tail) < 0.5f, "a still jiggle bone stands where it's posed");
        float most = 0.0f;
        for (int i = 0; i < 20; ++i) {   // shove the part sideways, fast
            body->transform.position.x += 0.3f;
            Skin::solve(*body, 1.0f / 60.0f);
            most = std::max(most, turned(tail));
        }
        check(most > 5.0f, "moving the part makes the tail swing (" + std::to_string(most) + " degrees)");
        glm::vec3 tip = worldTip(tail);
        check(tip.x < body->transform.position.x, "the tail trails behind the way it moved");
        for (int i = 0; i < 300; ++i) Skin::solve(*body, 1.0f / 60.0f);
        check(turned(tail) < 1.0f, "then it settles back (" + std::to_string(turned(tail)) + " degrees)");
        tail->transform.rotation = {0.0f, 0.0f, -90.0f};   // sticking out sideways
        tail->rig.gravity = 40.0f;
        for (int i = 0; i < 300; ++i) Skin::solve(*body, 1.0f / 60.0f);
        check(turned(tail) > 5.0f && worldTip(tail).y < worldHead(tail).y - 0.1f, "gravity makes it droop (" + std::to_string(turned(tail)) + " degrees, tip " + std::to_string(worldTip(tail).y - worldHead(tail).y) + ")");
        Skin::still(*body);
        check(turned(tail) < 1e-3f, "still() takes the wobble off");
    }

    // --- LookAt: points its tip at the target ---
    {
        SceneNode* head = addPart(scene, "LookHead", {-20, 5, 0}, {2, 2, 2});
        SceneNode* eye = addBone(scene, head, "Eye", {0, 0, 0}, 1.0f);
        SceneNode* target = addPart(scene, "LookTarget", {-14, 5, 3}, {1, 1, 1});
        eye->rig.type = BoneType::LookAt;
        eye->rig.target = target->id;
        Skin::solve(*head, 1.0f / 60.0f);
        glm::vec3 dir = glm::normalize(worldTip(eye) - worldHead(eye));
        glm::vec3 want = glm::normalize(target->transform.position - worldHead(eye));
        check(glm::dot(dir, want) > 0.999f, "a LookAt bone points at its target");
        eye->rig.influence = 0.5f;
        Skin::solve(*head, 1.0f / 60.0f);
        dir = glm::normalize(worldTip(eye) - worldHead(eye));
        check(glm::dot(dir, want) < 0.99f && dir.x > 0.1f, "Influence 0.5 turns it half way");
        // Things attached to the bone turn with it.
        auto att = std::make_unique<SceneNode>("Pupil", NodeKind::Attachment);
        att->transform.position = {0, 1, 0};
        SceneNode* pupil = scene.insert(std::move(att), eye);
        eye->rig.influence = 1.0f;
        Skin::solve(*head, 1.0f / 60.0f);
        glm::vec3 p(pupil->worldMatrix()[3]);
        check(glm::length(p - worldTip(eye)) < 1e-3f, "an attachment inside the bone turns with it");
    }

    // --- IK: a two-bone arm bends to reach the target ---
    SceneNode* arm = addPart(scene, "IkArm", {0, 5, 20}, {1, 4, 1});
    SceneNode* upper = addBone(scene, arm, "Upper", {0, -2, 0}, 0.0f);
    SceneNode* lower = addBone(scene, upper, "Lower", {0, 2, 0}, 2.0f);
    SceneNode* goal = addPart(scene, "IkGoal", {2.0f, 4.0f, 20.0f}, {0.5f, 0.5f, 0.5f});
    {
        lower->rig.type = BoneType::IK;
        lower->rig.target = goal->id;
        lower->rig.chain = 2;
        Skin::solve(*arm, 1.0f / 60.0f);
        float miss = glm::length(worldTip(lower) - goal->transform.position);
        check(miss < 0.05f, "an IK arm's tip reaches the target (" + std::to_string(miss) + " studs off)");
        check(turned(upper) > 1.0f && turned(lower) > 1.0f, "both bones of the chain bend");
        goal->transform.position = {0.0f, 20.0f, 20.0f};   // too far: it points straight at it
        Skin::solve(*arm, 1.0f / 60.0f);
        glm::vec3 d = glm::normalize(worldTip(lower) - worldHead(upper));
        check(d.y > 0.99f, "out of reach, it stretches straight towards it");
        lower->rig.chain = 1;
        goal->transform.position = {2.0f, 4.0f, 20.0f};
        Skin::solve(*arm, 1.0f / 60.0f);
        check(turned(upper) < 1e-3f, "Chain Length 1 only turns the bone itself");
        lower->rig.chain = 2;
    }

    // --- Saved, copied and sent ---
    {
        auto copy = Serializer::nodeFromString(Serializer::nodeToString(*arm), false);
        SceneNode* l = copy ? copy->findChild("Lower", true) : nullptr;
        check(l && l->rig.type == BoneType::IK && l->rig.target == goal->id && l->rig.chain == 2 &&
              std::abs(l->rig.length - 2.0f) < 1e-4f, "a bone's type and settings are saved");
        SceneNode* rigModel = scene.insert(std::make_unique<SceneNode>("RigModel", NodeKind::Model));
        auto movedArm = scene.root()->detachChild(arm);
        auto movedGoal = scene.root()->detachChild(goal);
        if (movedArm) arm = rigModel->addChild(std::move(movedArm));
        if (movedGoal) goal = rigModel->addChild(std::move(movedGoal));
        auto dup = Serializer::clone(*rigModel);
        SceneNode* dl = dup ? dup->findChild("Lower", true) : nullptr;
        SceneNode* dg = dup ? dup->findChild("IkGoal", true) : nullptr;
        check(dl && dg && dl->rig.target == dg->id, "a copied rig reaches for its own copied target");
        BoneRig plain;
        Serializer::boneRigFromJson(Serializer::boneRigToJson(lower->rig), plain);
        check(plain == lower->rig, "the settings go to other players and back unchanged");
    }

    // --- Scripts ---
    check(run(session,
              "local p = Instance.new('Part') p.Name = 'LuaRig' p.Parent = workspace "
              "local b = Instance.new('Bone') b.Parent = p "
              "assert(b.BoneType == 'Normal') "
              "b.BoneType = 'Jiggle' b.Stiffness = 2 b.Damping = 0.5 b.Gravity = 10 "
              "assert(b.BoneType == 'Jiggle' and b.Stiffness == 1 and b.Damping == 0.5 and b.Gravity == 10) "
              "local t = Instance.new('Part') t.Parent = workspace "
              "b.BoneType = 'IK' b.Target = t b.ChainLength = 3 b.Length = 1.5 "
              "assert(b.Target == t and b.ChainLength == 3 and math.abs(b.Length - 1.5) < 1e-4) "
              "assert(not pcall(function() b.BoneType = 'Wobbly' end)) "
              "assert(b.BoneType == 'IK') b.Target = nil assert(b.Target == nil)", err),
          "scripts set a bone's type, settings and target " + err);

    // --- The weight brush ---
    {
        SceneNode* mp = addPart(scene, "PaintMe", {0, 5, -20}, {1, 4, 1});
        MeshEdit::attach(*mp, MeshEdit::fromPrimitive(PrimitiveType::Cube));
        SceneNode* b1 = addBone(scene, mp, "Root", {0, -2, 0}, 2.0f);
        addBone(scene, b1, "Top", {0, 2, 0}, 2.0f);
        check(Skin::bindAutomatic(*mp), "binding a mesh to two bones");
        const size_t nv = mp->editMesh->verts.size();
        float before = 0.0f, after = 0.0f;
        for (size_t v = 0; v < nv; ++v) before += Skin::weightOf(*mp->editMesh, v, "Top");
        std::vector<float> all(nv, 0.5f);
        check(Skin::paint(*mp, all, "Top", 0), "the Add brush changes weights");
        for (size_t v = 0; v < nv; ++v) after += Skin::weightOf(*mp->editMesh, v, "Top");
        check(after > before, "Add makes the bone pull harder");
        bool sums = true;
        for (size_t v = 0; v < nv; ++v) {
            float s = 0.0f;
            for (auto& w : mp->editMesh->weights[v]) s += w.w;
            if (s > 1.0001f) sums = false;
        }
        check(sums, "every corner's weights still add up to 1 at most");
        Skin::paint(*mp, all, "Top", 1);
        Skin::paint(*mp, all, "Top", 1);
        float less = 0.0f;
        for (size_t v = 0; v < nv; ++v) less += Skin::weightOf(*mp->editMesh, v, "Top");
        check(less < after, "Take Away makes it pull less");
        std::vector<float> none(nv, 0.0f);
        check(!Skin::paint(*mp, none, "Top", 0), "a brush that touches nothing changes nothing");
    }

    session.stop();
    ImGui::DestroyContext();
    std::cout << (failures ? "bone rig tests FAILED\n" : "bone rig tests passed\n");
    return failures ? 1 : 0;
}
