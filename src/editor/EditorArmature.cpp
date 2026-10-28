// Armatures in Studio: the Armature window (bones of a MeshPart, binding the
// mesh to them, and painting which corners each bone moves), adding bones and
// ready-made rigs, and a bone's type (Jiggle / LookAt / IK; BoneUi.h).
// The bending itself is in scene/Skin.cpp.
#include "Editor.h"
#include "BoneUi.h"
#include "panels/OutlinerPanel.h"
#include "../scene/Scene.h"
#include "../scene/EditMesh.h"
#include "../scene/Skin.h"
#include "../renderer/MeshLibrary.h"
#include "../core/Log.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cstdio>
#include <set>

namespace {
// Turn a place (studs space of the part, or of a parent bone) into a bone's Position / Orientation.
void placeBone(SceneNode* b, const glm::mat4& local) {
    float z, y, x;
    glm::extractEulerAngleZYX(local, z, y, x);
    b->transform.position = glm::vec3(local[3]);
    b->transform.rotation = glm::degrees(glm::vec3(x, y, z));
    b->transform.scale = glm::vec3(1.0f);
}
// A turn that points +Y along `dir`.
glm::mat4 pointing(glm::vec3 from, glm::vec3 dir) {
    glm::vec3 y = glm::length(dir) > 1e-6f ? glm::normalize(dir) : glm::vec3(0, 1, 0);
    glm::vec3 x = std::abs(y.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
    glm::vec3 z = glm::normalize(glm::cross(x, y));
    x = glm::cross(y, z);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x, 0); m[1] = glm::vec4(y, 0); m[2] = glm::vec4(z, 0); m[3] = glm::vec4(from, 1);
    return m;
}
// A new bone in `part` (inside `parentBone`, or right in the part) from `from` to `to`
// (studs space of the part).
SceneNode* makeBone(Scene& scene, SceneNode* part, SceneNode* parentBone, const std::string& name, glm::vec3 from, glm::vec3 to) {
    auto b = std::make_unique<SceneNode>(name, NodeKind::Attachment);
    b->bone = true;
    glm::mat4 parentFrame = parentBone ? Skin::frame(parentBone, part) : glm::mat4(1.0f);
    placeBone(b.get(), glm::inverse(parentFrame) * pointing(from, to - from));
    b->rig.length = std::max(0.05f, glm::length(to - from));
    return scene.insert(std::move(b), parentBone ? parentBone : part);
}
} // namespace

