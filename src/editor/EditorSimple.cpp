// The Simple layout and Search Studio.
//
// Simple layout (the one Studio starts with): each ribbon tab shows its everyday
// buttons and keeps the rest behind a "More" button (EditorRibbon.cpp), and only the
// main panels are open. Nothing is taken away: VIEW > Advanced puts every button and
// panel back on screen.
//
// Search Studio (Ctrl+Shift+P, or the box next to the tabs): type part of a name to
// find any tool, window or object in the game, then Enter.
#include "Editor.h"
#include "Premades.h"
#include "MultiTest.h"
#include "Plugins.h"
#include "panels/ViewportPanel.h"
#include "panels/OutlinerPanel.h"
#include "panels/ScriptEditorPanel.h"
#include "../core/Settings.h"
#include "../core/Log.h"
#include "../scene/Scene.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

bool Editor::simpleLayout() const { return GraphicsSettings::get().studioSimpleLayout; }

// The panels a fresh workspace shows.
void Editor::applyLayoutPanels() {
    const bool simple = simpleLayout();
    for (int i = 0; i < kPanelCount; ++i) m_showPanel[i] = i != kPanelAnimation;
    if (!simple) return;
    // Simple: the Explorer, Properties, Toolbox and Output. The others open from the
    // VIEW tab, their own buttons (Assistant, Lighting...) or Search.
    m_showPanel[kPanelCommandBar] = false;
    m_showPanel[kPanelScript] = false;      // (opens by itself with a script)
    m_showPanel[kPanelLighting] = false;
    m_showPanel[kPanelPlayer] = false;
    m_showPanel[kPanelTeam] = false;
    m_showPanel[kPanelAssistant] = false;
}

void Editor::setSimpleLayout(bool simple) {
    GraphicsSettings& gs = GraphicsSettings::get();
    if (gs.studioSimpleLayout == simple) return;
    gs.studioSimpleLayout = simple;
    gs.save();
    resetWorkspace();   // the panels where this layout starts them
    Log::info(simple ? "Simple layout: the everyday buttons and panels. The rest is behind More, in the menus and in Search (Ctrl+Shift+P)."
                     : "Advanced layout: every button and panel is on screen.");
}

// ---------------------------------------------------------------------------
// Search Studio
// ---------------------------------------------------------------------------

namespace {
struct Found {
    std::string name;    // "Anchor"
    std::string where;   // "Edit", "Window", "In your game"...
    std::string keys;    // "Alt+A"
    bool enabled = true;
    std::function<void()> run;
    int score = 0;
};

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Every word typed has to be somewhere in the name (or where it lives). Lower = better.
int matchScore(const std::string& name, const std::string& where, const std::vector<std::string>& words, const std::string& whole) {
    const std::string n = lower(name), w = lower(where);
    for (const std::string& word : words)
        if (n.find(word) == std::string::npos && w.find(word) == std::string::npos) return -1;
    if (whole.empty()) return 2;
    if (n.rfind(whole, 0) == 0) return 0;                         // starts with what was typed
    if (n.find(" " + whole) != std::string::npos) return 1;       // a word starts with it
    if (n.find(whole) != std::string::npos) return 2;
    return 3;
}
} // namespace

void Editor::openSearch() {
    m_searchOpen = true;
    m_searchFocus = true;
    m_searchText.clear();
    m_searchPick = 0;
}

