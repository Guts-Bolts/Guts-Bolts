#include "Application.h"
#include "core/AppWindow.h"
#include "scene/Scene.h"
#include "editor/Editor.h"
#include "scripting/ScriptEngine.h"

#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace {
double wallSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// "ctrl+shift+left" -> modifiers + key (test helper).
struct Chord { bool ctrl = false, shift = false, alt = false; ImGuiKey key = ImGuiKey_None; std::string text; };
Chord parseChord(const std::string& text) {
    Chord c;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, '+')) {
        if (part == "ctrl") c.ctrl = true;
        else if (part == "shift") c.shift = true;
        else if (part == "alt") c.alt = true;
        else if (part == "left") c.key = ImGuiKey_LeftArrow;
        else if (part == "right") c.key = ImGuiKey_RightArrow;
        else if (part == "up") c.key = ImGuiKey_UpArrow;
        else if (part == "down") c.key = ImGuiKey_DownArrow;
        else if (part == "del") c.key = ImGuiKey_Delete;
        else if (part == "esc") c.key = ImGuiKey_Escape;
        else if (part == "enter") c.key = ImGuiKey_Enter;
        else if (part == "tab") c.key = ImGuiKey_Tab;
        else if (part == "home") c.key = ImGuiKey_Home;
        else if (part.size() >= 2 && part[0] == 'f') c.key = (ImGuiKey)(ImGuiKey_F1 + std::stoi(part.substr(1)) - 1);
        else if (part.size() == 1 && part[0] >= '0' && part[0] <= '9') c.key = (ImGuiKey)(ImGuiKey_0 + (part[0] - '0'));
        else if (part.size() == 1 && part[0] >= 'a' && part[0] <= 'z') c.key = (ImGuiKey)(ImGuiKey_A + (part[0] - 'a'));
    }
    return c;
}
} // namespace

Application::Application(LaunchOptions opts) : m_opts(std::move(opts)) {
    if (!m_opts.testCommand.empty()) ScriptEngine::testHooks = true;   // (__gb_testInput for tests)
    m_window = std::make_unique<AppWindow>("Guts and Bolts", 1280, 720, "editor_layout.ini");
    m_window->setFixedTimestep(!m_opts.screenshot.empty());
    m_scene  = std::make_unique<Scene>();
    m_editor = std::make_unique<Editor>(m_window->handle(), m_scene.get());
    if (!m_opts.openFile.empty()) m_editor->openFile(m_opts.openFile);
}

Application::~Application() {
    m_editor.reset();
    m_scene.reset();
    m_window.reset();
}

