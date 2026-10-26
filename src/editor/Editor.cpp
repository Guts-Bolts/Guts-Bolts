#include "Editor.h"
#include <sstream>
#include <cmath>
#include <unordered_map>
#include "../scene/Guis.h"
#include "../game/Profile.h"
#include "../scene/EditMesh.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"
#include "Plugins.h"
#include "../scene/Physics.h"
#include "../scripting/ScriptEngine.h"
#include "Theme.h"
#include "Icons.h"
#include "../scene/RobloxFile.h"
#include "panels/ViewportPanel.h"
#include "panels/OutlinerPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/EnvironmentPanel.h"
#include "panels/ToolboxPanel.h"
#include "Thumbnailer.h"
#include "panels/PlayerPanel.h"
#include "panels/AssistantPanel.h"
#include "panels/OutputPanel.h"
#include "panels/ScriptEditorPanel.h"
#include "panels/AnimationEditor.h"
#include "TeamCreate.h"
#include "MultiTest.h"
#include "../scene/Scene.h"
#include "../scene/Player.h"
#include "../scene/Serializer.h"
#include "../game/GameSession.h"
#include "../game/SettingsWindow.h"
#include "../game/UpdateToast.h"
#include "../core/Settings.h"
#include "../renderer/MeshLibrary.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../core/AppWindow.h"

#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <ImGuizmo.h>
#include <string>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <filesystem>
#include <vector>

namespace {
const char* kNewScript =
R"(-- A new script! It runs when you press Play (F5).
-- 'script.Parent' is the object this script is inside of.

print("Hello world!")
)";
const char* kNewLocalScript =
R"(-- A LocalScript runs on each player's own computer (good for UI and keys).
-- To tell the server something, use a RemoteEvent:
--   game.ReplicatedStorage.MyEvent:FireServer("hi")

print("Hello from " .. game.Players.LocalPlayer.Name)
)";
} // namespace

Editor::Editor(GLFWwindow* window, Scene* scene)
    : m_window(window), m_scene(scene) {
    m_session  = std::make_unique<GameSession>(scene);
    m_viewport = std::make_unique<ViewportPanel>(window, scene, &m_state);
    auto open  = [this](SceneNode* n) { openScript(n); };
    m_outliner = std::make_unique<OutlinerPanel>(scene, open, [this](SceneNode* n) { addScript(n); });
    EditorTheme::applyStudio();
    m_outliner->onInsert = [this](SceneNode* parent) { m_insertParent = parent; m_openInsert = true; };
    m_outliner->onInsertNamed = [this](const std::string& what, SceneNode* parent) {
        m_deferred = [this, what, parent] { insertObject(what, parent); };   // (not while the Explorer is drawing)
    };
    m_outliner->onLighting = [] { ImGui::SetWindowFocus("Lighting"); };
    m_outliner->playing = [this] { return m_playing; };
    m_viewport->onMode = [this](StudioMode mode) { setMode(mode); };
    m_outliner->contextMenuExtras = [this] {
        ImGui::Separator();
        if (ImGui::MenuItem("Expand Selected", "Ctrl+Right")) m_outliner->expand(m_scene->selection());
        if (ImGui::MenuItem("Collapse Selected", "Ctrl+Left")) m_outliner->collapse(m_scene->selection());
        selectionMenuItems(false);
    };
    // Right-clicking the 3D view: the same things for what's selected.
    m_viewport->contextMenu = [this] {
        if (m_state.mode == StudioMode::Modeling) { modelingContextMenu(); return; }
        if (!m_scene->selected()) {
            ImGui::TextDisabled("Click something to select it first.");
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboard.empty())) m_deferred = [this] { paste(); };
            return;
        }
        selectionMenuItems(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) m_deferred = [this] { deleteSelected(); };
    };
    m_properties   = std::make_unique<PropertiesPanel>(scene, open);
    m_properties->m_editCollisionGroups = [this] { m_showCollisionGroups = true; };
    m_properties->m_openUVEditor = [this] { m_uv.show = true; };
    m_properties->m_editMesh = [this](SceneNode* n) {
        m_deferred = [this, n] { m_scene->select(n); setMode(StudioMode::Modeling); };   // not while Properties is drawing
    };
    m_environment  = std::make_unique<EnvironmentPanel>(scene);
    m_player       = std::make_unique<PlayerPanel>(scene);
    m_output       = std::make_unique<OutputPanel>();
    m_scriptEditor = std::make_unique<ScriptEditorPanel>(scene);
    m_animEditor   = std::make_unique<AnimationEditor>(scene, &m_state);
    m_assistant    = std::make_unique<AssistantPanel>(*this);
    m_team         = std::make_unique<TeamCreate>(scene);
    m_viewport->setTeam(m_team.get());
    m_multiTest    = std::make_unique<MultiTest>();

    ToolboxPanel::Actions actions;
    actions.spawnPart    = [this](PrimitiveType t) { spawnPrimitive(t); };
    actions.addScript    = [this] { addScript(m_scene->selected()); };
    actions.addModel     = [this] { addModel(); };
    actions.addLight     = [this](LightType t) { addLight(t); };
    actions.addSound     = [this] { addSound(); };
    actions.startConnect = [this](int t) { m_state.connectTool = t; m_state.connectFirst = 0; m_viewport->focus(); };
    m_viewport->onConnect = [this](SceneNode* a, glm::vec3 pa, SceneNode* b, glm::vec3 pb) {
        connectParts(m_state.connectTool, a, pa, b, pb);
    };
    actions.spawnPremade = [this](Premade p) { spawnPremade(p); };
    m_thumbnailer = std::make_unique<Thumbnailer>();
    actions.thumbnail = [this](const std::string& key, const std::function<void(Scene&)>& build) {
        return m_thumbnailer->texture(key, build);
    };
    actions.library = [this](bool mine, int kind, const std::string& query, bool bestRated, bool reload, std::string& status) {
        return libraryTiles(mine, kind, query, bestRated, reload, status);
    };
    m_toolbox = std::make_unique<ToolboxPanel>(actions);

    resetHistory();
    Log::system("Welcome to Guts and Bolts! Press Play (F5) to test your game.");
    m_plugins = std::make_unique<Plugins>(scene);
    m_plugins->reload();
}

// Out-of-line so the panel types are complete here.
Editor::~Editor() {
    m_multiTest.reset();   // closes the test server and its player windows
    m_plugins.reset();
    m_team.reset();
    if (m_session) m_session->stop();
    dropAutoSave();   // a normal close: nothing to recover next time
}

void Editor::render(float dt) {
    const auto frameStart = std::chrono::steady_clock::now();
    m_perfSimulationMs = 0.0f;
    Online::update();   // replies from the Guts&Bolts server
    m_multiTest->update();   // the multiplayer test's output -> Output
    ImGuizmo::BeginFrame();
    // Docked tabs appear over a few frames; make sure the 3D view ends up on top.
    if (++m_frame <= 3) m_viewport->focus();

    // Team Create: bring in other people's edits (they wait while playtesting).
    m_team->update(!m_playing);
    if (m_team->consumeRemoteEdit()) {
        m_committed = Serializer::saveScene(*m_scene);
        m_undo.clear();          // undo can't safely rewind other people's work
        m_redo.clear();
        m_dirty = true;
    }
    m_team->setSelection(m_scene->selected() ? m_scene->selected()->id : 0);
    handleShortcuts();
    checkModeling();
    if (m_playing && (!m_state.simPaused || m_state.simStep)) {
        // F6 pauses the world; F7 moves it on by one frame.
        const auto simulationStart = std::chrono::steady_clock::now();
        m_session->update(m_state.simStep ? 1.0f / 60.0f : dt, m_viewport->cameraYaw(), true, m_viewport->swimLook());
        m_perfSimulationMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - simulationStart).count();
        m_state.simStep = false;
        // Game pass Buy windows only show in the Player: a test here answers "not bought".
        for (auto& [userId, pass] : m_session->scripts().takePassPrompts()) {
            const bool product = pass.rfind("product:", 0) == 0;
            Log::info(std::string(product ? "PromptProductPurchase(" + pass.substr(8) : "PromptGamePassPurchase(" + pass) +
                      "): the Buy window shows in the Guts&Bolts Player, not in Studio tests.");
            m_session->scripts().passPromptDone(userId, pass, false);
        }
        Player* p = m_scene->player();
        if (p && !m_session->runOnly()) m_viewport->followPlayer(*p, dt);   // Run: the camera stays free
    }
    buildDockspace();
    if (!m_playing) m_animEditor->update(dt, m_viewport->gizmoInUse());   // show the rig posed
    if (!m_playing) m_scene->particles().runEmitters(*m_scene, dt);       // particle effects show while you edit too
    const auto viewportStart = std::chrono::steady_clock::now();
    m_viewport->render(dt);
    m_perfViewportMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - viewportStart).count();
    // Files dragged from the computer onto Studio.
    if (auto dropped = AppWindow::takeDroppedFiles(); !dropped.empty()) importFiles(dropped, ImGui::GetMousePos(), true);
    if (m_showPanel[kPanelExplorer]) m_outliner->render();
    if (m_deferred) { auto f = std::move(m_deferred); m_deferred = nullptr; f(); }
    if (m_showPanel[kPanelProperties]) m_properties->render();
    if (m_showPanel[kPanelLighting])   m_environment->render();
    if (m_showPanel[kPanelToolbox])    { m_thumbnailer->newFrame(); m_toolbox->render(); }
    if (m_showPanel[kPanelPlayer])     m_player->render();
    if (m_showPanel[kPanelOutput])     m_output->render();
    if (m_showPanel[kPanelScript])     m_scriptEditor->render();
    m_scriptEditor->renderFindAll();
    m_scriptEditor->renderAnalysis();
    m_scriptEditor->renderDependencyMap();
    renderServerDialog();
    renderPublishDialog();
    renderPublishModelDialog();
    renderMakeGearDialog();
    renderRecoverDialog();
    autoSaveTick();
    renderAccessoryWindow();
    renderBundleWindow();
    renderArmature();
    renderUVEditor();
    renderPluginLibrary();
    if (m_showPanel[kPanelCommandBar]) renderCommandBar();
    renderInsertObject();
    renderDialogs();
    renderShortcuts();
    renderModelingMenus();
    renderCollisionGroups();
    renderBulkEdit();
    if (m_showPanel[kPanelTeam]) renderTeamPanel();
    m_assistant->update();
    if (m_showPanel[kPanelAssistant]) m_assistant->render(&m_showPanel[kPanelAssistant]);
    if (m_showPanel[kPanelAnimation] && !m_playing) {
        // A tab next to Output (like Roblox's, along the bottom), in front when just opened.
        if (ImGuiWindow* out = ImGui::FindWindowByName("Output"); out && out->DockId)
            ImGui::SetNextWindowDockID(out->DockId, ImGuiCond_FirstUseEver);
        if (m_focusAnim) { ImGui::SetNextWindowFocus(); m_focusAnim = false; }
        m_animEditor->render(&m_showPanel[kPanelAnimation]);
    }
    if (!m_showPanel[kPanelAnimation] && m_animEditor->editing()) m_animEditor->close();
    SettingsWindow::draw(&m_showSettings);
    if (UpdateToast::draw("GutsAndBolts", !m_dirty)) glfwSetWindowShouldClose(m_window, GLFW_TRUE);

    // Frame delta captures pacing between calls; CPU time measures the editor's own
    // render/update work before the monitor is drawn.
    // Keep Edit and Play/Simulate samples separate so the doctor does not attribute
    // editor-only frame deltas to a game session (or vice versa).
    if (m_perfSampleMode != m_playing) {
        m_perfFrameSamples.clear();
        m_perfSampleMode = m_playing;
    }
    m_perfFrameSamples.push_back(std::max(0.0f, dt * 1000.0f));
    if (m_perfFrameSamples.size() > 180) m_perfFrameSamples.erase(m_perfFrameSamples.begin());
    m_perfCpuFrameMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - frameStart).count();
    renderPerformanceWindow();
    renderPerformanceDoctor();
    renderLuaDebugger();
    renderSceneValidator();
    renderProjectTimeMachine();

    trackChanges();
    updateTitle();
}

