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

#include <cctype>
#include <set>
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
