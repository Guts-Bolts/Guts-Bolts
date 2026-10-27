// The Compare and Merge window: what's different between the open game and another
// saved version of it (added, removed, moved and changed objects), and bringing the
// ticked differences in. With the version both started from, it merges two people's
// changes and marks the objects you both changed as conflicts.
#include "Editor.h"
#include "SceneDiff.h"
#include "../core/FileDialog.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"
#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

using nlohmann::json;

namespace {

ImVec4 kindColor(SceneDiff::Change::Kind k) {
    switch (k) {
        case SceneDiff::Change::Added:   return ImVec4(0.45f, 0.85f, 0.5f, 1);
        case SceneDiff::Change::Removed: return ImVec4(1.0f, 0.5f, 0.45f, 1);
        case SceneDiff::Change::Moved:   return ImVec4(0.5f, 0.75f, 1.0f, 1);
        default:                         return ImVec4(0.95f, 0.8f, 0.4f, 1);
    }
}

// A saved game: pick one from the games folder, type a path, or Browse.
void filePicker(const char* id, std::string& path) {
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(-150);
    ImGui::InputTextWithHint("##path", "A saved game (.gbscene)", &path);
    ImGui::SameLine();
    if (ImGui::Button("Games")) ImGui::OpenPopup("##games");
    if (ImGui::BeginPopup("##games")) {
        auto games = Paths::listGames();
        if (games.empty()) ImGui::TextDisabled("No saved games yet.");
        for (const auto& g : games)
            if (ImGui::MenuItem(g.stem().string().c_str())) path = g.string();
        ImGui::EndPopup();
    }
    if (FileDialog::available()) {
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            const std::string p = FileDialog::openAny("Pick a saved game", "Guts and Bolts games", "*.gbscene");
            if (!p.empty()) path = p;
        }
    }
    ImGui::PopID();
}

} // namespace

json Editor::compareWith(const std::string& otherPath, const std::string* basePath) {
    m_compare = SceneDiff::Comparison{};
    m_compareTicked.clear();
    m_compareMessage.clear();
    std::string err;
    const std::string other = SceneDiff::loadText(otherPath, &err);
    if (other.empty()) return {{"error", "Couldn't open " + otherPath + (err.empty() ? "" : ": " + err)}};
    std::string base;
    if (basePath) {
        base = SceneDiff::loadText(*basePath, &err);
        if (base.empty()) return {{"error", "Couldn't open " + *basePath + (err.empty() ? "" : ": " + err)}};
    }
    m_compare = SceneDiff::compare(Serializer::saveScene(*m_scene), other, basePath ? &base : nullptr);
    if (!m_compare.error.empty()) return {{"error", m_compare.error}};
    // Everything that isn't a conflict starts ticked.
    for (const auto& c : m_compare.changes) m_compareTicked.push_back(!c.conflict);
    json list = json::array();
    int conflicts = 0;
    for (size_t i = 0; i < m_compare.changes.size(); ++i) {
        const auto& c = m_compare.changes[i];
        json fields = json::array();
        for (const auto& f : c.fields) fields.push_back({{"name", f.name}, {"before", f.before}, {"after", f.after}});
        json j = {{"index", (int)i}, {"kind", SceneDiff::kindName(c.kind)}, {"name", c.name}, {"path", c.path},
                  {"class", c.className}, {"fields", fields}, {"conflict", c.conflict}};
        if (c.id) j["object_id"] = "#" + std::to_string(c.id);
        if (c.inside) j["inside"] = c.inside;
        if (c.conflict) { j["why"] = c.conflictWhy; ++conflicts; }
        list.push_back(j);
    }
    return {{"added", m_compare.count(SceneDiff::Change::Added)}, {"removed", m_compare.count(SceneDiff::Change::Removed)},
            {"moved", m_compare.count(SceneDiff::Change::Moved)}, {"changed", m_compare.count(SceneDiff::Change::Changed)},
            {"conflicts", conflicts}, {"three_way", basePath != nullptr}, {"changes", list}};
}

json Editor::mergeChosen(const std::vector<int>& chosen) {
    std::vector<std::string> problems;
    const int done = SceneDiff::apply(*m_scene, m_compare, chosen, problems);
    char text[128];
    std::snprintf(text, sizeof text, "Brought in %d change(s) from the other version (one Ctrl+Z puts them back).", done);
    m_compareMessage = text;
    Log::system(text);
    for (const auto& p : problems) Log::warn("Compare and Merge: " + p);
    // What's left is different again: compare once more next time.
    m_compare.changes.clear();
    m_compareTicked.clear();
    return {{"done", done}, {"problems", problems}};
}