void Editor::renderSearch() {
    if (!m_searchOpen) return;

    // --- What can be found ---
    std::vector<Found> all;
    SceneNode* sel = m_scene->selected();
    const bool building = !m_playing;
    const bool editable = canEdit(sel) && building;
    auto add = [&](const char* name, const char* where, const char* keys, bool enabled, std::function<void()> run) {
        all.push_back({name, where, keys ? keys : "", enabled, std::move(run)});
    };
    auto window = [&](const char* name, bool* flag) {
        add(name, "Window", "", true, [flag] { *flag = true; });
    };

    // Tools
    add("Select tool", "Tools", "Ctrl+1", true, [this] { m_state.tool = GizmoTool::Select; });
    add("Move tool", "Tools", "Ctrl+2", true, [this] { m_state.tool = GizmoTool::Translate; });
    add("Scale tool (resize)", "Tools", "Ctrl+3", true, [this] { m_state.tool = GizmoTool::Scale; });
    add("Rotate tool (turn)", "Tools", "Ctrl+4", true, [this] { m_state.tool = GizmoTool::Rotate; });
    // Insert
    add("Part (block)", "Insert", "", building, [this] { spawnPrimitive(PrimitiveType::Cube); });
    add("Sphere (ball)", "Insert", "", building, [this] { spawnPrimitive(PrimitiveType::Sphere); });
    add("Cylinder", "Insert", "", building, [this] { spawnPrimitive(PrimitiveType::Cylinder); });
    add("Plane (flat)", "Insert", "", building, [this] { spawnPrimitive(PrimitiveType::Plane); });
    add("MeshPart (a part you shape yourself)", "Insert", "", building, [this] { addMeshPart(); });
    add("Insert Object...", "Insert", "Ctrl+I", building, [this] { m_insertParent = m_scene->selected(); m_openInsert = true; });
    add("Import a model, picture or sound...", "Insert", "", building, [this] { importDialog(); });
    add("Script", "Insert", "", building, [this] { addScript(m_scene->selected()); });
    add("LocalScript", "Insert", "", building, [this] { addScript(m_scene->selected(), true); });
    add("Model (empty group)", "Insert", "", building, [this] { addModel(); });
    add("Point Light", "Insert", "", building, [this] { addLight(LightType::Point); });
    add("Spot Light", "Insert", "", building, [this] { addLight(LightType::Spot); });
    add("Sound", "Insert", "", building, [this] { addSound(); });
    add("Rig (dummy character)", "Insert", "", building, [this] { insertObject("Rig", nullptr); });
    for (const PremadeInfo& p : premadeList()) {
        const Premade kind = p.kind;
        all.push_back({p.name, "Ready-made", "", building, [this, kind] { spawnPremade(kind); }});
    }
    // Edit
    add("Undo", "Edit", "Ctrl+Z", !m_undo.empty() && building, [this] { undo(); });
    add("Redo", "Edit", "Ctrl+Y", !m_redo.empty() && building, [this] { redo(); });
    add("Copy", "Edit", "Ctrl+C", editable, [this] { copySelected(); });
    add("Cut", "Edit", "Ctrl+X", editable, [this] { cutSelected(); });
    add("Paste", "Edit", "Ctrl+V", !m_clipboard.empty() && building, [this] { paste(); });
    add("Paste Into the selection", "Edit", "Ctrl+Shift+V", !m_clipboard.empty() && editable, [this] { pasteInto(); });
    add("Duplicate", "Edit", "Ctrl+D", editable, [this] { duplicateSelected(); });
    add("Delete", "Edit", "Del", sel && building && !m_scene->isProtected(sel), [this] { deleteSelected(); });
    add("Rename", "Edit", "F2", editable, [this] { m_outliner->beginRename(m_scene->selected()); });
    add("Select All", "Edit", "Ctrl+A", true, [this] { selectAll(); });
    add("Group as Model", "Edit", "Ctrl+G", editable, [this] { groupSelected(); });
    add("Ungroup", "Edit", "Ctrl+U", editable && sel->kind == NodeKind::Model, [this] { ungroupSelected(); });
    add("Anchor (stays put)", "Edit", "Alt+A", editable, [this] { toggleAnchored(); });
    add("Lock (can't be clicked)", "Edit", "Alt+L", editable, [this] { toggleLocked(); });
    add("Find and Change (lots of objects at once)", "Edit", "Ctrl+Shift+E", true, [this] { m_showBulkEdit = true; });
    add("Hide While Building", "Edit", "H", sel != nullptr, [this] { hideSelected(); });
    add("Show Everything Hidden", "Edit", "Alt+H", !m_editorHidden.empty(), [this] { unhideAll(); });
    add("Pivot to middle", "Edit", "", editable && sel->kind == NodeKind::Model, [this] { centerModelPivot(); });
    add("Union (join parts)", "Solid Modeling", "Ctrl+Shift+G", editable, [this] { unionSelected(0); });
    add("Negate (make a hole)", "Solid Modeling", "Ctrl+Shift+N", editable, [this] { negateSelected(); });
    add("Intersect (keep the overlap)", "Solid Modeling", "Ctrl+Shift+I", editable, [this] { unionSelected(1); });
    add("Separate a union", "Solid Modeling", "Ctrl+Shift+U", editable && !sel->unionSource.empty(), [this] { separateSelected(); });
    add("Edit Mesh (Modeling mode)", "Mode", "Tab", editable && sel->isPart(), [this] { setMode(StudioMode::Modeling); });
    // Switches
    add(m_state.snapEnabled ? "Snap to grid: turn off" : "Snap to grid: turn on", "Snapping", "", true,
        [this] { m_state.snapEnabled = !m_state.snapEnabled; });
    add(m_state.rotSnapEnabled ? "Rotate in steps: turn off" : "Rotate in steps: turn on", "Snapping", "", true,
        [this] { m_state.rotSnapEnabled = !m_state.rotSnapEnabled; });
    add(m_state.collisions ? "Collisions while moving: turn off" : "Collisions while moving: turn on", "Snapping", "", true,
        [this] { m_state.collisions = !m_state.collisions; });
    add(m_state.showGrid ? "Floor grid: hide" : "Floor grid: show", "View", "", true, [this] { m_state.showGrid = !m_state.showGrid; });
    add(m_state.orthographic ? "Orthographic view: turn off" : "Orthographic view: turn on", "View", "Numpad 5", true,
        [this] { m_state.orthographic = !m_state.orthographic; });
    add(m_state.gizmoLocal ? "Use world axes" : "Use local axes", "Tools", "Ctrl+L", true, [this] { m_state.gizmoLocal = !m_state.gizmoLocal; });
    // Constraints
    {
        static const char* kNames[] = {"Rope", "Rod", "Spring", "Weld", "Hinge", "Motor", "Ball Socket", "Prismatic", "Cylindrical"};
        for (int t = 0; t < 9; ++t)
            all.push_back({std::string(kNames[t]) + " (join two parts)", "Constraints", "", building,
                           [this, t] { m_state.connectTool = t; m_state.connectFirst = 0; m_viewport->focus(); }});
    }
    // Test
    add("Play", "Test", "F5", building, [this] { startPlay(0); });
    add("Play Here (start where the camera is)", "Test", "", building, [this] { startPlay(1); });
    add("Simulate (physics and scripts, no character)", "Test", "F8", building, [this] { startPlay(2); });
    add("Stop", "Test", "Shift+F5", m_playing, [this] { togglePlay(); });
    add(m_state.simPaused ? "Resume" : "Pause", "Test", "F6", m_playing, [this] { m_state.simPaused = !m_state.simPaused; });
    for (int n = MultiTest::kMinPlayers; n <= MultiTest::kMaxPlayers; ++n)
        all.push_back({"Test with " + std::to_string(n) + " players", "Test", "", !m_multiTest->running(),
                       [this, n] { std::string err; if (!startMultiTest(n, err) && !err.empty()) Log::warn(err); }});
    add("Stop multiplayer test", "Test", "", m_multiTest->running(), [this] { stopMultiTest(); });
    // File
    add("New Game", "File", "Ctrl+N", true, [this] {
        m_pending = Pending::New;
        if (m_dirty) m_openDiscard = true; else { newScene(); m_pending = Pending::None; }
    });
    add("New from Template...", "File", "", true, [this] { m_showTemplates = true; });
    add("Open...", "File", "Ctrl+O", true, [this] {
        m_pending = Pending::Open;
        if (m_dirty) m_openDiscard = true; else m_openOpen = true;
    });
    add("Save", "File", "Ctrl+S", true, [this] { save(); });
    add("Save As...", "File", "Ctrl+Shift+S", true, [this] { m_nameInput = m_scene->info().title; m_openSaveAs = true; });
    add("Export As... (Roblox, .obj, .fbx, .glb...)", "File", "", building, [this] { openExportAs(m_scene->selected() != nullptr); });
    add("Publish to Guts&Bolts...", "File", "", true, [this] { m_openPublish = true; });
    add("Publish Selection to Library...", "File", "", sel != nullptr, [this] { m_openPublishModel = true; });
    add("Game Settings (title, description...)", "File", "", building, [this] { m_openInfo = true; });
    add("Team Create (edit with friends)", "File", "", true, [this] { m_openTeam = true; });
    add("Library (plugins, audio)", "File", "", true, [this] { m_showPluginLibrary = true; });
    // Panels
    {
        struct P { const char* name; int panel; };
        static const P kPanels[] = {{"Explorer", kPanelExplorer}, {"Properties", kPanelProperties}, {"Toolbox", kPanelToolbox},
                                    {"Output", kPanelOutput}, {"Command Bar", kPanelCommandBar}, {"Script Editor", kPanelScript},
                                    {"Lighting", kPanelLighting}, {"Player settings", kPanelPlayer}, {"Team Chat", kPanelTeam},
                                    {"Assistant (AI help)", kPanelAssistant}};
        for (const P& p : kPanels) {
            const int panel = p.panel;
            all.push_back({std::string(p.name) + (m_showPanel[panel] ? ": hide" : ": show"), "Panel", "", true,
                           [this, panel] { m_showPanel[panel] = !m_showPanel[panel]; }});
        }
        add("Animation Editor", "Panel", "", building, [this] { openAnimationEditor(); });
    }
    // Windows
    window("Studio Learn (lessons)", &m_showLearn);
    add("Script Analysis (find mistakes)", "Window", "", true, [this] { m_scriptEditor->showAnalysis(); });
    add("Project Dependency Map", "Window", "", true, [this] { m_scriptEditor->showDependencyMap(); });
    window("Performance Monitor", &m_showPerformance);
    window("Live Profiler", &m_showLiveProfiler);
    window("Performance Doctor", &m_showPerformanceDoctor);
    window("Test like a phone", &m_showPhoneTest);
    window("Lua Debugger", &m_showLuaDebugger);
    window("Scene Validator", &m_showSceneValidator);
    window("Project Time Machine", &m_showProjectTimeMachine);
    window("Visual Scripting (Blocks)", &m_showBlocks);
    window("Material Editor", &m_showMaterialEditor);
    window("AI Activity", &m_showAiActivity);
    window("Script History", &m_showScriptHistory);
    window("Level Generator", &m_showLevelGen);
    window("Cutscene Maker", &m_showCutscenes);
    window("Physics Replay", &m_showReplay);
    window("Compare and Merge", &m_showCompare);
    window("Collision Groups", &m_showCollisionGroups);
    window("Armature (bones)", &m_showArmature);
    window("Accessories (hats)", &m_showAccessory);
    window("UV Editor", &m_uv.show);
    window("Settings (graphics, keys...)", &m_showSettings);
    window("Keyboard Shortcuts", &m_showShortcuts);
    add(m_state.showPhysics ? "Physics View: turn off" : "Physics View: turn on", "View", "", true,
        [this] { m_state.showPhysics = !m_state.showPhysics; });
    add(m_state.showNavMesh ? "Navmesh: hide" : "Navmesh: show (where characters can walk)", "View", "", true,
        [this] { m_state.showNavMesh = !m_state.showNavMesh; });
    add("Bake the navmesh", "Navigation", "", true, [this] { m_state.bakeNavMesh = 1; m_state.showNavMesh = true; });
    // View
    add("Zoom to the selection", "View", "F", sel != nullptr, [this] { activeView()->focusSelected(); });
    add("Reset Camera", "View", "", true, [this] { m_viewport->resetCamera(); });
    add("Add Viewport (another 3D view)", "View", "", true, [this] { addViewport(); });
    add("Four Views (top, front, right)", "View", "Ctrl+Alt+Q", true, [this] { quadView(); });
    add("Reset Workspace (panels back where they start)", "View", "", true, [this] { resetWorkspace(); });
    add("X-Ray", "View", "Alt+Z", true, [this] { activeView()->xray = !activeView()->xray; });
    add("Wireframe", "View", "Shift+Z", true, [this] {
        ViewportPanel* v = activeView();
        v->shading = v->shading == SceneRenderer::Shading::Wireframe ? SceneRenderer::Shading::Material : SceneRenderer::Shading::Wireframe;
    });
    add("Simple layout (fewer buttons)", "Layout", "", !simpleLayout(), [this] { setSimpleLayout(true); });
    add("Advanced layout (every button)", "Layout", "", simpleLayout(), [this] { setSimpleLayout(false); });
    // Ribbon tabs
    {
        static const char* kTabs[] = {"HOME", "MODEL", "TEST", "VIEW", "PLUGINS", "MESH", "AVATAR", "TERRAIN"};
        for (int i = 0; i < 8; ++i) {
            if (i == 5) continue;   // (MESH only exists in Modeling mode)
            all.push_back({std::string(kTabs[i]) + " tab", "Ribbon", "", true, [this, i] { m_ribbonTab = i; }});
        }
    }
    // Plugins
    add("New plugin", "Plugins", "", building, [this] { newPluginScript(); });
    add("Reload plugins", "Plugins", "", true, [this] { m_plugins->reload(); });

    // --- What matches ---
    std::vector<std::string> words;
    {
        std::istringstream in(lower(m_searchText));
        for (std::string w; in >> w; ) words.push_back(w);
    }
    std::string whole = lower(m_searchText);
    while (!whole.empty() && whole.back() == ' ') whole.pop_back();
    std::vector<Found> found;
    for (Found& f : all) {
        f.score = matchScore(f.name, f.where, words, whole);
        if (f.score >= 0) found.push_back(std::move(f));
    }
    // Things in the game, by name (after typing something).
    if (!words.empty()) {
        int objects = 0;
        m_scene->forEach([&](SceneNode* n) {
            if (objects >= 12) return;
            const int score = matchScore(n->name, "", words, whole);
            if (score < 0) return;
            ++objects;
            const uint64_t id = n->id;
            found.push_back({n->name, "In your game", "", true, [this, id] {
                if (SceneNode* node = m_scene->findById(id)) {
                    m_scene->select(node);
                    activeView()->focusSelected();
                }
            }, score + 1});
        });
    }
    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        if (a.enabled != b.enabled) return a.enabled;   // what can be done right now first
        return a.score < b.score;
    });
    const int count = (int)found.size();
    m_searchPick = count ? std::clamp(m_searchPick, 0, count - 1) : 0;

    // --- The window: under the ribbon, in the middle ---
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float width = std::min(560.0f, vp->WorkSize.x - 40.0f);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - width) * 0.5f, vp->WorkPos.y + 60.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.635f, 1.0f, 0.8f));
    bool open = true;
    ImGui::Begin("Search Studio", &open,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    const bool justOpened = m_searchFocus;
    if (m_searchFocus) {
        ImGui::SetWindowFocus();
        ImGui::SetKeyboardFocusHere();
        m_searchFocus = false;
    }
    ImGui::SetNextItemWidth(-1);
    const bool enter = ImGui::InputTextWithHint("##searchstudio", "Search tools, windows and objects...", &m_searchText,
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemEdited()) m_searchPick = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) m_searchPick = count ? (m_searchPick + 1) % count : 0;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) m_searchPick = count ? (m_searchPick + count - 1) % count : 0;

    int runIndex = enter && count ? m_searchPick : -1;
    const float rowH = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
    ImGui::BeginChild("##found", ImVec2(0, rowH * 10.0f + 4.0f));
    if (!count) ImGui::TextDisabled("Nothing called that. Try another word.");
    for (int i = 0; i < count; ++i) {
        const Found& f = found[(size_t)i];
        ImGui::PushID(i);
        ImGui::BeginDisabled(!f.enabled);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##row", i == m_searchPick, ImGuiSelectableFlags_None, ImVec2(0, rowH - 4.0f))) runIndex = i;
        if (i == m_searchPick && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
            ImGui::SetScrollHereY(0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float right = p.x + ImGui::GetContentRegionAvail().x;
        const ImU32 text = ImGui::GetColorU32(f.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        dl->AddText(ImVec2(p.x + 6, p.y + 2), text, f.name.c_str());
        const float nameW = ImGui::CalcTextSize(f.name.c_str()).x;
        dl->AddText(ImVec2(p.x + 6 + nameW + 12, p.y + 2), dim, f.where.c_str());
        if (!f.keys.empty()) dl->AddText(ImVec2(right - ImGui::CalcTextSize(f.keys.c_str()).x - 6, p.y + 2), dim, f.keys.c_str());
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::TextDisabled("Up / Down to pick   Enter to do it   Esc to close");

    const bool away = !justOpened && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    if (runIndex >= 0 && runIndex < count && found[(size_t)runIndex].enabled) {
        m_deferred = found[(size_t)runIndex].run;   // (after the panels have drawn)
        m_searchOpen = false;
    } else if (!open || away || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_searchOpen = false;
    }
}

// Tests: --test-mesh "simple:<step>" runs one of these.
void Editor::testSimple(const std::string& step) {
    if (step == "on") setSimpleLayout(true);
    else if (step == "off") setSimpleLayout(false);
    else if (step == "search") openSearch();
    else if (step.rfind("type=", 0) == 0) { openSearch(); m_searchText = step.substr(5); m_searchFocus = true; }
    else if (step.rfind("tab=", 0) == 0) m_ribbonTab = std::clamp(std::atoi(step.c_str() + 4), 0, 7);
    else if (step == "more") m_openRibbonMore = true;
    else if (step == "report") {
        int shown = 0;
        for (int i = 0; i < kPanelCount; ++i) shown += m_showPanel[i] ? 1 : 0;
        Log::info(std::string("simple: layout ") + (simpleLayout() ? "simple" : "advanced") + "; " + std::to_string(shown) +
                  " panels open; search " + (m_searchOpen ? "open" : "closed"));
    }
}
