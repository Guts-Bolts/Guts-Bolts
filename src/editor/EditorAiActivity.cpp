// AI Activity: a log of every tool call an AI made (the Assistant tab or an AI app over
// MCP), a report of everything it changed since the person last pressed Keep, with one
// button to undo all of it, and the person's OK before it deletes or restores things.
#include "Editor.h"
#include "../core/Paths.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>
#include <imgui.h>

using nlohmann::json;

namespace {

constexpr size_t kMaxLog = 300;
constexpr const char* kLogFile = "AiActivity.json";

struct NodeInfo { json shallow; std::string path; uint64_t parent = 0; };

void collect(const json& node, const std::string& parentPath, uint64_t parent, std::map<uint64_t, NodeInfo>& out) {
    if (!node.is_object() || !node.contains("id")) return;
    const uint64_t id = node["id"].get<uint64_t>();
    const std::string path = parentPath.empty() ? node.value("name", std::string()) : parentPath + "." + node.value("name", std::string());
    NodeInfo info;
    info.shallow = node;
    info.shallow.erase("children");
    info.path = path;
    info.parent = parent;
    if (node.contains("children") && node["children"].is_array())
        for (const auto& c : node["children"]) collect(c, parent ? path : std::string(), id, out);   // (no "Workspace." in front)
    out[id] = std::move(info);
}

// What a saved field is called in the Properties panel (the common ones).
std::string fieldName(const std::string& k) {
    static const std::map<std::string, std::string> names = {
        {"pos", "Position"}, {"rot", "Rotation"}, {"size", "Size"}, {"color", "Color"}, {"source", "Code"},
        {"name", "Name"}, {"anchored", "Anchored"}, {"canCollide", "CanCollide"}, {"transparency", "Transparency"},
        {"material", "Material"}, {"shape", "Shape"}, {"attributes", "Attributes"}, {"tags", "Tags"}};
    auto it = names.find(k);
    if (it != names.end()) return it->second;
    std::string out = k;
    if (!out.empty()) out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

int countLines(const std::string& s) { return s.empty() ? 0 : 1 + (int)std::count(s.begin(), s.end(), '\n'); }

// Lines in b that aren't in a (a quick count, not a real diff).
int newLines(const std::string& a, const std::string& b) {
    std::multiset<std::string> old;
    size_t i = 0;
    while (i <= a.size()) { size_t e = a.find('\n', i); if (e == std::string::npos) e = a.size(); old.insert(a.substr(i, e - i)); i = e + 1; }
    int n = 0;
    i = 0;
    while (i <= b.size()) {
        size_t e = b.find('\n', i); if (e == std::string::npos) e = b.size();
        auto it = old.find(b.substr(i, e - i));
        if (it == old.end()) ++n; else old.erase(it);
        i = e + 1;
    }
    return n;
}

// The difference between two saved games: {added, removed, changed, settings}.
json diffScenes(const std::string& before, const std::string& after) {
    json report = {{"added", json::array()}, {"removed", json::array()}, {"changed", json::array()}, {"settings", false}};
    json a = json::parse(before, nullptr, false), b = json::parse(after, nullptr, false);
    if (!a.is_object() || !b.is_object() || !a.contains("workspace") || !b.contains("workspace")) return report;
    std::map<uint64_t, NodeInfo> was, now;
    collect(a["workspace"], "", 0, was);
    collect(b["workspace"], "", 0, now);
    auto item = [](uint64_t id, const NodeInfo& n) {
        return json{{"object_id", "#" + std::to_string(id)}, {"name", n.shallow.value("name", std::string())},
                    {"class", n.shallow.value("kind", std::string())}, {"path", n.path}};
    };
    for (const auto& [id, n] : now) {
        auto it = was.find(id);
        if (it == was.end()) {
            // Only the top of something new (not every part inside a new model).
            if (n.parent && !was.count(n.parent)) continue;
            report["added"].push_back(item(id, n));
            continue;
        }
        const NodeInfo& o = it->second;
        if (o.shallow == n.shallow && o.parent == n.parent) continue;
        json c = item(id, n);
        json props = json::array();
        for (const auto& [k, v] : n.shallow.items())
            if (k != "id" && (!o.shallow.contains(k) || o.shallow[k] != v)) props.push_back(fieldName(k));
        for (const auto& [k, v] : o.shallow.items())
            if (!n.shallow.contains(k)) props.push_back(fieldName(k));
        if (o.parent != n.parent) props.push_back("Parent");
        c["properties"] = props;
        if (n.shallow.contains("source") && o.shallow.contains("source") && n.shallow["source"].is_string()) {
            const std::string s0 = o.shallow["source"].get<std::string>(), s1 = n.shallow["source"].get<std::string>();
            if (s0 != s1) { c["lines_added"] = newLines(s0, s1); c["lines_removed"] = newLines(s1, s0); c["lines"] = countLines(s1); }
        }
        report["changed"].push_back(c);
    }
    for (const auto& [id, o] : was)
        if (!now.count(id) && !(o.parent && !now.count(o.parent) && was.count(o.parent))) report["removed"].push_back(item(id, o));
    a.erase("workspace");
    b.erase("workspace");
    report["settings"] = a != b;
    return report;
}

// Calls that can lose work: they wait for the person's OK (when that's on).
bool needsOk(const std::string& tool, const json& args) {
    if (tool == "delete_object") return true;
    if (tool == "checkpoint") {
        const std::string a = args.value("action", std::string());
        return a == "restore" || a == "delete";
    }
    return false;
}

std::string argsText(const json& args) {
    std::string s = args.dump();
    if (s.size() > 300) s = s.substr(0, 297) + "...";
    return s;
}

std::string clockText(long long t) {
    std::time_t tt = (std::time_t)t;
    std::tm* tm = std::localtime(&tt);
    char buf[32] = "";
    if (tm) std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    return buf;
}

AiToolResult refusal(const std::string& tool, const char* code, const std::string& message, const std::string& suggestion) {
    json d = {{"success", false}, {"operation", tool},
              {"error", {{"code", code}, {"message", message}, {"recoverable", true}, {"suggested_action", suggestion}}}};
    AiToolResult r;
    r.text = d.dump(1);
    r.error = true;
    r.data = std::move(d);
    return r;
}

} // namespace

AiToolResult Editor::runAiToolFrom(const std::string& source, const std::string& name, const json& args) {
    loadAiLog();
    // A file was opened or a new game started: the old report is about another game.
    if (!m_aiBaseline.empty() && m_aiBaselinePath != m_path) m_aiBaseline.clear();

    if (name == "ai_activity") return runAiTool(name, args);   // (reading the log isn't logged)

    AiLogEntry e;
    e.time = (long long)std::time(nullptr);
    e.source = source;
    e.tool = name;
    e.args = argsText(args);

    AiToolResult r;
    const std::string key = name + " " + args.dump();
    if (m_aiAskFirst && needsOk(name, args) && !m_aiAllowed.count(key)) {
        if (m_aiRefused.erase(key)) {
            r = refusal(name, "NOT_ALLOWED", "The person said no to this in Studio.",
                        "Don't do it another way. Ask the person what they want instead.");
        } else {
            const bool waiting = std::any_of(m_aiAsks.begin(), m_aiAsks.end(), [&](const AiAsk& a) { return a.tool + " " + a.args.dump() == key; });
            if (!waiting) {
                std::string what = name == "delete_object" ? "Delete " + args.value("object", std::string("?"))
                                 : "Checkpoint " + args.value("action", std::string()) + " \"" + args.value("name", std::string("default")) + "\"";
                m_aiAsks.push_back({source, name, what, args});
                m_showAiActivity = true;
            }
            r = refusal(name, "NEEDS_PERSON_OK",
                        "Studio is set to ask the person before this. It's waiting in the AI Activity window for them to press Allow.",
                        "Tell the person to press Allow in the AI Activity window, wait for them to say they did, then make exactly the same call again.");
        }
        e.result = r.data["error"].value("code", std::string());
        m_aiLog.push_back(e);
        if (m_aiLog.size() > kMaxLog) m_aiLog.erase(m_aiLog.begin());
        saveAiLog();
        return r;
    }
    m_aiAllowed.erase(key);

    const bool track = !m_playing;
    std::string before = track ? Serializer::saveScene(*m_scene) : std::string();
    r = runAiTool(name, args);
    if (track && !m_playing) {
        std::string after = Serializer::saveScene(*m_scene);
        if (after != before) {
            json d = diffScenes(before, after);
            e.added = (int)d["added"].size();
            e.removed = (int)d["removed"].size();
            e.changed = (int)d["changed"].size();
            e.settings = d["settings"].get<bool>();
            if (m_aiBaseline.empty()) { m_aiBaseline = std::move(before); m_aiBaselinePath = m_path; }
        }
    }
    if (r.error && r.data.is_object() && r.data.contains("error")) e.result = r.data["error"].value("code", std::string("error"));
    else if (r.error) e.result = "error";
    m_aiLog.push_back(e);
    if (m_aiLog.size() > kMaxLog) m_aiLog.erase(m_aiLog.begin());
    saveAiLog();
    return r;
}

void Editor::loadAiLog() {
    if (m_aiLogLoaded) return;
    m_aiLogLoaded = true;
    std::string text;
    if (!Serializer::readFile(Paths::file(kLogFile).string(), text)) return;
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return;
    m_aiAskFirst = j.value("askFirst", true);
    if (j.contains("log") && j["log"].is_array())
        for (const auto& l : j["log"]) {
            if (!l.is_object()) continue;
            AiLogEntry e;
            e.time = l.value("time", 0LL);
            e.source = l.value("source", std::string());
            e.tool = l.value("tool", std::string());
            e.args = l.value("args", std::string());
            e.result = l.value("result", std::string());
            e.added = l.value("added", 0);
            e.removed = l.value("removed", 0);
            e.changed = l.value("changed", 0);
            e.settings = l.value("settings", false);
            m_aiLog.push_back(std::move(e));
        }
}

void Editor::saveAiLog() const {
    json log = json::array();
    for (const AiLogEntry& e : m_aiLog)
        log.push_back({{"time", e.time}, {"source", e.source}, {"tool", e.tool}, {"args", e.args}, {"result", e.result},
                       {"added", e.added}, {"removed", e.removed}, {"changed", e.changed}, {"settings", e.settings}});
    Serializer::writeFile(Paths::file(kLogFile).string(), json{{"askFirst", m_aiAskFirst}, {"log", log}}.dump());
}

json Editor::aiChangeReport() {
    if (!m_aiBaseline.empty() && m_aiBaselinePath != m_path) m_aiBaseline.clear();
    if (m_aiBaseline.empty() || m_playing) {
        json r = {{"added", json::array()}, {"removed", json::array()}, {"changed", json::array()}, {"settings", false}};
        r["pending"] = !m_aiBaseline.empty();
        return r;
    }
    json r = diffScenes(m_aiBaseline, Serializer::saveScene(*m_scene));
    r["pending"] = true;
    return r;
}

void Editor::aiKeepChanges() { m_aiBaseline.clear(); }

bool Editor::aiUndoChanges() {
    if (m_aiBaseline.empty() || m_playing) return false;
    trackChanges();                 // what's there now becomes one undo step (Ctrl+Z brings it back)
    m_undo.push_back(m_committed);
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_redo.clear();
    restore(m_aiBaseline);
    m_aiBaseline.clear();
    return true;
}

void Editor::renderAiActivity() {
    if (!m_showAiActivity) return;
    loadAiLog();
    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AI Activity", &m_showAiActivity)) { ImGui::End(); return; }

    // --- Waiting for the person's OK ---
    if (!m_aiAsks.empty()) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "The AI wants to do this. Is that OK?");
        for (size_t i = 0; i < m_aiAsks.size(); ++i) {
            const AiAsk& a = m_aiAsks[i];
            ImGui::PushID((int)i);
            ImGui::Bullet();
            ImGui::TextWrapped("%s (%s)", a.summary.c_str(), a.source.c_str());
            if (a.tool == "delete_object")
                if (SceneNode* n = m_scene->findById(std::strtoull(a.args.value("object", std::string()).c_str() + 1, nullptr, 10))) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Show")) m_scene->select(n);
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", n->fullName().c_str());
                }
            const std::string key = a.tool + " " + a.args.dump();
            bool answered = false;
            if (ImGui::Button("Allow")) { m_aiAllowed.insert(key); answered = true; }
            ImGui::SameLine();
            if (ImGui::Button("Don't allow")) { m_aiRefused.insert(key); answered = true; }
            ImGui::PopID();
            if (answered) { m_aiAsks.erase(m_aiAsks.begin() + (long)i); break; }
        }
        ImGui::TextDisabled("Then tell the AI you answered, so it tries again.");
        ImGui::Separator();
    }
    if (ImGui::Checkbox("Ask me before the AI deletes things or restores a save point", &m_aiAskFirst)) saveAiLog();

    // --- What it changed ---
    ImGui::SeparatorText("What the AI changed");
    static double lastAt = -10.0;
    static json report;
    if (ImGui::GetTime() - lastAt > 0.5) { report = aiChangeReport(); lastAt = ImGui::GetTime(); }
    const bool pending = report.value("pending", false);
    if (m_playing && pending) {
        ImGui::TextDisabled("Stop the playtest to see the report.");
    } else if (!pending) {
        ImGui::TextWrapped("Nothing to review. When the AI changes your game, everything it did shows up here, and you can undo all of it at once.");
    } else {
        const auto& added = report["added"];
        const auto& removed = report["removed"];
        const auto& changed = report["changed"];
        ImGui::Text("%d added, %d removed, %d changed", (int)added.size(), (int)removed.size(), (int)changed.size());
        if (report.value("settings", false)) {
            ImGui::SameLine();
            ImGui::TextDisabled("(and Lighting or game settings)");
        }
        if (ImGui::Button("Keep these changes")) { aiKeepChanges(); lastAt = -10.0; }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.25f, 0.2f, 1));
        if (ImGui::Button("Undo all of it")) { aiUndoChanges(); lastAt = -10.0; }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Puts the game back how it was before the AI's first change.\nYour own edits since then go too. Ctrl+Z brings it all back.");
        ImGui::BeginChild("##aichanges", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.5f), ImGuiChildFlags_Borders);
        auto row = [&](const json& it, const ImVec4& col, const char* sign, int i) {
            ImGui::PushID(i);
            ImGui::TextColored(col, "%s", sign);
            ImGui::SameLine();
            const std::string id = it.value("object_id", std::string("#0"));
            SceneNode* n = m_scene->findById(std::strtoull(id.c_str() + 1, nullptr, 10));
            std::string label = it.value("path", std::string()) + "  (" + it.value("class", std::string()) + ")";
            if (n) { if (ImGui::Selectable(label.c_str(), m_scene->selected() == n)) m_scene->select(n); }
            else ImGui::TextDisabled("%s", label.c_str());
            if (it.contains("properties") && !it["properties"].empty()) {
                std::string props;
                for (const auto& p : it["properties"]) props += (props.empty() ? "" : ", ") + p.get<std::string>();
                if (it.contains("lines_added"))
                    props += "   (code: +" + std::to_string(it["lines_added"].get<int>()) + " / -" + std::to_string(it["lines_removed"].get<int>()) + " lines)";
                ImGui::TextDisabled("      %s", props.c_str());
            }
            ImGui::PopID();
        };
        int i = 0;
        for (const auto& it : added) row(it, ImVec4(0.45f, 0.9f, 0.5f, 1), "+", i++);
        for (const auto& it : removed) row(it, ImVec4(1, 0.45f, 0.4f, 1), "-", i++);
        for (const auto& it : changed) row(it, ImVec4(0.55f, 0.8f, 1, 1), "~", i++);
        ImGui::EndChild();
    }

    // --- Every call ---
    ImGui::SeparatorText("Everything the AI did");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear log")) { m_aiLog.clear(); saveAiLog(); }
    ImGui::BeginChild("##ailog", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (m_aiLog.empty()) ImGui::TextDisabled("No AI tool calls yet.");
    for (int k = (int)m_aiLog.size() - 1; k >= 0; --k) {
        const AiLogEntry& e = m_aiLog[k];
        ImGui::PushID(k);
        ImGui::TextDisabled("%s", clockText(e.time).c_str());
        ImGui::SameLine();
        if (e.result.empty()) ImGui::TextUnformatted(e.tool.c_str());
        else ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s: %s", e.tool.c_str(), e.result.c_str());
        if (e.added || e.removed || e.changed || e.settings) {
            ImGui::SameLine();
            std::string what;
            if (e.added) what += "+" + std::to_string(e.added) + " ";
            if (e.removed) what += "-" + std::to_string(e.removed) + " ";
            if (e.changed) what += "~" + std::to_string(e.changed) + " ";
            if (e.settings) what += "settings";
            ImGui::TextColored(ImVec4(0.55f, 0.8f, 1, 1), "%s", what.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", e.source.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.args.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}