// Live frame-pacing and scene-complexity monitor. Timings are sampled on the editor
// thread; viewport time includes viewport rendering, and simulation time is the GameSession
// update. This is not a GPU timestamp profiler.
void Editor::renderPerformanceWindow() {
    if (!m_showPerformance) return;
    ImGui::SetNextWindowSize(ImVec2(430, 390), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Performance Monitor", &m_showPerformance)) { ImGui::End(); return; }

    std::vector<float> sorted = m_perfFrameSamples;
    std::sort(sorted.begin(), sorted.end());
    float average = 0.0f;
    for (float ms : sorted) average += ms;
    if (!sorted.empty()) average /= (float)sorted.size();
    const float minimum = sorted.empty() ? 0.0f : sorted.front();
    const float maximum = sorted.empty() ? 0.0f : sorted.back();
    const size_t p95Index = sorted.empty() ? 0 : std::min(sorted.size() - 1, (sorted.size() * 95) / 100);
    const float p95 = sorted.empty() ? 0.0f : sorted[p95Index];
    const float fps = average > 0.001f ? 1000.0f / average : 0.0f;

    ImGui::Text("Frame rate");
    ImGui::SameLine();
    ImGui::TextColored(fps >= 58.0f ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
                                    : fps >= 30.0f ? ImVec4(0.95f, 0.75f, 0.25f, 1.0f)
                                                   : ImVec4(1.0f, 0.4f, 0.35f, 1.0f),
                       "%.1f FPS", fps);
    if (!m_perfFrameSamples.empty()) {
        ImGui::Text("Frame time   avg %.2f ms   p95 %.2f ms", average, p95);
        ImGui::Text("Range        %.2f - %.2f ms", minimum, maximum);
        m_perfHitchCount = (int)std::count_if(m_perfFrameSamples.begin(), m_perfFrameSamples.end(),
                                              [](float ms) { return ms > 33.0f; });
        ImGui::Text("Hitches over 33 ms: %d in the last %d samples",
                    m_perfHitchCount, (int)m_perfFrameSamples.size());
        ImGui::PlotLines("Frame time (ms)", m_perfFrameSamples.data(), (int)m_perfFrameSamples.size(),
                         0, nullptr, 0.0f, std::max(33.0f, maximum), ImVec2(-1, 72));
    } else {
        ImGui::TextDisabled("Waiting for frame samples...");
    }
    ImGui::SeparatorText("CPU time (editor thread)");
    ImGui::Text("Editor update / UI: %.2f ms", m_perfCpuFrameMs);
    ImGui::Text("Viewport:           %.2f ms", m_perfViewportMs);
    ImGui::Text("Simulation:         %.2f ms", m_perfSimulationMs);
    ImGui::TextDisabled("Frame delta includes waiting between frames. CPU time excludes GPU execution and presentation.");

    int objects = 0, parts = 0, dynamicParts = 0, lights = 0, emitters = 0, scripts = 0;
    m_scene->forEach([&](SceneNode* n) {
        if (n->internal) return;
        ++objects;
        if (n->isPart()) { ++parts; if (!n->anchored) ++dynamicParts; }
        if (n->isLight() && n->enabled) ++lights;
        if (n->isParticles() && n->enabled) ++emitters;
        if (n->isScript()) ++scripts;
    });
    ImGui::SeparatorText("Scene");
    if (ImGui::BeginTable("##performanceScene", 2,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH |
                          ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        auto metric = [](const char* label, int value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
            ImGui::TableNextColumn(); ImGui::Text("%d", value);
        };
        metric("Objects", objects);
        metric("Parts", parts);
        metric("Unanchored parts", dynamicParts);
        metric("Enabled lights", lights);
        metric("Enabled particle emitters", emitters);
        metric("Scripts", scripts);
        ImGui::EndTable();
    }
    if (ImGui::Button("Copy performance report")) {
        std::ostringstream report;
        report << "Guts&Bolts Studio performance report\n"
               << "Average FPS: " << fps << "\n"
               << "Average frame time (ms): " << average << "\n"
               << "P95 frame time (ms): " << p95 << "\n"
               << "Maximum frame time (ms): " << maximum << "\n"
               << "Frames over 33 ms in rolling window: " << m_perfHitchCount << "/"
               << m_perfFrameSamples.size() << "\n"
               << "Editor update / UI (ms): " << m_perfCpuFrameMs << "\n"
               << "Viewport CPU time (ms): " << m_perfViewportMs << "\n"
               << "Simulation update (ms): " << m_perfSimulationMs << "\n"
               << "Scene objects: " << objects << "\n"
               << "Parts: " << parts << "\n"
               << "Unanchored parts: " << dynamicParts << "\n"
               << "Enabled lights: " << lights << "\n"
               << "Enabled particle emitters: " << emitters << "\n"
               << "Scripts: " << scripts << "\n"
               << "Note: CPU timings exclude GPU execution and presentation; frame delta includes waiting/presentation time.";
        const std::string reportText = report.str();
        ImGui::SetClipboardText(reportText.c_str());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Copies timings and scene counts; CPU timings are not GPU measurements.");
    ImGui::TextDisabled("Use Play / Simulate and compare frame-time samples before and after changes.");
    ImGui::End();
}

// Performance Doctor turns the monitor's recent samples into ranked, explainable advice.
// Scene thresholds are heuristics; they are not a claim that a particular count caused a hitch.
void Editor::renderLuaDebugger() {
    if (!m_showLuaDebugger) return;
    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Lua Debugger", &m_showLuaDebugger)) { ImGui::End(); return; }
    if (!m_session || !m_session->running()) {
        ImGui::TextDisabled("Start Play or Simulate to debug scripts. Breakpoints can be configured during the session.");
        ImGui::End(); return;
    }
    ScriptEngine& scripts=m_session->scripts();
    SceneNode* selected=m_scene->findById(m_scriptEditor->current());
    if (!selected || !selected->isScript()) selected=m_scene->selected();
    if (selected && selected->isScript()) {
        ImGui::Text("Script: %s", selected->fullName().c_str());
        ImGui::SetNextItemWidth(90);
        ImGui::InputInt("Line", &m_debugLineInput);
        if (m_debugLineInput<1) m_debugLineInput=1;
        ImGui::SameLine();
        const auto key=std::make_pair(selected->id,m_debugLineInput);
        const bool exists=scripts.debugBreakpoints().count(key)!=0;
        if (ImGui::Button(exists?"Remove breakpoint":"Add breakpoint"))
            scripts.debugSetBreakpoint(selected->id,m_debugLineInput,!exists);
    } else {
        ImGui::TextDisabled("Select a Script in the Explorer or open it in the Script Editor to add a breakpoint.");
    }
    ImGui::Separator();
    if (scripts.debugPaused()) {
        ImGui::TextColored(ImVec4(1.0f,0.72f,0.25f,1.0f),"Paused at %s:%d",
                           scripts.debugSourceForUi().c_str(),scripts.debugLine());
        if (ImGui::Button("Continue")) scripts.debugContinue();
        ImGui::SameLine(); if (ImGui::Button("Step Into")) scripts.debugStepInto();
        ImGui::SameLine(); if (ImGui::Button("Step Over")) scripts.debugStepOver();
        ImGui::SameLine(); if (ImGui::Button("Step Out")) scripts.debugStepOut();
        ImGui::SeparatorText("Call stack");
        for (const auto& frame:scripts.debugCallStack()) ImGui::BulletText("%s",frame.c_str());
        ImGui::SeparatorText("Locals");
        for (const auto& local:scripts.debugLocals()) ImGui::BulletText("%s",local.c_str());
    } else {
        ImGui::TextDisabled("Waiting for a breakpoint. Script execution pauses only at Lua line boundaries.");
    }
    ImGui::SeparatorText("Breakpoints");
    if (scripts.debugBreakpoints().empty()) ImGui::TextDisabled("No breakpoints configured.");
    for (const auto& bp:scripts.debugBreakpoints()) {
        SceneNode* node=m_scene->findById(bp.first);
        ImGui::PushID((int)(bp.first & 0x7fffffff)); ImGui::PushID(bp.second);
        ImGui::BulletText("%s:%d",node?node->fullName().c_str():"[deleted script]",bp.second);
        ImGui::SameLine(); if (ImGui::SmallButton("Remove")) scripts.debugSetBreakpoint(bp.first,bp.second,false);
        ImGui::PopID(); ImGui::PopID();
    }
    ImGui::TextDisabled("Locals are read without evaluating expressions. Tables are intentionally not expanded.");
    ImGui::End();
}


void Editor::renderPerformanceDoctor() {
    if (!m_showPerformanceDoctor) return;
    ImGui::SetNextWindowSize(ImVec2(700, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Performance Doctor", &m_showPerformanceDoctor)) { ImGui::End(); return; }

    const bool rescanScene = ImGui::Button("Rescan scene");
    ImGui::SameLine();
    ImGui::TextDisabled("Scene scan refreshes automatically about once per second.");

    if (rescanScene || m_perfDoctorLastScan < 0.0 || ImGui::GetTime() - m_perfDoctorLastScan >= 1.0) {
        m_perfDoctorObjects = m_perfDoctorParts = m_perfDoctorDynamicParts = 0;
        m_perfDoctorLights = m_perfDoctorEmitters = m_perfDoctorScriptsCount = 0;
        m_perfDoctorTotalScriptBytes = 0;
        m_perfDoctorLargestScripts.clear();
        m_scene->forEach([&](SceneNode* node) {
            if (node->internal) return;
            ++m_perfDoctorObjects;
            if (node->isPart()) {
                ++m_perfDoctorParts;
                if (!node->anchored) ++m_perfDoctorDynamicParts;
            }
            if (node->isLight() && node->enabled) ++m_perfDoctorLights;
            if (node->isParticles() && node->enabled) ++m_perfDoctorEmitters;
            if (node->isScript()) {
                ++m_perfDoctorScriptsCount;
                m_perfDoctorTotalScriptBytes += node->source.size();
                m_perfDoctorLargestScripts.push_back({node->id, node->fullName(), node->source.size()});
            }
        });
        std::sort(m_perfDoctorLargestScripts.begin(), m_perfDoctorLargestScripts.end(),
                  [](const PerformanceDoctorScript& a, const PerformanceDoctorScript& b) {
                      return a.sourceBytes > b.sourceBytes;
                  });
        if (m_perfDoctorLargestScripts.size() > 5) m_perfDoctorLargestScripts.resize(5);
        m_perfDoctorLastScan = ImGui::GetTime();
    }

    std::vector<float> sorted = m_perfFrameSamples;
    std::sort(sorted.begin(), sorted.end());
    const int sampleCount = (int)sorted.size();
    float averageMs = 0.0f;
    for (float ms : sorted) averageMs += ms;
    if (!sorted.empty()) averageMs /= (float)sorted.size();
    const float p95Ms = sorted.empty() ? 0.0f : sorted[std::min(sorted.size() - 1, (sorted.size() * 95) / 100)];
    const float maxMs = sorted.empty() ? 0.0f : sorted.back();
    const int hitches = (int)std::count_if(sorted.begin(), sorted.end(), [](float ms) { return ms > 33.0f; });
    const float hitchPercent = sampleCount ? 100.0f * (float)hitches / (float)sampleCount : 0.0f;
    const float averageFps = averageMs > 0.001f ? 1000.0f / averageMs : 0.0f;

    struct Finding {
        int severity; // 2 critical, 1 warning, 0 information
        std::string title;
        std::string evidence;
        std::string action;
    };
    std::vector<Finding> findings;
    auto addFinding = [&](int severity, const std::string& title,
                          const std::string& evidence, const std::string& action) {
        findings.push_back({severity, title, evidence, action});
    };

    if (sampleCount < 30) {
        addFinding(0, "Collect more measurements",
                   "Only " + std::to_string(sampleCount) + " frame samples are available; short samples can exaggerate one-off spikes.",
                   "Run Play or Simulate while reproducing the slowdown, then review the report again.");
    } else {
        if (p95Ms >= 33.0f)
            addFinding(2, "Frame pacing is poor",
                       "The 95th-percentile frame delta is " + std::to_string((int)(p95Ms * 100) / 100.0f) +
                       " ms; the slowest tail of the sample window is around 30 FPS or worse.",
                       "Reproduce the hitch and compare the editor-thread, viewport and simulation timings below.");
        else if (p95Ms >= 20.0f)
            addFinding(1, "Frame pacing is inconsistent",
                       "The 95th-percentile frame delta is " + std::to_string((int)(p95Ms * 100) / 100.0f) + " ms.",
                       "Use the Performance Monitor graph to check whether the spikes are isolated or continuous.");

        if (hitchPercent >= 25.0f)
            addFinding(2, "Frequent long frames",
                       std::to_string(hitches) + " of " + std::to_string(sampleCount) +
                       " samples exceed 33 ms (" + std::to_string((int)(hitchPercent * 10) / 10.0f) + "%).",
                       "Check whether the hitches line up with physics, script work, importing or other editor operations.");
        else if (hitchPercent >= 8.0f)
            addFinding(1, "Occasional long frames",
                       std::to_string(hitches) + " of " + std::to_string(sampleCount) +
                       " samples exceed 33 ms (" + std::to_string((int)(hitchPercent * 10) / 10.0f) + "%).",
                       "Repeat the same playtest after one change at a time to identify which change affects the hitch count.");
    }

    if (m_perfCpuFrameMs >= 33.0f)
        addFinding(2, "Editor-thread work is taking a long time",
                   "The latest measured editor update and UI work took " +
                   std::to_string((int)(m_perfCpuFrameMs * 100) / 100.0f) + " ms.",
                   "Close panels you do not need and compare the result. If this occurs during Play, use script and scene diagnostics before changing content.");
    else if (m_perfCpuFrameMs >= 16.7f)
        addFinding(1, "Editor-thread work uses the frame budget",
                   "The latest editor update and UI measurement is at or above 16.7 ms.",
                   "Check the viewport and simulation subtimings to narrow down where the measured CPU work is concentrated.");

    if (m_perfViewportMs >= 20.0f)
        addFinding(2, "Viewport CPU work is high",
                   "Viewport rendering took " + std::to_string((int)(m_perfViewportMs * 100) / 100.0f) + " ms in the latest sample.",
                   "Compare with fewer visible objects, lights and effects. This is CPU-side timing, not a direct GPU measurement.");
    else if (m_perfViewportMs >= 10.0f)
        addFinding(1, "Viewport CPU work may be a bottleneck",
                   "Viewport rendering took " + std::to_string((int)(m_perfViewportMs * 100) / 100.0f) + " ms in the latest sample.",
                   "Temporarily reduce visible scene complexity and compare timings. Use a GPU profiler to confirm any GPU limitation.");

    if (m_playing && m_perfSimulationMs >= 16.7f)
        addFinding(2, "Simulation step exceeds a 60 FPS budget",
                   "The latest GameSession update took " + std::to_string((int)(m_perfSimulationMs * 100) / 100.0f) + " ms.",
                   "Inspect unanchored parts, constraints and scripts that do repeated work during simulation; change one variable and measure again.");
    else if (m_playing && m_perfSimulationMs >= 8.0f)
        addFinding(1, "Simulation is using a large share of the frame",
                   "The latest GameSession update took " + std::to_string((int)(m_perfSimulationMs * 100) / 100.0f) + " ms.",
                   "Check the physics-heavy scene indicators and test with fewer moving bodies to see whether simulation time changes.");

    if (m_perfDoctorDynamicParts >= 2500)
        addFinding(2, "Very high number of unanchored parts",
                   std::to_string(m_perfDoctorDynamicParts) + " unanchored parts were found in the scene.",
                   "Anchor parts that should not move, simplify repeated assemblies, and compare a playtest. This count is a risk indicator, not proof of the cause.");
    else if (m_perfDoctorDynamicParts >= 800)
        addFinding(1, "Many unanchored parts",
                   std::to_string(m_perfDoctorDynamicParts) + " unanchored parts were found in the scene.",
                   "Review which parts actually need physics. Static floors, walls and decorations should usually be anchored.");

    if (m_perfDoctorParts >= 10000)
        addFinding(2, "Very large part count",
                   std::to_string(m_perfDoctorParts) + " parts were found in the scene.",
                   "Simplify distant or hidden geometry and reuse meshes where appropriate; verify the change in a repeatable playtest.");
    else if (m_perfDoctorParts >= 4000)
        addFinding(1, "Large part count",
                   std::to_string(m_perfDoctorParts) + " parts were found in the scene.",
                   "Inspect repeated small parts and hidden geometry. The best fix depends on visibility, collision and target hardware.");

    if (m_perfDoctorLights >= 128)
        addFinding(2, "Many enabled lights",
                   std::to_string(m_perfDoctorLights) + " enabled lights were found in the scene.",
                   "Disable lights that do not contribute to the current view and compare the result on target hardware.");
    else if (m_perfDoctorLights >= 64)
        addFinding(1, "High enabled-light count",
                   std::to_string(m_perfDoctorLights) + " enabled lights were found in the scene.",
                   "Review overlapping lights and remove lights that do not change the scene meaningfully.");

    if (m_perfDoctorEmitters >= 64)
        addFinding(2, "Many enabled particle emitters",
                   std::to_string(m_perfDoctorEmitters) + " enabled emitters were found in the scene.",
                   "Check emitter Rate, lifetime and whether off-screen effects should be disabled.");
    else if (m_perfDoctorEmitters >= 32)
        addFinding(1, "High particle-emitter count",
                   std::to_string(m_perfDoctorEmitters) + " enabled emitters were found in the scene.",
                   "Review active emission rates and lifetimes; count alone does not measure how many particles are being drawn.");

    if (m_perfDoctorTotalScriptBytes >= 1024 * 1024)
        addFinding(1, "Large total script source",
                   std::to_string(m_perfDoctorTotalScriptBytes / 1024) + " KiB of script source was found in the scene.",
                   "This is not execution time. Inspect large scripts for unbounded loops or repeated expensive work, then measure during Play.");

    std::stable_sort(findings.begin(), findings.end(),
                     [](const Finding& a, const Finding& b) { return a.severity > b.severity; });

    int critical = 0, warnings = 0;
    for (const auto& finding : findings) {
        if (finding.severity == 2) ++critical;
        else if (finding.severity == 1) ++warnings;
    }
    if (critical) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "Diagnosis: %d critical indicator(s), %d warning(s)", critical, warnings);
    } else if (warnings) {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "Diagnosis: %d warning(s); %d critical indicator(s)", warnings, critical);
    } else if (sampleCount < 30) {
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "Diagnosis: collecting enough frame samples");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.82f, 0.5f, 1.0f), "Diagnosis: no major hotspot detected by the current heuristics");
    }

    ImGui::Text("%s mode | %d samples | average %.1f FPS | p95 %.2f ms | peak %.2f ms",
                m_playing ? "Play / Simulate" : "Edit", sampleCount, averageFps, p95Ms, maxMs);
    ImGui::TextDisabled("Frame deltas include waiting/presentation. CPU subtimings exclude GPU execution and may miss short spikes.");
    ImGui::SeparatorText("Findings and recommended next steps");
    if (findings.empty()) {
        ImGui::TextDisabled("No threshold was crossed. If the game still feels slow, capture measurements while reproducing the problem.");
    }
    ImGui::BeginChild("##performanceDoctorFindings", ImVec2(0, 225), true);
    for (size_t i = 0; i < findings.size(); ++i) {
        const Finding& f = findings[i];
        ImGui::PushID((int)i);
        const ImVec4 color = f.severity == 2 ? ImVec4(1.0f, 0.38f, 0.32f, 1.0f)
                              : f.severity == 1 ? ImVec4(0.95f, 0.73f, 0.3f, 1.0f)
                                                : ImVec4(0.55f, 0.75f, 0.95f, 1.0f);
        ImGui::TextColored(color, "%s", f.severity == 2 ? "CRITICAL" : f.severity == 1 ? "WARNING" : "INFO");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", f.title.c_str());
        ImGui::TextWrapped("Evidence: %s", f.evidence.c_str());
        ImGui::TextWrapped("Try: %s", f.action.c_str());
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Scene context");
    if (ImGui::BeginTable("##performanceDoctorScene", 2,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();
        auto metric = [](const char* label, long long value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
            ImGui::TableNextColumn(); ImGui::Text("%lld", value);
        };
        metric("Objects", m_perfDoctorObjects);
        metric("Parts", m_perfDoctorParts);
        metric("Unanchored parts", m_perfDoctorDynamicParts);
        metric("Enabled lights", m_perfDoctorLights);
        metric("Enabled particle emitters", m_perfDoctorEmitters);
        metric("Scripts", m_perfDoctorScriptsCount);
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Scene counts are clues, not direct performance measurements. Thresholds are intentionally conservative heuristics.");

    if (!m_perfDoctorLargestScripts.empty()) {
        ImGui::SeparatorText("Largest script sources (not execution-time rankings)");
        for (const auto& script : m_perfDoctorLargestScripts) {
            ImGui::PushID((int)(script.id & 0x7fffffff));
            ImGui::Text("%s", script.path.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%zu bytes", script.sourceBytes);
            ImGui::SameLine();
            if (ImGui::SmallButton("Open")) {
                if (SceneNode* node = m_scene->findById(script.id)) {
                    m_scene->select(node);
                    m_scriptEditor->open(script.id);
                    m_showPanel[kPanelScript] = true;
                }
            }
            ImGui::PopID();
        }
    }

    if (ImGui::Button("Open Performance Monitor")) m_showPerformance = true;
    ImGui::SameLine();
    if (ImGui::Button("Open Script Analysis")) m_scriptEditor->showAnalysis();
    ImGui::SameLine();
    if (ImGui::Button("Copy diagnosis report")) {
        std::ostringstream report;
        report << "Guts&Bolts Performance Doctor\n"
               << "Mode: " << (m_playing ? "Play / Simulate" : "Edit") << "\n"
               << "Samples: " << sampleCount << "\n"
               << "Average FPS: " << averageFps << "\n"
               << "Average frame delta (ms): " << averageMs << "\n"
               << "P95 frame delta (ms): " << p95Ms << "\n"
               << "Peak frame delta (ms): " << maxMs << "\n"
               << "Frames over 33 ms: " << hitches << "/" << sampleCount << "\n"
               << "Editor-thread update/UI (ms): " << m_perfCpuFrameMs << "\n"
               << "Viewport CPU (ms): " << m_perfViewportMs << "\n"
               << "Simulation update (ms): " << m_perfSimulationMs << "\n"
               << "Objects: " << m_perfDoctorObjects << "\n"
               << "Parts: " << m_perfDoctorParts << "\n"
               << "Unanchored parts: " << m_perfDoctorDynamicParts << "\n"
               << "Enabled lights: " << m_perfDoctorLights << "\n"
               << "Enabled particle emitters: " << m_perfDoctorEmitters << "\n"
               << "Scripts: " << m_perfDoctorScriptsCount << "\n"
               << "Total script source bytes: " << m_perfDoctorTotalScriptBytes << "\n"
               << "\nFindings:\n";
        for (const auto& finding : findings) {
            report << (finding.severity == 2 ? "[CRITICAL] " : finding.severity == 1 ? "[WARNING] " : "[INFO] ")
                   << finding.title << "\n  Evidence: " << finding.evidence << "\n  Try: " << finding.action << "\n";
        }
        report << "\nLimitations: frame delta includes waiting/presentation; CPU subtimings exclude GPU execution. "
                  "Scene counts and thresholds are heuristic risk indicators, not proof of cause.";
        ImGui::SetClipboardText(report.str().c_str());
    }
    ImGui::End();
}

// Scene Validator checks saved scene relationships and common setup mistakes without
// executing scripts or modifying the project. Findings are cached while the window is open.
void Editor::renderSceneValidator() {
    if (!m_showSceneValidator) return;
    ImGui::SetNextWindowSize(ImVec2(860, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene Validator", &m_showSceneValidator)) { ImGui::End(); return; }

    const bool manualScan = ImGui::Button("Validate scene");
    ImGui::SameLine();
    ImGui::TextDisabled("Rescans automatically every 2 seconds; no changes are made to your project.");

    if (manualScan || m_sceneValidatorLastScan < 0.0 ||
        ImGui::GetTime() - m_sceneValidatorLastScan >= 2.0) {
        m_sceneValidationIssues.clear();
        m_sceneValidatorObjectCount = 0;
        std::vector<SceneNode*> nodes;
        std::unordered_map<uint64_t, SceneNode*> byId;
        m_scene->forEach([&](SceneNode* node) {
            if (!node) return;
            byId[node->id] = node;
            if (!node->internal) nodes.push_back(node);
        });
        m_sceneValidatorObjectCount = (int)nodes.size();

        auto add = [&](SceneNode* node, int severity, const char* category,
                       const std::string& message, const std::string& recommendation) {
            m_sceneValidationIssues.push_back({
                node ? node->id : 0, severity,
                node ? node->fullName() : std::string("Scene"),
                category, message, recommendation
            });
        };
        auto reference = [&](SceneNode* owner, uint64_t id, const char* label,
                             const char* expected, const std::string& recommendation) -> SceneNode* {
            if (id == 0) {
                add(owner, 1, "Missing reference",
                    std::string(label) + " is not assigned.",
                    recommendation);
                return nullptr;
            }
            auto it = byId.find(id);
            if (it == byId.end()) {
                add(owner, 2, "Broken reference",
                    std::string(label) + " points to an object that no longer exists.",
                    recommendation);
                return nullptr;
            }
            SceneNode* target = it->second;
            bool valid = true;
            if (std::string(expected) == "Part") valid = target->isPart();
            else if (std::string(expected) == "Attachment") valid = target->isAttachment();
            else if (std::string(expected) == "SoundGroup") valid = target->isSoundGroup();
            else if (std::string(expected) == "FluidSystem") valid = target->kind == NodeKind::FluidSystem;
            if (!valid) {
                add(owner, 2, "Wrong reference type",
                    std::string(label) + " must reference a " + expected + ".",
                    recommendation);
                return nullptr;
            }
            return target;
        };

        for (SceneNode* node : nodes) {
            const auto finite3 = [](const glm::vec3& v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            };
            if (!finite3(node->transform.position) || !finite3(node->transform.rotation) ||
                !finite3(node->transform.scale)) {
                add(node, 2, "Invalid transform", "Position, rotation, or scale contains a non-finite number.",
                    "Reset the affected transform values to finite numbers in Properties.");
            }
            if (node->isPart() &&
                (std::abs(node->transform.scale.x) < 0.000001f ||
                 std::abs(node->transform.scale.y) < 0.000001f ||
                 std::abs(node->transform.scale.z) < 0.000001f)) {
                add(node, 1, "Degenerate scale", "This part has a zero or near-zero scale on at least one axis.",
                    "Check whether the part should be visible or collide; restore a non-zero scale if this was accidental.");
            }
            if (node->kind == NodeKind::Part && !node->mesh) {
                add(node, 2, "Missing geometry", "This object is typed as a Part but has no mesh assigned.",
                    "Choose a primitive shape or restore the missing mesh before publishing.");
            }
            if (node->name.empty()) {
                add(node, 1, "Empty name", "This object has an empty name.",
                    "Give it a descriptive name so it is easier to find and reference from scripts.");
            }

            if (node->primaryPart != 0) {
                SceneNode* primary = reference(node, node->primaryPart, "PrimaryPart", "Part",
                    "Assign the model's PrimaryPart to a Part contained inside that model.");
                if (primary && !node->isAncestorOf(primary)) {
                    add(node, 2, "Invalid PrimaryPart", "PrimaryPart is not a descendant of this object.",
                        "Choose a Part inside this model as its PrimaryPart, or clear the reference.");
                }
            }

            if (node->isConstraint()) {
                const bool weld = node->constraintType == ConstraintType::Weld;
                const char* expected = weld ? "Part" : "Attachment";
                reference(node, node->ref0, weld ? "Part0" : "Attachment0", expected,
                          "Assign this constraint to two existing endpoints of the required type.");
                reference(node, node->ref1, weld ? "Part1" : "Attachment1", expected,
                          "Assign this constraint to two existing endpoints of the required type.");
            }
            if (node->isEffect()) {
                reference(node, node->effect.a0, "Attachment0", "Attachment",
                          "Choose an Attachment for the first end of this Trail or Beam.");
                reference(node, node->effect.a1, "Attachment1", "Attachment",
                          "Choose an Attachment for the second end of this Trail or Beam.");
            }
            if (node->isHighlight() && node->highlight.adornee != 0) {
                reference(node, node->highlight.adornee, "Adornee", "any",
                          "Choose an existing object to highlight or clear Adornee to use its parent.");
            }
            if (node->isSound() && node->soundGroup != 0) {
                reference(node, node->soundGroup, "SoundGroup", "SoundGroup",
                          "Assign an existing SoundGroup or clear the SoundGroup property.");
            }
            if (node->kind == NodeKind::FluidEmitter && node->fluidSystem != 0) {
                reference(node, node->fluidSystem, "FluidSystem", "FluidSystem",
                          "Assign an existing FluidSystem or clear the FluidSystem reference.");
            }
            if (node->isTool()) {
                SceneNode* handle = node->findChild("Handle", true);
                if (!handle || !handle->isPart()) {
                    add(node, 1, "Tool setup", "This Tool has no descendant Part named Handle.",
                        "Add a Part named Handle under the Tool; tools may not equip or grip correctly without it.");
                }
            }
            if (node->isSoundEffect() && node->parent &&
                !node->parent->isSound() && !node->parent->isSoundGroup()) {
                add(node, 1, "Sound effect placement", "This SoundEffect is not parented to a Sound or SoundGroup.",
                    "Move the effect under a Sound or SoundGroup so it can be applied during playback.");
            }
            if (node->isScript() && node->enabled && node->source.empty()) {
                add(node, 0, "Empty script", "This enabled script contains no source code.",
                    "Add the intended code or disable/remove the script if it is only a placeholder.");
            }
        }

        // Duplicate sibling names are valid (and common in Roblox projects).
        // FindFirstChild intentionally returns a matching child; scripts that need a
        // particular duplicate should use class/ancestry checks or iterate GetChildren().
 
        std::stable_sort(m_sceneValidationIssues.begin(), m_sceneValidationIssues.end(),
            [](const SceneValidationIssue& a, const SceneValidationIssue& b) {
                if (a.severity != b.severity) return a.severity > b.severity;
                if (a.path != b.path) return a.path < b.path;
                return a.category < b.category;
            });
        m_sceneValidatorLastScan = ImGui::GetTime();
    }

    int errors = 0, warnings = 0, info = 0;
    for (const auto& issue : m_sceneValidationIssues) {
        if (issue.severity == 2) ++errors;
        else if (issue.severity == 1) ++warnings;
        else ++info;
    }
    ImGui::Text("%d objects scanned", m_sceneValidatorObjectCount);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "%d errors", errors);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.73f, 0.3f, 1.0f), "%d warnings", warnings);
    ImGui::SameLine();
    ImGui::TextDisabled("%d info", info);

    const char* filters[] = {"All findings", "Errors", "Warnings", "Info"};
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("Severity", &m_sceneValidatorSeverityFilter, filters, IM_ARRAYSIZE(filters));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("Search", &m_sceneValidatorSearch);
    ImGui::SameLine();
    if (ImGui::Button("Copy report")) {
        std::ostringstream report;
        report << "Guts & Bolts Scene Validator\nObjects scanned: " << m_sceneValidatorObjectCount
               << "\nErrors: " << errors << "\nWarnings: " << warnings << "\nInfo: " << info << "\n\n";
        for (const auto& issue : m_sceneValidationIssues) {
            report << (issue.severity == 2 ? "[ERROR] " : issue.severity == 1 ? "[WARNING] " : "[INFO] ")
                   << issue.category << " | " << issue.path << "\n"
                   << "  " << issue.message << "\n  Recommendation: " << issue.recommendation << "\n\n";
        }
        ImGui::SetClipboardText(report.str().c_str());
    }

    ImGui::Separator();
    if (ImGui::BeginTable("##sceneValidatorResults", 4,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY, ImVec2(0, 390))) {
        ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Object / check", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("Finding and recommendation", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableHeadersRow();
        bool anyVisible = false;
        for (size_t i = 0; i < m_sceneValidationIssues.size(); ++i) {
            const auto& issue = m_sceneValidationIssues[i];
            if (m_sceneValidatorSeverityFilter == 1 && issue.severity != 2) continue;
            if (m_sceneValidatorSeverityFilter == 2 && issue.severity != 1) continue;
            if (m_sceneValidatorSeverityFilter == 3 && issue.severity != 0) continue;
            if (!m_sceneValidatorSearch.empty()) {
                const std::string haystack = issue.path + " " + issue.category + " " + issue.message;
                if (haystack.find(m_sceneValidatorSearch) == std::string::npos) continue;
            }
            anyVisible = true;
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const ImVec4 color = issue.severity == 2 ? ImVec4(1.0f, 0.38f, 0.32f, 1.0f)
                                  : issue.severity == 1 ? ImVec4(0.95f, 0.73f, 0.3f, 1.0f)
                                                        : ImVec4(0.55f, 0.75f, 0.95f, 1.0f);
            ImGui::TextColored(color, "%s", issue.severity == 2 ? "ERROR" : issue.severity == 1 ? "WARNING" : "INFO");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", issue.path.c_str());
            ImGui::TextDisabled("%s", issue.category.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", issue.message.c_str());
            ImGui::TextDisabled("Try: %s", issue.recommendation.c_str());
            ImGui::TableSetColumnIndex(3);
            if (issue.nodeId != 0 && ImGui::SmallButton("Select")) {
                if (SceneNode* node = m_scene->findById(issue.nodeId)) m_scene->select(node);
            }
            ImGui::PopID();
        }
        if (!anyVisible) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("No findings match this filter.");
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Validation is static and conservative. Review each recommendation; findings do not automatically change the scene.");
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Play mode
// ---------------------------------------------------------------------------

void Editor::togglePlay() {
    if (!m_playing) {
        m_animEditor->close();   // the rig goes back how it really is
        // Remember the world exactly as it is, so Stop can put it back.
        m_playSnapshot = Serializer::saveScene(*m_scene);
        m_playing = true;
        m_viewport->setSession(m_session.get());
        m_viewport->focus();
        if (m_state.mode == StudioMode::Modeling) exitModeling();
        m_state.mode = m_session->runOnly() ? StudioMode::Simulate : StudioMode::Play;
        m_state.simPaused = false;
        const int run = Log::beginRun();
        Log::system(m_session->runOnly() ? "Simulation started (run " + std::to_string(run) + ", no character) - Shift+F5 to stop."
                                         : "Game started (run " + std::to_string(run) + ").");
        m_session->start();
    } else {
        uint64_t sel = m_scene->selected() ? m_scene->selected()->id : 0;
        m_session->stop();
        m_session->setRunOnly(false);
        m_viewport->setSession(nullptr);
        m_playing = false;
        Serializer::loadScene(*m_scene, m_playSnapshot);
        m_scene->select(m_scene->findById(sel));
        m_committed = m_playSnapshot;
        m_state.mode = StudioMode::Build;
        m_state.simPaused = false;
        Log::system("Game stopped - everything is back to how it was.");
        Log::endRun();
    }
}

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

void Editor::resetHistory() {
    m_undo.clear();
    m_redo.clear();
    m_committed = Serializer::saveScene(*m_scene);
    m_dirty = false;
}

void Editor::trackChanges() {
    // Wait until a drag / text edit is finished so it becomes one undo step.
    if (m_playing || ImGui::IsAnyItemActive() || m_viewport->gizmoInUse() || m_uv.modal) return;
    std::string now = Serializer::saveScene(*m_scene);
    if (now == m_committed) return;
    m_undo.push_back(std::move(m_committed));
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_committed = std::move(now);
    m_dirty = true;
    m_team->localChanged();
}

void Editor::restore(const std::string& snapshot) {
    std::vector<uint64_t> sel;
    for (SceneNode* n : m_scene->selection()) sel.push_back(n->id);
    Serializer::loadScene(*m_scene, snapshot);
    for (uint64_t id : sel) m_scene->addToSelection(m_scene->findById(id));
    m_committed = snapshot;
    m_dirty = true;
    m_team->localChanged();
}

void Editor::undo() {
    if (m_playing || m_undo.empty()) return;
    m_redo.push_back(m_committed);
    std::string s = std::move(m_undo.back());
    m_undo.pop_back();
    restore(s);
}

void Editor::redo() {
    if (m_playing || m_redo.empty()) return;
    m_undo.push_back(m_committed);
    std::string s = std::move(m_redo.back());
    m_redo.pop_back();
    restore(s);
}

// ---------------------------------------------------------------------------
// Creating things
// ---------------------------------------------------------------------------

glm::vec3 Editor::spawnPoint() const {
    glm::vec3 p = m_viewport->cameraPivot();
    return {std::round(p.x), 0.0f, std::round(p.z)};
}

bool Editor::canEdit(const SceneNode* n) const {
    return n && !m_scene->isProtected(n) && !m_scene->isCharacterPart(n);
}

SceneNode* Editor::addPrimitive(const char* label, PrimitiveType type) {
    ++m_objCounter;
    std::string name = std::string(label) + " " + std::to_string(m_objCounter);
    auto* node = m_scene->addNode(name, type, MeshLibrary::get(type));
    node->transform.position = spawnPoint() + glm::vec3(0, type == PrimitiveType::Plane ? 0.01f : 0.5f, 0);
    m_scene->select(node);
    return node;
}

void Editor::spawnPrimitive(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube:     addPrimitive("Cube",     type); break;
        case PrimitiveType::Sphere:   addPrimitive("Sphere",   type); break;
        case PrimitiveType::Plane:    addPrimitive("Plane",    type); break;
        case PrimitiveType::Cylinder: addPrimitive("Cylinder", type); break;
        default: break;
    }
}

// A new MeshPart (a cube to start from), straight into Modeling mode.
void Editor::addMeshPart() {
    SceneNode* n = addPrimitive("MeshPart", PrimitiveType::Cube);
    MeshEdit::attach(*n, MeshEdit::fromPrimitive(PrimitiveType::Cube));
    n->transform.scale = glm::vec3(2.0f);
    n->transform.position.y += 0.5f;
    setMode(StudioMode::Modeling);
}

void Editor::spawnPremade(Premade kind) {
    if (SceneNode* n = buildPremade(*m_scene, kind, spawnPoint())) m_scene->select(n);
}

void Editor::addScript(SceneNode* parent, bool local) {
    // Scripts can't go inside other scripts or the character.
    if (!parent || parent->isScript() || m_scene->isCharacterPart(parent)) parent = m_scene->root();
    auto s = std::make_unique<SceneNode>(local ? "LocalScript" : "Script", NodeKind::Script);
    s->isLocal = local;
    s->source = local ? kNewLocalScript : kNewScript;
    SceneNode* raw = m_scene->insert(std::move(s), parent);
    m_scene->select(raw);
    openScript(raw);
}

void Editor::openAnimationEditor() {
    m_showPanel[kPanelAnimation] = true;
    m_focusAnim = true;
    SceneNode* sel = m_scene->selected();
    if (sel && !m_animEditor->editing() && (Anim::rigOf(sel) || (sel->kind == NodeKind::Model && sel->parent)) &&
        !Anim::rigParts(sel->kind == NodeKind::Model && sel->parent ? sel : Anim::rigOf(sel)).empty())
        m_animEditor->editRig(sel);
}

void Editor::addModel() {
    SceneNode* m = m_scene->insert(std::make_unique<SceneNode>("Model", NodeKind::Model));
    m_scene->select(m);
}

void Editor::startTeamCreate(bool host, const std::string& address) {
    std::string err;
    bool ok = host ? m_team->host(kTeamCreatePort, err) : m_team->join(address, err);
    if (!ok) Log::error("Team Create: " + err);
}

void Editor::testInsert(const std::string& names) {
    std::stringstream ss(names);
    std::string w;
    while (ss >> w) insertObject(w, m_scene->selected());
}

void Editor::testAddPart(const std::string& name) {
    SceneNode* n = addPrimitive("Cube", PrimitiveType::Cube);
    n->name = name;
    n->color = {0.2f, 0.4f, 1.0f};
    n->transform.position = {-3, 0.5f, 2};
}

void Editor::testExportRoblox(const std::string& path) {
    std::string err;
    if (RobloxFile::exportPlace(*m_scene, path, err)) Log::system("Exported " + path);
    else Log::error(err);
}

void Editor::testAnimation(int frame) {
    auto rig = [&]() -> SceneNode* {
        SceneNode* found = nullptr;
        m_scene->forEach([&](SceneNode* n) { if (!found && n->name == "Rig" && n->kind == NodeKind::Model) found = n; });
        return found;
    };
    auto arm = [&]() { SceneNode* r = rig(); return r ? r->findChild("Right Arm") : nullptr; };
    auto show = [&](const char* what) {
        SceneNode* a = arm();
        if (!a) return;
        glm::vec3 w(a->worldMatrix()[3]);
        char buf[200];
        std::snprintf(buf, sizeof(buf), "ANIMTEST %s t=%.2f arm local pos %.2f %.2f %.2f rot %.1f %.1f %.1f world %.2f %.2f %.2f",
                      what, m_animEditor->time(), a->transform.position.x, a->transform.position.y, a->transform.position.z,
                      a->transform.rotation.x, a->transform.rotation.y, a->transform.rotation.z, w.x, w.y, w.z);
        Log::info(buf);
    };
    switch (frame) {
    case 5:
        insertObject("Rig", nullptr);
        openAnimationEditor();
        break;
    case 8: {   // New animation
        auto a = std::make_unique<SceneNode>("Wave", NodeKind::Animation);
        a->source = Anim::emptyClipText();
        m_scene->insert(std::move(a), rig());
        break;
    }
    case 12:   // turn the arm forward at 0 s (like dragging the Rotate tool)
        m_scene->select(arm());
        arm()->transform.rotation.x = -90.0f;
        break;
    case 16:
        m_animEditor->testSetTime(1.0f);
        break;
    case 20:   // and up over the head at 1 s
        arm()->transform.rotation = glm::vec3(0.0f, 0.0f, 170.0f);
        break;
    case 24:
        m_animEditor->testSetTime(0.5f);
        break;
    case 28: {
        show("half-way");
        auto j = nlohmann::json::parse(Serializer::nodeToString(*arm()));
        Log::info("ANIMTEST saved arm pos " + j["pos"].dump() + " rot " + j["rot"].dump());
        SceneNode* w = nullptr;
        m_scene->forEach([&](SceneNode* n) { if (n->isAnimation()) w = n; });
        if (w) Log::info("ANIMTEST clip " + w->source);
        break;
    }
    case 30:
        m_animEditor->testSetTime(0.0f);
        break;
    case 34:
        show("at 0");
        undo();   // the last key (the arm up at 1 s) goes away
        break;
    case 38: {
        SceneNode* w = nullptr;
        m_scene->forEach([&](SceneNode* n) { if (n->isAnimation()) w = n; });
        if (w) Log::info("ANIMTEST after undo " + w->source);
        redo();
        m_animEditor->testSetTime(0.75f);
        break;
    }
    case 42:
        show("0.75 after redo");
        break;
    }
}

void Editor::testCollisions() {
    // Two 2-stud blocks, B at x = 5; slide A from x = 0 to x = 4.5 (into B), then turn it.
    auto block = [&](const char* name, float x) {
        SceneNode* n = addPrimitive("Cube", PrimitiveType::Cube);
        n->name = name;
        n->transform.position = {x, 20.0f, 0.0f};
        n->transform.scale = glm::vec3(2.0f);
        return n;
    };
    SceneNode* a = block("A", 0.0f);
    block("B", 5.0f);
    std::vector<SceneNode*> movers{a};
    std::vector<Transform> before{a->transform};
    auto hit0 = ViewportPanel::collisionsOf(*m_scene, movers);
    a->transform.position.x = 4.5f;
    ViewportPanel::stopAtCollisions(*m_scene, movers, before, hit0, true);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "COLLIDE slide: A.x = %.3f (flush = 3.000)", a->transform.position.x);
    Log::info(buf);
    before = {a->transform};
    hit0 = ViewportPanel::collisionsOf(*m_scene, movers);
    a->transform.rotation.y = 45.0f;   // a turned corner would poke into B
    ViewportPanel::stopAtCollisions(*m_scene, movers, before, hit0, false);
    std::snprintf(buf, sizeof(buf), "COLLIDE turn: A.rotY = %.1f (blocked = 0.0)", a->transform.rotation.y);
    Log::info(buf);
    a->transform.position.x = -3.0f;   // away from B, turning is fine
    before = {a->transform};
    hit0 = ViewportPanel::collisionsOf(*m_scene, movers);
    a->transform.rotation.y = 45.0f;
    ViewportPanel::stopAtCollisions(*m_scene, movers, before, hit0, false);
    std::snprintf(buf, sizeof(buf), "COLLIDE free turn: A.rotY = %.1f (45.0)", a->transform.rotation.y);
    Log::info(buf);
}

void Editor::testSelect(const std::string& names) {
    m_scene->deselect();
    std::string list = "," + names + ",";
    m_scene->forEach([&](SceneNode* n) {
        if (list.find("," + n->name + ",") != std::string::npos) m_scene->addToSelection(n);
    });
    Log::info("Selected " + std::to_string(m_scene->selection().size()) + " object(s)");
    if (SceneNode* one = m_scene->selected(); one && one->isScript() && m_scene->selection().size() == 1) openScript(one);
}

void Editor::testPremades(const std::string& list) {
    int i = 0;
    for (const PremadeInfo& p : premadeList()) {
        if (list != "all" && list.find(p.name) == std::string::npos) continue;
        glm::vec3 at((i % 4) * 14.0f - 21.0f, 0.0f, (i / 4) * -14.0f - 8.0f);
        buildPremade(*m_scene, p.kind, at);
        ++i;
    }
}

void Editor::testAnalysis() {
    m_scriptEditor->showAnalysis();
    m_scriptEditor->runAnalysis();
    for (const auto& is : m_scriptEditor->issues())
        std::printf("ANALYSIS %s %s:%d %s\n", is.error ? "error" : "warning", is.name.c_str(), is.line, is.text.c_str());
    std::fflush(stdout);
}

void Editor::testToolStep(const std::string& step) {
    if (!m_session || !m_session->running()) return;
    if (step.size() == 1 && step[0] >= '1' && step[0] <= '9') m_session->selectToolSlot(step[0] - '1');
    else if (step == "click") m_session->click(0);
    else if (step == "print" && m_scene->player()) {
        Player* p = m_scene->player();
        SceneNode* held = p->equippedTool();
        int rockets = 0, bombs = 0;
        m_scene->forEach([&](SceneNode* n) { rockets += n->name == "Rocket"; bombs += n->name == "Bomb" && n->isPart(); });
        std::printf("TOOLS held=%s slots=%d walk=%.1f jumpHeight=%.1f health=%.0f rockets=%d bombs=%d\n", held ? held->name.c_str() : "-",
                    (int)p->tools().size(), p->humanoid().walkSpeed, p->humanoid().jumpHeight, p->humanoid().health, rockets, bombs);
        std::fflush(stdout);
    }
}

void Editor::addLight(LightType type) {
    auto l = std::make_unique<SceneNode>(type == LightType::Spot ? "SpotLight" : "PointLight", NodeKind::Light);
    l->lightType = type;
    l->color     = {1.0f, 0.9f, 0.75f};
    SceneNode* parent = m_scene->selected();
    if (!parent || !parent->isPart() || m_scene->isCharacterPart(parent)) {
        // No part selected: float the light above the ground.
        parent = m_scene->root();
        l->transform.position = spawnPoint() + glm::vec3(0, 4, 0);
    }
    m_scene->select(m_scene->insert(std::move(l), parent));
}

void Editor::addSound() {
    SceneNode* parent = m_scene->selected();
    if (!parent || parent->isScript() || parent->isSound() || m_scene->isCharacterPart(parent))
        parent = m_scene->root();
    auto s = std::make_unique<SceneNode>("Sound", NodeKind::Sound);
    m_scene->select(m_scene->insert(std::move(s), parent));
}

void Editor::connectParts(int type, SceneNode* a, glm::vec3 pa, SceneNode* b, glm::vec3 pb) {
    if (!canEdit(a) || !canEdit(b)) {
        Log::warn("Constraints can only connect normal parts (not the character).");
        return;
    }
    bool motor = type == 5;
    // (Tool 5 is the motor, so the joints after it are one further along.)
    ConstraintType ct = motor ? ConstraintType::Hinge : (ConstraintType)(type > 5 ? type - 1 : type);
    // Hinges turn (and sliders slide) along the direction the first clicked surface faces.
    glm::mat4 wa = a->worldMatrix();
    glm::vec3 local = glm::vec3(glm::inverse(wa) * glm::vec4(pa, 1.0f));
    int ax = 0;
    for (int i = 1; i < 3; ++i) if (std::abs(local[i]) > std::abs(local[ax])) ax = i;
    glm::vec3 normal = glm::normalize(glm::vec3(wa[ax])) * (local[ax] >= 0 ? 1.0f : -1.0f);

    SceneNode* made = makeConstraint(*m_scene, ct, a, pa, b, pb, normal, motor);
    m_scene->select(made);
    Log::system(std::string("Connected ") + a->name + " and " + b->name + " with a " +
                (motor ? "motor" : kConstraintNames[(int)ct]) + ". Press Play to try it!");
}

void Editor::openScript(SceneNode* script) {
    if (script && script->isScript()) {
        m_scene->select(script);
        m_showPanel[kPanelScript] = true;
        m_scriptEditor->open(script->id);
    }
}

void Editor::duplicateSelected() {
    std::vector<SceneNode*> copies;
    for (SceneNode* sel : m_scene->selectionRoots()) {
        if (!canEdit(sel)) continue;
        auto copy = Serializer::clone(*sel);
        // Keep the original name. Roblox Instances do not acquire "Copy" suffixes
        // when duplicated, and sibling Instances are allowed to share names.
        if (copy->kind == NodeKind::Part) copy->transform.position.x += 1.0f;   // so it's visible
        copies.push_back(m_scene->insert(std::move(copy), sel->parent));
    }
    if (copies.empty()) return;
    m_scene->deselect();
    for (SceneNode* c : copies) m_scene->addToSelection(c);
}

void Editor::deleteSelected() {
    for (SceneNode* sel : m_scene->selectionRoots())
        if (!m_scene->isProtected(sel)) m_scene->removeNode(sel);
}

void Editor::copySelected() {
    std::vector<std::string> clip;
    for (SceneNode* sel : m_scene->selectionRoots())
        if (canEdit(sel)) clip.push_back(Serializer::nodeToString(*sel));
    if (!clip.empty()) m_clipboard = std::move(clip);
}

void Editor::paste() {
    if (m_clipboard.empty()) return;
    SceneNode* parent = m_scene->selected();
    if (!parent || parent->kind != NodeKind::Model || m_scene->isCharacterPart(parent))
        parent = m_scene->root();
    std::vector<SceneNode*> pasted;
    for (const std::string& c : m_clipboard) {
        auto n = Serializer::nodeFromString(c, true);
        if (!n) continue;
        if (n->kind == NodeKind::Part) n->transform.position.x += 1.0f;
        pasted.push_back(m_scene->insert(std::move(n), parent));
    }
    if (pasted.empty()) return;
    m_scene->deselect();
    for (SceneNode* n : pasted) m_scene->addToSelection(n);
}

// Ctrl+G: put everything selected inside a new Model.
void Editor::groupSelected() {
    std::vector<SceneNode*> items;
    for (SceneNode* n : m_scene->selectionRoots()) if (canEdit(n)) items.push_back(n);
    if (items.empty()) return;
    SceneNode* parent = items.back()->parent ? items.back()->parent : m_scene->root();
    for (SceneNode* n : items) if (n->parent != parent) { parent = m_scene->root(); break; }
    SceneNode* model = m_scene->insert(std::make_unique<SceneNode>("Model", NodeKind::Model), parent);
    for (SceneNode* n : items) m_scene->reparent(n, model);
    m_scene->select(model);
    m_outliner->expand({model});
    Log::info("Grouped " + std::to_string(items.size()) + " object(s) into a Model.");
}

// Ctrl+U: take everything out of the selected Models and remove the Models.
void Editor::ungroupSelected() {
    std::vector<SceneNode*> freed;
    for (SceneNode* m : m_scene->selectionRoots()) {
        if (m->kind != NodeKind::Model || !canEdit(m) || !m->parent) continue;
        SceneNode* parent = m->parent;
        std::vector<SceneNode*> kids;
        for (auto& c : m->children) kids.push_back(c.get());
        for (SceneNode* k : kids) if (m_scene->reparent(k, parent)) freed.push_back(k);
        m_scene->removeNode(m);
    }
    if (freed.empty()) return;
    m_scene->deselect();
    for (SceneNode* n : freed) m_scene->addToSelection(n);
}

void Editor::selectAll() {
    m_scene->deselect();
    for (auto& c : m_scene->root()->children)
        if (canEdit(c.get()) && !c->internal) m_scene->addToSelection(c.get());
}

void Editor::selectParent() {
    std::vector<SceneNode*> parents;
    for (SceneNode* n : m_scene->selection())
        if (n->parent && n->parent != m_scene->root() &&
            std::find(parents.begin(), parents.end(), n->parent) == parents.end())
            parents.push_back(n->parent);
    if (parents.empty()) return;
    m_scene->deselect();
    for (SceneNode* p : parents) m_scene->addToSelection(p);
}

void Editor::selectChildren() {
    std::vector<SceneNode*> kids;
    for (SceneNode* n : m_scene->selection())
        for (auto& c : n->children) if (!c->internal) kids.push_back(c.get());
    if (kids.empty()) return;
    m_outliner->expand(m_scene->selection());
    m_scene->deselect();
    for (SceneNode* k : kids) m_scene->addToSelection(k);
}

void Editor::toggleHidden() {
    auto sel = m_scene->selectionRoots();
    if (sel.empty()) return;
    bool show = !sel.back()->visible;
    for (SceneNode* n : sel) if (!m_scene->isCharacterPart(n)) n->visible = show;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

void Editor::newScene() {
    if (m_playing) togglePlay();
    m_animEditor->close();
    m_scene->buildDefault();
    m_objCounter = 0;
    m_path.clear();
    resetHistory();
    Log::system("Started a new game.");
}

void Editor::openFile(const std::string& path) {
    if (m_playing) togglePlay();
    m_animEditor->close();
    if (RobloxFile::isRobloxFile(path)) {
        RobloxFile::Report report;
        std::string err;
        if (RobloxFile::isPlace(path)) {
            if (!RobloxFile::importPlace(*m_scene, path, report, err)) { Log::error("Couldn't import " + path + ": " + err); return; }
            m_path.clear();   // it's a Guts and Bolts game now: Save As picks a name
            resetHistory();
            m_dirty = true;
            Log::system("Imported Roblox place " + path + ": " + report.summary() + ". Save it to keep it.");
        } else {
            auto added = RobloxFile::importModel(*m_scene, nullptr, path, report, err);
            if (added.empty()) { Log::error("Couldn't import " + path + ": " + err); return; }
            m_scene->deselect();
            for (SceneNode* n : added) m_scene->addToSelection(n);
            Log::system("Inserted Roblox model " + path + ": " + report.summary());
        }
        for (const std::string& n : report.notes) Log::warn("Roblox import: " + n);
        return;
    }
    std::string err;
    if (!Serializer::loadGameFile(*m_scene, path, &err)) {
        Log::error("Couldn't load " + path + ": " + err);
        m_scene->buildDefault();
    } else {
        m_path = path;
        Log::system("Opened " + path);
    }
    resetHistory();
}

void Editor::exportRoblox(bool selectionOnly) {
    std::string name = m_scene->info().title.empty() ? "My Game" : m_scene->info().title;
    std::string err;
    if (selectionOnly) {
        auto sel = m_scene->selectionRoots();
        if (sel.empty()) { Log::warn("Select something to export first."); return; }
        std::string path = (Paths::gamesFolder() / (sel.back()->name + ".rbxmx")).string();
        if (RobloxFile::exportModel(*m_scene, sel, path, err)) Log::system("Exported a Roblox model to " + path);
        else Log::error("Export failed: " + err);
    } else {
        std::string path = (Paths::gamesFolder() / (name + ".rbxlx")).string();
        if (RobloxFile::exportPlace(*m_scene, path, err))
            Log::system("Exported a Roblox place to " + path + " (open it in Roblox Studio)");
        else Log::error("Export failed: " + err);
    }
}

void Editor::saveFile(const std::string& path, bool sync) {
    if (m_playing) togglePlay();
    // A new game is signed by whoever made it (it shows on the site's Create page).
    GameInfo& info = m_scene->info();
    if ((info.author.empty() || info.author == "Builder") && Profile::get().name != "Player")
        info.author = Profile::get().name;
    if (Serializer::writeFile(path, Serializer::saveScene(*m_scene, true))) {
        m_path  = path;
        m_dirty = false;
        dropAutoSave();   // saved for real: the backup isn't needed
        const std::string& published = info.publishedId;
        if (published.empty()) {
            Log::system("Saved to " + path + ". It's only on this computer: use File > Publish to put it online "
                        "for everyone (and on the website).");
        } else if (sync && Online::online()) {
            // Published: send the new version to the server, so the website and everyone playing it get it.
            Log::system("Saved to " + path + ". Updating the published game...");
            nlohmann::json args = {{"id", published}, {"name", info.title}, {"description", info.description},
                         {"data", Online::base64Encode(Serializer::saveScene(*m_scene))}};
            Online::request("update", args, [](const nlohmann::json& r) {
                if (r.value("ok", false)) Log::system("The published game is up to date on the server.");
                else Log::warn("Saved, but the server didn't take the update: " + r.value("error", std::string()) +
                               " (File > Publish to try again.)");
            }, 120);
        } else {
            Log::system("Saved to " + path + ". You're offline, so the published game wasn't updated: "
                        "save again (or File > Publish) when you're back online.");
        }
    } else {
        Log::error("Couldn't save to " + path);
    }
}

void Editor::save() {
    if (m_path.empty()) {
        m_nameInput = m_scene->info().title;
        m_openSaveAs = true;
    } else {
        saveFile(m_path);
    }
}

void Editor::updateTitle() {
    std::string name = m_path.empty() ? "Untitled"
                                      : std::filesystem::path(m_path).stem().string();
    std::string title = name + (m_dirty ? "*" : "") + " - Guts and Bolts";
    if (title != m_shownTitle) {
        glfwSetWindowTitle(m_window, title.c_str());
        m_shownTitle = title;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Editor::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;   // don't steal keys while typing

    // Testing (like Roblox Studio): F5 Play, F8 Simulate, Shift+F5 Stop.
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        if (io.KeyShift || m_playing) setMode(StudioMode::Build);
        else setMode(StudioMode::Play);
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false) && !m_playing) { setMode(StudioMode::Simulate); return; }
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) m_showShortcuts = !m_showShortcuts;

    if (m_playing) {
        // While testing, WASD drives the character, not the editor.
        if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) m_state.simPaused = !m_state.simPaused;
        if (ImGui::IsKeyPressed(ImGuiKey_F7, false)) { m_state.simPaused = true; m_state.simStep = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) togglePlay();
        return;
    }
    if (m_uv.hovered && !io.KeyCtrl) return;   // the UV Editor has its own keys (G, R, S, A, L, U)
    if (m_state.mode == StudioMode::Modeling) { handleModelingKeys(); return; }
    // Tab: reshape the selected part (Blender's Edit Mode).
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !io.KeyCtrl && !io.KeyAlt) { setMode(StudioMode::Modeling); return; }
    // F: zoom to the selection (things with a body only), from any panel.
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (!m_viewport->focusSelected() && m_scene->selected())
            Log::info("F zooms to things with a body (parts, models, tools) - \"" + m_scene->selected()->name + "\" has none.");
    }

    if (io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) toggleLocked();
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) toggleAnchored();
        return;
    }

    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { io.KeyShift ? redo() : undo(); }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) redo();
        if (ImGui::IsKeyPressed(ImGuiKey_J, false)) joinSelected();   // Blender's Join
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copySelected();
        if (ImGui::IsKeyPressed(ImGuiKey_X, false) && !io.KeyShift) cutSelected();   // Ctrl+Shift+X = Explorer search
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) { if (io.KeyShift) pasteInto(); else paste(); }
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected();
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) { if (io.KeyShift) { m_nameInput = m_scene->info().title; m_openSaveAs = true; } else save(); }
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) { m_pending = Pending::Open; m_openDiscard = m_dirty; if (!m_dirty) m_openOpen = true; }
        if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N, false)) negateSelected();
        else if (ImGui::IsKeyPressed(ImGuiKey_N, false)) { m_pending = Pending::New;  m_openDiscard = m_dirty; if (!m_dirty) { newScene(); m_pending = Pending::None; } }
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) selectAll();
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) { if (io.KeyShift) unionSelected(0); else groupSelected(); }
        if (ImGui::IsKeyPressed(ImGuiKey_U, false)) { if (io.KeyShift) separateSelected(); else ungroupSelected(); }
        if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_I, false)) unionSelected(1);
        else if (ImGui::IsKeyPressed(ImGuiKey_I, false)) { m_insertParent = m_scene->selected(); m_openInsert = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) m_state.gizmoLocal = !m_state.gizmoLocal;
        if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_E, false)) m_showBulkEdit = !m_showBulkEdit;
        // Roblox's tool keys.
        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) m_state.tool = GizmoTool::Select;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) m_state.tool = GizmoTool::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) m_state.tool = GizmoTool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_4, false)) m_state.tool = GizmoTool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) m_outliner->expand(m_scene->selection());
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
            if (io.KeyShift) m_outliner->collapseAll();
            else             m_outliner->collapse(m_scene->selection());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))   selectParent();
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) selectChildren();
        return;
    }

    // Plain keys (W A S D Q E fly the camera in the Viewport).
    if (ImGui::IsKeyPressed(ImGuiKey_H, false)) toggleHidden();
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) m_outliner->beginRename(m_scene->selected());
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) deleteSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_scene->deselect(); m_state.connectTool = -1; }
}

