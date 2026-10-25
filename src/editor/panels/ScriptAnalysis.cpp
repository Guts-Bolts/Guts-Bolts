// View > Script Analysis, like Roblox Studio's: checks every script without
// running anything. Errors are mistakes Lua can't read at all (a missing "end");
// warnings are names that aren't defined anywhere, which is usually a typo
// ("pirnt" for "print") and would only blow up once the game runs that line.

#include "ScriptEditorPanel.h"
#include "../Theme.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../scripting/ScriptEngine.h"
#include "../../scripting/Luau.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <regex>
#include <unordered_map>
#include <imgui.h>

extern "C" {
#include <lua.h>
}

namespace {

const std::set<std::string>& keywords() {
    static const std::set<std::string> k = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function",
        "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while", "continue"};
    return k;
}

// Every global a game script starts with (game, workspace, Instance, print...).
const std::set<std::string>& knownGlobals(Scene* scene) {
    static std::set<std::string> names;
    if (!names.empty()) return names;
    ScriptEngine engine(scene);
    engine.start(false);
    if (lua_State* L = engine.lua()) {
        lua_pushglobaltable(L);
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING) names.insert(lua_tostring(L, -2));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    for (const char* n : {"script", "self", "_G", "_ENV", "game", "workspace", "Workspace"}) names.insert(n);
    return names;
}

struct Tok { std::string text; int line; bool name; };

// Lua source -> names and punctuation (comments, strings and numbers left out).
std::vector<Tok> tokens(const std::string& s) {
    std::vector<Tok> out;
    int line = 1;
    size_t i = 0, n = s.size();
    auto longBracket = [&](size_t at) -> int {   // "[[", "[=[" ... -> its level, or -1
        if (at >= n || s[at] != '[') return -1;
        size_t j = at + 1;
        while (j < n && s[j] == '=') ++j;
        return j < n && s[j] == '[' ? (int)(j - at - 1) : -1;
    };
    auto skipLong = [&](int level) {   // i is at the opening bracket
        std::string close = "]" + std::string((size_t)level, '=') + "]";
        size_t end = s.find(close, i);
        size_t stop = end == std::string::npos ? n : end + close.size();
        for (; i < stop; ++i) if (s[i] == '\n') ++line;
    };
    while (i < n) {
        char c = s[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        if (c == '-' && i + 1 < n && s[i + 1] == '-') {   // comment
            i += 2;
            int lv = longBracket(i);
            if (lv >= 0) skipLong(lv);
            else while (i < n && s[i] != '\n') ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            for (++i; i < n && s[i] != c && s[i] != '\n'; ++i) if (s[i] == '\\') ++i;
            ++i;
            out.push_back({"\"", line, false});
            continue;
        }
        if (int lv = longBracket(i); lv >= 0) { skipLong(lv); out.push_back({"\"", line, false}); continue; }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)s[i + 1]))) {
            while (i < n && (std::isalnum((unsigned char)s[i]) || s[i] == '.' || s[i] == '_')) ++i;
            out.push_back({"0", line, false});
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t j = i;
            while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '_')) ++j;
            out.push_back({s.substr(i, j - i), line, true});
            i = j;
            continue;
        }
        // Two-character operators that matter here.
        if (i + 1 < n && ((c == '=' && s[i + 1] == '=') || (c == '~' && s[i + 1] == '=') || (c == '<' && s[i + 1] == '=') ||
                          (c == '>' && s[i + 1] == '=') || (c == '.' && s[i + 1] == '.'))) {
            out.push_back({s.substr(i, 2), line, false});
            i += 2;
            continue;
        }
        out.push_back({std::string(1, c), line, false});
        ++i;
    }
    return out;
}

} // namespace

