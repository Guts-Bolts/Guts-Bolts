// File > Export As: save the whole game, or what's picked, as another type of file.
// The Roblox ones (.rbxmx / .rbxlx) keep everything; the 3D model ones (.obj, .fbx,
// .glb, .gltf, Roblox .mesh, .stl, .ply, .dae) keep the shapes and their colours,
// for Blender, Unity, a 3D printer and so on.
#include "Editor.h"
#include "ModelExport.h"
#include "../core/FileDialog.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../scene/RobloxFile.h"
#include "../scene/Scene.h"
#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace fs = std::filesystem;

namespace {

bool badName(const std::string& n) { return n.empty() || n.find_first_of("/\\:*?\"<>|") != std::string::npos; }

} // namespace

void Editor::openExportAs(bool selectionOnly) {
    if (m_playing) return;
    m_exportSelection = selectionOnly && !m_scene->selectionRoots().empty();
    auto sel = m_scene->selectionRoots();
    m_exportName = m_exportSelection ? sel.back()->name
                                     : (m_scene->info().title.empty() ? std::string("My Game") : m_scene->info().title);
    if (m_exportFolder.empty()) m_exportFolder = Paths::gamesFolder().string();
    m_openExportAs = true;
}

bool Editor::exportTo(const std::string& path, std::vector<SceneNode*> picked, std::string& message, int* parts, int* triangles) {
    const ModelExport::Format* fmt = ModelExport::formatFor(path);
    if (!fmt) { message = "Export As can't write that type of file (" + fs::path(path).extension().string() + ")."; return false; }
    if (picked.empty()) {
        // The whole game: everything in the Workspace (not the storage services).
        for (auto& c : m_scene->root()->children)
            if (!(c->kind == NodeKind::Model && Scene::isServiceName(c->name)) && !c->internal) picked.push_back(c.get());
    }
    std::string err;
    if (fmt->roblox) {
        const bool ok = fmt->wholeGame ? RobloxFile::exportPlace(*m_scene, path, err)
                                       : RobloxFile::exportModel(*m_scene, picked, path, err);
        if (!ok) { message = "Export failed: " + err; return false; }
        message = std::string("Exported a ") + (fmt->wholeGame ? "Roblox place" : "Roblox model") + " to " + path +
                  " (open it in Roblox Studio)";
        return true;
    }
    std::vector<const SceneNode*> roots(picked.begin(), picked.end());
    int n = 0, tris = 0;
    const bool ok = ModelExport::write(roots, path, err, &n, &tris);
    if (parts) *parts = n;
    if (triangles) *triangles = tris;
    if (!ok) { message = "Export failed: " + err; return false; }
    message = "Exported " + std::to_string(n) + (n == 1 ? " part" : " parts") + " (" + std::to_string(tris) +
              " triangles) as " + fmt->label + " to " + path;
    return true;
}

void Editor::renderExportDialog() {
    const auto& formats = ModelExport::formats();
    if (m_openExportAs) { ImGui::OpenPopup("Export As"); m_openExportAs = false; }
    m_exportFormat = std::clamp(m_exportFormat, 0, (int)formats.size() - 1);

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Export As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const float width = 480.0f;   // the window grows down (not sideways) to fit the words
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);

    const ModelExport::Format& fmt = formats[m_exportFormat];
    const size_t picked = m_scene->selectionRoots().size();

    ImGui::SeparatorText("What to export");
    if (ImGui::RadioButton("The whole game", !m_exportSelection)) m_exportSelection = false;
    ImGui::SameLine();
    ImGui::BeginDisabled(picked == 0 || fmt.wholeGame);
    std::string selLabel = picked == 0 ? std::string("What I picked (nothing picked)")
                                       : "What I picked (" + std::to_string(picked) + (picked == 1 ? " object)" : " objects)");
    if (ImGui::RadioButton(selLabel.c_str(), m_exportSelection && !fmt.wholeGame)) m_exportSelection = true;
    ImGui::EndDisabled();
    if (picked == 0) m_exportSelection = false;

    ImGui::SeparatorText("File type");
    ImGui::SetNextItemWidth(width);
    std::string current = std::string(fmt.label) + "  (" + fmt.ext + ")";
    if (ImGui::BeginCombo("##type", current.c_str(), ImGuiComboFlags_HeightLargest)) {
        for (int i = 0; i < (int)formats.size(); ++i) {
            if (i == 2) ImGui::Separator();   // Roblox files above, 3D model files below
            std::string label = std::string(formats[i].label) + "  (" + formats[i].ext + ")";
            if (ImGui::Selectable(label.c_str(), i == m_exportFormat)) m_exportFormat = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", formats[i].hint);
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted(fmt.hint);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (fmt.wholeGame) ImGui::TextUnformatted("A Roblox place is always the whole game.");
    else if (!fmt.roblox) ImGui::TextUnformatted("Only shapes and colours go in (no scripts, lights or sounds). 1 unit = 1 stud.");
    ImGui::PopStyleColor();

    ImGui::SeparatorText("Where");
    ImGui::Text("Name:");
    ImGui::SameLine(70);
    ImGui::SetNextItemWidth(width - 62);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool enter = ImGui::InputText("##exname", &m_exportName, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Text("Folder:");
    ImGui::SameLine(70);
    ImGui::SetNextItemWidth(FileDialog::available() ? width - 152 : width - 62);
    ImGui::InputText("##exfolder", &m_exportFolder);
    if (FileDialog::available()) {
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(82, 0))) {
            const std::string start = (fs::path(m_exportFolder) / (m_exportName + fmt.ext)).string();
            const std::string p = FileDialog::saveAny("Export As", start, fmt.label, std::string("*") + fmt.ext);
            if (!p.empty()) {
                fs::path chosen(p);
                m_exportFolder = chosen.parent_path().string();
                m_exportName = chosen.stem().string();
                if (const ModelExport::Format* f = ModelExport::formatFor(p))   // they typed another type: use it
                    m_exportFormat = (int)(f - formats.data());
            }
        }
    }
    const ModelExport::Format& now = formats[m_exportFormat];
    const fs::path target = fs::path(m_exportFolder) / (m_exportName + now.ext);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Saves as: %s", target.string().c_str());
    ImGui::PopStyleColor();
    std::error_code ec;
    if (fs::exists(target, ec)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.35f, 1.0f));
        ImGui::TextUnformatted("There's already a file with that name: it will be replaced.");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    const bool valid = !badName(m_exportName) && !m_exportFolder.empty();
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Export", ImVec2(120, 0)) || (enter && valid)) {
        std::string msg;
        std::vector<SceneNode*> picked;
        if (m_exportSelection && !now.wholeGame) picked = m_scene->selectionRoots();
        if (exportTo(target.string(), picked, msg)) Log::system(msg);
        else Log::error(msg);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
}
