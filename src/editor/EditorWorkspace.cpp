// The workspace: more 3D views (like Blender's), panels put wherever you like and kept
// that way next time, and View > Workspace > Reset to put everything back.
//
// Where the panels are is ImGui's own layout file (editor_layout.ini). This adds a
// [GutsWorkspace][Layout] part to it: which extra views are open and how each one looks.
// Without that part (a new computer, or a layout file from before) Studio starts with
// its standard layout.
#include "Editor.h"
#include "panels/ViewportPanel.h"
#include "panels/OutlinerPanel.h"
#include "../core/I18n.h"
#include "../core/Log.h"
#include "../scene/Scene.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace {
// One view as the layout file keeps it.
struct SavedView {
    int number = 1;
    int shading = 0;
    bool xray = false, ortho = false;
    float yaw = 45.0f, pitch = 25.0f, distance = 12.0f;
    float px = 0.0f, py = 1.0f, pz = 0.0f;
};
struct SavedWorkspace {
    bool seen = false;                  // the layout file had our part
    std::vector<SavedView> views;       // [0] may be the main one (number 1)
    std::vector<int> panels;            // which panels were open (Editor::Panel order); empty = from before that was kept
};
SavedWorkspace g_saved;
std::function<std::string()> g_writeWorkspace;   // the Editor's views, as lines

void* wsReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* name) {
    if (std::strcmp(name, "Layout") != 0) return nullptr;
    g_saved = SavedWorkspace{};
    g_saved.seen = true;
    return &g_saved;
}

void wsReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
    if (std::strncmp(line, "Panels=", 7) == 0) {
        g_saved.panels.clear();
        std::istringstream in(line + 7);
        for (int v; in >> v; ) g_saved.panels.push_back(v);
        return;
    }
    SavedView v;
    int xray = 0, ortho = 0;
    if (std::sscanf(line, "View=%d %d %d %d %f %f %f %f %f %f", &v.number, &v.shading, &xray, &ortho,
                    &v.yaw, &v.pitch, &v.distance, &v.px, &v.py, &v.pz) >= 4) {
        v.xray = xray != 0;
        v.ortho = ortho != 0;
        v.number = std::clamp(v.number, 1, 99);
        v.shading = std::clamp(v.shading, 0, 3);
        g_saved.views.push_back(v);
    }
}

void wsWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
    if (!g_writeWorkspace) return;
    out->appendf("[%s][Layout]\n", handler->TypeName);
    out->append(g_writeWorkspace().c_str());
    out->append("\n");
}

std::string viewLine(ViewportPanel& v) {
    const Camera& c = v.camera();
    char b[256];
    std::snprintf(b, sizeof(b), "View=%d %d %d %d %.2f %.2f %.2f %.2f %.2f %.2f\n", v.number(), (int)v.shading, v.xray ? 1 : 0,
                  v.orthographic() ? 1 : 0, c.yaw, c.pitch, c.distance, c.pivot.x, c.pivot.y, c.pivot.z);
    return b;
}

void applySaved(ViewportPanel& v, const SavedView& s, bool camera) {
    v.shading = (SceneRenderer::Shading)s.shading;
    v.xray = s.xray;
    if (!camera) return;
    v.setOrthographic(s.ortho);
    Camera& c = v.camera();
    c.yaw = s.yaw;
    c.pitch = s.pitch;
    c.distance = std::clamp(s.distance, 0.5f, 5000.0f);
    c.pivot = glm::vec3(s.px, s.py, s.pz);
}
} // namespace

void Editor::registerWorkspaceSettings() {
    g_writeWorkspace = [this] {
        std::string s = viewLine(*m_viewport);
        for (auto& v : m_moreViews) s += viewLine(*v);
        s += "Panels=";   // which panels are open, so a closed one stays closed next time
        for (int i = 0; i < kPanelCount; ++i) s += m_showPanel[i] ? "1 " : "0 ";
        s += "\n";
        return s;
    };
    ImGuiSettingsHandler h;
    h.TypeName = "GutsWorkspace";
    h.TypeHash = ImHashStr("GutsWorkspace");
    h.ReadOpenFn = wsReadOpen;
    h.ReadLineFn = wsReadLine;
    h.WriteAllFn = wsWriteAll;
    ImGui::AddSettingsHandler(&h);
}

ViewportPanel* Editor::activeView() {
    ViewportPanel* a = ViewportPanel::active();
    if (a == m_viewport.get()) return a;
    for (auto& v : m_moreViews) if (v.get() == a) return a;
    return m_viewport.get();
}