void ScriptEditorPanel::runAnalysis() {
    m_issues.clear();
    const std::set<std::string>& known = knownGlobals(m_scene);
    m_scene->forEach([&](SceneNode* sn) {
        if (!sn->isScript()) return;
        std::string err;
        int errLine = 0;
        if (!ScriptEngine::checkSyntax(sn->source, err, errLine)) {
            m_issues.push_back({sn->id, sn->fullName(), errLine, true, err});
            return;   // can't make sense of the names in a script Lua can't read
        }
        // (Luau extras read as the Lua they run as: same lines, no type names taken for globals)
        const std::vector<Tok> t = tokens(Luau::toLua(sn->source));
        auto is = [&](size_t k, const char* s) { return k < t.size() && t[k].text == s; };
        // Pass 1: every name the script defines anywhere (locals, functions, parameters,
        // loop variables, globals it assigns). Simple on purpose: no scopes, so no false alarms.
        std::set<std::string> defined;
        for (size_t k = 0; k < t.size(); ++k) {
            if (is(k, "local") || is(k, "for")) {
                size_t j = k + 1;
                if (is(j, "function")) ++j;
                while (j < t.size() && t[j].name && !keywords().count(t[j].text)) {
                    defined.insert(t[j].text);
                    if (is(j + 1, ",")) j += 2; else break;
                }
            }
            if (is(k, "function")) {
                size_t j = k + 1;
                if (j < t.size() && t[j].name) defined.insert(t[j].text);   // function name() / function a.b()
                while (j < t.size() && !is(j, "(")) ++j;
                for (++j; j < t.size() && !is(j, ")"); ++j) if (t[j].name) defined.insert(t[j].text);
            }
            // name = ...  (a global set by the script), but not a.name = or { name = }
            if (t[k].name && is(k + 1, "=") && !(k > 0 && (is(k - 1, ".") || is(k - 1, ":") || is(k - 1, "{") || is(k - 1, ","))))
                defined.insert(t[k].text);
        }
        // Pass 2: names used that nothing defines.
        std::set<std::string> told;
        for (size_t k = 0; k < t.size(); ++k) {
            const Tok& tok = t[k];
            if (!tok.name || keywords().count(tok.text) || defined.count(tok.text) || known.count(tok.text)) continue;
            if (k > 0 && (is(k - 1, ".") || is(k - 1, ":") || is(k - 1, "goto") || is(k - 1, "::"))) continue;   // a field / label
            if (is(k + 1, "=") && k > 0 && (is(k - 1, "{") || is(k - 1, ","))) continue;   // { key = value }
            if (!told.insert(tok.text).second) continue;
            m_issues.push_back({sn->id, sn->fullName(), tok.line, false, "Unknown global '" + tok.text + "' (a typo?)"});
        }
    });
}

