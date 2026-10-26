// Studio's ribbon (HOME / MODEL / TEST / VIEW), laid out like Roblox Studio's.
#include "Editor.h"
#include "../online/OnlineClient.h"
#include "panels/ScriptEditorPanel.h"
#include "Icons.h"
#include "Theme.h"
#include "panels/ViewportPanel.h"
#include "../scene/Scene.h"
#include "../scene/Physics.h"
#include "../scene/EditMesh.h"
#include "Plugins.h"
#include "MultiTest.h"
#include "../core/Log.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>

namespace {

const ImU32 kRibbonBg  = IM_COL32(46, 46, 46, 255);
const ImU32 kTabsBg    = IM_COL32(37, 37, 37, 255);
const ImU32 kHover     = IM_COL32(70, 70, 70, 255);
const ImU32 kActive    = IM_COL32(11, 90, 175, 255);
const ImU32 kGroupText = IM_COL32(150, 150, 150, 255);

// A big ribbon button: icon on top, label underneath.
bool bigButton(const char* label, Icons::Id icon, bool active = false, bool enabled = true, const char* tip = nullptr) {
    ImGui::PushID(label);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float w = std::max(52.0f, textSize.x + 14.0f), h = 62.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    bool clicked = ImGui::InvisibleButton("##b", ImVec2(w, h));
    ImGui::EndDisabled();
    bool hover = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kActive, 3.0f);
    else if (hover && enabled) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kHover, 3.0f);
    Icons::draw(dl, ImVec2(p.x + w * 0.5f, p.y + 20), 28.0f, icon,
                enabled ? IM_COL32(225, 225, 225, 255) : IM_COL32(110, 110, 110, 255));
    dl->AddText(ImVec2(p.x + (w - textSize.x) * 0.5f, p.y + h - textSize.y - 5),
                enabled ? IM_COL32(225, 225, 225, 255) : IM_COL32(110, 110, 110, 255), label);
    if (hover && tip) ImGui::SetTooltip("%s", tip);
    ImGui::PopID();
    ImGui::SameLine(0, 2);
    return clicked && enabled;
}

// A small ribbon button (icon + text), stacked three high.
bool smallButton(const char* label, Icons::Id icon, bool active = false, bool enabled = true, const char* tip = nullptr) {
    ImGui::PushID(label);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float w = textSize.x + 30.0f, h = 19.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    bool clicked = ImGui::InvisibleButton("##s", ImVec2(w, h));
    ImGui::EndDisabled();
    bool hover = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kActive, 2.0f);
    else if (hover && enabled) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kHover, 2.0f);
    ImU32 col = enabled ? IM_COL32(225, 225, 225, 255) : IM_COL32(110, 110, 110, 255);
    Icons::draw(dl, ImVec2(p.x + 11, p.y + h * 0.5f), 15.0f, icon, col);
    dl->AddText(ImVec2(p.x + 24, p.y + (h - textSize.y) * 0.5f), col, label);
    if (hover && tip) ImGui::SetTooltip("%s", tip);
    ImGui::PopID();
    return clicked && enabled;
}

// Groups: a column of buttons with a caption under it and a divider after.
struct Group {
    ImVec2 start;
    float  top;
    const char* name;
    Group(const char* n) : name(n) {
        start = ImGui::GetCursorScreenPos();
        top = start.y;
        ImGui::BeginGroup();
    }
    ~Group() {
        ImGui::EndGroup();
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 ts = ImGui::CalcTextSize(name);
        float capY = top + 66;
        float width = std::max(mx.x - mn.x, ts.x);
        dl->AddText(ImVec2(mn.x + (width - ts.x) * 0.5f, capY), kGroupText, name);
        ImGui::SameLine(0, 0);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float x = std::max(p.x, mn.x + width) + 8;
        dl->AddLine(ImVec2(x, top + 4), ImVec2(x, top + 80), IM_COL32(70, 70, 70, 255));
        ImGui::SetCursorScreenPos(ImVec2(x + 9, top));
    }
};

// Three small buttons stacked in a column.
struct Stack {
    Stack() { ImGui::BeginGroup(); ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2)); }
    ~Stack() { ImGui::PopStyleVar(); ImGui::EndGroup(); ImGui::SameLine(0, 4); }
};

} // namespace