namespace BoneUi {

SceneNode* makeTarget(Scene& scene, SceneNode& bone) {
    SceneNode* part = Skin::partOf(&bone);
    if (!part) return nullptr;
    // At the bone's tip (a bit further out for a look), in the world.
    glm::vec3 local = Skin::localTip(&bone);
    if (bone.rig.type == BoneType::LookAt) local *= 4.0f;
    glm::vec3 at(Skin::partSpace(part) * Skin::frame(&bone, part) * glm::vec4(local, 1.0f));
    auto t = std::make_unique<SceneNode>(bone.name + "Target", NodeKind::Part);
    t->primitiveType = PrimitiveType::Sphere;
    t->mesh = MeshLibrary::get(PrimitiveType::Sphere);
    t->transform.scale = glm::vec3(0.4f);
    t->color = bone.rig.type == BoneType::IK ? glm::vec3(1.0f, 0.55f, 0.15f) : glm::vec3(0.3f, 0.8f, 1.0f);
    t->transparency = 0.4f;
    t->anchored = true;
    t->canCollide = false;
    t->castShadow = false;
    // Next to the part (in the same Model, so they move together), or in the world.
    SceneNode* where = part->parent && part->parent->kind != NodeKind::Part ? part->parent : nullptr;
    const glm::mat4 toLocal = where ? glm::inverse(where->worldMatrix()) : glm::mat4(1.0f);
    t->transform.position = glm::vec3(toLocal * glm::vec4(at, 1.0f));
    SceneNode* made = scene.insert(std::move(t), where);
    bone.rig.target = made->id;
    return made;
}

bool edit(Scene& scene, SceneNode& bone) {
    BoneRig& r = bone.rig;
    bool changed = false;
    auto tip = [](const char* text) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text); };
    int type = (int)r.type;
    const char* types[] = {"Normal", "Jiggle", "LookAt", "IK"};
    if (ImGui::Combo("Bone Type", &type, types, 4)) { r.type = (BoneType)type; changed = true; }
    tip("Normal: stays where you pose it.\n"
        "Jiggle: wobbles on a spring when the part moves (tails, ears, hair, belly).\n"
        "LookAt: turns to point at its Target (eyes, heads, turrets).\n"
        "IK: bends the bones it's in so its tip reaches its Target (arms, legs).");
    if (ImGui::DragFloat("Length", &r.length, 0.02f, 0.0f, 1000.0f, r.length > 0.0f ? "%.2f studs" : "auto")) changed = true;
    tip("How long the bone is when no bone is inside it (0 = guess). Binding uses it.");
    switch (r.type) {
        case BoneType::Jiggle:
            changed |= ImGui::SliderFloat("Stiffness", &r.stiffness, 0.0f, 1.0f, "%.2f");
            tip("How hard it springs back: low = floppy, high = stiff");
            changed |= ImGui::SliderFloat("Damping", &r.damping, 0.0f, 1.0f, "%.2f");
            tip("How quickly the wobble dies down: low = bouncy, high = calm");
            changed |= ImGui::DragFloat("Gravity", &r.gravity, 0.5f, -500.0f, 500.0f, "%.1f");
            tip("Pulls the tip down (studs/s²): droopy ears, hanging hair. 0 = none");
            break;
        case BoneType::LookAt:
        case BoneType::IK: {
            SceneNode* t = r.target ? scene.findById(r.target) : nullptr;
            std::string label = t ? (t->parent && t->parent->parent ? t->parent->name + "." + t->name : t->name) : std::string("(none)");
            if (ImGui::BeginCombo("Target", label.c_str())) {
                if (ImGui::Selectable("(none)", !t)) { r.target = 0; changed = true; }
                int shown = 0;
                scene.forEach([&](SceneNode* n) {
                    if (shown > 400 || n == &bone || n->internal) return;
                    if (!n->isPart() && !n->isAttachment()) return;
                    if (scene.isCharacterPart(n)) return;
                    ++shown;
                    std::string name = n->parent && n->parent->parent ? n->parent->name + "." + n->name : n->name;
                    ImGui::PushID((void*)n);
                    if (ImGui::Selectable(name.c_str(), n == t)) { r.target = n->id; changed = true; }
                    ImGui::PopID();
                });
                ImGui::EndCombo();
            }
            tip(r.type == BoneType::IK ? "What its tip reaches for: move this and the arm follows"
                                       : "What it points at");
            if (ImGui::Button("Make Target")) { makeTarget(scene, bone); changed = true; }
            tip("A small see-through ball at the bone's tip to use as its Target.\nMove it with the Move tool.");
            if (r.type == BoneType::IK) {
                changed |= ImGui::SliderInt("Chain Length", &r.chain, 1, 8);
                tip("How many bones bend to reach: this one and the ones it's in.\n2 for an arm (upper + lower), 3 with a hand");
            }
            changed |= ImGui::SliderFloat("Influence", &r.influence, 0.0f, 1.0f, "%.2f");
            tip("How much of the move it does: 1 = all of it, 0 = none");
            break;
        }
        default:
            break;
    }
    if (r.type == BoneType::Jiggle) {
        changed |= ImGui::SliderFloat("Influence", &r.influence, 0.0f, 1.0f, "%.2f");
        tip("How much of the wobble shows: 1 = all of it, 0 = none");
    }
    return changed;
}

} // namespace BoneUi

