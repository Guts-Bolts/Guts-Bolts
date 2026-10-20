// Armatures in Studio: the Armature window (bones of a MeshPart, binding the
// mesh to them, and painting which corners each bone moves), and adding bones.
// The bending itself is in scene/Skin.cpp.
#include "Editor.h"
#include "panels/OutlinerPanel.h"
#include "../scene/Scene.h"
#include "../scene/EditMesh.h"
#include "../scene/Skin.h"
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
} // namespace

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
    if (!m_showArmature || m_playing) return;
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
            ImGui::TextDisabled("Go into Modeling mode (Tab) to pick corners and choose which bone moves them.");
            m_state.weightBone.clear();
        } else if (!cur) {
            ImGui::TextDisabled("Click a bone in the list above.");
            m_state.weightBone.clear();
        } else {
            bool show = m_state.weightBone == cur->name;
            if (ImGui::Checkbox("Show weights", &show)) m_state.weightBone = show ? cur->name : "";
            if (show) m_state.weightBone = cur->name;   // (follows the picked bone)
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
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Pose bones with the Rotate tool, or key them in the Animation Editor (put the part in a Model). "
                        "Scripts see them as Bone objects.");
    ImGui::PopTextWrapPos();
    ImGui::End();
}
