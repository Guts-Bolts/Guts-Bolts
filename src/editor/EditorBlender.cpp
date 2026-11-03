// Blender things in Studio's Build mode:
//  - Hiding things while you build (the eye in the Explorer, like Blender's): H hides
//    what's selected, Shift+H everything else, Alt+H shows it all again, and "/" looks
//    at just the selection (again = back). It's only for you, now: the game, saving,
//    Team Create and playtests never see it.
//  - Blender's object keys: G / R move and rotate with the mouse (ViewportObjectModal.cpp),
//    X deletes, M moves into a Model or Folder, Ctrl+P / Alt+P parent / unparent, Home
//    shows everything. With "Blender keys" on (F1 window), also S scale, A pick all,
//    Shift+D duplicate and move, Shift+A add; then WASD fly only while right-dragging.
#include "Editor.h"
#include "panels/OutlinerPanel.h"
#include "panels/ViewportPanel.h"
#include "../core/Log.h"
#include "../core/Settings.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
bool hideable(const Scene& scene, const SceneNode* n) {
    return n && n->parent && !n->isScript() && !n->isGui() && !scene.isServiceFolder(n);
}
} // namespace

void Editor::setEditorHidden(SceneNode* n, bool hide) {
    if (!hideable(*m_scene, n)) return;
    n->editorHidden = hide && !m_playing;
    if (hide) {
        m_editorHidden.insert(n->id);
        // Like Blender: what you hide stops being selected (so G / Delete can't touch it unseen).
        std::vector<SceneNode*> keep;
        for (SceneNode* s : m_scene->selection())
            if (s != n && !n->isAncestorOf(s)) keep.push_back(s);
        if (keep.size() != m_scene->selection().size()) {
            m_scene->deselect();
            for (SceneNode* s : keep) m_scene->addToSelection(s);
        }
    } else {
        m_editorHidden.erase(n->id);
    }
}

// Undo, redo, Stop and Team Create load the scene again (new objects, flags gone): keep
// the hidden ones hidden by their ids. Playtests show everything.
void Editor::applyEditorHidden() {
    if (m_playing) return;
    for (auto it = m_editorHidden.begin(); it != m_editorHidden.end();) {
        SceneNode* n = m_scene->findById(*it);
        if (!n) { it = m_editorHidden.erase(it); continue; }   // deleted
        n->editorHidden = true;
        ++it;
    }
}

void Editor::eyeClicked(SceneNode* n, bool isolate) {
    if (!n) return;
    if (isolate) {   // Shift+click: only this one shows
        m_scene->deselect();
        m_scene->select(n);
        hideUnselected();
        setEditorHidden(n, false);
        return;
    }
    setEditorHidden(n, !n->editorHidden);
}

void Editor::hideSelected() {
    auto sel = m_scene->selectionRoots();
    int count = 0;
    for (SceneNode* n : sel)
        if (hideable(*m_scene, n) && !m_scene->isCharacterPart(n)) { setEditorHidden(n, true); ++count; }
    if (count) Log::info("Hidden while you build (Alt+H shows everything again). The game still has " +
                         std::string(count == 1 ? "it." : "them."));
}

// Shift+H: hide everything in the Workspace except what's selected (and what it's in).
void Editor::hideUnselected() {
    std::vector<SceneNode*> sel = m_scene->selectionRoots();
    if (sel.empty()) return;
    auto keepShown = [&](SceneNode* n) {
        for (SceneNode* s : sel)
            if (s == n || s->isAncestorOf(n) || n->isAncestorOf(s)) return true;
        return false;
    };
    // The top-most things that hold nothing selected get hidden (their insides go with them).
    std::vector<SceneNode*> stack;
    for (auto& c : m_scene->root()->children) stack.push_back(c.get());
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (!hideable(*m_scene, n) || m_scene->isCharacterRoot(n->id)) continue;
        if (!keepShown(n)) { if (n->isPart() || n->kind == NodeKind::Model || n->isLight() || n->isEffect() || n->isDecal()) setEditorHidden(n, true); continue; }
        bool holdsSelection = false;
        for (SceneNode* s : sel) if (n->isAncestorOf(s) && n != s) holdsSelection = true;
        if (holdsSelection) for (auto& c : n->children) stack.push_back(c.get());
    }
}

void Editor::unhideAll() {
    const size_t count = m_editorHidden.size();
    for (uint64_t id : m_editorHidden)
        if (SceneNode* n = m_scene->findById(id)) n->editorHidden = false;
    m_editorHidden.clear();
    m_localView.clear();
    if (count) Log::info("Everything hidden is showing again.");
}