SceneNode* Editor::addRig(SceneNode* part, int kind, int chain) {
    if (!part || !part->isPart()) { Log::warn("Rigs go inside a part: pick a MeshPart first."); return nullptr; }
    const glm::vec3 S = part->transform.scale;
    std::set<std::string> taken;
    for (SceneNode* b : Skin::bones(part)) taken.insert(b->name);
    auto name = [&](const std::string& want) {
        std::string n = want;
        for (int i = 2; taken.count(n); ++i) n = want + std::to_string(i);
        taken.insert(n);
        return n;
    };
    Scene& sc = *m_scene;
    SceneNode* last = nullptr;
    Skin::still(*part);   // (placed from where the bones are posed, not mid-wobble)
    // Along the part's longest side, from one end (like Add Chain).
    int ax = S.x > S.y && S.x > S.z ? 0 : (S.y >= S.z ? 1 : 2);
    glm::vec3 dir(0.0f);
    dir[ax] = 1.0f;
    const glm::vec3 start = -dir * S[ax] * 0.5f;
    switch (kind) {
        case 0: {   // a wobbly chain: tails, hair, antennas, ropes
            int n = std::clamp(chain, 1, 12);
            float step = S[ax] / (float)n;
            for (int i = 0; i < n; ++i) {
                last = makeBone(sc, part, last, name(n == 1 ? "Jiggle" : "Tail"), start + dir * step * (float)i, start + dir * step * (float)(i + 1));
                last->rig.type = BoneType::Jiggle;
                last->rig.stiffness = 0.45f - 0.25f * (float)i / (float)n;   // floppier towards the end
            }
            break;
        }
        case 1: {   // an arm or a leg that reaches for a target
            SceneNode* upper = makeBone(sc, part, nullptr, name("Upper"), start, start + dir * S[ax] * 0.5f);
            last = makeBone(sc, part, upper, name("Lower"), start + dir * S[ax] * 0.5f, start + dir * S[ax]);
            last->rig.type = BoneType::IK;
            last->rig.chain = 2;
            BoneUi::makeTarget(sc, *last);
            break;
        }
        case 2: {   // one bone at the top that looks at a target (a head, an eye, a turret)
            last = makeBone(sc, part, nullptr, name("Look"), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -std::max(0.5f, S.z * 0.5f)));
            last->rig.type = BoneType::LookAt;
            BoneUi::makeTarget(sc, *last);
            break;
        }
        case 3: {   // a whole person: hips, spine, head, arms and legs, fitted to the part's box
            const float H = S.y, W = S.x, D = S.z;
            auto y = [&](float t) { return -H * 0.5f + t * H; };
            SceneNode* hips  = makeBone(sc, part, nullptr, name("Hips"),  {0, y(0.50f), 0}, {0, y(0.60f), 0});
            SceneNode* spine = makeBone(sc, part, hips,    name("Spine"), {0, y(0.60f), 0}, {0, y(0.72f), 0});
            SceneNode* chest = makeBone(sc, part, spine,   name("Chest"), {0, y(0.72f), 0}, {0, y(0.82f), 0});
            SceneNode* neck  = makeBone(sc, part, chest,   name("Neck"),  {0, y(0.82f), 0}, {0, y(0.87f), 0});
            last = makeBone(sc, part, neck, name("Head"), {0, y(0.87f), 0}, {0, y(1.0f), 0});
            const bool tPose = W > H * 0.6f;   // arms out to the sides, or down
            for (int side = -1; side <= 1; side += 2) {
                const std::string s = side < 0 ? "Left" : "Right";   // (a character faces -Z: its left is -X)
                const float x = (float)side;
                glm::vec3 sh, el, wr, end;
                if (tPose) {
                    sh = {x * W * 0.10f, y(0.80f), 0}; el = {x * W * 0.28f, y(0.80f), 0};
                    wr = {x * W * 0.42f, y(0.80f), 0}; end = {x * W * 0.5f, y(0.80f), 0};
                } else {
                    sh = {x * W * 0.36f, y(0.80f), 0}; el = {x * W * 0.40f, y(0.63f), 0};
                    wr = {x * W * 0.42f, y(0.48f), 0}; end = {x * W * 0.42f, y(0.41f), 0};
                }
                SceneNode* ua = makeBone(sc, part, chest, name(s + "UpperArm"), sh, el);
                SceneNode* la = makeBone(sc, part, ua, name(s + "LowerArm"), el, wr);
                makeBone(sc, part, la, name(s + "Hand"), wr, end);
                const float lx = x * std::min(W * 0.25f, H * 0.09f);
                SceneNode* ul = makeBone(sc, part, hips, name(s + "UpperLeg"), {lx, y(0.50f), 0}, {lx, y(0.27f), 0});
                SceneNode* ll = makeBone(sc, part, ul, name(s + "LowerLeg"), {lx, y(0.27f), 0}, {lx, y(0.05f), 0});
                makeBone(sc, part, ll, name(s + "Foot"), {lx, y(0.05f), 0}, {lx, y(0.0f), -std::max(D * 0.4f, H * 0.06f)});
            }
            break;
        }
        default:
            return nullptr;
    }
    m_outliner->expand({part});
    if (last) m_armBone = last->id;
    return last;
}

