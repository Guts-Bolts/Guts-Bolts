#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <imgui.h>

class Scene;
class ScriptEngine;
struct ImGuiInputTextCallbackData;

// A simple code editor for Script objects, with live error checking, a menu
// of ready-to-use code snippets for beginners, find / replace (Ctrl+F /
// Ctrl+H), find in every script (Ctrl+Shift+F) and autocomplete (Tab).
class ScriptEditorPanel {
public:
    explicit ScriptEditorPanel(Scene* scene);
    void open(uint64_t scriptId);   // show this script and bring the tab forward
    void openAtLine(uint64_t scriptId, int line);   // ...with the text cursor at the start of that line
    void render();
    void renderFindAll();           // the "Find in All Scripts" window
    uint64_t current() const { return m_id; }
    void goToLine(int line);        // put the text cursor at the start of this line
    // The play session's scripts, for the Lua Debugger's breakpoints in the line numbers.
    void setDebugger(ScriptEngine* scripts) { m_debugger = scripts; }
    void showFindAll() { m_showFindAll = true; m_focusFindAll = true; }
    void renderAnalysis();          // the "Script Analysis" window (ScriptAnalysis.cpp)
    void showAnalysis() { m_showAnalysis = true; m_analysedAt = -100.0; }
    bool analysisShown() const { return m_showAnalysis; }
    void runAnalysis();
    struct Issue { uint64_t script; std::string name; int line; bool error; std::string text; };
    const std::vector<Issue>& issues() const { return m_issues; }
    // Project Dependency Map (View menu): static, best-effort script relationships.
    void showDependencyMap() { m_showDependencyMap = true; m_dependencyAnalysedAt = -100.0; }
    void renderDependencyMap();
    void runDependencyMap();

private:
    static int onEdit(ImGuiInputTextCallbackData* d);
    void drawColoredCode(ImDrawList* dl, const std::string& src, ImVec2 origin, float lineH, int firstLine, int lastLine);
    void findNext(const std::string& text, bool backwards);
    void updateSuggestions(const std::string& source);
    void runFindAll();

    Scene*      m_scene;
    ScriptEngine* m_debugger = nullptr;
    uint64_t    m_id = 0;
    bool        m_focus = false;
    std::string m_checked;          // source text the error below belongs to
    std::string m_error;
    int         m_errorLine = 0;
    int         m_cursorLine = 1;
    int         m_cursor = 0;       // byte position of the text cursor

    // Find / replace
    bool        m_showFind = false, m_showReplace = false, m_focusFind = false, m_matchCase = false;
    std::string m_find, m_replace, m_findStatus;
    int         m_selectStart = -1, m_selectEnd = -1;   // ask the editor to select this next frame
    bool        m_focusCode = false;
    int         m_matchAt = -1;                         // where the last "Find" landed

    // Autocomplete
    std::vector<std::string> m_suggest;
    std::string m_prefix;
    ImVec2      m_caret{0, 0};      // where the text cursor is on screen
    bool        m_codeActive = false;
    int         m_pick = 0;

    // Find in all scripts
    struct Hit { uint64_t script; std::string name; int line; int pos; std::string text; };
    bool        m_showFindAll = false, m_focusFindAll = false;
    std::string m_findAll, m_findAllRan;
    std::vector<Hit> m_hits;

    // Script Analysis
    bool               m_showAnalysis = false;
    double             m_analysedAt = -100.0;
    std::vector<Issue> m_issues;

    // Project Dependency Map. Edges are static clues, not proof that a code path executes.
    struct DependencyNode {
        uint64_t id = 0;
        std::string name, path;
        int sourceBytes = 0, outgoing = 0, incoming = 0;
        bool cyclic = false;
    };
    struct DependencyEdge {
        uint64_t from = 0, to = 0;
        std::string reason;
    };
    struct UnresolvedDependency {
        uint64_t script = 0;
        std::string path, expression;
    };
    struct AssetReference {
        uint64_t owner = 0, targetNode = 0;
        std::string ownerPath, property, value, availability;
        bool nodeLink = false, unresolvedNode = false;
    };
    bool m_showDependencyMap = false, m_dependencyOnlyUnresolved = false;
    bool m_dependencyOnlyIsolated = false;
    double m_dependencyAnalysedAt = -100.0;
    uint64_t m_dependencySelected = 0;
    std::string m_dependencySearch;
    std::string m_assetSearch;
    bool m_assetOnlyBrokenLinks = false;
    bool m_assetOnlyMissingFiles = false;
    std::vector<DependencyNode> m_dependencyNodes;
    std::vector<DependencyEdge> m_dependencyEdges;
    std::vector<UnresolvedDependency> m_unresolvedDependencies;
    std::vector<AssetReference> m_assetReferences;
};