void Editor::cutSelected() {
    copySelected();
    deleteSelected();
}

// Ctrl+Shift+V: paste inside the selected object instead of next to it.
void Editor::pasteInto() {
    SceneNode* target = m_scene->selected();
    if (m_clipboard.empty() || !target || m_scene->isCharacterPart(target) || target->isScript()) { paste(); return; }
    std::vector<SceneNode*> pasted;
    for (const std::string& c : m_clipboard) {
        auto n = Serializer::nodeFromString(c, true);
        if (n) pasted.push_back(m_scene->insert(std::move(n), target));
    }
    m_outliner->expand({target});
    m_scene->deselect();
    for (SceneNode* n : pasted) m_scene->addToSelection(n);
}

void Editor::toggleLocked() {
    auto sel = m_scene->selectionRoots();
    if (sel.empty()) return;
    bool lock = !sel.back()->locked;
    for (SceneNode* n : sel) if (canEdit(n)) n->locked = lock;
    Log::info(lock ? "Locked: they can't be clicked in the Viewport now (pick them in the Explorer)." : "Unlocked.");
}

void Editor::toggleAnchored() {
    auto sel = m_scene->selectionRoots();
    if (sel.empty()) return;
    bool anchor = !sel.back()->anchored;
    for (SceneNode* n : sel) {
        std::vector<SceneNode*> stack{n};
        while (!stack.empty()) {
            SceneNode* k = stack.back(); stack.pop_back();
            if (k->isPart() && canEdit(k)) k->anchored = anchor;
            for (auto& c : k->children) stack.push_back(c.get());
        }
    }
}

