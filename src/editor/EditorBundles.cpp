// AVATAR > Bundles: Guts&Bolts staff make bundles with a new body here. Change the
// mannequin's Head, Torso, arms and legs (Modeling mode, or swap in any MeshPart), pick
// catalog items to go with it, and upload: anyone who wears the bundle gets that body.
#include "Editor.h"
#include "../core/Log.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"
#include "../scene/Scene.h"
#include "../scene/EditMesh.h"
#include "../scene/Player.h"
#include "../scene/PlayerModel.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <sstream>

using json = nlohmann::json;

namespace {
const char* const kMannequin = "Bundle Mannequin";

// Is this part still the normal body part (same shape, size and spot)?
bool standard(const SceneNode& part) {
    glm::vec3 pos, size;
    if (part.primitiveType != PrimitiveType::Mesh || !PlayerModel::nameOf(part.editMesh.get())) return false;
    if (!PlayerModel::placement(part.name, pos, size)) return true;
    return glm::length(part.transform.position - pos) < 1e-3f && glm::length(part.transform.scale - size) < 1e-3f;
}

void putBack(SceneNode& part) {   // the normal shape, size and spot
    glm::vec3 pos, size;
    if (!PlayerModel::placement(part.name, pos, size) || !PlayerModel::apply(part)) return;
    part.transform.position = pos;
    part.transform.scale = size;
}

// "123, 456 789" -> {"123", "456", "789"}
std::vector<std::string> itemIds(const std::string& text) {
    std::vector<std::string> out;
    std::string word;
    std::istringstream in(text);
    while (std::getline(in, word, ',')) {
        std::istringstream words(word);
        std::string w;
        while (words >> w) out.push_back(w);
    }
    return out;
}
} // namespace