void ScriptEditorPanel::renderAnalysis() {
    if (!m_showAnalysis) return;
    ImGui::SetNextWindowSize(ImVec2(620, 360), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Script Analysis", &m_showAnalysis)) { ImGui::End(); return; }
    // Check again every couple of seconds (cheap: nothing runs).
    if (ImGui::GetTime() - m_analysedAt > 2.0) { runAnalysis(); m_analysedAt = ImGui::GetTime(); }
    int errors = 0, warnings = 0;
    for (const Issue& is : m_issues) (is.error ? errors : warnings)++;
    ImGui::TextColored(errors ? ImVec4(0.95f, 0.35f, 0.3f, 1) : ImVec4(0.5f, 0.8f, 0.5f, 1), "%d error%s", errors, errors == 1 ? "" : "s");
    ImGui::SameLine();
    ImGui::TextColored(warnings ? ImVec4(0.95f, 0.75f, 0.25f, 1) : ImVec4(0.5f, 0.8f, 0.5f, 1), "  %d warning%s", warnings, warnings == 1 ? "" : "s");
    ImGui::SameLine();
    ImGui::TextDisabled("   Click one to go to it.");
    ImGui::Separator();
    if (m_issues.empty()) ImGui::TextDisabled("No problems found in any script.");
    ImGui::BeginChild("##issues");
    for (size_t i = 0; i < m_issues.size(); ++i) {
        const Issue& is = m_issues[i];
        ImGui::PushID((int)i);
        ImGui::TextColored(is.error ? ImVec4(0.95f, 0.35f, 0.3f, 1) : ImVec4(0.95f, 0.75f, 0.25f, 1), is.error ? "Error" : "Warning");
        ImGui::SameLine(80);
        char label[512];
        std::snprintf(label, sizeof(label), "%s (line %d): %s", is.name.c_str(), is.line, is.text.c_str());
        if (ImGui::Selectable(label)) {
            if (SceneNode* n = m_scene->findById(is.script)) {
                m_scene->select(n);
                open(is.script);
                // Select the start of that line in the editor.
                int pos = 0, line = 1;
                for (; pos < (int)n->source.size() && line < is.line; ++pos) if (n->source[pos] == '\n') ++line;
                m_selectStart = pos;
                m_selectEnd = pos;
                m_focusCode = true;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}


namespace {

// Matches a complete identifier so "Door" does not accidentally match "DoorFrame".
bool containsIdentifier(const std::string& text, const std::string& name) {
    if (name.empty()) return false;
    size_t at = 0;
    while ((at = text.find(name, at)) != std::string::npos) {
        const bool left = at == 0 || !(std::isalnum((unsigned char)text[at - 1]) || text[at - 1] == '_');
        const size_t end = at + name.size();
        const bool right = end >= text.size() || !(std::isalnum((unsigned char)text[end]) || text[end] == '_');
        if (left && right) return true;
        at = end;
    }
    return false;
}

std::string trimDependencyExpression(std::string s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

} // namespace

void ScriptEditorPanel::runDependencyMap() {
    m_dependencyNodes.clear();
    m_dependencyEdges.clear();
    m_unresolvedDependencies.clear();

    std::vector<SceneNode*> scripts;
    m_scene->forEach([&](SceneNode* node) {
        if (!node->isScript()) return;
        scripts.push_back(node);
        m_dependencyNodes.push_back({node->id, node->name, node->fullName(),
                                     (int)node->source.size(), 0, 0});
    });

    // Only claim relationships when a require() expression or a named child lookup
    // explicitly mentions another script's name. This is static evidence, not runtime tracing.
    const std::regex requirePattern(R"(\brequire\s*\(\s*([^\)]*)\))");
    const std::regex childPattern(R"((?:WaitForChild|FindFirstChild)\s*\(\s*["']([^"']+)["'])");
    auto addEdge = [&](uint64_t from, uint64_t to, const std::string& reason) {
        if (from == to) return;
        for (const auto& edge : m_dependencyEdges)
            if (edge.from == from && edge.to == to && edge.reason == reason) return;
        m_dependencyEdges.push_back({from, to, reason});
    };

    for (SceneNode* source : scripts) {
        const std::string& text = source->source;
        for (std::sregex_iterator it(text.begin(), text.end(), requirePattern), end; it != end; ++it) {
            const std::string expression = trimDependencyExpression((*it)[1].str());
            if (expression.empty()) continue;
            bool found = false;
            for (SceneNode* target : scripts) {
                if (target == source || !containsIdentifier(expression, target->name)) continue;
                addEdge(source->id, target->id, "require()");
                found = true;
            }
            // Numeric module IDs are external assets, not missing local scripts.
            const bool numericId = std::all_of(expression.begin(), expression.end(),
                [](unsigned char c) { return std::isdigit(c) || std::isspace(c); });
            if (!found && !numericId)
                m_unresolvedDependencies.push_back({source->id, source->fullName(), expression});
        }

        for (std::sregex_iterator it(text.begin(), text.end(), childPattern), end; it != end; ++it) {
            const std::string childName = (*it)[1].str();
            for (SceneNode* target : scripts) {
                if (target != source && target->name == childName)
                    addEdge(source->id, target->id, "child lookup");
            }
        }
    }

    std::unordered_map<uint64_t, DependencyNode*> byId;
    for (auto& node : m_dependencyNodes) byId[node.id] = &node;
    for (const auto& edge : m_dependencyEdges) {
        if (auto it = byId.find(edge.from); it != byId.end()) ++it->second->outgoing;
        if (auto it = byId.find(edge.to); it != byId.end()) ++it->second->incoming;
    }
    if (!m_dependencySelected || !byId.count(m_dependencySelected))
        m_dependencySelected = m_dependencyNodes.empty() ? 0 : m_dependencyNodes.front().id;
}

void ScriptEditorPanel::renderDependencyMap() {
    if (!m_showDependencyMap) return;
    ImGui::SetNextWindowSize(ImVec2(900, 540), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Project Dependency Map", &m_showDependencyMap)) { ImGui::End(); return; }
    if (ImGui::GetTime() - m_dependencyAnalysedAt > 2.0) {
        runDependencyMap();
        m_dependencyAnalysedAt = ImGui::GetTime();
    }

    ImGui::Text("%d scripts", (int)m_dependencyNodes.size());
    ImGui::SameLine();
    ImGui::TextDisabled("%d relationships", (int)m_dependencyEdges.size());
    ImGui::SameLine();
    ImGui::TextColored(m_unresolvedDependencies.empty() ? ImVec4(0.45f, 0.8f, 0.5f, 1.0f)
                                                         : ImVec4(0.95f, 0.7f, 0.25f, 1.0f),
                       "%d unresolved require() expressions", (int)m_unresolvedDependencies.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) {
        runDependencyMap();
        m_dependencyAnalysedAt = ImGui::GetTime();
    }
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##dependencySearch", "Filter by script name or path...", &m_dependencySearch);
    ImGui::SameLine();
    ImGui::Checkbox("Only scripts with unresolved require()", &m_dependencyOnlyUnresolved);
    ImGui::TextDisabled("Relationships are inferred from require() and named child lookups. They are not runtime call traces.");
    ImGui::Separator();

    if (ImGui::BeginTable("##dependencyLayout", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Scripts", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Selected script", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::BeginChild("##dependencyScripts", ImVec2(0, 0), false);
        if (ImGui::BeginTable("##dependencyScriptRows", 4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Script", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Uses", ImGuiTableColumnFlags_WidthFixed, 42.0f);
            ImGui::TableSetupColumn("Used by", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableHeadersRow();
            for (const auto& node : m_dependencyNodes) {
                const bool unresolved = std::any_of(m_unresolvedDependencies.begin(), m_unresolvedDependencies.end(),
                    [&](const UnresolvedDependency& d) { return d.script == node.id; });
                if (m_dependencyOnlyUnresolved && !unresolved) continue;
                if (!m_dependencySearch.empty() &&
                    node.path.find(m_dependencySearch) == std::string::npos &&
                    node.name.find(m_dependencySearch) == std::string::npos) continue;
                ImGui::TableNextRow();
                ImGui::PushID((int)(node.id & 0x7fffffff));
                ImGui::TableNextColumn();
                if (ImGui::Selectable((node.name + "##dependency").c_str(),
                                      m_dependencySelected == node.id,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_dependencySelected = node.id;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (SceneNode* selected = m_scene->findById(node.id)) {
                            m_scene->select(selected);
                            open(node.id);
                            m_focusCode = true;
                        }
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", node.path.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%d", node.outgoing);
                ImGui::TableNextColumn(); ImGui::Text("%d", node.incoming);
                ImGui::TableNextColumn(); ImGui::Text("%d", node.sourceBytes);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::TableNextColumn();
        ImGui::BeginChild("##dependencyDetails", ImVec2(0, 0), false);
        const auto selectedIt = std::find_if(m_dependencyNodes.begin(), m_dependencyNodes.end(),
            [&](const DependencyNode& node) { return node.id == m_dependencySelected; });
        if (selectedIt == m_dependencyNodes.end()) {
            ImGui::TextDisabled("No scripts in this project yet.");
        } else {
            const DependencyNode& selected = *selectedIt;
            ImGui::TextWrapped("%s", selected.path.c_str());
            ImGui::TextDisabled("%d bytes of source", selected.sourceBytes);
            if (ImGui::Button("Open script")) {
                if (SceneNode* node = m_scene->findById(selected.id)) {
                    m_scene->select(node);
                    open(selected.id);
                    m_focusCode = true;
                }
            }
            ImGui::SeparatorText("Depends on");
            bool anyOutgoing = false;
            for (const auto& edge : m_dependencyEdges) if (edge.from == selected.id) {
                anyOutgoing = true;
                auto target = std::find_if(m_dependencyNodes.begin(), m_dependencyNodes.end(),
                    [&](const DependencyNode& node) { return node.id == edge.to; });
                if (target == m_dependencyNodes.end()) continue;
                ImGui::PushID((int)(edge.to & 0x7fffffff));
                if (ImGui::Selectable((target->path + "##outgoing").c_str())) m_dependencySelected = edge.to;
                ImGui::SameLine();
                ImGui::TextDisabled("%s", edge.reason.c_str());
                ImGui::PopID();
            }
            if (!anyOutgoing) ImGui::TextDisabled("No statically detected local script dependencies.");
            ImGui::SeparatorText("Used by");
            bool anyIncoming = false;
            for (const auto& edge : m_dependencyEdges) if (edge.to == selected.id) {
                anyIncoming = true;
                auto source = std::find_if(m_dependencyNodes.begin(), m_dependencyNodes.end(),
                    [&](const DependencyNode& node) { return node.id == edge.from; });
                if (source == m_dependencyNodes.end()) continue;
                ImGui::PushID((int)(edge.from & 0x7fffffff));
                if (ImGui::Selectable((source->path + "##incoming").c_str())) m_dependencySelected = edge.from;
                ImGui::SameLine();
                ImGui::TextDisabled("%s", edge.reason.c_str());
                ImGui::PopID();
            }
            if (!anyIncoming) ImGui::TextDisabled("No statically detected callers or lookups.");
            ImGui::SeparatorText("Unresolved require() expressions");
            bool anyUnresolved = false;
            for (const auto& dep : m_unresolvedDependencies) if (dep.script == selected.id) {
                anyUnresolved = true;
                ImGui::PushID((int)(&dep - m_unresolvedDependencies.data()));
                ImGui::TextWrapped("%s", dep.expression.c_str());
                ImGui::TextDisabled("Could not match this expression to another local script by name.");
                ImGui::PopID();
            }
            if (!anyUnresolved) ImGui::TextDisabled("No unresolved require() expressions for this script.");
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
    ImGui::End();
}