// Move a Model's origin to the middle of its parts (keeping the parts where they are).
void Editor::centerModelPivot() {
    SceneNode* m = m_scene->selected();
    if (!m || m->kind != NodeKind::Model || !canEdit(m)) return;
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (auto& c : m->children) if (c->isPart()) { glm::vec3 p(c->worldMatrix()[3]); lo = glm::min(lo, p); hi = glm::max(hi, p); }
    if (lo.x > hi.x) return;
    glm::vec3 center = (lo + hi) * 0.5f;
    glm::vec3 local = glm::vec3(glm::inverse(m->worldMatrix()) * glm::vec4(center, 1.0f));
    for (auto& c : m->children) c->transform.position -= local;
    glm::vec3 d = center - glm::vec3(m->worldMatrix()[3]);
    if (m->parent) d = glm::vec3(glm::inverse(m->parentFrame()) * glm::vec4(d, 0.0f));
    m->transform.position += d;
}

// Play (0), Play Here (1: start where the camera is looking) or Run (2: no player).
void Editor::startPlay(int mode) {
    if (m_playing) return;
    m_playMode = mode;
    m_session->setRunOnly(mode == 2);
    togglePlay();
    Player* p = m_scene->player();
    SceneNode* r = p ? p->root() : nullptr;
    if (!r) return;
    if (mode == 1) {
        glm::vec3 target = m_viewport->cameraPivot();
        SceneNode* character = r;
        SceneNode* ground = Physics::raycast(*m_scene, target + glm::vec3(0, 60, 0), glm::vec3(0, -1, 0), nullptr, character);
        glm::vec3 at = target;
        if (ground) { AABB b = Physics::worldBounds(ground); at.y = b.max.y + 0.01f; }
        r->transform.position = at;
    }
    if (mode == 2) r->visible = false;   // Run: nobody's playing (Stop brings the character back)
}