ViewportPanel* Editor::addViewport() {
    int number = 2;   // the lowest number not in use
    for (bool taken = true; taken; ) {
        taken = false;
        for (auto& v : m_moreViews) if (v->number() == number) { taken = true; ++number; break; }
    }
    auto view = std::make_unique<ViewportPanel>(m_window, m_scene, &m_state, number);
    view->camera() = m_viewport->camera();   // starts looking where the main one does
    view->setOrthographic(false);
    view->onMode = [this](StudioMode mode) { setMode(mode); };
    view->contextMenu = m_viewport->contextMenu;
    view->onConnect = m_viewport->onConnect;
    view->setTeam(m_team.get());
    ViewportPanel* made = view.get();
    m_moreViews.push_back(std::move(view));
    // A new one: a tab next to the main Viewport (drag its tab to put it anywhere).
    if (ImGuiWindow* main = ImGui::FindWindowByName("Viewport"); main && main->DockId)
        m_dockNewView.push_back({made->number(), main->DockId});
    return made;
}

void Editor::renderMoreViews(float dt) {
    for (auto& v : m_moreViews) {
        v->setWatching(m_playing);
        for (const auto& [number, dock] : m_dockNewView)
            if (number == v->number()) ImGui::SetNextWindowDockID(dock, ImGuiCond_Always);
        v->render(dt);
    }
    m_dockNewView.clear();
    // Closed with its X.
    const size_t before = m_moreViews.size();
    m_moreViews.erase(std::remove_if(m_moreViews.begin(), m_moreViews.end(), [](auto& v) { return !v->isOpen(); }),
                      m_moreViews.end());
    if (m_moreViews.size() != before) ImGui::MarkIniSettingsDirty();
    // The layout file is saved when something changed (a view style, a camera after a while).
    std::string now = viewLine(*m_viewport);
    for (auto& v : m_moreViews) now += viewLine(*v);
    for (int i = 0; i < kPanelCount; ++i) now += m_showPanel[i] ? '1' : '0';
    if (now != m_workspaceSaved) {
        m_workspaceSaved = now;
        ImGui::MarkIniSettingsDirty();
    }
}

// Blender's Quad View: the main Viewport plus three flat views (top, front, right), 2 by 2.
void Editor::quadView() {
    while (m_moreViews.size() < 3) addViewport();
    static const int kSides[3] = {4, 0, 2};   // top, front, right
    for (int i = 0; i < 3; ++i) {
        m_moreViews[(size_t)i]->lookFrom(kSides[i]);
        m_moreViews[(size_t)i]->camera().pivot = m_viewport->camera().pivot;
        m_moreViews[(size_t)i]->camera().distance = m_viewport->camera().distance;
        m_moreViews[(size_t)i]->shading = SceneRenderer::Shading::Solid;
    }
    m_dockNewView.clear();
    m_wantQuadView = true;   // (docked in buildDockspace, before the windows are drawn)
}

void Editor::resetWorkspace() {
    m_moreViews.clear();
    m_dockNewView.clear();
    applyLayoutPanels();   // (Simple layout: just the main ones. EditorSimple.cpp)
    m_viewport->shading = SceneRenderer::Shading::Material;
    m_viewport->xray = false;
    m_state.orthographic = false;
    m_firstLayout = true;   // buildDockspace puts every panel back where it starts
    ImGui::MarkIniSettingsDirty();
    Log::info("Workspace reset: every panel is back where it starts.");
}