// "/" (Blender's Local View): just the selection; "/" again brings back what it hid.
void Editor::toggleLocalView() {
    if (!m_localView.empty()) {
        for (uint64_t id : m_localView)
            if (SceneNode* n = m_scene->findById(id)) setEditorHidden(n, false);
        m_localView.clear();
        if (!m_scene->selectionRoots().empty()) m_viewport->focusSelected();
        return;
    }
    if (m_scene->selectionRoots().empty()) return;
    const std::set<uint64_t> before = m_editorHidden;
    hideUnselected();
    for (uint64_t id : m_editorHidden)
        if (!before.count(id)) m_localView.insert(id);
    m_viewport->focusSelected();
}

void Editor::startObjectModal(ObjectModal m) {
    if (m_scene->selectionRoots().empty() || !m_viewport->hovered()) return;   // (it follows the mouse in the 3D view)
    m_state.objectModal = m;
}

// Ctrl+P: put the other selected things inside the last one picked (Blender's Make Parent).
void Editor::parentToActive() {
    SceneNode* active = m_scene->selected();
    if (!active || !canEdit(active) || active->isScript()) return;
    int moved = 0;
    for (SceneNode* n : m_scene->selectionRoots())
        if (n != active && canEdit(n) && !n->isAncestorOf(active) && m_scene->reparent(n, active)) ++moved;
    if (moved) {
        m_outliner->expand({active});
        Log::info("Put " + std::to_string(moved) + " object(s) inside \"" + active->name + "\".");
    }
}

// Alt+P: take them out of what they're in, into the Workspace.
void Editor::clearParent() {
    for (SceneNode* n : m_scene->selectionRoots())
        if (canEdit(n) && n->parent && n->parent != m_scene->root()) m_scene->reparent(n, m_scene->root());
}

// M: move the selection into a Model or Folder (Blender's "Move to Collection").
void Editor::renderMoveTo() {
    if (m_openMoveTo) { ImGui::OpenPopup("Move to##blender"); m_openMoveTo = false; }
    if (!ImGui::BeginPopup("Move to##blender")) return;
    ImGui::TextDisabled("Move the selection into...");
    std::vector<SceneNode*> sel = m_scene->selectionRoots();
    auto moveInto = [&](SceneNode* target) {
        for (SceneNode* n : sel)
            if (canEdit(n) && n != target && !n->isAncestorOf(target)) m_scene->reparent(n, target);
        if (target != m_scene->root()) m_outliner->expand({target});
        ImGui::CloseCurrentPopup();
    };
    if (ImGui::MenuItem("Workspace")) moveInto(m_scene->root());
    ImGui::Separator();
    int shown = 0;
    std::vector<std::pair<SceneNode*, int>> stack;
    for (auto it = m_scene->root()->children.rbegin(); it != m_scene->root()->children.rend(); ++it) stack.push_back({it->get(), 0});
    while (!stack.empty() && shown < 60) {
        auto [n, depth] = stack.back();
        stack.pop_back();
        if (n->kind != NodeKind::Model || m_scene->isServiceFolder(n) || m_scene->isCharacterRoot(n->id) || !canEdit(n)) continue;
        bool inSelection = false;
        for (SceneNode* s : sel) if (s == n || s->isAncestorOf(n)) inSelection = true;
        ImGui::PushID(n);
        ImGui::Indent((float)depth * 12.0f);
        if (ImGui::MenuItem(n->name.c_str(), nullptr, false, !inSelection)) moveInto(n);
        ImGui::Unindent((float)depth * 12.0f);
        ImGui::PopID();
        ++shown;
        if (depth < 3)
            for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) stack.push_back({it->get(), depth + 1});
    }
    ImGui::Separator();
    if (ImGui::MenuItem("New Model")) {
        SceneNode* model = m_scene->insert(std::make_unique<SceneNode>("Model", NodeKind::Model), m_scene->root());
        moveInto(model);
        m_scene->select(model);
    }
    ImGui::EndPopup();
}

void Editor::renderBlenderKeysToggle() {
    GraphicsSettings& gs = GraphicsSettings::get();
    if (ImGui::Checkbox("Blender keys", &gs.studioBlenderKeys)) gs.save();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: S scales, A picks everything, Shift+D duplicates and moves, Shift+A adds,\n"
                          "and W A S D Q E fly the camera only while you hold the right mouse button.\n"
                          "Off (like Roblox Studio): WASD fly whenever the 3D view is clicked into.\n"
                          "G, R, X, M, H, Ctrl+P and the eye work either way.");
}