// Line the selection up along one axis (0 = X, 1 = Y, 2 = Z): where 0 = low
// side, 1 = centre, 2 = high side of the whole selection's box.
void Editor::alignSelected(int axis, int where) {
    auto items = m_scene->selectionRoots();
    if (items.size() < 2) { Log::warn("Align: select two or more things first."); return; }
    auto bounds = [&](SceneNode* n, AABB& out) {
        bool any = false;
        std::vector<SceneNode*> stack{n};
        while (!stack.empty()) {
            SceneNode* k = stack.back(); stack.pop_back();
            if (k->isPart()) {
                AABB b = Physics::worldBounds(k);
                if (!any) out = b; else { out.min = glm::min(out.min, b.min); out.max = glm::max(out.max, b.max); }
                any = true;
            }
            for (auto& c : k->children) stack.push_back(c.get());
        }
        return any;
    };
    AABB all{};
    bool first = true;
    for (SceneNode* n : items) {
        AABB b;
        if (!bounds(n, b)) continue;
        if (first) { all = b; first = false; }
        else { all.min = glm::min(all.min, b.min); all.max = glm::max(all.max, b.max); }
    }
    if (first) return;
    float target = where == 0 ? all.min[axis] : where == 2 ? all.max[axis] : (all.min[axis] + all.max[axis]) * 0.5f;
    for (SceneNode* n : items) {
        AABB b;
        if (!canEdit(n) || !bounds(n, b)) continue;
        float cur = where == 0 ? b.min[axis] : where == 2 ? b.max[axis] : (b.min[axis] + b.max[axis]) * 0.5f;
        glm::vec3 d(0.0f);
        d[axis] = target - cur;
        if (n->parent) d = glm::vec3(glm::inverse(n->parentFrame()) * glm::vec4(d, 0.0f));
        n->transform.position += d;
    }
}