// Called from buildDockspace once the dock space exists: the saved views come back,
// and Quad View splits the main Viewport's spot in 4.
void Editor::applyWorkspaceRequests(unsigned dockspaceId) {
    if (!m_workspaceLoaded) {
        m_workspaceLoaded = true;
        if (g_saved.seen) {
            m_firstLayout = false;   // keep the panels where they were left
            for (const SavedView& s : g_saved.views) {
                if (s.number == 1) { applySaved(*m_viewport, s, false); continue; }
                ViewportPanel* v = addViewport();
                applySaved(*v, s, true);
            }
            m_dockNewView.clear();   // (they go back to their own spots, from the layout file)
            // The panels that were open. A layout file from before panels were kept:
            // the Simple layout closes the extra ones once (they stay where they were docked).
            if (g_saved.panels.empty()) applyLayoutPanels();
            else
                for (int i = 0; i < kPanelCount && i < (int)g_saved.panels.size(); ++i)
                    if (i != kPanelAnimation) m_showPanel[i] = g_saved.panels[(size_t)i] != 0;
        } else {
            applyLayoutPanels();   // a new computer: this layout's panels
        }
    }
    if (!m_wantQuadView) return;
    m_wantQuadView = false;
    ImGuiWindow* main = ImGui::FindWindowByName("Viewport");
    ImGuiID node = main && main->DockId ? main->DockId : 0;
    if (!node || m_moreViews.size() < 3) return;
    // Out of their old spots first (a view already docked inside the area would go too).
    for (size_t i = 0; i < 3; ++i) ImGui::DockBuilderDockWindow(m_moreViews[i]->title(), 0);
    ImGuiID left = 0, right = 0, topLeft = 0, bottomLeft = 0, topRight = 0, bottomRight = 0;
    ImGui::DockBuilderSplitNode(node, ImGuiDir_Right, 0.5f, &right, &left);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.5f, &bottomLeft, &topLeft);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.5f, &bottomRight, &topRight);
    ImGui::DockBuilderDockWindow(m_moreViews[0]->title(), topRight);
    ImGui::DockBuilderDockWindow(m_moreViews[1]->title(), bottomLeft);
    ImGui::DockBuilderDockWindow(m_moreViews[2]->title(), bottomRight);
    ImGui::DockBuilderFinish(dockspaceId);
}

void Editor::renderWorkspaceMenu() {
    if (!ImGui::BeginMenu(I18n::tr("Workspace"))) return;
    if (ImGui::MenuItem(I18n::tr("Add Viewport"))) addViewport();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", I18n::tr("Another 3D view with its own camera. Drag its tab anywhere."));
    if (ImGui::MenuItem(I18n::tr("Four Views (top, front, right)"), "Ctrl+Alt+Q")) quadView();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", I18n::tr("Like Blender's Quad View: your view plus flat views from the top, front and right"));
    if (ImGui::MenuItem(I18n::tr("Close Extra Viewports"), nullptr, false, !m_moreViews.empty())) m_moreViews.clear();
    ImGui::Separator();
    ImGui::TextDisabled("%s", I18n::tr("Drag any panel's tab to move it, or onto another to stack them."));
    if (ImGui::MenuItem(I18n::tr("Reset Workspace"))) resetWorkspace();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", I18n::tr("Put every panel back where it started and close the extra views"));
    ImGui::EndMenu();
}

// The view keys, in the 3D view the mouse is in (like Blender): Z the view style menu,
// Shift+Z Wireframe on / off, Alt+Z X-ray. True if one was pressed.
bool Editor::handleViewKeys() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || io.KeyCtrl || !ImGui::IsKeyPressed(ImGuiKey_Z, false)) return false;
    ViewportPanel* v = activeView();
    if (io.KeyAlt) {
        if (m_state.mode == StudioMode::Modeling) return false;   // (Modeling has its own X-ray on Alt+Z)
        v->xray = !v->xray;
    } else if (io.KeyShift) {
        v->shading = v->shading == SceneRenderer::Shading::Wireframe ? SceneRenderer::Shading::Material : SceneRenderer::Shading::Wireframe;
    } else {
        if (m_state.mode == StudioMode::Modeling) return false;
        v->openShadingMenu();
    }
    return true;
}

// Tests: --test-mesh "workspace:<step>" runs one of these.
void Editor::testWorkspace(const std::string& step) {
    if (step == "add") addViewport();
    else if (step == "quad") quadView();
    else if (step == "reset") resetWorkspace();
    else if (step == "xray") activeView()->xray = !activeView()->xray;
    else if (step == "wire") activeView()->shading = SceneRenderer::Shading::Wireframe;
    else if (step == "solid") activeView()->shading = SceneRenderer::Shading::Solid;
    else if (step == "flat") activeView()->shading = SceneRenderer::Shading::Flat;
    else if (step == "material") activeView()->shading = SceneRenderer::Shading::Material;
    else if (step == "report") {
        std::ostringstream o;
        o << "workspace: " << (1 + m_moreViews.size()) << " views; main shading " << (int)m_viewport->shading
          << " xray " << (m_viewport->xray ? 1 : 0);
        for (auto& v : m_moreViews)
            o << "; " << v->title() << " shading " << (int)v->shading << (v->orthographic() ? " ortho" : "")
              << " yaw " << (int)v->camera().yaw << " pitch " << (int)v->camera().pitch;
        Log::info(o.str());
    }
}