bool Editor::startMultiTest(int players, std::string& error) {
    if (m_playing) togglePlay();   // the game as it is in Build mode, not mid-playtest
    if (!m_multiTest->start(Serializer::saveScene(*m_scene), players, error)) {
        Log::error("Couldn't start the multiplayer test: " + error);
        return false;
    }
    return true;
}

void Editor::stopMultiTest() { m_multiTest->stop(); }

// Ctrl+I / the Explorer's + button: a searchable list of things to insert.
void Editor::insertObject(const std::string& what, SceneNode* parent) {
    if (parent && (m_scene->isCharacterPart(parent) || parent->isScript())) parent = nullptr;
    auto put = [&](std::unique_ptr<SceneNode> n) {
        SceneNode* raw = m_scene->insert(std::move(n), parent);
        m_scene->select(raw);
        if (parent) m_outliner->expand({parent});
        return raw;
    };
    auto part = [&](const char* name, PrimitiveType t) {
        auto n = std::make_unique<SceneNode>(name, NodeKind::Part);
        n->primitiveType = t;
        n->mesh = MeshLibrary::get(t);
        n->transform.scale = t == PrimitiveType::Sphere ? glm::vec3(2) : glm::vec3(2, 1, 1);
        n->transform.position = parent ? glm::vec3(0, 1, 0) : spawnPoint() + glm::vec3(0, 0.5f, 0);
        return put(std::move(n));
    };
    if (what == "Part") part("Part", PrimitiveType::Cube);
    else if (what == "Sphere") part("Sphere", PrimitiveType::Sphere);
    else if (what == "Cylinder") part("Cylinder", PrimitiveType::Cylinder);
    else if (what == "MeshPart") {
        SceneNode* mp = part("MeshPart", PrimitiveType::Cube);
        MeshEdit::attach(*mp, MeshEdit::fromPrimitive(PrimitiveType::Cube));
        mp->transform.scale = glm::vec3(2.0f);
        setMode(StudioMode::Modeling);
    }
    else if (what == "TrussPart") {   // walk into it to climb
        SceneNode* t = part("TrussPart", PrimitiveType::Cube);
        t->transform.scale = {2, 10, 2};
        t->transform.position.y += 4.5f;
        t->color = {0.6f, 0.62f, 0.66f};
        t->material = Material::Metal;
    }
    else if (what == "Seat") {   // walk into it to sit down (jump to get up), like Roblox's
        SceneNode* t = part("Seat", PrimitiveType::Cube);
        t->transform.scale = {1.0f, 0.2f, 1.0f};
        t->transform.position.y -= 0.35f;
        t->color = {0.64f, 0.64f, 0.64f};
        t->tags.push_back("Seat");
    }
    else if (what == "Water") {   // swim in it
        SceneNode* w = part("Water", PrimitiveType::Cube);
        w->transform.scale = {16, 6, 16};
        w->transform.position.y += 2.5f;
        w->color = {0.2f, 0.5f, 0.95f};
        w->transparency = 0.45f;
        w->material = Material::Glass;
        w->canCollide = false;
        w->castShadow = false;
    }
    else if (what == "FluidVolume") {   // water with waves, clarity and a current (attributes)
        SceneNode* w = part("FluidVolume", PrimitiveType::Cube);
        w->transform.scale = {20, 5, 20};
        w->transform.position.y += 2.0f;
        w->color = {0.13f, 0.45f, 0.62f};
        w->transparency = 0.45f;
        w->material = Material::Glass;
        w->canCollide = false;
        w->castShadow = false;
        w->tags = {"Water", "FluidVolume"};
        auto num = [&](const char* name, double v) { Attribute a; a.name = name; a.type = Attribute::Number; a.n = v; w->attributes.push_back(a); };
        num("Clarity", 0.8);
        num("WaveScale", 1.0);
        Attribute flow; flow.name = "Flow"; flow.type = Attribute::Vector3; flow.v = glm::vec3(0.0f); w->attributes.push_back(flow);
        num("Drag", 1.0);   // how hard it pushes on things (1 = water, 5 = like mud)
    }
    else if (what == "WaterSource") {   // pours water that flows downhill and fills things up
        SceneNode* w = part("WaterSource", PrimitiveType::Cylinder);
        w->transform.scale = {1.2f, 0.4f, 1.2f};
        w->transform.position.y += 6.0f;
        w->color = {0.2f, 0.5f, 0.95f};
        w->material = Material::Metal;
        w->canCollide = false;
        Attribute rate;
        rate.name = "Rate";
        rate.n = 8.0;
        w->attributes.push_back(rate);
    }
    else if (what == "SpawnLocation") {
        SceneNode* sp = part("SpawnLocation", PrimitiveType::Cube);
        sp->transform.scale = {3, 0.2f, 3};
        sp->color = {0.25f, 0.6f, 1.0f};
    }
    else if (what == "Model" || what == "Folder") put(std::make_unique<SceneNode>(what, NodeKind::Model));
    else if (what == "WorldModel") {   // a Model for inside a ViewportFrame
        auto m = std::make_unique<SceneNode>(what, NodeKind::Model);
        Attribute cls; cls.name = "RobloxClass"; cls.type = Attribute::String; cls.s = "WorldModel";
        m->attributes.push_back(cls);
        put(std::move(m));
    }
    else if (what == "Camera") {
        // A camera for a ViewportFrame: in the selected one (it becomes its CurrentCamera),
        // a little back from the middle, looking at it.
        auto cam = std::make_unique<SceneNode>("Camera", NodeKind::Camera);
        cam->transform.position = {0.0f, 1.5f, -6.0f};
        cam->transform.rotation = {-10.0f, 180.0f, 0.0f};
        const bool inFrame = parent && parent->isViewportFrame();
        SceneNode* c = put(std::move(cam));
        if (inFrame && !parent->gui.camera) parent->gui.camera = c->id;
    }
    else if (what == "Team") {
        // Teams live in a folder called Teams (Roblox's Teams service). A Team has a
        // colour; players join the AutoAssignable ones and spawn on SpawnLocations
        // with the same TeamColor attribute.
        SceneNode* folder = nullptr;
        for (auto& c : m_scene->root()->children) if (c->name == "Teams" && c->kind == NodeKind::Model) folder = c.get();
        if (!folder) folder = m_scene->insert(std::make_unique<SceneNode>("Teams", NodeKind::Model), nullptr);
        static const glm::vec3 colors[] = {{0.77f, 0.16f, 0.11f}, {0.05f, 0.41f, 0.67f}, {0.16f, 0.5f, 0.27f}, {0.96f, 0.8f, 0.19f}};
        auto t = std::make_unique<SceneNode>(std::string("Team ") + std::to_string(folder->children.size() + 1), NodeKind::Model);
        Attribute cls; cls.name = "RobloxClass"; cls.type = Attribute::String; cls.s = "Team";
        Attribute col; col.name = "TeamColor"; col.type = Attribute::Color3; col.v = colors[folder->children.size() % 4];
        Attribute aa; aa.name = "AutoAssignable"; aa.type = Attribute::Bool; aa.b = true;
        t->attributes = {cls, col, aa};
        parent = folder;
        put(std::move(t));
    }
    else if (what == "Script" || what == "LocalScript") { addScript(parent, what == "LocalScript"); }
    else if (what == "ProximityPrompt") {
        // It goes on the part (or attachment, or model) it's for.
        if (!parent || (!parent->isPart() && parent->kind != NodeKind::Model && parent->kind != NodeKind::Attachment)) {
            Log::warn("Select a part first: a ProximityPrompt goes inside the part players walk up to.");
            return;
        }
        put(std::make_unique<SceneNode>("ProximityPrompt", NodeKind::Prompt));
    }
    else if (what == "Sky" || std::find(std::begin(kPostEffectClassNames), std::end(kPostEffectClassNames), what) != std::end(kPostEffectClassNames)) {
        // They only work right inside Lighting, so that's where they go.
        auto n = std::make_unique<SceneNode>(what, what == "Sky" ? NodeKind::Sky : NodeKind::PostEffect);
        for (int i = 0; i < kPostEffectCount; ++i) if (what == kPostEffectClassNames[i]) n->post = postEffectDefaults((PostEffectType)i);
        if (parent != m_scene->lightingFolder(false)) {
            parent = m_scene->lightingFolder(true);
            Log::info(what + " goes in Lighting (that's where it works).");
        }
        put(std::move(n));
    }
    else if (what == "ClickDetector") {
        // It goes on the part (or model) players click.
        if (!parent || parent == m_scene->root() || (!parent->isPart() && parent->kind != NodeKind::Model)) {
            Log::warn("Select a part or model first: a ClickDetector goes inside the thing players click.");
            return;
        }
        put(std::make_unique<SceneNode>("ClickDetector", NodeKind::ClickDetector));
    }
    else if (what == "Highlight") {
        // It colours what it's in (a part or a model).
        if (!parent || parent == m_scene->root() || (!parent->isPart() && parent->kind != NodeKind::Model)) {
            Log::warn("Select a part or model first: a Highlight colours and outlines what it's inside.");
            return;
        }
        put(std::make_unique<SceneNode>("Highlight", NodeKind::Highlight));
    }
    else if (what == "Trail" || what == "Beam") {
        // They run between two Attachments: a Trail in a part gets one at its top and
        // one at its bottom; a Beam gets its start here, and you pick where it ends.
        auto fx = std::make_unique<SceneNode>(what, what == "Trail" ? NodeKind::Trail : NodeKind::Beam);
        if (parent && parent->isPart()) {
            auto add = [&](const char* name, glm::vec3 at) {
                auto a = std::make_unique<SceneNode>(name, NodeKind::Attachment);
                a->transform.position = at;
                return m_scene->insert(std::move(a), parent)->id;
            };
            if (what == "Trail") {
                fx->effect.a0 = add("TrailTop", {0.0f, 0.5f, 0.0f});
                fx->effect.a1 = add("TrailBottom", {0.0f, -0.5f, 0.0f});
            } else {
                fx->effect.a0 = add("BeamStart", {0.0f, 0.5f, 0.0f});
                Log::info("Pick the Beam's Attachment1 in Properties: it's where the beam ends.");
            }
        } else {
            Log::warn("Put a " + what + " inside a part: it runs between Attachments (set them in Properties).");
        }
        put(std::move(fx));
    }
    else if (auto pt = std::find(std::begin(kParticleClassNames), std::end(kParticleClassNames), what); pt != std::end(kParticleClassNames)) {
        // Particles come out of the part (or Attachment) they're in.
        if (!parent || (!parent->isPart() && !parent->isAttachment())) {
            Log::warn("Select a part first: a " + what + " goes inside the part (or Attachment) it comes out of.");
            return;
        }
        auto fx = std::make_unique<SceneNode>(what, NodeKind::Particles);
        fx->particle = particleDefaults((ParticleType)(pt - std::begin(kParticleClassNames)));
        put(std::move(fx));
    }
    else if (what == "RemoteEvent" || what == "RemoteFunction") {
        // Both sides need to find it, so it goes in ReplicatedStorage unless you picked a place.
        if (!parent || parent == m_scene->root()) {
            parent = nullptr;
            for (auto& c : m_scene->root()->children)
                if (c->name == "ReplicatedStorage" && c->kind == NodeKind::Model) parent = c.get();
            if (!parent) parent = m_scene->insert(std::make_unique<SceneNode>("ReplicatedStorage", NodeKind::Model), nullptr);
        }
        auto r = std::make_unique<SceneNode>(what, NodeKind::Remote);
        r->remoteFunction = what == "RemoteFunction";
        put(std::move(r));
    }
    else if (what == "ModuleScript") {
        auto n = std::make_unique<SceneNode>("ModuleScript", NodeKind::Script);
        n->isModule = true;
        n->source = "local module = {}\n\nreturn module\n";
        openScript(put(std::move(n)));
    }
    else if (what == "PointLight" || what == "SpotLight") {
        m_scene->select(parent);
        addLight(what == "SpotLight" ? LightType::Spot : LightType::Point);
    }
    else if (what == "Sound") { m_scene->select(parent); addSound(); }
    else if (what == "SoundGroup") {
        // Groups live in SoundService (or inside another group, to be part of it).
        if (!parent || !parent->isSoundGroup()) parent = m_scene->serviceFolder("SoundService", true);
        auto g = std::make_unique<SceneNode>("SoundGroup", NodeKind::SoundGroup);
        g->volume = 0.5f;   // (Roblox's starting Volume for a group)
        put(std::move(g));
        Log::info("Set a Sound's SoundGroup (in Properties) to this group: its Volume and effects then apply to that sound.");
    }
    else if (auto ft = std::find(std::begin(SoundFx::kClassNames), std::end(SoundFx::kClassNames), what); ft != std::end(SoundFx::kClassNames)) {
        // Sound effects change the Sound or SoundGroup they're inside.
        if (!parent || (!parent->isSound() && !parent->isSoundGroup())) {
            Log::warn("Select a Sound or SoundGroup first: a " + what + " changes the sound it's inside.");
            return;
        }
        auto f = std::make_unique<SceneNode>(what, NodeKind::SoundEffect);
        f->soundFx = soundFxDefaults((SoundFx::Type)(ft - std::begin(SoundFx::kClassNames)));
        put(std::move(f));
    }
    else if (what == "Attachment") put(std::make_unique<SceneNode>("Attachment", NodeKind::Attachment));
    else if (what == "Bone") { if (addBone(parent, 1)) m_showArmature = true; }
    else if (auto mt = std::find(std::begin(kMoverClassNames), std::end(kMoverClassNames), what); mt != std::end(kMoverClassNames)) {
        // BodyVelocity, AlignPosition...: they go inside the part they push. The newer
        // ones push through an Attachment, so the part gets one if it hasn't any.
        auto mv = std::make_unique<SceneNode>(what, NodeKind::Mover);
        mv->mover = moverDefaults((MoverType)(mt - std::begin(kMoverClassNames)));
        if (!isBodyMover(mv->mover.type) && parent && parent->isPart()) {
            SceneNode* att = nullptr;
            for (auto& c : parent->children) if (c->isAttachment()) { att = c.get(); break; }
            if (!att) att = m_scene->insert(std::make_unique<SceneNode>("Attachment", NodeKind::Attachment), parent);
            mv->ref0 = att->id;
        }
        if (!parent || !parent->isPart()) Log::warn("Put a " + what + " inside an unanchored part to push it.");
        put(std::move(mv));
    }
    else if (auto ct = std::find(std::begin(kConstraintClassNames), std::end(kConstraintClassNames), what);
             ct != std::end(kConstraintClassNames)) {
        // A joint on its own: inside a part it gets that part's Attachment as its
        // Attachment0 (or Part0 for a weld); pick the other end in Properties or a script.
        // (The Constraints tool in the Toolbox joins two clicked parts in one go.)
        auto c = std::make_unique<SceneNode>(what, NodeKind::Constraint);
        c->constraintType = (ConstraintType)(ct - std::begin(kConstraintClassNames));
        c->color = c->constraintType == ConstraintType::Rope ? glm::vec3(0.45f, 0.32f, 0.2f) : glm::vec3(0.6f, 0.62f, 0.66f);
        if (parent && parent->isPart()) {
            if (c->constraintType == ConstraintType::Weld) c->ref0 = parent->id;
            else {
                SceneNode* att = nullptr;
                for (auto& ch : parent->children) if (ch->isAttachment()) { att = ch.get(); break; }
                if (!att) att = m_scene->insert(std::make_unique<SceneNode>("Attachment", NodeKind::Attachment), parent);
                c->ref0 = att->id;
            }
        }
        Log::system("Set the " + what + "'s " +
                    (c->constraintType == ConstraintType::Weld ? "Part1" : "Attachment1") +
                    " to join it to another part (or use the Constraints tool to click two parts).");
        put(std::move(c));
    }
    else if (what == "FluidSystem") {
        auto f = std::make_unique<SceneNode>("FluidSystem", NodeKind::FluidSystem);
        f->color = {0.12f, 0.56f, 1.0f};
        put(std::move(f));
    }
    else if (what == "FluidEmitter") {
        // In front of the camera, a bit up, pouring down.
        auto f = std::make_unique<SceneNode>("FluidEmitter", NodeKind::FluidEmitter);
        f->transform.position = spawnPoint() + glm::vec3(0.0f, 8.0f, 0.0f);
        SceneNode* e = m_scene->insert(std::move(f), m_scene->root());
        m_scene->select(e);
    }
    else if (what == "ForceField") put(std::make_unique<SceneNode>("ForceField", NodeKind::ForceField));
    else if (what == "IntValue" || what == "NumberValue" || what == "StringValue" || what == "BoolValue") {
        auto v = std::make_unique<SceneNode>(what, NodeKind::Value);
        v->intValue = what == "IntValue";
        v->value.type = what == "StringValue" ? Attribute::String : what == "BoolValue" ? Attribute::Bool : Attribute::Number;
        put(std::move(v));
    }
    else if (what == "Animation") {
        // Animations usually live inside the rig they move.
        auto a = std::make_unique<SceneNode>("Animation", NodeKind::Animation);
        a->source = Anim::emptyClipText();
        put(std::move(a));
    }
    else if (what == "Rig") {
        // A classic R6 dummy to animate (or to use as an NPC).
        SceneNode* rig = Player::buildRig(*m_scene, "Rig", spawnPoint());
        if (parent && parent != m_scene->root()) m_scene->reparent(rig, parent);
        m_scene->select(rig);
    }
    else if (GuiType gt; Guis::typeFromName(what, gt)) {
        // Game UI: a ScreenGui goes in the StarterGui folder; the rest go inside
        // the selected UI object (or a new ScreenGui if nothing like that is picked).
        auto uiFolder = [&]() {
            SceneNode* f = m_scene->root()->findChild("StarterGui");
            if (!f || f->kind != NodeKind::Model)
                f = m_scene->insert(std::make_unique<SceneNode>("StarterGui", NodeKind::Model), m_scene->root());
            return f;
        };
        auto make = [&](GuiType t) {
            auto n = std::make_unique<SceneNode>(kGuiClassNames[(int)t], NodeKind::Gui);
            n->gui.type = t;
            Guis::setDefaults(*n);
            return n;
        };
        if (gt == GuiType::BillboardGui || gt == GuiType::SurfaceGui) {
            // These go on a part: the selected one (or its model).
            if (!parent || (!parent->isPart() && parent->kind != NodeKind::Model)) {
                Log::warn("Select a part first: a " + what + " goes inside the part it's on.");
                return;
            }
        } else if (gt == GuiType::ScreenGui) {
            if (!parent || parent == m_scene->root()) parent = uiFolder();
        } else if (!parent || !parent->isGui() || isGuiModifier(parent->gui.type)) {
            if (isGuiModifier(gt)) { Log::warn("Put a " + what + " inside a Frame, label or button."); return; }
            SceneNode* screen = nullptr;   // reuse the first ScreenGui, or make one
            for (auto& c : uiFolder()->children) if (c->isGui() && c->gui.type == GuiType::ScreenGui) { screen = c.get(); break; }
            if (!screen) screen = m_scene->insert(make(GuiType::ScreenGui), uiFolder());
            parent = screen;
        }
        auto n = make(gt);
        if (!isGuiLayer(gt) && !isGuiModifier(gt) && parent->isGui() && isGuiLayer(parent->gui.type)) {
            n->gui.pos = {0.5f, 0, 0.5f, 0};   // in the middle of the screen, so you see it
            n->gui.anchor = {0.5f, 0.5f};
        }
        put(std::move(n));
    }
    else if (what == "Decal") {
        auto d = std::make_unique<SceneNode>("Decal", NodeKind::Decal);
        d->color = {1.0f, 1.0f, 1.0f};
        put(std::move(d));
    }
    else if (what == "Tool") {
        // A tool with a Handle ready to hold, sitting where you're looking.
        SceneNode* tool = put(std::make_unique<SceneNode>("Tool", NodeKind::Tool));
        tool->transform.position = parent ? glm::vec3(0.0f) : spawnPoint() + glm::vec3(0.0f, 1.0f, 0.0f);
        auto handle = std::make_unique<SceneNode>("Handle");
        handle->primitiveType = PrimitiveType::Cube;
        handle->mesh = MeshLibrary::get(PrimitiveType::Cube);
        handle->transform.scale = {0.2f, 1.2f, 0.2f};
        handle->color = {0.55f, 0.35f, 0.2f};
        handle->canCollide = false;
        tool->addChild(std::move(handle));
        m_scene->markDirty();
    }
    else {
        for (const PremadeInfo& p : premadeList()) if (what == p.name) { spawnPremade(p.kind); break; }
    }
}

