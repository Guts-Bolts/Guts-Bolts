// File > New from Template: pick Obby, Tycoon, Sword Fight or Racing and get a game that
// already works (see GameTemplates.h), instead of an empty baseplate.
#include "Editor.h"
#include "GameTemplates.h"
#include "../core/Log.h"
#include "../scene/Scene.h"

#include <imgui.h>
#include <string>

void Editor::newFromTemplate(int kind) {
    const auto& list = GameTemplates::list();
    if (kind < 0 || kind >= (int)list.size()) return;
    newScene();
    GameTemplates::build(*m_scene, list[(size_t)kind].kind);
    m_dirty = true;
    m_showTemplates = false;
    Log::system(std::string("Started a new ") + list[(size_t)kind].name + " game from a template. Press Play to try it, then make it yours.");
}

void Editor::renderTemplates() {
    if (!m_showTemplates) return;
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("New from Template", &m_showTemplates, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Start from a game that already works. Everything in it is normal parts and short scripts, so you can change any of it.");
    ImGui::Spacing();
    const auto& list = GameTemplates::list();
    for (int i = 0; i < (int)list.size(); ++i) {
        ImGui::PushID(i);
        ImGui::Separator();
        ImGui::TextUnformatted(list[(size_t)i].name);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 330);
        ImGui::TextDisabled("%s", list[(size_t)i].tip);
        ImGui::PopTextWrapPos();
        ImGui::SameLine(360);
        if (ImGui::Button("Use", ImVec2(80, 0))) {
            m_templateKind = i;
            if (m_dirty) { m_pending = Pending::Template; m_openDiscard = true; }
            else newFromTemplate(i);
        }
        ImGui::PopID();
    }
    ImGui::End();
}