void Editor::renderToolbar() {
    ImDrawList* bg = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;

    // --- Tab row ---
    const float tabsH = 26.0f, ribbonH = 88.0f;
    bg->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + tabsH), kTabsBg);
    bg->AddRectFilled(ImVec2(origin.x, origin.y + tabsH), ImVec2(origin.x + width, origin.y + tabsH + ribbonH), kRibbonBg);
    bg->AddLine(ImVec2(origin.x, origin.y + tabsH + ribbonH - 1), ImVec2(origin.x + width, origin.y + tabsH + ribbonH - 1),
                IM_COL32(26, 26, 26, 255));
    const char* tabs[] = {"HOME", "MODEL", "TEST", "VIEW", "PLUGINS", "MESH", "AVATAR", "TERRAIN"};
    const bool modeling = m_state.mode == StudioMode::Modeling;
    // Shown in Roblox Studio's order; MESH only shows up in Modeling mode.
    const int order[] = {0, 1, 7, 6, 2, 3, 4, 5};
    int tabCount = modeling ? 8 : 7;
    float x = origin.x + 10;
    for (int oi = 0; oi < tabCount; ++oi) {
        const int i = order[oi];
        ImVec2 ts = ImGui::CalcTextSize(tabs[i]);
        ImVec2 a(x, origin.y), b(x + ts.x + 24, origin.y + tabsH);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tab", ImVec2(b.x - a.x, tabsH))) m_ribbonTab = i;
        ImGui::PopID();
        bool on = m_ribbonTab == i;
        if (on) {
            bg->AddRectFilled(a, b, kRibbonBg);
            bg->AddLine(ImVec2(a.x, a.y + 1), ImVec2(b.x, a.y + 1), EditorTheme::kRobloxBlue, 2.0f);
        } else if (ImGui::IsItemHovered()) {
            bg->AddRectFilled(a, b, IM_COL32(55, 55, 55, 255));
        }
        bg->AddText(ImVec2(a.x + 12, a.y + (tabsH - ts.y) * 0.5f),
                    i == 5 ? IM_COL32(255, 170, 70, 255) : i == 7 && m_state.terrainBrush >= 0 ? IM_COL32(140, 230, 120, 255) : on ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 255), tabs[i]);
        x = b.x + 2;
    }

    // Quick buttons on the right of the tab row (like Roblox's undo / redo).
    {
        float qx = origin.x + width - 70;
        ImGui::SetCursorScreenPos(ImVec2(qx, origin.y + 3));
        ImGui::PushID("quick");
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(m_undo.empty() || m_playing);
        if (ImGui::InvisibleButton("##undo", ImVec2(28, 20))) undo();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Undo (Ctrl+Z)");
        ImGui::EndDisabled();
        Icons::draw(bg, ImVec2(p.x + 14, p.y + 10), 16, Icons::Id::Undo);
        ImGui::SameLine(0, 4);
        p = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(m_redo.empty() || m_playing);
        if (ImGui::InvisibleButton("##redo", ImVec2(28, 20))) redo();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Redo (Ctrl+Y)");
        ImGui::EndDisabled();
        Icons::draw(bg, ImVec2(p.x + 14, p.y + 10), 16, Icons::Id::Redo);
        ImGui::PopID();
    }

    // --- Ribbon contents ---
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10, origin.y + tabsH + 3));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
    SceneNode* sel = m_scene->selected();
    bool editable = canEdit(sel) && !m_playing;
    auto tool = [&](const char* label, Icons::Id icon, GizmoTool t, const char* tip) {
        bool usable = !m_playing || m_state.mode == StudioMode::Simulate;   // Simulate: drag things live
        if (bigButton(label, icon, m_state.tool == t, usable, tip)) m_state.tool = t;
    };
    auto tools = [&] {
        Group g("Tools");
        tool("Select", Icons::Id::Select, GizmoTool::Select, "Select (Ctrl+1)");
        tool("Move", Icons::Id::Move, GizmoTool::Translate, "Move (Ctrl+2)");
        tool("Scale", Icons::Id::Scale, GizmoTool::Scale, "Scale (Ctrl+3)");
        tool("Rotate", Icons::Id::Rotate, GizmoTool::Rotate, "Rotate (Ctrl+4)");
    };
    auto test = [&](bool pauseButtons, bool stepButton) {
        Group g("Test");
        if (bigButton("Play", Icons::Id::Play, m_playing && m_playMode == 0, !m_playing, "Play mode: playtest with your character (F5)"))
            startPlay(0);
        if (bigButton("Play Here", Icons::Id::PlayHere, m_playing && m_playMode == 1, !m_playing,
                      "Play, starting where the camera is looking")) startPlay(1);
        if (bigButton("Simulate", Icons::Id::Simulate, m_playing && m_playMode == 2, !m_playing,
                      "Simulate mode: physics and scripts run live, no character.\n"
                      "Fly around, click things to inspect them and drag them with the gizmo (F8)")) startPlay(2);
        if (pauseButtons && bigButton(m_state.simPaused ? "Resume" : "Pause", m_state.simPaused ? Icons::Id::Play : Icons::Id::Pause,
                      m_state.simPaused, m_playing, "Freeze / unfreeze the world (F6)"))
            m_state.simPaused = !m_state.simPaused;
        if (stepButton && bigButton("Step", Icons::Id::Step, false, m_playing && m_state.simPaused, "Move on one frame while paused (F7)"))
            m_state.simStep = true;
        if (bigButton("Stop", Icons::Id::Stop, false, m_playing, "Stop and go back to Build mode (Shift+F5)")) togglePlay();
    };

    switch (m_ribbonTab) {
    case 0: {   // HOME
        {
            Group g("Clipboard");
            if (bigButton("Paste", Icons::Id::Paste, false, !m_clipboard.empty() && !m_playing, "Paste (Ctrl+V)")) paste();
            Stack st;
            if (smallButton("Copy", Icons::Id::Copy, false, editable, "Copy (Ctrl+C)")) copySelected();
            if (smallButton("Cut", Icons::Id::Cut, false, editable, "Cut (Ctrl+X)")) cutSelected();
            if (smallButton("Duplicate", Icons::Id::Duplicate, false, editable, "Duplicate (Ctrl+D)")) duplicateSelected();
        }
        tools();
        {
            Group g("Insert");
            if (bigButton("Part", Icons::Id::Part, false, !m_playing, "Insert a block (Ctrl+Shift+P for more shapes in the MODEL tab)"))
                spawnPrimitive(PrimitiveType::Cube);
            if (bigButton("Object", Icons::Id::Insert, false, !m_playing, "Insert Object... (Ctrl+I)")) m_openInsert = true;
            if (bigButton("Import", Icons::Id::Import, false, !m_playing,
                          "Import a 3D model (.fbx .obj .gltf .glb .stl .ply), picture, sound or script.\n"
                          "Or just drag files from your computer onto Studio.\n(Roblox files: File > Import Roblox File)"))
                importDialog();
        }
        {
            Group g("Edit");
            {
                Stack st;
                if (smallButton("Group", Icons::Id::Group, false, editable, "Group as Model (Ctrl+G)")) groupSelected();
                if (smallButton("Ungroup", Icons::Id::Ungroup, false, editable && sel->kind == NodeKind::Model, "Ungroup (Ctrl+U)"))
                    ungroupSelected();
                if (smallButton("Lock", Icons::Id::Lock, sel && sel->locked, editable, "Lock: can't be clicked in the Viewport (Alt+L)"))
                    toggleLocked();
                if (smallButton("Find", Icons::Id::Select, m_showBulkEdit, true,
                                "Find and Change: find objects by kind, tag or value and change them all at once (Ctrl+Shift+E)"))
                    m_showBulkEdit = !m_showBulkEdit;
            }
            {
                Stack st;
                if (smallButton("Anchor", Icons::Id::Anchor, sel && sel->anchored && sel->isPart(), editable, "Anchor: stays put (Alt+A)"))
                    toggleAnchored();
                if (smallButton("Snap", Icons::Id::Snap, m_state.snapEnabled, true, "Move in steps of the grid (studs: Increments)"))
                    m_state.snapEnabled = !m_state.snapEnabled;
                if (smallButton(m_state.gizmoLocal ? "Local" : "World", Icons::Id::Transform, false, true, "Local / world axes (Ctrl+L)"))
                    m_state.gizmoLocal = !m_state.gizmoLocal;
            }
            {
                Stack st;
                if (smallButton("Collide", Icons::Id::Collide, m_state.collisions, true,
                                "Collisions: moved parts stop flush against others instead of going through"))
                    m_state.collisions = !m_state.collisions;
                if (smallButton("Rot snap", Icons::Id::Rotate, m_state.rotSnapEnabled, true,
                                "Turn in steps (degrees: Increments)"))
                    m_state.rotSnapEnabled = !m_state.rotSnapEnabled;
                if (smallButton("Grid", Icons::Id::Snap, m_state.showGrid, true, "Show the floor grid"))
                    m_state.showGrid = !m_state.showGrid;
                if (smallButton("Ortho", Icons::Id::Plane, m_state.orthographic, true,
                                "Orthographic view: no perspective, far things aren't smaller (numpad 5)"))
                    m_state.orthographic = !m_state.orthographic;
            }
        }
        {
            // Like modern Roblox Studio: the step sizes right on the Home tab. Ticked =
            // moves / sizes / turns in steps; unticked = free (Blender style). While you
            // drag, how far shows next to the mouse.
            Group g("Increments");
            ImGui::BeginGroup();
            ImGui::Checkbox("##rotinc", &m_state.rotSnapEnabled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate in steps (unticked: turn freely)");
            ImGui::SameLine(0, 2);
            ImGui::TextUnformatted("Rotate");
            ImGui::SameLine(62);
            ImGui::SetNextItemWidth(58);
            ImGui::BeginDisabled(!m_state.rotSnapEnabled);
            ImGui::DragFloat("##rotdeg", &m_state.snapRotate, 1.0f, 1.0f, 180.0f, "%.0f\xC2\xB0");
            ImGui::EndDisabled();
            ImGui::Checkbox("##moveinc", &m_state.snapEnabled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move and resize in steps of studs (unticked: free)");
            ImGui::SameLine(0, 2);
            ImGui::TextUnformatted("Move");
            ImGui::SameLine(62);
            ImGui::SetNextItemWidth(58);
            ImGui::BeginDisabled(!m_state.snapEnabled);
            ImGui::DragFloat("##movestuds", &m_state.snapTranslate, 0.05f, 0.05f, 64.0f, "%.2f st");
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Studs per step (moving, and growing / shrinking with Scale)");
            ImGui::EndGroup();
            ImGui::SameLine(0, 4);
        }
        test(m_playing, false);   // Pause only while testing (Pause and Step are always on the TEST tab)
        {
            Group g("Settings");
            if (bigButton("Game Settings", Icons::Id::Settings, false, !m_playing, "Title, description and more")) m_openInfo = true;
            if (bigButton("Team Create", Icons::Id::Team, false, true, "Edit together with friends")) m_openTeam = true;
            if (bigButton("Assistant", Icons::Id::CommandBar, m_showPanel[kPanelAssistant], true,
                          "AI help: chat with Claude, or let AI apps (MCP) build with you"))
                m_showPanel[kPanelAssistant] = true;
        }
        break;
    }
    case 1: {   // MODEL
        tools();
        {
            Group g("Snap to Grid");
            ImGui::BeginGroup();
            ImGui::Checkbox("Move##snapmv", &m_state.snapEnabled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move (and resize) in steps of this many studs; the grid follows");
            ImGui::SameLine(80);
            ImGui::SetNextItemWidth(60);
            ImGui::DragFloat("studs##mv", &m_state.snapTranslate, 0.05f, 0.05f, 64.0f, "%.2f");
            ImGui::Checkbox("Rotate##snaprot", &m_state.rotSnapEnabled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn in steps of this many degrees");
            ImGui::SameLine(80);
            ImGui::SetNextItemWidth(60);
            ImGui::DragFloat("deg##rot", &m_state.snapRotate, 1.0f, 1.0f, 180.0f, "%.0f");
            ImGui::Checkbox("Collisions", &m_state.collisions);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Moved parts stop flush against other parts instead of going through them");
            ImGui::SameLine();
            ImGui::Checkbox("Grid", &m_state.showGrid);
            ImGui::SameLine();
            ImGui::Checkbox("Ortho", &m_state.orthographic);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Orthographic view: no perspective, far things aren't smaller (numpad 5)");
            ImGui::EndGroup();
            ImGui::SameLine(0, 4);
        }
        {
            Group g("Parts");
            if (bigButton("Block", Icons::Id::Part, false, !m_playing)) spawnPrimitive(PrimitiveType::Cube);
            if (bigButton("Sphere", Icons::Id::Sphere, false, !m_playing)) spawnPrimitive(PrimitiveType::Sphere);
            if (bigButton("Cylinder", Icons::Id::Cylinder, false, !m_playing)) spawnPrimitive(PrimitiveType::Cylinder);
            if (bigButton("Plane", Icons::Id::Plane, false, !m_playing)) spawnPrimitive(PrimitiveType::Plane);
        }
        {
            Group g("Mesh");
            if (bigButton("MeshPart", Icons::Id::Mesh, false, !m_playing, "A new part you shape yourself (starts as a cube)"))
                addMeshPart();
            if (bigButton("Edit Mesh", Icons::Id::Extrude, false, editable && sel->isPart() && !m_scene->isCharacterPart(sel),
                          "Modeling mode: reshape the selected part's corners, edges and faces (Tab)"))
                setMode(StudioMode::Modeling);
        }
        {
            Group g("Solid Modeling");   // like Roblox's: join parts, cut holes, keep the overlap
            const bool parts = !m_playing && m_scene->selectionRoots().size() >= 2;
            if (bigButton("Union", Icons::Id::Union, false, parts, "Join the selected parts into one (Ctrl+Shift+G). "
                          "Negated parts in the selection get cut out."))
                unionSelected(0);
            {
                Stack st;
                if (smallButton("Negate", Icons::Id::Negate, sel && sel->negated, editable && sel->isPart(),
                                "Turn the part into a hole that Union cuts out (Ctrl+Shift+N)"))
                    negateSelected();
                if (smallButton("Intersect", Icons::Id::Intersect, false, parts, "Keep only where the parts overlap (Ctrl+Shift+I)"))
                    unionSelected(1);
                if (smallButton("Separate", Icons::Id::Separate, false, editable && !sel->unionSource.empty(),
                                "Break a union back into its parts (Ctrl+Shift+U)"))
                    separateSelected();
            }
        }
        {
            Group g("Navigation");
            if (bigButton("Navmesh", Icons::Id::NavMesh, m_state.showNavMesh, true,
                          "Show the navigation mesh: the blue floor is where characters can walk; yellow arcs are "
                          "jumps, orange lines are drops. PathfindingService uses it."))
                m_state.showNavMesh = !m_state.showNavMesh;
            std::string tip = "Bake the navigation mesh again now (it also rebakes by itself when parts change)";
            if (!m_state.navInfo.empty()) tip += "\n" + m_state.navInfo;
            if (bigButton("Bake", Icons::Id::Bake, false, true, tip.c_str())) {
                m_state.bakeNavMesh = 1;
                m_state.showNavMesh = true;
            }
        }
        {
            Group g("Constraints");
            const char* names[] = {"Rope", "Rod", "Spring", "Weld", "Hinge", "Motor", "Ball Socket", "Prismatic", "Cylindrical"};
            for (int col = 0; col < 3; ++col) {
                Stack st;
                for (int r = 0; r < 3; ++r) {
                    int t = col * 3 + r;
                    if (smallButton(names[t], Icons::Id::Constraint, m_state.connectTool == t, !m_playing,
                                    "Click this, then click two parts to join them")) {
                        m_state.connectTool = t;
                        m_state.connectFirst = 0;
                    }
                }
            }
        }
        {
            Group g("Advanced");
            {
                Stack st;
                if (smallButton("Script", Icons::Id::Script, false, !m_playing, "Add a Script (Ctrl+I for more)")) addScript(sel);
                if (smallButton("Model", Icons::Id::Model, false, !m_playing)) addModel();
                if (smallButton("Light", Icons::Id::Light, false, !m_playing)) addLight(LightType::Point);
            }
            {
                Stack st;
                if (smallButton("Sound", Icons::Id::Sound, false, !m_playing)) addSound();
                if (smallButton("Pivot to middle", Icons::Id::Transform, false, editable && sel->kind == NodeKind::Model,
                                "Move this Model's origin to the middle of its parts"))
                    centerModelPivot();
                if (smallButton("Export", Icons::Id::Export, false, !m_playing, "Export to Roblox (.rbxlx)")) exportRoblox(false);
            }
        }
        {
            Group g("Physics");
            if (bigButton("Collision Groups", Icons::Id::Collide, m_showCollisionGroups, true,
                          "Which groups of parts bump into each other (players walking through each other, team doors...)"))
                m_showCollisionGroups = !m_showCollisionGroups;
            if (bigButton("Physics View", Icons::Id::Simulate, m_state.showPhysics, true,
                          "See the physics: moving and sleeping parts, speeds, where things touch, how hard each joint is pulled, force zones"))
                m_state.showPhysics = !m_state.showPhysics;
        }
        {
            Group g("Align");
            ImGui::BeginGroup();
            static int axis = 0;
            ImGui::RadioButton("X", &axis, 0); ImGui::SameLine(); ImGui::RadioButton("Y", &axis, 1); ImGui::SameLine();
            ImGui::RadioButton("Z", &axis, 2);
            bool can = m_scene->selectionRoots().size() >= 2 && !m_playing;
            ImGui::BeginDisabled(!can);
            if (ImGui::Button("Min")) alignSelected(axis, 0);
            ImGui::SameLine();
            if (ImGui::Button("Center")) alignSelected(axis, 1);
            ImGui::SameLine();
            if (ImGui::Button("Max")) alignSelected(axis, 2);
            ImGui::EndDisabled();
            ImGui::EndGroup();
            ImGui::SameLine(0, 4);
        }
        break;
    }
    case 2: {   // TEST
        test(true, true);
        {
            // Test with 2-4 players: a local test server and that many Player windows on this computer.
            Group g("Multiplayer");
            MultiTest& mt = multiTest();
            const bool on = mt.running();
            if (bigButton("Players", Icons::Id::Team, on, true,
                          on ? "A multiplayer test is running: add a player or stop it"
                             : "Test with more than one player: a local test server and a Player window for each,\n"
                               "joined as Player1, Player2... Output shows what the server and each player print."))
                ImGui::OpenPopup("##multitest");
            if (bigButton("End Test", Icons::Id::Stop, false, on, "Stop the multiplayer test (closes the server and every player window)"))
                stopMultiTest();
            if (ImGui::BeginPopup("##multitest")) {
                std::string err;
                for (int n = MultiTest::kMinPlayers; n <= MultiTest::kMaxPlayers; ++n) {
                    const std::string label = "Test with " + std::to_string(n) + " players";
                    if (ImGui::MenuItem(label.c_str(), nullptr, false, !on)) startMultiTest(n, err);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Add a player", nullptr, false, on && mt.playerCount() < MultiTest::kMostPlayers) &&
                    !mt.addPlayer(err))
                    Log::warn(err);
                if (ImGui::MenuItem("Stop multiplayer test", nullptr, false, on)) stopMultiTest();
                ImGui::EndPopup();
            }
        }
        {
            Group g("Settings");
            if (bigButton("Player", Icons::Id::Player, false, true, "Walk speed, jump, death and gore")) m_showPanel[kPanelPlayer] = true;
            if (bigButton("Lighting", Icons::Id::Lighting, false, true)) m_showPanel[kPanelLighting] = true;
        }
        break;
    }
    case 6: {   // AVATAR
        {
            Group g("Rig");
            if (bigButton("Rig Builder", Icons::Id::Rig, false, !m_playing, "Insert a dummy character to animate (or use as an NPC)"))
                insertObject("Rig", nullptr);
            if (bigButton("Armature", Icons::Id::Rig, m_showArmature, !m_playing,
                          "Bones inside a MeshPart that bend it, posed and animated like a rig's parts"))
                m_showArmature = !m_showArmature;
        }
        {
            Group g("Catalog");
            if (bigButton("Accessories", Icons::Id::Rig, m_showAccessory, !m_playing,
                          "Verified creators: put a hat or accessory on a mannequin, save where it sits, and upload it"))
                m_showAccessory = !m_showAccessory;
            if (Online::staff() &&
                bigButton("Bundles", Icons::Id::Rig, m_showBundles, !m_playing,
                          "Staff: give the mannequin a new body (and items) and upload it as a bundle"))
                m_showBundles = !m_showBundles;
        }
        {
            Group g("Animation");
            if (bigButton("Animation Editor", Icons::Id::Animation, m_showPanel[kPanelAnimation], !m_playing,
                          "Make animations for a rig: pose its parts on a timeline"))
                openAnimationEditor();
            if (bigButton("Animation", Icons::Id::Animation, false, !m_playing && sel,
                          "Add an Animation object to the selected rig (then open it in the Animation Editor)"))
                insertObject("Animation", sel);
        }
        tools();
        break;
    }
    case 4:     // PLUGINS
        renderPluginsTab();
        break;
    case 7: {   // TERRAIN
        Terrain& terrain = m_scene->terrain();
        const bool can = !m_playing;
        {
            Group g("Create");
            static const int kSizes[] = {256, 512, 1024};
            const int size = kSizes[std::clamp(m_state.terrainGenSize, 0, 2)];
            if (bigButton("Generate", Icons::Id::Terrain, false, can,
                          "Make new hills (replaces the terrain you have). Each click makes different ones.")) {
                static uint32_t seed = 1;
                seed = seed * 1664525u + 1013904223u + (uint32_t)(ImGui::GetTime() * 1000.0);
                terrain.generate(size / (int)Terrain::kDefaultCell, Terrain::kDefaultCell, seed, m_state.terrainHills);
            }
            if (bigButton("Clear", Icons::Id::TerrainClear, false, can && !terrain.empty(), "Remove all the terrain")) {
                terrain.clear();
                m_state.terrainBrush = -1;
            }
            Stack st;
            ImGui::SetNextItemWidth(110);
            ImGui::SliderFloat("##hills", &m_state.terrainHills, 0.0f, 1.0f, "Hills %.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How bumpy Generate makes it (0 = gentle, 1 = mountains)");
            ImGui::SetNextItemWidth(110);
            static const char* kSizeNames[] = {"256 studs", "512 studs", "1024 studs"};
            ImGui::Combo("##tsize", &m_state.terrainGenSize, kSizeNames, 3);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How big Generate makes it (across)");
        }
        {
            Group g("Sculpt");
            struct B { const char* label; Icons::Id icon; const char* tip; };
            const B brushes[] = {{"Raise", Icons::Id::TerrainRaise, "Drag to pull the ground up"},
                                 {"Lower", Icons::Id::TerrainLower, "Drag to push the ground down"},
                                 {"Smooth", Icons::Id::TerrainSmooth, "Drag to round off bumps and sharp edges"},
                                 {"Flatten", Icons::Id::TerrainFlatten, "Drag to level the ground to where you started"},
                                 {"Paint", Icons::Id::TerrainPaint, "Drag to change the ground (grass, sand, rock...)"}};
            for (int i = 0; i < 5; ++i)
                if (bigButton(brushes[i].label, brushes[i].icon, m_state.terrainBrush == i, can, brushes[i].tip)) {
                    m_state.terrainBrush = m_state.terrainBrush == i ? -1 : i;
                    m_state.connectTool = -1;
                }
        }
        {
            Group g("Brush");
            Stack st;
            ImGui::SetNextItemWidth(130);
            ImGui::SliderFloat("##bsize", &m_state.terrainSize, 2.0f, 64.0f, "Size %.0f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How big the brush is (studs from the middle to the edge)");
            ImGui::SetNextItemWidth(130);
            ImGui::SliderFloat("##bstr", &m_state.terrainStrength, 0.05f, 1.0f, "Strength %.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How fast it changes the ground");
            ImGui::SetNextItemWidth(130);
            ImGui::Combo("##bmat", &m_state.terrainMaterial, kTerrainMaterialNames, kTerrainMaterialCount);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("What Paint paints");
        }
        break;
    }
    case 5: {   // MESH (Modeling mode)
        ModelingState& ms = m_state.modeling;
        {
            Group g("Pick");
            if (bigButton("Vertex", Icons::Id::Vertex, ms.selectMode == 0, true, "Pick corners (1)")) ms.selectMode = 0;
            if (bigButton("Edge", Icons::Id::Edge, ms.selectMode == 1, true, "Pick edges (2)")) ms.selectMode = 1;
            if (bigButton("Face", Icons::Id::Face, ms.selectMode == 2, true, "Pick faces (3)")) ms.selectMode = 2;
            Stack st;
            if (smallButton("All", Icons::Id::Select, false, true, "Pick everything / nothing (A)")) meshOp(MeshOp::SelectAll);
            if (smallButton("Invert", Icons::Id::Select, false, true, "Swap picked and not picked (Ctrl+I)")) meshOp(MeshOp::Invert);
            if (smallButton("X-Ray", Icons::Id::XRay, ms.xray, true, "See and pick through the mesh (Alt+Z)")) ms.xray = !ms.xray;
        }
        tools();
        {
            Group g("Shape");
            if (bigButton("Extrude", Icons::Id::Extrude, false, true, "Pull the picked faces (or edges) out into new ones (E)"))
                meshOp(MeshOp::Extrude);
            if (bigButton("Inset", Icons::Id::Inset, false, true, "A smaller face inside each picked face (I)")) meshOp(MeshOp::Inset);
            if (bigButton("Subdivide", Icons::Id::Subdivide, false, true, "Cut the picked faces (or all) into smaller ones"))
                meshOp(MeshOp::Subdivide);
            Stack st;
            if (smallButton("Merge", Icons::Id::Merge, false, true, "Squash the picked corners into one (M for more)")) meshOp(MeshOp::Merge);
            if (smallButton("Fill", Icons::Id::Fill, false, true, "Make a face between the picked corners (F)")) meshOp(MeshOp::Fill);
            if (smallButton("Delete", Icons::Id::Delete, false, true, "Delete what's picked (Del; X for the Delete menu)")) meshOp(MeshOp::Delete);
        }
        {
            Group g("Cut");
            Stack st;
            if (smallButton("Loop Cut", Icons::Id::Subdivide, false, true, "Cut a new loop of edges round the mesh: point at an edge, click (Ctrl+R)"))
                meshOp(MeshOp::LoopCut);
            if (smallButton("Bridge", Icons::Id::Edge, false, true, "Join two picked edge loops with faces")) meshOp(MeshOp::Bridge);
            if (smallButton("Smooth Sub.", Icons::Id::Smooth, false, true, "Subdivide smooth: smaller AND rounder faces (Ctrl+1)"))
                meshOp(MeshOp::SubdivideSmooth);
        }
        {
            // Blender's header menus; each opens at the mouse (the same ones the keys open).
            Group g("Menus");
            Stack st;
            if (smallButton("Select", Icons::Id::Select, false, true, "Pick more, less, linked, loops...")) m_meshMenu = "Select";
            if (smallButton("Add", Icons::Id::Part, false, true, "Add a shape into this mesh (Shift+A)")) m_meshMenu = "Add";
            if (smallButton("Mesh", Icons::Id::Mesh, false, true, "Move, mirror, hide, separate... (right-click and F3 have the rest)"))
                m_meshMenu = "Mesh";
        }
        {
            Group g("Surface");
            SceneNode* mn = m_scene->findById(ms.node);
            bool smooth = mn && mn->editMesh && mn->editMesh->smooth;
            if (bigButton(smooth ? "Smooth" : "Flat", Icons::Id::Smooth, smooth, true, "Smooth or flat shading"))
                meshOp(MeshOp::Smooth);
            Stack st;
            if (smallButton("Flip", Icons::Id::Flip, false, true, "Turn the picked faces (or all) inside out")) meshOp(MeshOp::Flip);
            if (smallButton("Outside", Icons::Id::Face, false, true, "Make every face point outwards (Shift+N)")) meshOp(MeshOp::NormalsOut);
            if (smallButton("Proportional", Icons::Id::Snap, ms.proportional, true,
                            "Moving corners drags nearby ones along, fading out (O; mouse wheel while moving = how far)"))
                meshOp(MeshOp::Proportional);
        }
        {
            Group g("UV");
            // One button with a menu, so the tab still fits a small window.
            if (bigButton("UV", Icons::Id::GuiImage, m_uv.show, true, "Lay the mesh out flat for a picture: UV Editor, Unwrap, seams"))
                ImGui::OpenPopup("##uvmenu");
            if (ImGui::BeginPopup("##uvmenu")) {
                if (ImGui::MenuItem("UV Editor", nullptr, m_uv.show)) m_uv.show = !m_uv.show;
                if (ImGui::MenuItem("Unwrap...", "U")) m_uv.openUnwrap = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Mark Seam")) markSeams(true);
                if (ImGui::MenuItem("Clear Seam")) markSeams(false);
                ImGui::EndPopup();
            }
        }
        {
            Group g("Mode");
            if (bigButton("Done", Icons::Id::Done, false, true, "Back to Build mode (Tab)")) setMode(StudioMode::Build);
        }
        break;
    }
    case 3: {   // VIEW
        {
            Group g("Show");
            struct P { const char* label; Icons::Id icon; int panel; };
            const P panels[] = {{"Explorer", Icons::Id::Explorer, kPanelExplorer}, {"Properties", Icons::Id::Properties, kPanelProperties},
                                {"Toolbox", Icons::Id::Toolbox, kPanelToolbox}, {"Output", Icons::Id::Output, kPanelOutput},
                                {"Command Bar", Icons::Id::CommandBar, kPanelCommandBar}, {"Script Editor", Icons::Id::Script, kPanelScript},
                                {"Lighting", Icons::Id::Lighting, kPanelLighting}, {"Player", Icons::Id::Player, kPanelPlayer},
                                {"Team Chat", Icons::Id::Team, kPanelTeam}, {"Animation", Icons::Id::Animation, kPanelAnimation},
                                {"Assistant", Icons::Id::CommandBar, kPanelAssistant}};
            for (const P& p : panels)
                if (bigButton(p.label, p.icon, m_showPanel[p.panel], true, "Show / hide")) m_showPanel[p.panel] = !m_showPanel[p.panel];
        }
        {
            Group g("Other");
            if (bigButton("Script Analysis", Icons::Id::Script, m_scriptEditor->analysisShown(), true,
                          "Find mistakes in every script without pressing Play"))
                m_scriptEditor->showAnalysis();
            if (bigButton("Shortcuts", Icons::Id::Keyboard, m_showShortcuts, true, "Every keyboard shortcut (F1)"))
                m_showShortcuts = !m_showShortcuts;
            if (bigButton("Settings", Icons::Id::Settings, false, true, "Graphics and frame rate")) m_showSettings = true;
            if (bigButton("Reset Camera", Icons::Id::Workspace, false, true)) m_viewport->resetCamera();
        }
        break;
    }
    }
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + tabsH + ribbonH));
    ImGui::Dummy(ImVec2(0, 0));
}

// The PLUGINS tab: every plugin's buttons, plus the plugin and audio Library.
void Editor::renderPluginsTab() {
    auto& list = m_plugins->list();
    for (size_t i = 0; i < list.size(); ++i) {
        Plugins::Plugin& p = *list[i];
        Group g(p.name.c_str());
        if (p.buttons.empty()) {
            bigButton(p.error.empty() ? "(no buttons)" : "Error", Icons::Id::Script, false, false,
                      p.error.empty() ? "This plugin didn't add any buttons" : p.error.c_str());
        }
        for (size_t b = 0; b < p.buttons.size(); ++b) {
            ImGui::PushID((int)(i * 100 + b));
            if (bigButton(p.buttons[b].label.c_str(), Icons::Id::Insert, false, !m_playing,
                          p.buttons[b].tip.empty() ? nullptr : p.buttons[b].tip.c_str()))
                m_plugins->click(i, b);
            ImGui::PopID();
        }
    }
    Group g("Manage");
    if (bigButton("Library", Icons::Id::Toolbox, m_showPluginLibrary, true, "Get plugins and audio people uploaded"))
        m_showPluginLibrary = !m_showPluginLibrary;
    if (bigButton("Reload", Icons::Id::Rotate, false, true, "Load the plugins folder again")) m_plugins->reload();
    std::string where = "Plugins folder:\n" + Plugins::folder().string() + "\n\nPut .lua plugin files there.";
    bigButton("Folder", Icons::Id::Folder, false, true, where.c_str());
}