namespace {
// Everything the Insert Object window offers (the ready-made things are added after).
struct InsertItem { const char* name; Icons::Id icon; };
const std::vector<InsertItem>& insertItems() {
    static const std::vector<InsertItem> list = {
        {"Part", Icons::Id::Part}, {"Sphere", Icons::Id::Sphere}, {"Cylinder", Icons::Id::Cylinder},
        {"MeshPart", Icons::Id::Mesh}, {"SpawnLocation", Icons::Id::Part}, {"TrussPart", Icons::Id::Part}, {"Seat", Icons::Id::Part}, {"Water", Icons::Id::Part}, {"FluidVolume", Icons::Id::Part}, {"WaterSource", Icons::Id::Part}, {"FluidSystem", Icons::Id::Value}, {"FluidEmitter", Icons::Id::Sound}, {"Model", Icons::Id::Model}, {"Folder", Icons::Id::Folder},
        {"Script", Icons::Id::Script}, {"LocalScript", Icons::Id::LocalScript}, {"ModuleScript", Icons::Id::ModuleScript},
        {"RemoteEvent", Icons::Id::Remote}, {"RemoteFunction", Icons::Id::Remote}, {"ProximityPrompt", Icons::Id::Prompt}, {"Highlight", Icons::Id::Highlight}, {"Trail", Icons::Id::Ribbon}, {"Beam", Icons::Id::Ribbon},
        {"Sky", Icons::Id::Lighting}, {"ColorCorrectionEffect", Icons::Id::Lighting}, {"SunRaysEffect", Icons::Id::Lighting},
        {"DepthOfFieldEffect", Icons::Id::Lighting}, {"BloomEffect", Icons::Id::Lighting}, {"BlurEffect", Icons::Id::Lighting},
        {"RemoteEvent", Icons::Id::Remote}, {"RemoteFunction", Icons::Id::Remote}, {"ProximityPrompt", Icons::Id::Prompt}, {"ClickDetector", Icons::Id::Click}, {"Highlight", Icons::Id::Highlight}, {"Trail", Icons::Id::Ribbon}, {"Beam", Icons::Id::Ribbon},
        {"ParticleEmitter", Icons::Id::Particles}, {"Fire", Icons::Id::Particles}, {"Smoke", Icons::Id::Particles}, {"Sparkles", Icons::Id::Particles},
        {"PointLight", Icons::Id::Light}, {"SpotLight", Icons::Id::Light}, {"Sound", Icons::Id::Sound},
        {"SoundGroup", Icons::Id::Sound}, {"ReverbSoundEffect", Icons::Id::Sound}, {"EchoSoundEffect", Icons::Id::Sound},
        {"EqualizerSoundEffect", Icons::Id::Sound}, {"DistortionSoundEffect", Icons::Id::Sound}, {"PitchShiftSoundEffect", Icons::Id::Sound},
        {"ChorusSoundEffect", Icons::Id::Sound}, {"CompressorSoundEffect", Icons::Id::Sound}, {"FlangeSoundEffect", Icons::Id::Sound},
        {"TremoloSoundEffect", Icons::Id::Sound},
        {"Attachment", Icons::Id::Attachment}, {"Bone", Icons::Id::Rig},
        {"RopeConstraint", Icons::Id::Constraint}, {"RodConstraint", Icons::Id::Constraint}, {"SpringConstraint", Icons::Id::Constraint},
        {"WeldConstraint", Icons::Id::Constraint}, {"HingeConstraint", Icons::Id::Constraint}, {"BallSocketConstraint", Icons::Id::Constraint},
        {"PrismaticConstraint", Icons::Id::Constraint}, {"CylindricalConstraint", Icons::Id::Constraint},
        {"BodyVelocity", Icons::Id::Constraint}, {"BodyPosition", Icons::Id::Constraint}, {"BodyGyro", Icons::Id::Constraint},
        {"BodyAngularVelocity", Icons::Id::Constraint}, {"BodyThrust", Icons::Id::Constraint}, {"BodyForce", Icons::Id::Constraint},
        {"LinearVelocity", Icons::Id::Constraint}, {"AlignPosition", Icons::Id::Constraint}, {"AlignOrientation", Icons::Id::Constraint},
        {"AngularVelocity", Icons::Id::Constraint}, {"VectorForce", Icons::Id::Constraint}, {"Torque", Icons::Id::Constraint}, {"ForceField", Icons::Id::ForceField}, {"Tool", Icons::Id::Tool}, {"Decal", Icons::Id::Decal},
        {"Animation", Icons::Id::Animation}, {"Rig", Icons::Id::Rig},
        {"ScreenGui", Icons::Id::ScreenGui}, {"Frame", Icons::Id::GuiFrame}, {"TextLabel", Icons::Id::GuiText},
        {"TextButton", Icons::Id::GuiButton}, {"ImageLabel", Icons::Id::GuiImage}, {"ImageButton", Icons::Id::GuiButton},
        {"UICorner", Icons::Id::GuiCorner}, {"UIStroke", Icons::Id::GuiCorner},
        {"UIShadow", Icons::Id::GuiCorner}, {"UIBlur", Icons::Id::GuiCorner},
        {"TextBox", Icons::Id::GuiText}, {"ScrollingFrame", Icons::Id::GuiFrame}, {"UIListLayout", Icons::Id::GuiCorner},
        {"UIGridLayout", Icons::Id::GuiCorner}, {"UIPadding", Icons::Id::GuiCorner},
        {"UIGradient", Icons::Id::GuiCorner}, {"UIScale", Icons::Id::GuiCorner}, {"UIAspectRatioConstraint", Icons::Id::GuiCorner},
        {"ViewportFrame", Icons::Id::GuiViewport}, {"Camera", Icons::Id::Camera}, {"WorldModel", Icons::Id::Model},
        {"BillboardGui", Icons::Id::ScreenGui}, {"SurfaceGui", Icons::Id::ScreenGui},
        {"IntValue", Icons::Id::Value}, {"NumberValue", Icons::Id::Value}, {"StringValue", Icons::Id::Value}, {"BoolValue", Icons::Id::Value}};
    return list;
}
} // namespace

std::vector<std::string> Editor::insertKinds() {
    std::vector<std::string> out;
    for (const InsertItem& o : insertItems()) out.push_back(o.name);
    for (const PremadeInfo& p : premadeList()) out.push_back(p.name);
    return out;
}