// Build mode's Blender keys. Called before the usual plain keys; true = the key was used.
bool Editor::handleBlenderKeys() {
    ImGuiIO& io = ImGui::GetIO();
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) return false;   // flying the camera
    const bool blender = GraphicsSettings::get().studioBlenderKeys;
    const bool overView = m_viewport->hovered();
    if (io.KeyShift) {
        if (pressed(ImGuiKey_H)) { hideUnselected(); return true; }
        if (blender && overView && pressed(ImGuiKey_D)) {
            duplicateSelected();
            for (SceneNode* n : m_scene->selectionRoots())
                if (n->isPart()) n->transform.position.x -= 1.0f;   // (the copy stays put; the mouse moves it)
            startObjectModal(ObjectModal::Grab);
            return true;
        }
        if (blender && pressed(ImGuiKey_A)) { m_insertParent = nullptr; m_openInsert = true; return true; }
        return false;
    }
    if (pressed(ImGuiKey_H)) { hideSelected(); return true; }
    if (pressed(ImGuiKey_Slash) || pressed(ImGuiKey_KeypadDivide)) { toggleLocalView(); return true; }
    if (overView && pressed(ImGuiKey_G)) { startObjectModal(ObjectModal::Grab); return true; }
    if (overView && pressed(ImGuiKey_R)) { startObjectModal(ObjectModal::Rotate); return true; }
    if (pressed(ImGuiKey_X) && !m_scene->selectionRoots().empty()) { deleteSelected(); return true; }
    if (pressed(ImGuiKey_M) && !m_scene->selectionRoots().empty()) { m_openMoveTo = true; return true; }
    if (pressed(ImGuiKey_Home)) {   // everything in view (the selection stays as it was)
        const std::vector<SceneNode*> keep = m_scene->selection();
        selectAll();
        m_viewport->focusSelected();
        m_scene->deselect();
        for (SceneNode* n : keep) m_scene->addToSelection(n);
        return true;
    }
    if (blender) {
        if (overView && pressed(ImGuiKey_S)) { startObjectModal(ObjectModal::Scale); return true; }
        if (pressed(ImGuiKey_A)) {   // pick all; again = none
            if (m_scene->selectionRoots().empty()) selectAll();
            else m_scene->deselect();
            return true;
        }
    }
    return false;
}

// Test steps (--test-mesh "blender:hide,blender:unhide,..."), on what's selected.
void Editor::testBlender(const std::string& step) {
    if (step == "hide") hideSelected();
    else if (step == "hideothers") hideUnselected();
    else if (step == "unhide") unhideAll();
    else if (step == "local") toggleLocalView();
    else if (step == "eye") { if (SceneNode* n = m_scene->selected()) eyeClicked(n, false); }
    else if (step == "parent") parentToActive();
    else if (step == "unparent") clearParent();
    else if (step == "keys") { GraphicsSettings::get().studioBlenderKeys = true; }
    else if (step.rfind("grab", 0) == 0 || step.rfind("rotate", 0) == 0 || step.rfind("scale", 0) == 0) {
        // grab / rotate / scale: start it as if the key was pressed over the 3D view.
        m_state.objectModal = step[0] == 'g' ? ObjectModal::Grab : step[0] == 'r' ? ObjectModal::Rotate : ObjectModal::Scale;
    } else if (step == "report") {
        int hidden = 0, shown = 0;
        m_scene->forEach([&](SceneNode* n) {
            if (!n->isPart() || n->internal || !n->parent) return;
            bool h = false;
            for (SceneNode* p = n; p; p = p->parent) if (p->editorHidden) h = true;
            (h ? hidden : shown)++;
        });
        std::string where;
        if (SceneNode* n = m_scene->selected()) {
            char b[200];
            const Transform& t = n->transform;
            std::snprintf(b, sizeof b, "; %s at (%.2f, %.2f, %.2f) turned (%.1f, %.1f, %.1f) size (%.2f, %.2f, %.2f) in %s", n->name.c_str(),
                          t.position.x, t.position.y, t.position.z, t.rotation.x, t.rotation.y, t.rotation.z,
                          t.scale.x, t.scale.y, t.scale.z, n->parent ? n->parent->name.c_str() : "-");
            where = b;
        }
        Log::info("blender: " + std::to_string(hidden) + " parts hidden, " + std::to_string(shown) + " shown, " +
                  std::to_string(m_scene->selectionRoots().size()) + " selected" + where);
    } else Log::warn("blender: unknown step " + step);
}
