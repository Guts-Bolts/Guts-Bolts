// Script History: what the playtest's scripts changed, and which events ran them
// (recorded by ScriptHistory, see src/scripting/ScriptHistory.h).
#include "Editor.h"
#include "panels/ScriptEditorPanel.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scripting/ScriptHistory.h"
#include <algorithm>
#include <cctype>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace {

bool hasText(const std::string& in, const std::string& lowerNeedle) {
    std::string s = in;
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s.find(lowerNeedle) != std::string::npos;
}

} // namespace

void Editor::renderScriptHistory() {
    if (!m_showScriptHistory) return;
    ImGui::SetNextWindowSize(ImVec2(760, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Script History", &m_showScriptHistory)) { ImGui::End(); return; }

    const bool on = ScriptHistory::recording();
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.2f, 0.2f, 1));
        if (ImGui::Button("Stop recording")) ScriptHistory::setRecording(false);
        ImGui::PopStyleColor();
    } else if (ImGui::Button("Record")) {
        ScriptHistory::setRecording(true);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("While it records, playtests write down every property a script sets\nand every event that runs a script (Heartbeat is left out: it's every frame).");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) ScriptHistory::clear();
    ImGui::SameLine();
    if (on) ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.3f, 1), m_playing ? "Recording" : "Recording (press Play)");
    else    ImGui::TextDisabled("Not recording");

    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##historyfilter", "Filter by object, script or property", &m_historyFilter);
    ImGui::SameLine();
    const char* kinds[] = {"Every entry", "Property changes", "Events"};
    ImGui::SetNextItemWidth(150);
    ImGui::Combo("##historykind", &m_historyKind, kinds, 3);
    ImGui::SameLine();
    ImGui::Checkbox("Only the selected object", &m_historySelected);

    // Newest first, filtered.
    std::string needle = m_historyFilter;
    for (char& c : needle) c = (char)std::tolower((unsigned char)c);
    SceneNode* sel = m_scene->selected();
    const auto& all = ScriptHistory::entries();
    std::vector<int> rows;
    for (int i = (int)all.size() - 1; i >= 0; --i) {
        const ScriptHistory::Entry& e = all[i];
        if (m_historyKind == 1 && e.kind != ScriptHistory::Entry::Set) continue;
        if (m_historyKind == 2 && e.kind != ScriptHistory::Entry::Event) continue;
        if (m_historySelected && (!sel || e.object != sel->id)) continue;
        if (!needle.empty() && !hasText(e.objectName, needle) && !hasText(e.scriptName, needle) && !hasText(e.what, needle))
            continue;
        rows.push_back(i);
    }
    ImGui::TextDisabled("%d of %d", (int)rows.size(), (int)all.size());
    ImGui::SameLine();
    ImGui::TextDisabled("Click an object to select it, or a script to open it at that line.");
    ImGui::Separator();

    if (all.empty()) {
        ImGui::TextWrapped("Nothing recorded yet. Press Record, then Play: every time a script changes something, or an event (like Touched or a click) runs a script, it shows up here.");
        ImGui::End();
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##history", 5, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Script", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const ScriptHistory::Entry& e = all[rows[r]];
                const bool set = e.kind == ScriptHistory::Entry::Set;
                ImGui::PushID(rows[r]);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%.2fs", e.time);
                ImGui::TableNextColumn();
                if (set) ImGui::TextUnformatted(e.what.c_str());
                else     ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1), "%s", e.what.c_str());
                ImGui::TableNextColumn();
                SceneNode* obj = e.object ? m_scene->findById(e.object) : nullptr;
                const std::string objLabel = e.objectName.empty() ? std::string("(a service)") : e.objectName;
                if (obj) {
                    if (ImGui::Selectable((objLabel + "##o").c_str(), sel == obj)) m_scene->select(obj);
                } else {
                    ImGui::TextDisabled("%s", objLabel.c_str());
                }
                ImGui::TableNextColumn();
                SceneNode* script = e.script ? m_scene->findById(e.script) : nullptr;
                std::string scriptLabel = e.scriptName.empty() ? std::string("(Command Bar)") : e.scriptName;
                if (set && e.line > 0) scriptLabel += "  :" + std::to_string(e.line);
                if (script) {
                    if (ImGui::Selectable((scriptLabel + "##s").c_str())) {
                        openScript(script);
                        m_scriptEditor->openAtLine(script->id, set ? e.line : 1);
                    }
                } else {
                    ImGui::TextDisabled("%s", scriptLabel.c_str());
                }
                ImGui::TableNextColumn();
                if (set) {
                    ImGui::TextDisabled("%s", e.before.empty() ? "?" : e.before.c_str());
                    ImGui::SameLine(0, 4);
                    ImGui::TextUnformatted("->");
                    ImGui::SameLine(0, 4);
                    ImGui::TextUnformatted(e.after.c_str());
                } else {
                    ImGui::TextDisabled("ran the script");
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