SceneNode* Editor::armaturePart() {
    SceneNode* n = m_state.mode == StudioMode::Modeling ? m_scene->findById(m_state.modeling.node) : m_scene->selected();
    if (n && n->isBone()) n = Skin::partOf(n);
    if (!n && m_armBone) if (SceneNode* b = m_scene->findById(m_armBone)) n = Skin::partOf(b);
    return n && n->isPart() && !m_scene->isCharacterPart(n) && canEdit(n) ? n : nullptr;
}

SceneNode* Editor::addBone(SceneNode* where, int chain) {
    SceneNode* part = where && where->isBone() ? Skin::partOf(where) : where;
    if (!part || !part->isPart()) { Log::warn("Bones go inside a part: pick a MeshPart (or one of its bones) first."); return nullptr; }
    SceneNode* parentBone = where && where->isBone() ? where : nullptr;
    // A name no other bone of this part has.
    std::set<std::string> taken;
    for (SceneNode* b : Skin::bones(part)) taken.insert(b->name);
    auto freshName = [&] {
        std::string name = "Bone";
        for (int i = 2; taken.count(name); ++i) name = "Bone" + std::to_string(i);
        taken.insert(name);
        return name;
    };
    const glm::vec3 S = part->transform.scale;
    // Where the first one starts (studs space of the part) and which way the bones go.
    glm::vec3 from, dir;
    float length;
    if (parentBone) {
        glm::mat4 f = Skin::frame(parentBone, part);
        from = Skin::tip(parentBone, part);
        dir = glm::vec3(f[1]);
        length = std::max(0.25f, glm::length(from - glm::vec3(f[3])));
    } else {
        // Along the part's longest side, from one end.
        int ax = S.x > S.y && S.x > S.z ? 0 : (S.y >= S.z ? 1 : 2);
        dir = glm::vec3(0.0f);
        dir[ax] = 1.0f;
        from = -dir * S[ax] * 0.5f;
        length = S[ax] / (float)std::max(chain, 1);
    }
    // In Modeling mode with corners picked, a single bone starts at their middle instead.
    if (m_state.mode == StudioMode::Modeling && chain <= 1 && part->editMesh && m_state.modeling.node == part->id) {
        glm::vec3 sum(0.0f);
        int count = 0;
        const auto& v = part->editMesh->verts;
        for (size_t i = 0; i < v.size() && i < m_state.modeling.sel.size(); ++i) if (m_state.modeling.sel[i]) { sum += v[i] * S; ++count; }
        if (count) from = sum / (float)count;
    }
    SceneNode* last = parentBone;
    glm::vec3 at = from;
    for (int i = 0; i < std::max(chain, 1); ++i) {
        auto b = std::make_unique<SceneNode>(freshName(), NodeKind::Attachment);
        b->bone = true;
        glm::mat4 place = pointing(at, dir);
        glm::mat4 parentFrame = last ? Skin::frame(last, part) : glm::mat4(1.0f);
        placeBone(b.get(), glm::inverse(parentFrame) * place);
        last = m_scene->insert(std::move(b), last ? last : part);
        at += dir * length;
    }
    // The last bone's tip: a child-less bone is as long as the one it's in, so a lone
    // first bone gets a stud; that's fine for picking, and binding uses the chain.
    m_outliner->expand({part});
    m_armBone = last->id;
    if (m_state.mode != StudioMode::Modeling) m_scene->select(last);
    return last;
}

