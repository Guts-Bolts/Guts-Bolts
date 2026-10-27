// The Material Editor: your own materials (MaterialVariants in MaterialService), each made
// from pictures on top of a built-in material, with a live preview ball and presets you
// keep on this computer for other games.
#include "Editor.h"
#include "MaterialUi.h"
#include "PictureImport.h"
#include "Thumbnailer.h"
#include "../core/FileDialog.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../renderer/MeshLibrary.h"
#include "../renderer/Textures.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"
#include <algorithm>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

using nlohmann::json;

namespace {

constexpr const char* kPresetFile = "MaterialPresets.json";

json readPresets() {
    std::string text;
    if (!Serializer::readFile(Paths::file(kPresetFile).string(), text)) return json::array();
    json j = json::parse(text, nullptr, false);
    return j.is_array() ? j : json::array();
}

// A name no other variant has: "Brick", "Brick 2", "Brick 3"...
std::string freeName(SceneNode* service, const std::string& want) {
    auto taken = [&](const std::string& n) {
        if (service) for (auto& c : service->children) if (c->name == n) return true;
        return false;
    };
    if (!taken(want)) return want;
    for (int i = 2;; ++i)
        if (!taken(want + " " + std::to_string(i))) return want + " " + std::to_string(i);
}

} // namespace

namespace MaterialUi {

bool edit(MaterialVariantProps& v) {
    bool changed = false;
    int base = (int)v.base;
    if (ImGui::Combo("BaseMaterial", &base, kMaterialNames, kMaterialCount)) { v.base = (Material)base; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The built-in material it's made on: parts with this Material can use it,\nand they still weigh and grip like it.");
    struct Pic { const char* label; std::string* value; const char* tip; };
    const Pic pics[] = {
        {"ColorMap", &v.colorMap, "The colours (the part's Color tints it: white shows it as it is)."},
        {"NormalMap", &v.normalMap, "Bumps and grooves (a bluish normal map picture)."},
        {"RoughnessMap", &v.roughnessMap, "Where it's rough (white) or shiny (black)."},
        {"MetalnessMap", &v.metalnessMap, "Where it's metal (white) or not (black)."}};
    for (const Pic& p : pics) {
        ImGui::PushID(p.label);
        const float button = ImGui::CalcTextSize("Choose...").x + ImGui::GetStyle().FramePadding.x * 2 + 4;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - button);
        if (ImGui::InputTextWithHint("##pic", "(none)", p.value)) changed = true;
        ImGui::SameLine(0, 4);
        if (ImGui::Button("Choose...")) {
            if (FileDialog::available()) {
                std::string path = FileDialog::openAny("Choose a picture", "Pictures", PictureImport::patterns());
                if (!path.empty()) {
                    std::string err;
                    std::string use = PictureImport::usable(path, err);
                    if (use.empty()) Log::warn(err);
                    else { *p.value = use; Textures::forget(use); changed = true; }
                }
            } else {
                Log::warn("Type the picture's path in the box (this computer has no Open window).");
            }
        }
        ImGui::SameLine(0, 4);
        ImGui::TextUnformatted(p.label);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tip);
        ImGui::PopID();
    }
    if (ImGui::DragFloat("StudsPerTile", &v.studsPerTile, 0.1f, 0.1f, 1000.0f, "%.1f")) changed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How big one copy of the pictures is on a part, in studs.");
    ImGui::BeginDisabled(!v.roughnessMap.empty());
    if (ImGui::SliderFloat("Roughness", &v.roughness, 0.0f, 1.0f)) changed = true;
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!v.metalnessMap.empty());
    if (ImGui::SliderFloat("Metalness", &v.metalness, 0.0f, 1.0f)) changed = true;
    ImGui::EndDisabled();
    if (ImGui::SliderFloat("Glow", &v.glow, 0.0f, 5.0f)) changed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Makes it light up on its own, in its own colours.");
    clampVariant(v);
    return changed;
}

} // namespace MaterialUi