void Application::run() {
    int frame = 0;
    std::vector<Chord> chords;
    {
        // "type:hello_world" types text ('_' = space); anything else is a key chord.
        std::stringstream ss(m_opts.testKeys);
        std::string w;
        while (ss >> w) {
            if (w.rfind("type:", 0) == 0) {
                Chord c;
                c.text = w.substr(5);
                std::replace(c.text.begin(), c.text.end(), '_', ' ');
                chords.push_back(c);
            } else {
                chords.push_back(parseChord(w));
            }
        }
    }
    // Test helper: mouse actions, one every 8 frames from frame 60.
    // "click:x:y", "right:x:y" (right-click), "shift:x:y", "drag:x0:y0:x1:y1", "wheel:x:y:amount" (+ = in).
    struct MouseAct { bool shift = false; float x0, y0, x1, y1; float wheel = 0.0f; int button = 0; };
    std::vector<MouseAct> mouse;
    {
        std::stringstream ss(m_opts.testMouse);
        std::string w;
        while (ss >> w) {
            MouseAct a{};
            float v[4] = {0, 0, 0, 0};
            std::string kind = w.substr(0, w.find(':'));
            std::replace(w.begin(), w.end(), ':', ' ');
            std::stringstream ps(w.substr(kind.size()));
            for (float& f : v) ps >> f;
            a.shift = kind == "shift";
            a.button = kind == "right" ? 1 : 0;
            if (kind == "wheel") a.wheel = v[2];
            a.x0 = v[0]; a.y0 = v[1];
            a.x1 = kind == "drag" ? v[2] : v[0];
            a.y1 = kind == "drag" ? v[3] : v[1];
            mouse.push_back(a);
        }
    }
    while (!m_window->shouldClose()) {
        ++frame;
        float dt = m_window->beginFrame([&] {
            // Test helper: hold a key down while playing.
            if (!m_opts.holdKey.empty() && frame > 3) {
                ImGuiKey k = m_opts.holdKey == "Space" ? ImGuiKey_Space
                           : (ImGuiKey)(ImGuiKey_A + (m_opts.holdKey[0] - 'A'));
                ImGui::GetIO().AddKeyEvent(k, true);
            }
            int mstep = frame - 60;
            if (mstep >= 0 && mstep / 8 < (int)mouse.size()) {
                const MouseAct& a = mouse[mstep / 8];
                ImGuiIO& io = ImGui::GetIO();
                int k = mstep % 8;
                io.AddKeyEvent(ImGuiMod_Shift, a.shift && k < 7);
                io.AddKeyEvent(ImGuiKey_LeftShift, a.shift && k < 7);
                if (k == 0) io.AddMousePosEvent(a.x0, a.y0);
                if (a.wheel != 0.0f) { if (k == 1) io.AddMouseWheelEvent(0.0f, a.wheel); }
                else if (k == 1) io.AddMouseButtonEvent(a.button, true);
                if (k >= 2 && k <= 4) io.AddMousePosEvent(a.x0 + (a.x1 - a.x0) * (k - 1) / 3.0f, a.y0 + (a.y1 - a.y0) * (k - 1) / 3.0f);
                if (k == 5 && a.wheel == 0.0f) io.AddMouseButtonEvent(a.button, false);
            }
            // Test helper: press one chord every 6 frames, starting at frame 20.
            int step = frame - 20;
            if (step >= 0 && step % 6 < 2 && step / 6 < (int)chords.size()) {
                const Chord& c = chords[step / 6];
                bool down = step % 6 == 0;
                ImGuiIO& io = ImGui::GetIO();
                io.AddKeyEvent(ImGuiMod_Ctrl, down && c.ctrl);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, down && c.ctrl);
                io.AddKeyEvent(ImGuiMod_Shift, down && c.shift);
                io.AddKeyEvent(ImGuiKey_LeftShift, down && c.shift);
                io.AddKeyEvent(ImGuiMod_Alt, down && c.alt);
                io.AddKeyEvent(ImGuiKey_LeftAlt, down && c.alt);
                if (!c.text.empty()) { if (down) io.AddInputCharactersUTF8(c.text.c_str()); }
                else io.AddKeyEvent(c.key, down);
            }
        });
        if (m_opts.play && frame == (m_opts.testDrop.empty() ? 3 : 20)) m_editor->togglePlay();   // (after a test drop)
        if (m_opts.teamHost && frame == 2) m_editor->startTeamCreate(true, "");
        if (!m_opts.teamJoin.empty() && frame == 2) m_editor->startTeamCreate(false, m_opts.teamJoin);
        if (!m_opts.testAddPart.empty() && frame == 60) m_editor->testAddPart(m_opts.testAddPart);
        if (m_opts.testAnalysis && frame == 5) m_editor->testAnalysis();
        if (m_opts.testFixPictures && frame == 120) m_editor->fixItemPictures();
        if (!m_opts.testPremades.empty() && frame == 2) m_editor->testPremades(m_opts.testPremades);
        if (!m_opts.testTools.empty() && frame > 40 && frame % 25 == 0) {   // one step every 25 frames
            size_t sp = m_opts.testTools.find(' ');
            m_editor->testToolStep(m_opts.testTools.substr(0, sp));
            m_opts.testTools = sp == std::string::npos ? "" : m_opts.testTools.substr(sp + 1);
        }
        if (!m_opts.testSelect.empty() && frame == 10) m_editor->testSelect(m_opts.testSelect);
        if (!m_opts.testMesh.empty() && frame == 14) m_editor->testMesh(m_opts.testMesh);
        if (m_opts.testAnim) m_editor->testAnimation(frame);
        if (m_opts.testCollide && frame == 5) m_editor->testCollisions();
        if (!m_opts.testCommand.empty() && frame == 12) m_editor->runCommand(m_opts.testCommand);
        if (!m_opts.testInsert.empty() && frame == 8) m_editor->testInsert(m_opts.testInsert);
        if (!m_opts.exportRoblox.empty() && frame == 2) m_editor->testExportRoblox(m_opts.exportRoblox);
        if (!m_opts.testSnapshot.empty() && frame == 3) m_editor->testSnapshot(m_opts.testSnapshot);
        if (!m_opts.testDrop.empty() && frame == 10) m_editor->testDrop(m_opts.testDrop);
        if (!m_opts.testMultiplayer.empty()) testMultiplayer(frame);

        m_editor->render(dt);

        bool shoot = !m_opts.screenshot.empty() && frame == m_opts.frames;
        m_window->endFrame(shoot ? m_opts.screenshot : std::string());
        if (shoot) m_window->close();
    }
}

// --test-multiplayer "N[:seconds]": drives the multiplayer test through the same AI tool
// the Assistant and MCP apps use, and prints each answer (tools/test_multiplayer.py reads them).
void Application::testMultiplayer(int frame) {
    const std::string& spec = m_opts.testMultiplayer;
    const size_t colon = spec.find(':');
    const int players = std::atoi(spec.c_str());
    const double length = colon == std::string::npos ? 30.0 : std::atof(spec.c_str() + colon + 1);
    auto tool = [&](const char* label, const nlohmann::json& args) {
        AiToolResult r = m_editor->runAiTool("multiplayer_test", args);
        std::printf("MULTITEST %s %s\n", label, r.data.dump().c_str());
        std::fflush(stdout);
    };
    if (m_multiStep == 0 && frame >= 5) {
        m_multiStart = wallSeconds();
        tool("start", {{"action", "start"}, {"players", players}});
        m_multiStep = 1;
    } else if (m_multiStep == 1 && wallSeconds() - m_multiStart > length * 0.5) {
        tool("add_player", {{"action", "add_player"}});   // someone joining late
        m_multiStep = 2;
    } else if (m_multiStep == 2 && wallSeconds() - m_multiStart > length) {
        tool("output", {{"action", "output"}, {"lines", 400}});
        tool("stop", {{"action", "stop"}});
        m_multiStep = 3;
        m_window->close();
    }
}