void Editor::renderArmature() {
    if (!m_showArmature || m_playing) { m_state.weightPaint = -1; return; }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 8, vp->WorkPos.y + 150), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Armature", &m_showArmature)) { ImGui::End(); return; }
    SceneNode* part = armaturePart();
    ImGui::PushTextWrapPos(0);
    if (!part) {
        ImGui::TextDisabled("Pick a MeshPart (or one of its bones) to give it an armature: bones that bend the mesh, "
                            "like an arm, a tail or a tentacle.");
        ImGui::TextDisabled("Make a part a MeshPart with Tab (Modeling mode).");
        ImGui::PopTextWrapPos();
        m_state.weightBone.clear();
        m_state.weightPaint = -1;
        ImGui::End();
        return;
    }
    std::vector<SceneNode*> bones = Skin::bones(part);
    SceneNode* cur = m_armBone ? m_scene->findById(m_armBone) : nullptr;
    if (cur && (!cur->isBone() || Skin::partOf(cur) != part)) cur = nullptr;
    if (!cur && m_scene->selected() && m_scene->selected()->isBone() && Skin::partOf(m_scene->selected()) == part) cur = m_scene->selected();
    if (cur) m_armBone = cur->id;

    ImGui::Text("%s", part->name.c_str());
    const bool mesh = part->primitiveType == PrimitiveType::Mesh && part->editMesh;
    const bool bound = mesh && part->editMesh->skinned();
    ImGui::SameLine();
    ImGui::TextDisabled(bound ? "(bound to %d bones)" : "(not bound)", bound ? (int)part->editMesh->bones.size() : 0);

    // --- Bones ---
    ImGui::SeparatorText("Bones");
    if (ImGui::BeginChild("##bones", ImVec2(0, 130), ImGuiChildFlags_Borders)) {
        if (bones.empty()) ImGui::TextDisabled("No bones yet.");
        for (SceneNode* b : bones) {
            int depth = 0;
            for (SceneNode* p = b->parent; p && p->isBone(); p = p->parent) ++depth;
            ImGui::PushID((void*)b);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + depth * 12.0f);
            if (ImGui::Selectable(b->name.c_str(), b == cur)) {
                m_armBone = b->id;
                cur = b;
                // In Build mode it's picked too, so the Rotate tool turns it (and bends the mesh).
                if (m_state.mode != StudioMode::Modeling) m_deferred = [this, id = b->id] { m_scene->select(m_scene->findById(id)); };
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (ImGui::Button(cur ? "Add Bone (in this one)" : "Add Bone")) addBone(cur ? cur : part, 1);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A new bone at the end of the picked one (or along the part).\nIn Modeling mode: at the middle of the picked corners.");
    ImGui::SameLine();
    static int chain = 4;
    if (ImGui::Button("Add Chain")) addBone(cur ? cur : part, chain);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Several bones in a row along the part: great for tails, ropes and tentacles");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##chain", &chain, 2, 12);
    ImGui::BeginDisabled(!cur);
    if (ImGui::Button("Delete Bone") && cur) {
        uint64_t id = cur->id;
        m_armBone = 0;
        m_deferred = [this, id] { if (SceneNode* b = m_scene->findById(id)) m_scene->removeNode(b); };
    }
    ImGui::EndDisabled();
    // Ready-made rigs: a few bones already set up, to start from.
    static int rigKind = 0;
    const char* rigs[] = {"Jiggle Chain (tail, hair)", "IK Arm / Leg", "Look At (head, eye)", "Humanoid Skeleton"};
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-60);
    ImGui::Combo("##rig", &rigKind, rigs, 4);
    ImGui::SameLine();
    if (ImGui::Button("Add##rig")) {
        if (SceneNode* b = addRig(part, rigKind, chain)) {
            if (bound) Log::info("Added a rig. Press Bind Again so the mesh follows the new bones.");
            if (m_state.mode != StudioMode::Modeling) m_deferred = [this, id = b->id] { m_scene->select(m_scene->findById(id)); };
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Jiggle Chain: wobbly bones along the part (the Add Chain number).\n"
                          "IK Arm / Leg: two bones that bend to reach a target ball.\n"
                          "Look At: a bone that turns to face a target ball.\n"
                          "Humanoid Skeleton: hips, spine, head, arms and legs fitted to the part.");

    // --- The picked bone's type ---
    if (cur) {
        ImGui::SeparatorText(("Bone: " + cur->name).c_str());
        ImGui::PushItemWidth(-90);
        BoneUi::edit(*m_scene, *cur);
        ImGui::PopItemWidth();
    }
    ImGui::Checkbox("Preview (jiggle, look and reach in Studio)", &m_armPreview);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Jiggle, LookAt and IK bones always move by themselves in the game.\n"
                          "Tick this to see them move here too: drag the part or its target around.");

    // --- Binding ---
    ImGui::SeparatorText("Bind the mesh");
    if (!mesh) {
        ImGui::TextDisabled("Only a MeshPart bends: make this one a MeshPart first (Tab).");
    } else {
        ImGui::BeginDisabled(bones.empty());
        if (ImGui::Button(bound ? "Bind Again" : "Bind (Automatic Weights)")) {
            Skin::bindAutomatic(*part);
            MeshEdit::refresh(*part);
            Log::info("Bound '" + part->name + "' to " + std::to_string(part->editMesh->bones.size()) +
                      " bones. Turn a bone with the Rotate tool to bend it.");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Each corner follows the bones nearest to it, like Blender's\n\"With Automatic Weights\". "
                              "The bones' places now are their rest.");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!bound);
        ImGui::SameLine();
        if (ImGui::Button("Rest Pose")) Skin::resetPose(*part);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put every bone back where it was when bound");
        ImGui::SameLine();
        if (ImGui::Button("Unbind")) Skin::unbind(*part);
        ImGui::EndDisabled();
    }

    // --- Weights (Modeling mode) ---
    if (mesh) {
        ImGui::SeparatorText("Weights");
        const bool modeling = m_state.mode == StudioMode::Modeling && m_state.modeling.node == part->id;
        if (!modeling) {
            ImGui::TextDisabled("Go into Modeling mode (Tab) to paint which corners each bone moves.");
            m_state.weightBone.clear();
            m_state.weightPaint = -1;
        } else if (!cur) {
            ImGui::TextDisabled("Click a bone in the list above.");
            m_state.weightBone.clear();
            m_state.weightPaint = -1;
        } else {
            bool show = m_state.weightBone == cur->name;
            if (ImGui::Checkbox("Show weights", &show)) m_state.weightBone = show ? cur->name : "";
            if (show || m_state.weightPaint >= 0) m_state.weightBone = cur->name;   // (follows the picked bone)
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Red corners follow this bone all the way, blue ones not at all");
            static float weight = 1.0f;
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##weight", &weight, 0.0f, 1.0f, "Weight %.2f");
            if (ImGui::Button("Assign to Picked")) {
                if (Skin::assign(*part, m_state.modeling.sel, cur->name, weight)) MeshEdit::refresh(*part);
                else Log::warn("Assign: pick some corners first.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                if (Skin::assign(*part, m_state.modeling.sel, cur->name, 0.0f)) MeshEdit::refresh(*part);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The picked corners stop following this bone");
            ImGui::SameLine();
            if (ImGui::Button("Pick Its Corners")) {
                auto& sel = m_state.modeling.sel;
                for (size_t i = 0; i < sel.size(); ++i) sel[i] = Skin::weightOf(*part->editMesh, i, cur->name) > 0.05f;
                m_state.modeling.selectMode = 0;
            }
            // The brush: drag over the mesh to paint this bone's weights.
            ImGui::TextUnformatted("Weight brush:");
            const char* brushes[] = {"Off", "Add", "Take Away", "Smooth"};
            for (int i = 0; i < 4; ++i) {
                if (i) ImGui::SameLine();
                bool on = m_state.weightPaint == i - 1;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(brushes[i])) {
                    m_state.weightPaint = i - 1;
                    if (i > 0) m_state.weightBone = cur->name;   // (painting shows the weights)
                }
                if (on) ImGui::PopStyleColor();
            }
            if (m_state.weightPaint >= 0) {
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##radius", &m_state.weightRadius, 8.0f, 200.0f, "Size %.0f px");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##strength", &m_state.weightStrength, 0.05f, 1.0f, "Strength %.2f");
                ImGui::TextDisabled("Drag over the mesh. Hold Ctrl to take away, Shift to smooth.");
            }
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Pose bones with the Rotate tool, or key them in the Animation Editor (put the part in a Model). "
                        "Scripts see them as Bone objects.");
    ImGui::PopTextWrapPos();
    ImGui::End();
}