void Editor::renderInsertObject() {
    if (m_openInsert) { ImGui::OpenPopup("Insert Object"); m_openInsert = false; m_insertFilter.clear(); }
    ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing, ImVec2(0.0f, 0.0f));
    if (!ImGui::BeginPopup("Insert Object")) return;
    SceneNode* parent = m_insertParent;
    ImGui::TextDisabled("Insert into: %s", parent ? parent->name.c_str() : "Workspace");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputTextWithHint("##find", "Search objects", &m_insertFilter, ImGuiInputTextFlags_EnterReturnsTrue);
    std::vector<InsertItem> list = insertItems();
    for (const PremadeInfo& p : premadeList()) list.push_back({p.name, Icons::Id::Model});
    std::string f = m_insertFilter;
    for (char& c : f) c = (char)std::tolower((unsigned char)c);
    ImGui::BeginChild("##list");
    bool first = true;
    for (const InsertItem& o : list) {
        std::string n = o.name;
        for (char& c : n) c = (char)std::tolower((unsigned char)c);
        if (!f.empty() && n.find(f) == std::string::npos) continue;
        Icons::inlineIcon(o.icon);
        ImGui::SameLine();
        if (ImGui::Selectable(o.name) || (enter && first)) {
            insertObject(o.name, parent);
            ImGui::CloseCurrentPopup();
        }
        first = false;
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

// The Command Bar: type Lua and press Enter to run it on the game right now
// (e.g. to change lots of parts at once). Up / Down go through what you ran.
// Command Bar: run a bit of Lua against the game right now (undoable).
void Editor::runCommand(const std::string& code) {
    Log::info("> " + code);
    // While playing, the code runs inside the game (so it can connect events and wait).
    if (m_session && m_session->running() && m_session->scripts().running()) {
        std::string err;
        if (!m_session->scripts().runCommand(code, err)) Log::error(err);
        return;
    }
    ScriptEngine engine(m_scene);
    engine.start(false);
    std::string err;
    if (!engine.runCommand(code, err)) Log::error(err);
    engine.stop();
}

void Editor::renderCommandBar() {
    if (!ImGui::Begin("Command Bar")) { ImGui::End(); return; }
    ImGui::TextDisabled("Run Lua on your game now. Enter runs, Shift+Enter adds a line, Ctrl+Up / Ctrl+Down: history.");
    ImGuiIO& io = ImGui::GetIO();
    static bool active = false;
    static std::string pending;
    static bool hasPending = false;
    if (active && io.KeyCtrl && !m_cmdHistory.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            m_cmdHistoryPos = m_cmdHistoryPos < 0 ? (int)m_cmdHistory.size() - 1 : std::max(0, m_cmdHistoryPos - 1);
            pending = m_cmdHistory[m_cmdHistoryPos]; hasPending = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && m_cmdHistoryPos >= 0) {
            m_cmdHistoryPos = m_cmdHistoryPos + 1 >= (int)m_cmdHistory.size() ? -1 : m_cmdHistoryPos + 1;
            pending = m_cmdHistoryPos >= 0 ? m_cmdHistory[m_cmdHistoryPos] : std::string(); hasPending = true;
        }
    }
    auto swap = [](ImGuiInputTextCallbackData* d) -> int {
        if (!hasPending) return 0;
        d->DeleteChars(0, d->BufTextLen);
        d->InsertChars(0, pending.c_str());
        hasPending = false;
        return 0;
    };
    // Shift+Enter makes a new line; plain Enter runs (ImGui's Ctrl+Enter mode, with Shift standing in for Ctrl).
    bool shiftEnter = io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Enter);
    ImGuiInputTextFlags fl = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways |
                             ImGuiInputTextFlags_AllowTabInput;
    if (shiftEnter) fl &= ~ImGuiInputTextFlags_EnterReturnsTrue;
    bool run = ImGui::InputTextMultiline("##cmd", &m_cmdInput, ImVec2(-1, -1), fl, swap);
    active = ImGui::IsItemActive();
    if (run && !m_cmdInput.empty()) {
        runCommand(m_cmdInput);
        m_cmdHistory.push_back(m_cmdInput);
        m_cmdHistoryPos = -1;
        m_cmdInput.clear();
        pending.clear(); hasPending = true;   // clear the box
    }
    ImGui::End();
}

