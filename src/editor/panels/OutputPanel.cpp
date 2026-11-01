#include "OutputPanel.h"
#include "../LearnHighlight.h"
#include "../../core/Log.h"

#include <imgui.h>

void OutputPanel::render() {
    ImGui::Begin("Output");

    if (ImGui::Button("Clear")) Log::clear();
    ImGui::SameLine();
    ImGui::Checkbox("Messages", &m_showInfo);   ImGui::SameLine();
    ImGui::Checkbox("Warnings", &m_showWarn);   ImGui::SameLine();
    ImGui::Checkbox("Errors",   &m_showError);  ImGui::SameLine();
    ImGui::Checkbox("Editor",   &m_showSystem); ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    ImGui::Separator();

    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    const auto& entries = Log::entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const Log::Entry& e = entries[i];
        ImVec4 col;
        switch (e.level) {
            case Log::Level::Info:   if (!m_showInfo)   continue; col = {0.90f, 0.91f, 0.93f, 1}; break;
            case Log::Level::Warn:   if (!m_showWarn)   continue; col = {1.00f, 0.72f, 0.25f, 1}; break;
            case Log::Level::Error:  if (!m_showError)  continue; col = {1.00f, 0.40f, 0.38f, 1}; break;
            default:                 if (!m_showSystem) continue; col = {0.50f, 0.66f, 1.00f, 1}; break;
        }
        ImGui::PushID((int)i);
        ImGui::TextDisabled("%s", e.time.c_str());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(e.text.c_str());
        if (e.level == Log::Level::Error) StudioLearnHighlight::mark("output.errors");
        if (e.level == Log::Level::Info) StudioLearnHighlight::mark("output.messages");
        ImGui::PopStyleColor();
        if (ImGui::BeginPopupContextItem("##line")) {
            if (ImGui::MenuItem("Copy")) ImGui::SetClipboardText(e.text.c_str());
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (m_autoScroll && entries.size() != m_lastCount) ImGui::SetScrollHereY(1.0f);
    m_lastCount = entries.size();
    ImGui::EndChild();

    ImGui::End();
}