void Editor::renderBundleWindow() {
    if (!m_showBundles) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 590, vp->WorkPos.y + 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Bundles", &m_showBundles)) { ImGui::End(); return; }
    ImGui::PushTextWrapPos(0);
    if (!Online::online() || !Online::staff()) {
        ImGui::TextDisabled("Bundles are made by Guts&Bolts staff. Sign in with a staff account to make one.");
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("1. Add the mannequin.  2. Change its body parts: Edit one in Modeling mode, or build a "
                        "MeshPart, select it and press Use Selected.  3. Add catalog items if you like, then upload.");
    ImGui::PopTextWrapPos();

    // 1. The mannequin.
    SceneNode* mannequin = m_scene->root()->findChild(kMannequin);
    ImGui::SeparatorText("Mannequin");
    if (!mannequin) {
        if (ImGui::Button("Add mannequin")) {
            SceneNode* rig = Player::buildRig(*m_scene, kMannequin, glm::vec3(0.0f));
            m_scene->select(rig);
            Log::system("Added the bundle mannequin. Change its body parts next.");
        }
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("\"%s\" is in the Workspace.", kMannequin);

    // 2. Its body parts.
    ImGui::SeparatorText("Body");
    SceneNode* picked = m_scene->selected();
    const bool canUse = picked && picked->isPart() && picked->parent != mannequin && picked->primitiveType != PrimitiveType::None;
    int changed = 0;
    if (ImGui::BeginTable("##bodyparts", 3, ImGuiTableFlags_SizingFixedFit)) {
        for (const char* name : Online::kBodyParts) {
            SceneNode* part = mannequin->findChild(name);
            if (!part) continue;
            ImGui::PushID(name);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            const bool same = standard(*part);
            if (!same) ++changed;
            if (same) ImGui::TextDisabled("Normal");
            else ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.4f, 1), "New (%d corners)", part->editMesh ? (int)part->editMesh->verts.size() : 0);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Select")) {
                m_scene->select(part);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select this body part to adjust its position, rotation and size below.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit")) {
                m_scene->select(part);
                setMode(StudioMode::Modeling);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reshape it in Modeling mode (Tab goes back)");
            ImGui::SameLine();
            ImGui::BeginDisabled(!canUse);
            if (ImGui::SmallButton("Use Selected")) {
                // The selected part's shape and size, in this part's spot.
                std::shared_ptr<EditMesh> m = picked->primitiveType == PrimitiveType::Mesh && picked->editMesh
                                                  ? std::make_shared<EditMesh>(*picked->editMesh)
                                                  : MeshEdit::fromPrimitive(picked->primitiveType);
                part->primitiveType = PrimitiveType::Mesh;
                MeshEdit::attach(*part, m);
                part->transform.scale = picked->transform.scale;
                m_scene->markDirty();
                m_bundleMsg = std::string(name) + " now has " + picked->name + "'s shape.";
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Select a part (not on the mannequin) first: it gives this body part its shape and size");
            ImGui::SameLine();
            ImGui::BeginDisabled(same);
            if (ImGui::SmallButton("Reset")) { putBack(*part); m_scene->markDirty(); }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Give body parts direct transform controls so small placement changes don't require
    // repeatedly entering and leaving Modeling mode.
    SceneNode* bodySelected = m_scene->selected();
    if (bodySelected && bodySelected->parent == mannequin && bodySelected->isPart()) {
        ImGui::SeparatorText(("Position: " + bodySelected->name).c_str());
        bool transformChanged = false;
        transformChanged |= ImGui::DragFloat3("Position", &bodySelected->transform.position.x, 0.01f, -100.0f, 100.0f, "%.3f");
        transformChanged |= ImGui::DragFloat3("Rotation", &bodySelected->transform.rotation.x, 0.5f, -360.0f, 360.0f, "%.1f deg");
        transformChanged |= ImGui::DragFloat3("Size", &bodySelected->transform.scale.x, 0.01f, 0.001f, 100.0f, "%.3f");
        if (ImGui::Button("Reset position and rotation")) {
            glm::vec3 pos, size;
            if (PlayerModel::placement(bodySelected->name, pos, size)) bodySelected->transform.position = pos;
            bodySelected->transform.rotation = glm::vec3(0.0f);
            transformChanged = true;
        }
        if (transformChanged) m_scene->markDirty();
    } else {
        ImGui::TextDisabled("Select a body part to edit its position, rotation and size here.");
    }

    // 3. What else comes with it.
    ImGui::SeparatorText("In the bundle");
    ImGui::Checkbox("Headless (hides the head and face)", &m_bundleHeadless);
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("Items", "catalog item IDs, like 12, 34", &m_bundleItems);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up to %d catalog items (hats, clothes...). Buying the bundle gives these too.", Online::kBundleMost);

    // 4. Uploading.
    ImGui::SeparatorText("Upload to the catalog");
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("Name", "Robot Body", &m_bundleName);
    ImGui::InputTextMultiline("Description", &m_bundleDesc, ImVec2(-90, 50));
    ImGui::SetNextItemWidth(140);
    if (ImGui::InputInt("Price (Bolts)", &m_bundlePrice, 5, 50)) m_bundlePrice = std::clamp(m_bundlePrice, 0, 1000000);
    const std::vector<std::string> items = itemIds(m_bundleItems);
    const bool something = changed > 0 || m_bundleHeadless || !items.empty();
    ImGui::BeginDisabled(m_onlineBusy || !something || m_bundleName.empty());
    if (ImGui::Button("Upload", ImVec2(140, 30))) {
        json body = json::object();
        for (const char* name : Online::kBodyParts)
            if (SceneNode* part = mannequin->findChild(name); part && !standard(*part)) {
                MeshEdit::fit(*part);   // corners inside -0.5..0.5, like every saved mesh
                body[name] = Player::bodyPartJson(*part);
            }
        json tidy;
        std::string problem = body.empty() ? std::string() : Online::bundleBody(body, tidy);
        if (!problem.empty()) {
            m_bundleMsg = problem;
        } else {
            json meta = {{"items", items}};
            if (m_bundleHeadless) meta["headless"] = true;
            json file = {{"format", "gbbundle"}};
            if (!tidy.empty()) { meta["body"] = true; file["body"] = tidy; }
            json args = {{"kind", "bundle"}, {"name", m_bundleName}, {"description", m_bundleDesc}, {"price", m_bundlePrice},
                         {"meta", meta}, {"data", Online::base64Encode(file.dump())}};
            m_onlineBusy = true;
            m_bundleMsg = "Uploading...";
            const std::string name = m_bundleName;
            Online::request("upload", args, [this, name](const json& r) {
                m_onlineBusy = false;
                if (!r.value("ok", false)) { m_bundleMsg = r.value("error", std::string("The upload didn't work.")); return; }
                m_bundleMsg = "Uploaded \"" + name + "\" to the catalog!";
                Log::system("Uploaded the bundle \"" + name + "\" (" + r["asset"].value("id", std::string()) + ")");
            }, 120);
        }
    }
    ImGui::EndDisabled();
    if (!something) { ImGui::SameLine(); ImGui::TextDisabled("Change a body part or add items first."); }
    else if (m_bundleName.empty()) { ImGui::SameLine(); ImGui::TextDisabled("Give it a name."); }

    // Publish one selected rig part as its own catalog item, so players can mix
    // and match heads, torsos, arms and legs without buying/wearing a whole bundle.
    const bool selectedRigPart = bodySelected && bodySelected->parent == mannequin && bodySelected->isPart() && bodySelected->editMesh;
    ImGui::BeginDisabled(m_onlineBusy || !selectedRigPart || m_bundleName.empty());
    if (ImGui::Button("Upload Selected Part", ImVec2(180, 30))) {
        MeshEdit::fit(*bodySelected);
        json body = {{bodySelected->name, Player::bodyPartJson(*bodySelected)}};
        json tidy;
        std::string problem = Online::bundleBody(body, tidy);
        if (!problem.empty()) {
            m_bundleMsg = problem;
        } else {
            const std::string name = m_bundleName + " " + bodySelected->name;
            json file = {{ "format", "gbbundle" }, { "body", tidy }};
            json meta = {{ "bodyPart", bodySelected->name }};
            json args = {{ "kind", "bodypart" }, { "name", name }, { "description", m_bundleDesc },
                         { "price", m_bundlePrice }, { "meta", meta }, { "data", Online::base64Encode(file.dump()) }};
            m_onlineBusy = true;
            m_bundleMsg = "Uploading standalone " + bodySelected->name + "...";
            Online::request("upload", args, [this, name](const json& r) {
                m_onlineBusy = false;
                if (!r.value("ok", false)) { m_bundleMsg = r.value("error", std::string("The upload didn't work.")); return; }
                m_bundleMsg = "Uploaded standalone body part \"" + name + "\"!";
                Log::system("Uploaded body part \"" + name + "\" (" + r["asset"].value("id", std::string()) + ")");
            }, 120);
        }
    }
    ImGui::EndDisabled();
    if (!selectedRigPart) { ImGui::SameLine(); ImGui::TextDisabled("Select a body part on the mannequin to upload it separately."); }
    if (!m_bundleMsg.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(m_bundleMsg.c_str()); ImGui::PopTextWrapPos(); }
    ImGui::End();
}