// F1: every keyboard shortcut in one place.
void Editor::renderShortcuts() {
    if (!m_showShortcuts) return;
    struct Row { const char* keys; const char* what; };
    static const Row kSections[][34] = {
        {{"Ctrl+1 / 2 / 3 / 4", "Select / Move / Scale / Rotate tool"},
         {"Ctrl+L", "Switch the gizmo between local and world"},
         {"W A S D", "Fly the camera (hold Shift to go faster)"},
         {"Q / E", "Camera down / up"},
         {"Right-drag", "Look around"},
         {"Middle-drag", "Pan (Shift+middle-drag orbits)"},
         {"Mouse wheel", "Zoom"},
         {"F", "Zoom to the selection (things with a body)"}},
        {{"Click", "Select (a part in a Model picks the Model)"},
         {"Alt+click", "Select just the part"},
         {"Ctrl+click / Shift+click", "Add to the selection"},
         {"Ctrl+A", "Select everything in the Workspace"},
         {"Ctrl+Up / Ctrl+Down", "Select parent / children"},
         {"Ctrl+Shift+E", "Find and Change: change lots of objects at once"},
         {"Esc", "Select nothing"}},
        {{"Ctrl+Right / Ctrl+Left", "Expand / collapse selected (everything inside)"},
         {"Ctrl+Shift+Left", "Collapse the whole Explorer"},
         {"Ctrl+I", "Insert Object (searchable)"},
         {"F2", "Rename"},
         {"H", "Hide / show"}},
        {{"Ctrl+C / X / V", "Copy / cut / paste"},
         {"Ctrl+Shift+V", "Paste into the selection"},
         {"Ctrl+D", "Duplicate"},
         {"Del", "Delete"},
         {"Ctrl+G / Ctrl+U", "Group into a Model / ungroup"},
         {"Ctrl+Shift+G / N / I / U", "Union / negate / intersect / separate"},
         {"Alt+L", "Lock (can't be clicked in the Viewport)"},
         {"Alt+A", "Anchor / unanchor"},
         {"Ctrl+Z / Ctrl+Y", "Undo / redo"}},
        {{"Ctrl+N / Ctrl+O", "New / open (also Roblox .rbxl / .rbxm)"},
         {"Ctrl+S", "Save (Ctrl+Shift+S: save as)"},
         {"F1", "Show / hide this list"}},
        {{"Tab", "Modeling mode: reshape the selected part (Tab again = done)"},
         {"F8", "Simulate mode: physics and scripts run, you fly around"},
         {"F5", "Play mode: playtest with your character"},
         {"Shift+F5 / Esc", "Stop: back to Build mode"},
         {"F6 / F7", "Pause / step one frame (Simulate and Play)"}},
        {{"1 / 2 / 3", "Pick vertices / edges / faces"},
         {"Click / Shift+click / Ctrl+click", "Pick / add or toggle / take away"},
         {"Drag a box", "Pick everything inside it"},
         {"Alt+click / Ctrl+Alt+click", "Pick an edge loop / edge ring"},
         {"L / Ctrl+L", "Pick everything joined (under the mouse / to what's picked)"},
         {"Ctrl+Plus / Ctrl+Minus", "Pick more / less"},
         {"A / Alt+A / Ctrl+I", "Pick all (again = none) / none / swap"},
         {"G / R / S", "Move / rotate / scale with the mouse (click = done, right-click = cancel)"},
         {"  then X / Y / Z", "Lock to an axis (twice = the part's own); Shift+X = not X"},
         {"  then a number", "Type how far (2 = 2 studs, R 90 = 90 degrees, S 2 = twice as big)"},
         {"  then Ctrl / O", "Go in steps / proportional editing (wheel = how far)"},
         {"E / Shift+E", "Extrude and move / extrude 1 stud out"},
         {"I", "Inset: a smaller face inside each face (mouse = how much)"},
         {"Ctrl+R", "Loop cut (wheel = how many cuts)"},
         {"Shift+D", "Duplicate and move"},
         {"Shift+A", "Add a shape into the mesh"},
         {"X / Del", "Delete menu / delete what's picked"},
         {"Ctrl+X", "Dissolve (take away, faces close up)"},
         {"M", "Merge menu (at center, collapse, by distance)"},
         {"F", "Fill: make a face between picked corners"},
         {"Ctrl+V / Ctrl+E / Ctrl+F", "Vertex / edge / face menus"},
         {"Ctrl+T / Alt+J", "Triangulate / triangles to quads"},
         {"Ctrl+1", "Subdivide smooth (rounder)"},
         {"Shift+N / Alt+N", "Faces point outwards / normals menu"},
         {"Ctrl+M", "Mirror menu"},
         {"H / Shift+H / Alt+H", "Hide picked / hide the rest / show all"},
         {"P", "Separate the picked faces into a new part"},
         {"Right-click / W", "The menu for what you're picking"},
         {"F3", "Search every modeling tool"},
         {"Alt+Z", "X-ray: see and pick through the mesh"},
         {"Numpad 1 / 3 / 7 (Ctrl)", "Front / right / top view (back / left / bottom)"},
         {"Right-drag + WASD", "Fly the camera (letters are tools here)"},
         {"Ctrl+J (Build mode)", "Join the selected parts into one mesh"}},
    };
    static const char* kTitles[] = {"Camera & tools", "Selecting", "Explorer", "Editing", "Files",
                                    "Modes", "Modeling mode"};
    ImGui::SetNextWindowSize(ImVec2(520, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("Keyboard Shortcuts", &m_showShortcuts)) {
        for (int s = 0; s < 7; ++s) {
            ImGui::SeparatorText(kTitles[s]);
            if (ImGui::BeginTable(kTitles[s], 2)) {
                ImGui::TableSetupColumn("keys", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
                for (const Row& r : kSections[s]) {
                    if (!r.keys) break;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1), "%s", r.keys);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.what);
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}

// The Collision Groups editor (like Roblox Studio's): every registered group, and a
// grid of ticks saying which pairs collide. Parts pick their group in Properties.
void Editor::renderCollisionGroups() {
    if (!m_showCollisionGroups) return;
    ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Collision Groups", &m_showCollisionGroups)) { ImGui::End(); return; }
    CollisionGroups& g = m_scene->world().collisionGroups;
    const std::vector<int> slots = g.slots();

    ImGui::TextWrapped("Ticked = the two groups bump into each other. Unticked = they pass through. "
                       "Put parts in a group with Collision Group in Properties (a character is in its "
                       "HumanoidRootPart's group).");
    // Add a group.
    ImGui::SetNextItemWidth(220);
    bool add = ImGui::InputTextWithHint("##newgroup", "New group name", &m_newGroupName, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    add |= ImGui::Button("Add Group");
    if (add) {
        std::string bad = CollisionGroups::badName(m_newGroupName);
        if (bad.empty() && g.find(m_newGroupName) >= 0) bad = "There's already a group called that.";
        if (bad.empty() && g.count() >= CollisionGroups::kMax) bad = "That's the most there can be (32).";
        m_groupError = bad;
        if (bad.empty()) { g.add(m_newGroupName); m_newGroupName.clear(); m_scene->markDirty(); }
    }
    if (!m_groupError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_groupError.c_str());
    ImGui::Separator();

    // The grid: a row per group, a column per group (the lower half, like Roblox's).
    const int n = (int)slots.size();
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg;
    int removeSlot = -1;
    if (ImGui::BeginTable("cgrid", n + 2, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Group");
        for (int i : slots) ImGui::TableSetupColumn(g.names[i].c_str());
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (int r = 0; r < n; ++r) {
            const int a = slots[r];
            ImGui::PushID(a);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (m_renamingGroup == a) {
                ImGui::SetNextItemWidth(140);
                if (ImGui::InputText("##rename", &m_renameText, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    std::string bad = CollisionGroups::badName(m_renameText);
                    if (bad.empty() && m_renameText != g.names[a] && g.find(m_renameText) >= 0) bad = "There's already a group called that.";
                    m_groupError = bad;
                    if (bad.empty() && m_renameText != g.names[a]) {
                        const std::string from = g.names[a], to = m_renameText;
                        g.names[a] = to;
                        m_scene->forEach([&](SceneNode* p) { if (p->collisionGroup == from) p->collisionGroup = to; });
                        m_scene->markDirty();
                    }
                    if (bad.empty()) m_renamingGroup = -1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_renamingGroup = -1;
            } else {
                ImGui::TextUnformatted(g.names[a].c_str());
                if (a != 0 && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Double-click to rename");
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { m_renamingGroup = a; m_renameText = g.names[a]; }
                }
            }
            for (int c = 0; c < n; ++c) {
                ImGui::TableNextColumn();
                if (c > r) continue;   // (the upper half is the same pairs again)
                const int b = slots[c];
                bool on = g.collides(a, b);
                ImGui::PushID(b);
                if (ImGui::Checkbox("##c", &on)) { g.set(a, b, on); m_scene->markDirty(); }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s and %s: %s", g.names[a].c_str(), g.names[b].c_str(), on ? "collide" : "pass through each other");
                ImGui::PopID();
            }
            ImGui::TableNextColumn();
            if (a != 0 && ImGui::SmallButton("Delete")) removeSlot = a;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeSlot > 0) {   // (its parts keep the name and act as Default until it's added again)
        g.remove(g.names[removeSlot]);
        if (m_renamingGroup == removeSlot) m_renamingGroup = -1;
        m_scene->markDirty();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Layout & chrome
// ---------------------------------------------------------------------------

void Editor::buildDockspace() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking        | ImGuiWindowFlags_NoTitleBar    |
        ImGuiWindowFlags_NoCollapse       | ImGuiWindowFlags_NoResize      |
        ImGuiWindowFlags_NoMove           | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus       | ImGuiWindowFlags_MenuBar;

    // Push WindowPadding first so it survives the early pop below and keeps the
    // host body edge-to-edge (the toolbar/status strips add their own padding).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##DockHost", nullptr, hostFlags);
    ImGui::PopStyleVar(2);  // rounding + border; WindowPadding(0,0) stays active

    renderMenuBar();
    renderToolbar();

    // Reserve a row at the bottom of the host window for the status bar.
    float statusH = ImGui::GetFrameHeightWithSpacing();
    ImGuiID dsId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dsId, ImVec2(0, -statusH), ImGuiDockNodeFlags_None);

    if (m_firstLayout) {
        m_firstLayout = false;
        ImGui::DockBuilderRemoveNode(dsId);
        ImGui::DockBuilderAddNode(dsId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dsId, vp->WorkSize);

        // Roblox Studio's layout: Toolbox on the left, Explorer over
        // Properties on the right, Output and the Command Bar along the bottom.
        ImGuiID left, center, right, bottom;
        ImGui::DockBuilderSplitNode(dsId,   ImGuiDir_Left,  0.17f, &left,   &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, &right,  &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,  0.26f, &bottom, &center);
        ImGuiID rightTop, rightBottom;
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.45f, &rightTop, &rightBottom);
        ImGuiID bottomLeft, bottomRight;
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.42f, &bottomRight, &bottomLeft);

        ImGui::DockBuilderDockWindow("Toolbox",       left);
        ImGui::DockBuilderDockWindow("Script Editor", center);
        ImGui::DockBuilderDockWindow("Viewport",      center);
        ImGui::DockBuilderDockWindow("Output",        bottomLeft);
        ImGui::DockBuilderDockWindow("Animation Editor", bottomLeft);
        ImGui::DockBuilderDockWindow("Command Bar",   bottomRight);
        ImGui::DockBuilderDockWindow("Team",          bottomRight);
        ImGui::DockBuilderDockWindow("Assistant",     bottomRight);
        ImGui::DockBuilderDockWindow("Explorer",      rightTop);
        ImGui::DockBuilderDockWindow("Properties",    rightBottom);
        ImGui::DockBuilderDockWindow("Lighting",      rightBottom);
        ImGui::DockBuilderDockWindow("Player",        rightBottom);
        ImGui::DockBuilderFinish(dsId);
        m_viewport->focus();
    }

    renderStatusBar();

    ImGui::PopStyleVar();   // WindowPadding
    ImGui::End();
}

void Editor::renderMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Game", "Ctrl+N")) {
            m_pending = Pending::New;
            if (m_dirty) m_openDiscard = true; else { newScene(); m_pending = Pending::None; }
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            m_pending = Pending::Open;
            if (m_dirty) m_openDiscard = true; else m_openOpen = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) save();
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
            m_nameInput = m_scene->info().title;
            m_openSaveAs = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import 3D Model, Picture, Sound...")) importDialog();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("3D models (.fbx .obj .gltf .glb .stl .ply), pictures, sounds and scripts.\n"
                              "You can also just drag files from your computer onto Studio.");
        if (ImGui::MenuItem("Import Roblox File (.rbxl / .rbxm)...")) { m_pending = Pending::Open; m_openOpen = true; }
        if (ImGui::MenuItem("Export to Roblox Place (.rbxlx)")) exportRoblox(false);
        if (ImGui::MenuItem("Export Selection to Roblox Model (.rbxmx)", nullptr, false, m_scene->selected() != nullptr))
            exportRoblox(true);
        ImGui::Separator();
        if (ImGui::MenuItem(m_team->active() ? "Team Create (on)..." : "Team Create...")) m_openTeam = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Game Settings...")) m_openInfo = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Publish to Guts&Bolts...")) m_openPublish = true;
        if (ImGui::MenuItem("Publish Selection to Library...", nullptr, false, m_scene->selected() != nullptr)) m_openPublishModel = true;
        if (Online::staff() && ImGui::MenuItem("Make Gear...")) m_openMakeGear = true;   // staff: Tools sold in the catalog
        if (ImGui::MenuItem("Upload Missing Item Pictures", nullptr, false, Online::online())) fixItemPictures();
        if (ImGui::MenuItem("Guts&Bolts Server...")) m_openServer = true;
        if (ImGui::MenuItem("Library (plugins, audio)")) m_showPluginLibrary = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Play in Guts&BoltsPlayer")) {
            if (m_path.empty()) {
                Log::warn("Save your game first (File > Save), then try again.");
                m_nameInput = m_scene->info().title;
                m_openSaveAs = true;
            } else {
                saveFile(m_path);
                if (!Paths::launch(Paths::sibling("GutsAndBoltsPlayer"), m_path))
                    Log::error("Couldn't find GutsAndBoltsPlayer next to the editor.");
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        SceneNode* sel = m_scene->selected();
        bool editable = canEdit(sel);
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undo.empty() && !m_playing)) undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redo.empty() && !m_playing)) redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Copy",      "Ctrl+C", false, editable)) copySelected();
        if (ImGui::MenuItem("Paste",     "Ctrl+V", false, !m_clipboard.empty())) paste();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, editable)) duplicateSelected();
        if (ImGui::MenuItem("Delete",    "Del",    false, sel && !m_scene->isProtected(sel))) deleteSelected();
        if (ImGui::MenuItem("Rename",    "F2",     false, editable)) m_outliner->beginRename(sel);
        ImGui::Separator();
        if (ImGui::MenuItem("Select All", "Ctrl+A")) selectAll();
        if (ImGui::MenuItem("Select Parent", "Ctrl+Up", false, sel != nullptr)) selectParent();
        if (ImGui::MenuItem("Select Children", "Ctrl+Down", false, sel != nullptr)) selectChildren();
        if (ImGui::MenuItem("Find and Change...", "Ctrl+Shift+E", m_showBulkEdit)) m_showBulkEdit = !m_showBulkEdit;
        ImGui::Separator();
        if (ImGui::MenuItem("Group", "Ctrl+G", false, editable)) groupSelected();
        if (ImGui::MenuItem("Ungroup", "Ctrl+U", false, editable && sel->kind == NodeKind::Model)) ungroupSelected();
        if (ImGui::MenuItem("Union", "Ctrl+Shift+G", false, editable)) unionSelected(0);
        if (ImGui::MenuItem("Negate", "Ctrl+Shift+N", false, editable)) negateSelected();
        if (ImGui::MenuItem("Intersect", "Ctrl+Shift+I", false, editable)) unionSelected(1);
        if (ImGui::MenuItem("Separate", "Ctrl+Shift+U", false, editable && !sel->unionSource.empty())) separateSelected();
        if (ImGui::MenuItem("Hide / Show", "H", false, sel != nullptr)) toggleHidden();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Cube"))     spawnPrimitive(PrimitiveType::Cube);
        if (ImGui::MenuItem("Sphere"))   spawnPrimitive(PrimitiveType::Sphere);
        if (ImGui::MenuItem("Plane"))    spawnPrimitive(PrimitiveType::Plane);
        if (ImGui::MenuItem("Cylinder")) spawnPrimitive(PrimitiveType::Cylinder);
        ImGui::Separator();
        if (ImGui::MenuItem("Script")) addScript(m_scene->selected());
        if (ImGui::MenuItem("Model"))  addModel();
        if (ImGui::MenuItem("Point Light")) addLight(LightType::Point);
        if (ImGui::MenuItem("Spot Light"))  addLight(LightType::Spot);
        if (ImGui::MenuItem("Sound"))       addSound();
        ImGui::Separator();
        if (ImGui::BeginMenu("Ready-made")) {
            for (const PremadeInfo& p : premadeList()) {
                if (ImGui::MenuItem(p.name)) spawnPremade(p.kind);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tip);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset Camera")) m_viewport->resetCamera();
        ImGui::Separator();
        SceneNode* sel = m_scene->selected();
        if (ImGui::MenuItem("Expand Selected", "Ctrl+Right", false, sel != nullptr)) m_outliner->expand(m_scene->selection());
        if (ImGui::MenuItem("Collapse Selected", "Ctrl+Left", false, sel != nullptr)) m_outliner->collapse(m_scene->selection());
        if (ImGui::MenuItem("Collapse All", "Ctrl+Shift+Left")) m_outliner->collapseAll();
        ImGui::Separator();
        if (ImGui::MenuItem("Script Analysis", nullptr, m_scriptEditor->analysisShown())) m_scriptEditor->showAnalysis();
        if (ImGui::MenuItem("Project Dependency Map")) m_scriptEditor->showDependencyMap();
        if (ImGui::MenuItem("Performance Monitor", nullptr, &m_showPerformance)) {}
        if (ImGui::MenuItem("Performance Doctor", nullptr, &m_showPerformanceDoctor)) {}
        if (ImGui::MenuItem("Lua Debugger", nullptr, &m_showLuaDebugger)) {}
        if (ImGui::MenuItem("Scene Validator", nullptr, &m_showSceneValidator)) {}
        if (ImGui::MenuItem("Project Time Machine", nullptr, &m_showProjectTimeMachine)) {}
        ImGui::Separator();
        if (ImGui::MenuItem("Settings...")) m_showSettings = true;
        if (ImGui::MenuItem("Keyboard Shortcuts", "F1", m_showShortcuts)) m_showShortcuts = !m_showShortcuts;
        if (ImGui::MenuItem("Collision Groups", nullptr, m_showCollisionGroups)) m_showCollisionGroups = !m_showCollisionGroups;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Mode")) {
        static const char* keys[] = {"Shift+F5", "Tab", "F8", "F5"};
        for (int i = 0; i < 4; ++i)
            if (ImGui::MenuItem(kStudioModeNames[i], keys[i], (int)m_state.mode == i)) setMode((StudioMode)i);
        ImGui::Separator();
        if (ImGui::MenuItem("Play Here", nullptr, false, !m_playing)) startPlay(1);
        if (ImGui::MenuItem(m_state.simPaused ? "Resume" : "Pause", "F6", false, m_playing)) m_state.simPaused = !m_state.simPaused;
        if (ImGui::MenuItem("Step one frame", "F7", false, m_playing)) { m_state.simPaused = true; m_state.simStep = true; }
        ImGui::Separator();
        const bool multi = m_multiTest->running();
        for (int n = MultiTest::kMinPlayers; n <= MultiTest::kMaxPlayers; ++n) {
            const std::string label = "Test with " + std::to_string(n) + " players";
            std::string err;
            if (ImGui::MenuItem(label.c_str(), nullptr, false, !multi)) startMultiTest(n, err);
        }
        if (ImGui::MenuItem("Stop multiplayer test", nullptr, false, multi)) stopMultiTest();
        ImGui::Separator();
        if (ImGui::MenuItem("Export session replay bundle...", nullptr, false, m_multiTest->hasSession())) {
            std::string path, err;
            if (m_multiTest->exportSession(path, err))
                Log::system("Multiplayer session bundle exported to " + path);
            else
                Log::error("Couldn't export multiplayer session: " + err);
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

void Editor::renderDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

    if (m_openDiscard) { ImGui::OpenPopup("Unsaved changes"); m_openDiscard = false; }
    if (m_openSaveAs)  { ImGui::OpenPopup("Save Game");       m_openSaveAs = false; }
    if (m_openOpen)    { ImGui::OpenPopup("Open Game");       m_openOpen = false; m_openPathInput.clear(); }
    if (m_openInfo)    { ImGui::OpenPopup("Game Settings");   m_openInfo = false; }
    if (m_openTeam)    { ImGui::OpenPopup("Team Create");     m_openTeam = false; m_teamError.clear(); }

    // --- Team Create: host or join ---
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Team Create", nullptr)) {
        if (m_team->active()) {
            ImGui::TextWrapped("You're in Team Create with %d %s.", (int)m_team->members().size(),
                               m_team->members().size() == 1 ? "person" : "people");
            ImGui::TextDisabled("See the Team panel for who's here and to chat.");
            ImGui::Spacing();
            if (ImGui::Button("Leave Team Create", ImVec2(180, 0))) { m_team->leave(); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextWrapped("Build a game together! Everyone sees everyone else's changes as they happen.");
            ImGui::SeparatorText("Host");
            ImGui::TextWrapped("Share the game that's open right now. Friends join with your address.");
            if (ImGui::Button("Start Team Create", ImVec2(200, 0))) {
                std::string err;
                if (m_team->host(kTeamCreatePort, err)) ImGui::CloseCurrentPopup();
                else m_teamError = err;
            }
            ImGui::SeparatorText("Join");
            ImGui::TextWrapped("Joining replaces what you have open with the host's game (save first!).");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##tcaddr", "host address, e.g. 192.168.1.20", &m_teamAddress);
            if (ImGui::Button("Join", ImVec2(120, 0))) {
                std::string err;
                if (m_team->join(m_teamAddress, err)) ImGui::CloseCurrentPopup();
                else m_teamError = err;
            }
            if (!m_teamError.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", m_teamError.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- Discard changes? ---
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have changes that aren't saved yet.");
        ImGui::Text("Throw them away?");
        ImGui::Spacing();
        if (ImGui::Button("Discard changes", ImVec2(150, 0))) {
            ImGui::CloseCurrentPopup();
            if (m_pending == Pending::New) { newScene(); m_pending = Pending::None; }
            else if (m_pending == Pending::Open) m_openOpen = true;
            else if (m_pending == Pending::OpenDropped) { m_pending = Pending::None; openFile(m_droppedGame); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_pending = Pending::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- Save As: just ask for a name; it goes in the games folder ---
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Name your game:");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##name", &m_nameInput, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled("Saved in: %s", Paths::gamesFolder().string().c_str());
        ImGui::Spacing();
        bool valid = !m_nameInput.empty() && m_nameInput.find_first_of("/\\:*?\"<>|") == std::string::npos;
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Save", ImVec2(100, 0)) || (enter && valid)) {
            if (m_scene->info().title == "My Game") m_scene->info().title = m_nameInput;
            saveFile((Paths::gamesFolder() / (m_nameInput + Paths::kExtension)).string());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // --- Open: pick from the games folder or type a path ---
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Open Game", nullptr)) {
        m_pending = Pending::None;
        ImGui::TextDisabled("Games in %s", Paths::gamesFolder().string().c_str());
        ImGui::BeginChild("##games", ImVec2(0, 220), ImGuiChildFlags_Borders);
        auto games = Paths::listGames();
        // Roblox models sitting in the games folder can be inserted too.
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(Paths::gamesFolder(), ec))
            if (e.path().extension() == ".rbxm" || e.path().extension() == ".rbxmx") games.push_back(e.path());
        if (games.empty()) ImGui::TextDisabled("No saved games yet.");
        for (auto& g : games) {
            std::string ext = g.extension().string();
            std::string label = g.stem().string();
            if (ext == ".rbxl" || ext == ".rbxlx") label += "   (Roblox place)";
            if (ext == ".rbxm" || ext == ".rbxmx") label += "   (Roblox model - inserts into this game)";
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                openFile(g.string());
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Double-click to open");
        }
        ImGui::EndChild();
        ImGui::Text("...or type a file path (.gbscene, or Roblox .rbxl / .rbxlx / .rbxm / .rbxmx):");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##path", &m_openPathInput);
        ImGui::BeginDisabled(m_openPathInput.empty());
        if (ImGui::Button("Open path", ImVec2(110, 0))) {
            openFile(m_openPathInput);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // --- Game settings (shown in the Player app) ---
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Game Settings", nullptr)) {
        if (ImGui::BeginTabBar("##gamesettings")) {
            if (ImGui::BeginTabItem("Basic Info")) {
                GameInfo& info = m_scene->info();
                ImGui::TextDisabled("How your game shows up in Guts&BoltsPlayer");
                ImGui::InputText("Title",  &info.title);
                ImGui::InputText("Author", &info.author);
                ImGui::InputTextMultiline("Description", &info.description, ImVec2(-1, 90));
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Damage & Blood")) {
                ImGui::TextDisabled("Fall damage, deaths and gore in your game");
                drawGameRules(m_scene->world());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        if (ImGui::Button("Done", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::renderTeamPanel() {
    ImGui::Begin("Team");
    if (!m_team->active()) {
        if (m_team->waiting()) ImGui::TextDisabled("Joining...");
        else {
            ImGui::TextDisabled("Not in Team Create.");
            if (ImGui::Button("Start or join Team Create...")) m_openTeam = true;
        }
    } else {
        ImGui::TextDisabled(m_team->hosting() ? "Hosting Team Create" : "In Team Create");
        for (const auto& mem : m_team->members()) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float h = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.35f,
                ImGui::ColorConvertFloat4ToU32(ImVec4(mem.color.r, mem.color.g, mem.color.b, 1)));
            ImGui::Dummy(ImVec2(h, h));
            ImGui::SameLine();
            ImGui::Text("%s%s", mem.name.c_str(), mem.id == m_team->myId() ? " (you)" : "");
            if (SceneNode* n = mem.selected ? m_scene->findById(mem.selected) : nullptr) {
                ImGui::SameLine();
                ImGui::TextDisabled("- editing %s", n->name.c_str());
            }
        }
    }
    ImGui::Separator();
    ImGui::BeginChild("##teamchat", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
    for (const auto& l : m_team->chat()) {
        if (l.system) ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1), "%s", l.text.c_str());
        else {
            ImGui::TextColored(ImVec4(0.55f, 0.8f, 1, 1), "%s:", l.from.c_str());
            ImGui::SameLine();
            ImGui::TextWrapped("%s", l.text.c_str());
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::BeginDisabled(!m_team->active());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##teamsay", "Message your team...", &m_teamChat, ImGuiInputTextFlags_EnterReturnsTrue)) {
        m_team->say(m_teamChat);
        m_teamChat.clear();
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void Editor::renderStatusBar() {
    const ImVec4 kBarBg  = {0.086f, 0.094f, 0.114f, 1.0f};
    const ImVec4 kAccent = {0.40f, 0.66f, 1.00f, 1.0f};

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBarBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 5));
    ImGui::BeginChild("##statusbar", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (m_playing) {
        ImGui::TextColored(m_session->runOnly() ? ImVec4(0.4f, 0.75f, 1.0f, 1.0f) : ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "%s%s",
                           m_session->runOnly() ? "SIMULATE" : "PLAY", m_state.simPaused ? " (paused)" : "");
    } else if (m_state.mode == StudioMode::Modeling) {
        static const char* picks[] = {"vertices", "edges", "faces"};
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "MODELING");
        ImGui::SameLine();
        SceneNode* mn = m_scene->findById(m_state.modeling.node);
        ImGui::TextDisabled("  %d picked (%s)   %d corners, %d faces", MeshEdit::countSelected(m_state.modeling.sel),
                            picks[std::clamp(m_state.modeling.selectMode, 0, 2)],
                            mn && mn->editMesh ? (int)mn->editMesh->verts.size() : 0,
                            mn && mn->editMesh ? (int)mn->editMesh->faces.size() : 0);
    } else {
        const char* toolName =
            m_state.tool == GizmoTool::Select    ? "Select" :
            m_state.tool == GizmoTool::Translate ? "Move"   :
            m_state.tool == GizmoTool::Rotate    ? "Rotate" : "Scale";
        ImGui::TextDisabled("Tool:");
        ImGui::SameLine();
        ImGui::TextColored(kAccent, "%s", toolName);
    }
    ImGui::SameLine(); ImGui::TextDisabled("   "); ImGui::SameLine();

    if (SceneNode* sel = m_scene->selected()) {
        ImGui::TextDisabled("Selected:");
        ImGui::SameLine();
        ImGui::TextUnformatted(sel->name.c_str());
    } else {
        ImGui::TextDisabled("Nothing selected");
    }

    if (GraphicsSettings::get().showFps) {
        ImGui::SameLine(); ImGui::TextDisabled("   "); ImGui::SameLine();
        ImGui::TextDisabled("%.0f FPS", ImGui::GetIO().Framerate);
    }

    ImGui::SameLine();
    const char* hint = m_state.mode == StudioMode::Modeling
        ? "1/2/3 pick mode   G/R/S move/turn/size   E extrude   Ctrl+R loop cut   X delete   Right-click menu   F3 search   Tab done"
        : m_playing && m_session->runOnly()
        ? "Click to inspect   Drag with gizmo   Right-drag + WASDQE fly   F6 pause   Shift+F5 stop"
        : m_playing
        ? "WASD move   Space jump   Right-drag camera   Wheel zoom   Click parts   F5/Esc stop"
        : "Ctrl+1-4 tools   Right-drag + WASDQE fly   MMB pan   F focus   Ctrl+D duplicate   F5 play   F1 all shortcuts";
    float avail = ImGui::GetContentRegionAvail().x;
    float tw    = ImGui::CalcTextSize(hint).x;
    if (tw < avail) ImGui::SameLine(ImGui::GetCursorPosX() + (avail - tw));
    ImGui::TextDisabled("%s", hint);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// The right-click menu's actions for what's selected (the Explorer's and the 3D view's),
// like Roblox Studio's: clipboard, grouping, selecting, anchoring and sharing.
void Editor::selectionMenuItems(bool inViewport) {
    SceneNode* sel = m_scene->selected();
    const bool any = sel != nullptr;
    auto later = [this](std::function<void()> f) { m_deferred = std::move(f); };
    ImGui::Separator();
    if (ImGui::MenuItem("Cut", "Ctrl+X", false, any)) later([this] { cutSelected(); });
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, any)) later([this] { copySelected(); });
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_clipboard.empty())) later([this] { paste(); });
    if (ImGui::MenuItem("Paste Into", "Ctrl+Shift+V", false, any && !m_clipboard.empty())) later([this] { pasteInto(); });
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, any)) later([this] { duplicateSelected(); });
    ImGui::Separator();
    if (ImGui::MenuItem("Group", "Ctrl+G", false, any)) later([this] { groupSelected(); });
    if (sel && sel->kind == NodeKind::Model && ImGui::MenuItem("Ungroup", "Ctrl+U")) later([this] { ungroupSelected(); });
    if (sel && sel->kind == NodeKind::Model && ImGui::MenuItem("Center the Model's Pivot")) later([this] { centerModelPivot(); });
    bool parts = false;
    for (SceneNode* n : m_scene->selection()) parts = parts || n->isPart();
    if (parts && ImGui::BeginMenu("Solid Modeling")) {
        if (ImGui::MenuItem("Union", "Ctrl+Shift+G")) later([this] { unionSelected(0); });
        if (ImGui::MenuItem("Intersect", "Ctrl+Shift+I")) later([this] { unionSelected(1); });
        if (ImGui::MenuItem("Negate", "Ctrl+Shift+N")) later([this] { negateSelected(); });
        if (ImGui::MenuItem("Separate", "Ctrl+Shift+U")) later([this] { separateSelected(); });
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Select Children", "Ctrl+Down", false, any && !sel->children.empty())) later([this] { selectChildren(); });
    if (ImGui::MenuItem("Select Parent", "Ctrl+Up", false, any && sel->parent && sel->parent != m_scene->root()))
        later([this] { selectParent(); });
    if (ImGui::MenuItem("Zoom To", "F", false, any)) later([this] { m_viewport->focusSelected(); });
    if (parts) {
        if (ImGui::MenuItem(sel && sel->isPart() && sel->anchored ? "Unanchor" : "Anchor", "Alt+A")) later([this] { toggleAnchored(); });
        if (ImGui::MenuItem(sel && sel->locked ? "Unlock" : "Lock", "Alt+L")) later([this] { toggleLocked(); });
    }
    if (inViewport && any && ImGui::MenuItem(sel->visible ? "Hide" : "Show", "H")) later([this] { toggleHidden(); });
    if (ImGui::MenuItem("Export to Roblox Model (.rbxmx)", nullptr, false, any)) later([this] { exportRoblox(true); });
    publishMenuItem();
}