void Editor::renderCompare() {
    if (!m_showCompare) return;
    ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Compare and Merge", &m_showCompare)) { ImGui::End(); return; }
    ImGui::TextWrapped("See what's different between this game and another saved version of it, then bring in the changes you want.");
    ImGui::Spacing();
    ImGui::TextUnformatted("The other version");
    filePicker("other", m_compareOther);
    ImGui::Checkbox("I have the version we both started from", &m_compareUseBase);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("For merging two people's work: then only the other person's changes are listed,\nand objects you both changed are marked as conflicts.");
    if (m_compareUseBase) filePicker("base", m_compareBase);
    ImGui::BeginDisabled(m_playing || m_compareOther.empty() || (m_compareUseBase && m_compareBase.empty()));
    static std::string error;
    if (ImGui::Button("Compare")) {
        json d = compareWith(m_compareOther, m_compareUseBase ? &m_compareBase : nullptr);
        error = d.value("error", std::string());
    }
    ImGui::EndDisabled();
    if (!error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.45f, 1), "%s", error.c_str());
    if (!m_compareMessage.empty()) ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1), "%s", m_compareMessage.c_str());

    const auto& changes = m_compare.changes;
    if (!changes.empty()) {
        ImGui::SeparatorText("Differences");
        int conflicts = 0, ticked = 0;
        for (size_t i = 0; i < changes.size(); ++i) {
            conflicts += changes[i].conflict;
            ticked += m_compareTicked[i];
        }
        ImGui::Text("%d added, %d removed, %d moved, %d changed", m_compare.count(SceneDiff::Change::Added),
                    m_compare.count(SceneDiff::Change::Removed), m_compare.count(SceneDiff::Change::Moved),
                    m_compare.count(SceneDiff::Change::Changed));
        if (conflicts) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.5f, 0.45f, 1), "(%d conflicts)", conflicts);
        }
        if (ImGui::SmallButton("Tick all")) std::fill(m_compareTicked.begin(), m_compareTicked.end(), 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("Untick all")) std::fill(m_compareTicked.begin(), m_compareTicked.end(), 0);
        ImGui::BeginChild("##diffs", ImVec2(0, -40), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < changes.size(); ++i) {
            const auto& c = changes[i];
            ImGui::PushID((int)i);
            bool on = m_compareTicked[i];
            if (ImGui::Checkbox("##take", &on)) m_compareTicked[i] = on;
            ImGui::SameLine();
            ImGui::TextColored(kindColor(c.kind), "%-8s", SceneDiff::kindName(c.kind));
            if (ImGui::IsItemHovered()) {
                if (c.kind == SceneDiff::Change::Added) ImGui::SetTooltip("Only in the other version: bringing it in adds it here.");
                else if (c.kind == SceneDiff::Change::Removed) ImGui::SetTooltip("Not in the other version: bringing it in deletes it here.");
                else if (c.kind == SceneDiff::Change::Moved) ImGui::SetTooltip("Somewhere else in the other version: bringing it in moves it there.");
                else ImGui::SetTooltip("Different in the other version: bringing it in makes it like that one.");
            }
            ImGui::SameLine();
            const std::string label = c.id ? c.path : c.name + "  " + c.path;
            const bool open = ImGui::TreeNodeEx("##row", c.fields.empty() ? ImGuiTreeNodeFlags_Leaf : 0, "%s", label.c_str());
            if (ImGui::IsItemClicked() && c.id)
                if (SceneNode* n = m_scene->findById(c.id)) m_scene->select(n);
            if (c.conflict) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.5f, 0.45f, 1), "Conflict");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.conflictWhy.c_str());
            }
            if (c.inside) {
                ImGui::SameLine();
                ImGui::TextDisabled("(+%d inside)", c.inside);
            }
            if (open) {
                for (const auto& f : c.fields)
                    ImGui::TextDisabled("%s: %s  ->  %s", f.name.c_str(), f.before.c_str(), f.after.c_str());
                if (c.conflict) ImGui::TextColored(ImVec4(1, 0.5f, 0.45f, 1), "%s", c.conflictWhy.c_str());
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::BeginDisabled(m_playing || ticked == 0);
        if (ImGui::Button("Bring in the ticked ones")) {
            std::vector<int> chosen;
            for (size_t i = 0; i < changes.size(); ++i) if (m_compareTicked[i]) chosen.push_back((int)i);
            mergeChosen(chosen);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%d of %d", ticked, (int)changes.size());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Makes those objects in this game like the other version's (one Ctrl+Z puts it all back).");
    } else if (m_compare.otherText.size() && m_compareMessage.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1), "No differences: they're the same.");
    }
    ImGui::End();
}