void Editor::renderMaterialEditor() {
    if (!m_showMaterialEditor) return;
    ImGui::SetNextWindowSize(ImVec2(640, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Material Editor", &m_showMaterialEditor)) { ImGui::End(); return; }

    SceneNode* service = m_scene->serviceFolder("MaterialService", false);
    std::vector<SceneNode*> variants;
    if (service) for (auto& c : service->children) if (c->isMaterialVariant()) variants.push_back(c.get());
    SceneNode* cur = m_materialEditing ? m_scene->findById(m_materialEditing) : nullptr;
    if (cur && !cur->isMaterialVariant()) cur = nullptr;
    if (!cur && !variants.empty()) { cur = variants.front(); m_materialEditing = cur->id; }

    auto add = [&](const std::string& name, const MaterialVariantProps& props) {
        SceneNode* svc = m_scene->serviceFolder("MaterialService", true);
        auto n = std::make_unique<SceneNode>(freeName(svc, name), NodeKind::MaterialVariant);
        n->variant = props;
        SceneNode* made = m_scene->insert(std::move(n), svc);
        m_materialEditing = made->id;
        return made;
    };

    // --- Left: the game's materials and your presets ---
    ImGui::BeginChild("##matlist", ImVec2(200, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    ImGui::BeginDisabled(m_playing);
    if (ImGui::Button("New")) cur = add("Material", MaterialVariantProps{});
    ImGui::SameLine();
    ImGui::BeginDisabled(!cur);
    if (ImGui::Button("Copy") && cur) cur = add(cur->name, cur->variant);
    ImGui::SameLine();
    if (ImGui::Button("Delete") && cur) {
        m_scene->removeNode(cur);
        cur = nullptr;
        m_materialEditing = 0;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SeparatorText("In this game");
    if (variants.empty()) ImGui::TextDisabled("None yet. Press New.");
    for (SceneNode* v : variants) {
        ImGui::PushID(v);
        if (ImGui::Selectable(v->name.c_str(), v == cur)) { m_materialEditing = v->id; cur = v; }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", kMaterialNames[(int)v->variant.base]);
        ImGui::PopID();
    }
    ImGui::SeparatorText("Your presets");
    static json presets;
    static bool loaded = false;
    if (!loaded) { presets = readPresets(); loaded = true; }
    if (presets.empty()) ImGui::TextDisabled("Save a material as a preset\nto use it in other games.");
    for (size_t i = 0; i < presets.size(); ++i) {
        const json& p = presets[i];
        ImGui::PushID((int)i);
        ImGui::BeginDisabled(m_playing);
        if (ImGui::SmallButton("Add")) {
            MaterialVariantProps props;
            Serializer::variantFromJson(p.value("variant", json::object()), props);
            cur = add(p.value("name", std::string("Material")), props);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextUnformatted(p.value("name", std::string("?")).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            presets.erase(presets.begin() + (long)i);
            Serializer::writeFile(Paths::file(kPresetFile).string(), presets.dump(1));
            ImGui::PopID();
            break;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forget this preset");
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // --- Right: the one being edited ---
    ImGui::BeginChild("##matedit", ImVec2(0, 0));
    if (!cur) {
        ImGui::TextWrapped("Make your own materials from pictures: colours, bumps, shiny and metal spots. Press New to start, then pick it in a part's MaterialVariant (Properties), or press \"Use on selected parts\".");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(m_playing);
    std::string oldName = cur->name;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60);
    if (ImGui::InputText("Name", &cur->name) && cur->name.empty()) cur->name = oldName;
    if (ImGui::IsItemDeactivatedAfterEdit() && cur->name != m_materialOldName && !m_materialOldName.empty()) {
        // Parts that used the old name follow the new one.
        const std::string from = m_materialOldName, to = cur->name;
        m_scene->forEach([&](SceneNode* n) { if (n->isPart() && n->materialVariant == from) n->materialVariant = to; });
    }
    if (ImGui::IsItemActivated()) m_materialOldName = oldName;

    // A ball and a block with it on, made again whenever it changes.
    const MaterialVariantProps props = cur->variant;
    const std::string name = cur->name;
    const std::string key = Serializer::variantToJson(props).dump() + name;
    unsigned tex = m_thumbnailer->live("material", key, [props, name](Scene& s) {
        SceneNode* svc = s.serviceFolder("MaterialService", true);
        auto v = std::make_unique<SceneNode>(name, NodeKind::MaterialVariant);
        v->variant = props;
        s.insert(std::move(v), svc);
        auto ball = std::make_unique<SceneNode>("Ball", NodeKind::Part);
        ball->primitiveType = PrimitiveType::Sphere;
        ball->mesh = MeshLibrary::get(PrimitiveType::Sphere);
        ball->transform.scale = glm::vec3(3.0f);
        ball->color = glm::vec3(1.0f);
        ball->material = props.base;
        ball->materialVariant = name;
        s.insert(std::move(ball));
    }, 192);
    if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(192, 192), ImVec2(0, 1), ImVec2(1, 0));
    else ImGui::Dummy(ImVec2(192, 192));
    ImGui::SameLine();
    ImGui::BeginGroup();
    int users = 0;
    m_scene->forEach([&](SceneNode* n) { if (n->isPart() && n->materialVariant == cur->name) ++users; });
    ImGui::Text("Used by %d part(s)", users);
    std::vector<SceneNode*> picked;
    for (SceneNode* n : m_scene->selection()) if (n->isPart()) picked.push_back(n);
    ImGui::BeginDisabled(picked.empty());
    if (ImGui::Button("Use on selected parts"))
        for (SceneNode* n : picked) { n->material = cur->variant.base; n->materialVariant = cur->name; }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets their Material to %s and MaterialVariant to this one.", kMaterialNames[(int)cur->variant.base]);
    if (ImGui::Button("Save as preset")) {
        json p = {{"name", cur->name}, {"variant", Serializer::variantToJson(cur->variant)}};
        bool replaced = false;
        for (auto& e : presets) if (e.value("name", std::string()) == cur->name) { e = p; replaced = true; }
        if (!replaced) presets.push_back(p);
        Serializer::writeFile(Paths::file(kPresetFile).string(), presets.dump(1));
        Log::info("Saved \"" + cur->name + "\" as a preset: the Material Editor can add it to any game.");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep it on this computer, for your other games.");
    if (ImGui::Button(cur->variant.graph.empty() ? "Make with nodes..." : "Edit nodes...")) openMaterialGraph(cur->id);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Make its pictures from boxes wired together (noise, bricks, cells, stripes...)\ninstead of photos.");
    ImGui::EndGroup();
    ImGui::Separator();
    ImGui::PushItemWidth(-110);
    MaterialUi::edit(cur->variant);
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextDisabled("Pictures tile on every side of a part. They work like a Decal's: an uploaded picture (gb:...) or a file.");
    ImGui::EndChild();
    ImGui::End();
}
