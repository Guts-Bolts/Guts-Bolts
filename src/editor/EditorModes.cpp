// Studio's modes (Build / Modeling / Simulate / Play) and the Modeling-mode
// mesh tools. See EditorState.h for what each mode is.
#include "Editor.h"
#include "panels/ViewportPanel.h"
#include "../scene/Scene.h"
#include "../scene/EditMesh.h"
#include "../scene/Skin.h"
#include "../scene/Serializer.h"
#include "../core/Log.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

void Editor::setMode(StudioMode mode) {
    StudioMode cur = m_state.mode;
    if (mode == cur) return;
    if (cur == StudioMode::Modeling) exitModeling();
    if (cur == StudioMode::Simulate || cur == StudioMode::Play) togglePlay();   // back to Build first
    switch (mode) {
        case StudioMode::Build:    break;
        case StudioMode::Modeling: enterModeling(); break;
        case StudioMode::Simulate: startPlay(2); break;
        case StudioMode::Play:     startPlay(0); break;
    }
}

void Editor::enterModeling() {
    SceneNode* n = m_scene->selected();
    if (!n || !n->isPart() || !canEdit(n) || m_scene->isCharacterPart(n)) {
        Log::warn("Modeling: select one part first, then press Tab (or pick Modeling in the mode menu).");
        return;
    }
    if (n->primitiveType != PrimitiveType::Mesh) {
        // A Plane is paper-thin: keep its box thin too so it still collides like one.
        if (n->primitiveType == PrimitiveType::Plane) n->transform.scale.y = 0.04f;
        MeshEdit::attach(*n, MeshEdit::fromPrimitive(n->primitiveType));
        Log::info("'" + n->name + "' is now an editable mesh (a MeshPart). Ctrl+Z turns it back.");
    }
    int pickMode = m_state.modeling.selectMode;
    bool xray = m_state.modeling.xray;
    m_state.modeling = ModelingState{};
    m_state.modeling.node = n->id;
    m_state.modeling.selectMode = pickMode;
    m_state.modeling.xray = xray;
    m_state.modeling.sel.assign(n->editMesh->verts.size(), 0);
    m_state.modeling.hidden.assign(n->editMesh->verts.size(), 0);
    m_state.mode = StudioMode::Modeling;
    m_state.connectTool = -1;
    m_ribbonTab = 5;   // MESH
    m_viewport->focus();
}

void Editor::exitModeling() {
    if (SceneNode* n = m_scene->findById(m_state.modeling.node)) MeshEdit::fit(*n);
    m_state.modeling.node = 0;
    m_state.modeling.sel.clear();
    m_state.modeling.hidden.clear();
    m_state.modeling.modal = MeshModal::None;
    if (m_state.mode == StudioMode::Modeling) m_state.mode = StudioMode::Build;
    if (m_ribbonTab == 5) m_ribbonTab = 0;
}

// Leave Modeling mode if the part went away (deleted, undone) or something
// else got selected in the Explorer.
void Editor::checkModeling() {
    if (m_state.mode != StudioMode::Modeling) return;
    SceneNode* n = m_scene->findById(m_state.modeling.node);
    if (!n || !n->editMesh) { exitModeling(); return; }
    if (m_scene->selected() != n) exitModeling();
}

void Editor::meshOp(MeshOp op) {
    SceneNode* n = m_scene->findById(m_state.modeling.node);
    if (!n || !n->editMesh) return;
    ModelingState& ms = m_state.modeling;
    if (ms.modal != MeshModal::None) return;   // one thing at a time
    // Work on a copy so a tool that goes wrong can't leave a broken mesh.
    EditMesh work = *n->editMesh;
    MeshEdit::Selection sel = ms.sel;
    sel.resize(work.verts.size(), 0);
    ms.hidden.resize(work.verts.size(), 0);
    for (size_t i = 0; i < sel.size(); ++i) if (ms.hidden[i]) sel[i] = 0;   // hidden corners are never picked
    const int picked = MeshEdit::countSelected(sel);
    bool ok = false;
    const char* why = "";
    const glm::mat4 M = n->worldMatrix();
    // Which way is "out" for the picked faces (or the faces next to picked edges), in the world.
    auto outward = [&](const EditMesh& m, const MeshEdit::Selection& s) {
        glm::mat3 normalM = glm::transpose(glm::inverse(glm::mat3(M)));
        glm::vec3 dir(0.0f);
        auto faces = MeshEdit::selectedFaces(m, s);
        if (faces.empty())
            for (size_t f = 0; f < m.faces.size(); ++f) {
                const auto& face = m.faces[f];
                for (size_t i = 0; i < face.size(); ++i)
                    if (s[face[i]] && s[face[(i + 1) % face.size()]]) { faces.push_back((int)f); break; }
            }
        for (int f : faces) dir += normalM * MeshEdit::faceNormal(m, m.faces[f]);
        return dir;
    };
    auto startModal = [&](MeshModal what, glm::vec3 axis = glm::vec3(0.0f)) {
        ms.modal = what;
        ms.modalAxis = glm::length(axis) > 1e-6f ? glm::normalize(axis) : glm::vec3(0.0f);
        m_viewport->focus();
    };
    // The first picked edge (for loop / ring picking from a menu).
    auto pickedEdge = [&](uint32_t& a, uint32_t& b) {
        for (auto [x, y] : MeshEdit::edges(work)) if (sel[x] && sel[y]) { a = x; b = y; return true; }
        return false;
    };

    switch (op) {
    case MeshOp::Extrude:
    case MeshOp::ExtrudeMove: {
        glm::vec3 dir = outward(work, sel);
        ok = MeshEdit::extrude(work, sel);
        why = "Extrude: pick some faces or edges first.";
        if (ok && op == MeshOp::ExtrudeMove) {
            // Like Blender: the new faces follow the mouse straight out (click to stop there).
            Skin::carry(*n->editMesh, work);
            MeshEdit::own(*n) = std::move(work);
            ms.sel = std::move(sel);
            ms.hidden.assign(ms.sel.size(), 0);
            MeshEdit::refresh(*n);
            startModal(MeshModal::Grab, dir);
            return;
        }
        if (ok && glm::length(dir) > 1e-6f) {
            glm::vec3 d = glm::vec3(glm::inverse(M) * glm::vec4(glm::normalize(dir) * 1.0f, 0.0f));   // 1 stud out
            for (size_t i = 0; i < sel.size(); ++i) if (sel[i]) work.verts[i] += d;
        }
        break;
    }
    case MeshOp::Grab: case MeshOp::Rotate: case MeshOp::Scale:
        if (!picked) { Log::warn("Pick some corners, edges or faces first."); return; }
        ms.sel = sel;
        startModal(op == MeshOp::Grab ? MeshModal::Grab : op == MeshOp::Rotate ? MeshModal::Rotate : MeshModal::Scale);
        return;
    case MeshOp::DuplicateMove:
        if (!MeshEdit::duplicate(work, sel)) { Log::warn("Duplicate: pick some faces first."); return; }
        ms.hidden = MeshEdit::carryFlags(*n->editMesh, ms.hidden, work);
        Skin::carry(*n->editMesh, work);
        MeshEdit::own(*n) = std::move(work);
        ms.sel = std::move(sel);
        for (size_t i = 0; i < ms.sel.size(); ++i) if (ms.sel[i]) ms.hidden[i] = 0;
        MeshEdit::refresh(*n);
        startModal(MeshModal::Grab);
        return;
    case MeshOp::InsetMove:
        if (MeshEdit::selectedFaces(work, sel).empty()) { Log::warn("Inset: pick some faces first."); return; }
        ms.sel = sel;
        startModal(MeshModal::Inset);
        return;
    case MeshOp::LoopCut:
        startModal(MeshModal::LoopCut);
        return;
    case MeshOp::Inset:     ok = MeshEdit::inset(work, sel, 0.25f); why = "Inset: pick some faces first."; break;
    case MeshOp::Subdivide: ok = MeshEdit::subdivide(work, sel); break;
    case MeshOp::Delete:
        ok = MeshEdit::remove(work, sel, ms.selectMode);
        why = ms.selectMode == 2 ? "Delete: pick some faces first." : ms.selectMode == 1 ? "Delete: pick some edges first."
                                                                    : "Delete: pick some vertices first.";
        break;
    case MeshOp::DeleteVerts: ok = MeshEdit::remove(work, sel, 0); why = "Delete: pick some vertices first."; break;
    case MeshOp::DeleteEdges: ok = MeshEdit::remove(work, sel, 1); why = "Delete: pick some edges first."; break;
    case MeshOp::DeleteFaces: ok = MeshEdit::remove(work, sel, 2); why = "Delete: pick some faces first."; break;
    case MeshOp::DissolveVerts: ok = picked && MeshEdit::dissolve(work, sel, 0); why = "Dissolve: pick some vertices first."; break;
    case MeshOp::DissolveEdges: ok = picked && MeshEdit::dissolve(work, sel, 1); why = "Dissolve: pick edges between faces first."; break;
    case MeshOp::DissolveFaces: ok = picked && MeshEdit::dissolve(work, sel, 2); why = "Dissolve: pick two or more faces side by side (no hole in the middle)."; break;
    case MeshOp::Merge:  ok = MeshEdit::merge(work, sel); why = "Merge: pick two or more vertices."; break;
    case MeshOp::Collapse: ok = MeshEdit::collapse(work, sel); why = "Collapse: pick two or more vertices."; break;
    case MeshOp::MergeByDistance: {
        int removed = 0;
        ok = MeshEdit::mergeByDistance(work, sel, 0.001f, &removed);
        Log::info("Merge by distance: removed " + std::to_string(removed) + " corner" + (removed == 1 ? "" : "s") + ".");
        if (!ok) return;
        break;
    }
    case MeshOp::Fill:   ok = MeshEdit::fill(work, sel);  why = "Fill: pick three or more vertices around a hole."; break;
    case MeshOp::Bridge: ok = MeshEdit::bridge(work, sel); why = "Bridge: pick two edge loops with the same number of corners."; break;
    case MeshOp::Flip:   ok = MeshEdit::flip(work, sel); break;
    case MeshOp::NormalsOut: ok = MeshEdit::recalcNormals(work, sel, false); break;
    case MeshOp::NormalsIn:  ok = MeshEdit::recalcNormals(work, sel, true); break;
    case MeshOp::Triangulate: ok = MeshEdit::triangulate(work, sel); why = "Triangulate: those faces are already triangles."; break;
    case MeshOp::TrisToQuads: ok = MeshEdit::trisToQuads(work, sel); why = "Tris to Quads: no pairs of triangles that make a good four-sided face."; break;
    case MeshOp::SmoothVerts:
        ok = picked > 0;
        why = "Smooth: pick some vertices first.";
        for (int k = 0; ok && k < 3; ++k) MeshEdit::smoothVerts(work, sel, 0.5f);
        break;
    case MeshOp::SubdivideSmooth: ok = MeshEdit::subdivideSmooth(work, sel); break;
    case MeshOp::MirrorX: case MeshOp::MirrorY: case MeshOp::MirrorZ:
        ok = MeshEdit::mirror(work, sel, op == MeshOp::MirrorX ? 0 : op == MeshOp::MirrorY ? 1 : 2);
        break;
    case MeshOp::Spin: {
        // Spin round the part's own up axis (a lathe): draw half a vase's outline, then spin it.
        glm::vec3 sc = n->transform.scale;
        for (auto& p : work.verts) p *= sc;
        ok = MeshEdit::spin(work, sel, glm::vec3(0, 1, 0), glm::vec3(0.0f), 360.0f, 12);
        for (auto& p : work.verts) p /= sc;
        why = "Spin: pick some edges (or faces) to spin round the part's middle.";
        break;
    }
    case MeshOp::Smooth: work.smooth = !work.smooth; ok = true; break;
    case MeshOp::ShadeSmooth: work.smooth = true; ok = true; break;
    case MeshOp::ShadeFlat:   work.smooth = false; ok = true; break;
    case MeshOp::Separate: {
        EditMesh piece = MeshEdit::extract(work, sel);
        if (piece.faces.empty()) { Log::warn("Separate: pick some faces first."); return; }
        if (!MeshEdit::remove(work, sel, 2) || work.faces.empty()) { Log::warn("Separate: leave some faces behind (that's the whole mesh)."); return; }
        auto copy = Serializer::nodeFromString(Serializer::nodeToString(*n), true);
        if (!copy) return;
        copy->children.clear();
        copy->name = n->name + ".001";
        copy->editMesh = std::make_shared<EditMesh>(std::move(piece));
        SceneNode* added = m_scene->insert(std::move(copy), n->parent);
        MeshEdit::fit(*added);
        MeshEdit::refresh(*added);
        Log::info("Separated the picked faces into '" + added->name + "'.");
        ok = true;
        break;
    }
    case MeshOp::SelectAll: {
        bool all = true;
        for (size_t i = 0; i < sel.size(); ++i) if (!ms.hidden[i] && !sel[i]) all = false;
        for (size_t i = 0; i < sel.size(); ++i) sel[i] = all ? 0 : !ms.hidden[i];
        ms.sel = sel;
        return;
    }
    case MeshOp::SelectNone: std::fill(ms.sel.begin(), ms.sel.end(), 0); return;
    case MeshOp::Invert:
        for (size_t i = 0; i < ms.sel.size(); ++i) ms.sel[i] = !ms.sel[i] && !(i < ms.hidden.size() && ms.hidden[i]);
        return;
    case MeshOp::SelectLinked: case MeshOp::SelectGrow: case MeshOp::SelectShrink:
    case MeshOp::SelectBoundary: case MeshOp::SelectNonManifold: case MeshOp::SelectLoop: case MeshOp::SelectRing: {
        if (op == MeshOp::SelectLinked) MeshEdit::selectLinked(work, sel);
        else if (op == MeshOp::SelectGrow) MeshEdit::grow(work, sel);
        else if (op == MeshOp::SelectShrink) MeshEdit::shrink(work, sel);
        else if (op == MeshOp::SelectBoundary) MeshEdit::selectBoundary(work, sel);
        else if (op == MeshOp::SelectNonManifold) MeshEdit::selectNonManifold(work, sel);
        else {
            uint32_t a = 0, b = 0;
            if (!pickedEdge(a, b)) { Log::warn("Pick an edge first (or Alt+click one)."); return; }
            if (op == MeshOp::SelectLoop) for (uint32_t v : MeshEdit::edgeLoop(work, a, b)) sel[v] = 1;
            else for (auto [x, y] : MeshEdit::edgeRing(work, a, b)) sel[x] = sel[y] = 1;
        }
        for (size_t i = 0; i < sel.size(); ++i) if (ms.hidden[i]) sel[i] = 0;
        ms.sel = sel;
        return;
    }
    case MeshOp::Hide:
        for (size_t i = 0; i < sel.size(); ++i) if (sel[i]) { ms.hidden[i] = 1; ms.sel[i] = 0; }
        return;
    case MeshOp::HideOthers:
        for (size_t i = 0; i < sel.size(); ++i) if (!sel[i]) ms.hidden[i] = 1;
        return;
    case MeshOp::Reveal:
        // What comes back is picked, so you can see what it was (like Blender).
        for (size_t i = 0; i < ms.hidden.size() && i < ms.sel.size(); ++i) if (ms.hidden[i]) { ms.sel[i] = 1; ms.hidden[i] = 0; }
        return;
    case MeshOp::Proportional:
        ms.proportional = !ms.proportional;
        Log::info(std::string("Proportional editing ") + (ms.proportional ? "on: moving corners drags nearby ones too "
                                                                         "(mouse wheel while moving = how far)." : "off."));
        return;
    }
    if (ok && work.faces.empty()) { ok = false; why = "That would leave nothing - to remove the part, delete it in Build mode."; }
    if (!ok) { if (*why) Log::warn(why); return; }
    std::vector<char> hidden = MeshEdit::carryFlags(*n->editMesh, ms.hidden, work);
    Skin::carry(*n->editMesh, work);   // bound to bones: new vertices get weights too
    MeshEdit::own(*n) = std::move(work);
    sel.resize(n->editMesh->verts.size(), 0);
    for (size_t i = 0; i < sel.size(); ++i) if (sel[i]) hidden[i] = 0;
    ms.sel = std::move(sel);
    ms.hidden = std::move(hidden);
    MeshEdit::fit(*n);
    MeshEdit::refresh(*n);
}

// Keys in Modeling mode (Blender's Edit Mode keys). Returns after handling so the
// Build-mode keys (Delete removes the part, ...) don't also fire.
void Editor::handleModelingKeys() {
    ImGuiIO& io = ImGui::GetIO();
    ModelingState& ms = m_state.modeling;
    // A tool that follows the mouse (G, R, S...) uses the keys itself (X / Y / Z, numbers, Esc).
    if (ms.modal != MeshModal::None || ms.modalEnded == ImGui::GetFrameCount()) return;
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (pressed(ImGuiKey_F3)) { m_meshMenu = "Search"; return; }
    if (io.KeyCtrl) {
        if (pressed(ImGuiKey_Z)) { io.KeyShift ? redo() : undo(); }
        if (pressed(ImGuiKey_Y)) redo();
        if (pressed(ImGuiKey_S)) save();
        if (pressed(ImGuiKey_I)) meshOp(MeshOp::Invert);
        if (pressed(ImGuiKey_L)) meshOp(MeshOp::SelectLinked);
        if (pressed(ImGuiKey_R)) meshOp(MeshOp::LoopCut);
        if (pressed(ImGuiKey_T)) meshOp(MeshOp::Triangulate);
        if (pressed(ImGuiKey_M)) m_meshMenu = "Mirror";
        if (pressed(ImGuiKey_V)) m_meshMenu = "Vertex";
        if (pressed(ImGuiKey_E)) m_meshMenu = "Edge";
        if (pressed(ImGuiKey_F)) m_meshMenu = "Face";
        if (pressed(ImGuiKey_X)) meshOp(ms.selectMode == 0 ? MeshOp::DissolveVerts : ms.selectMode == 1 ? MeshOp::DissolveEdges
                                                                                                    : MeshOp::DissolveFaces);
        if (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd)) meshOp(MeshOp::SelectGrow);
        if (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract)) meshOp(MeshOp::SelectShrink);
        if (pressed(ImGuiKey_1)) meshOp(MeshOp::SubdivideSmooth);
        if (pressed(ImGuiKey_Tab)) setMode(StudioMode::Build);
        return;
    }
    if (io.KeyAlt) {
        if (pressed(ImGuiKey_A)) meshOp(MeshOp::SelectNone);
        if (pressed(ImGuiKey_Z)) ms.xray = !ms.xray;
        if (pressed(ImGuiKey_H)) meshOp(MeshOp::Reveal);
        if (pressed(ImGuiKey_J)) meshOp(MeshOp::TrisToQuads);
        if (pressed(ImGuiKey_N)) m_meshMenu = "Normals";
        return;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) return;   // flying the camera with WASDQE
    if (io.KeyShift) {
        if (pressed(ImGuiKey_A)) m_meshMenu = "Add";
        if (pressed(ImGuiKey_D)) meshOp(MeshOp::DuplicateMove);
        if (pressed(ImGuiKey_H)) meshOp(MeshOp::HideOthers);
        if (pressed(ImGuiKey_N)) meshOp(MeshOp::NormalsOut);
        if (pressed(ImGuiKey_E)) meshOp(MeshOp::Extrude);   // the old way: straight out 1 stud
        return;
    }
    if (pressed(ImGuiKey_1)) ms.selectMode = 0;
    if (pressed(ImGuiKey_2)) ms.selectMode = 1;
    if (pressed(ImGuiKey_3)) ms.selectMode = 2;
    if (pressed(ImGuiKey_A)) meshOp(MeshOp::SelectAll);
    if (pressed(ImGuiKey_G)) meshOp(MeshOp::Grab);
    if (pressed(ImGuiKey_R)) meshOp(MeshOp::Rotate);
    if (pressed(ImGuiKey_S)) meshOp(MeshOp::Scale);
    if (pressed(ImGuiKey_E)) meshOp(MeshOp::ExtrudeMove);
    if (pressed(ImGuiKey_I)) meshOp(MeshOp::InsetMove);
    if (pressed(ImGuiKey_X)) m_meshMenu = "Delete";
    if (pressed(ImGuiKey_Delete)) meshOp(MeshOp::Delete);
    if (pressed(ImGuiKey_M)) m_meshMenu = "Merge";
    if (pressed(ImGuiKey_F)) meshOp(MeshOp::Fill);
    if (pressed(ImGuiKey_H)) meshOp(MeshOp::Hide);
    if (pressed(ImGuiKey_O)) meshOp(MeshOp::Proportional);
    if (pressed(ImGuiKey_P)) meshOp(MeshOp::Separate);
    if (pressed(ImGuiKey_W)) m_meshMenu = "Context";
    if (pressed(ImGuiKey_Tab) || pressed(ImGuiKey_Escape)) setMode(StudioMode::Build);
}

// Test helper: --test-mesh "cube:face-top,extrude,inset" style scripts.
void Editor::testMesh(const std::string& steps) {
    size_t start = 0;
    while (start <= steps.size()) {
        size_t end = steps.find(',', start);
        if (end == std::string::npos) end = steps.size();
        std::string s = steps.substr(start, end - start);
        start = end + 1;
        if (s.empty()) continue;
        if (s == "enter") setMode(StudioMode::Modeling);
        else if (s == "exit") setMode(StudioMode::Build);
        else if (s == "simulate") setMode(StudioMode::Simulate);
        else if (s == "play") setMode(StudioMode::Play);
        else if (s == "vertex" || s == "edge" || s == "face") m_state.modeling.selectMode = s == "vertex" ? 0 : s == "edge" ? 1 : 2;
        else if (s == "all") meshOp(MeshOp::SelectAll);
        else if (s == "none") meshOp(MeshOp::SelectNone);
        else if (s == "extrude") meshOp(MeshOp::Extrude);
        else if (s == "inset") meshOp(MeshOp::Inset);
        else if (s == "subdivide") meshOp(MeshOp::Subdivide);
        else if (s == "delete") meshOp(MeshOp::Delete);
        else if (s == "merge") meshOp(MeshOp::Merge);
        else if (s == "fill") meshOp(MeshOp::Fill);
        else if (s == "flip") meshOp(MeshOp::Flip);
        else if (s == "smooth") meshOp(MeshOp::Smooth);
        else if (s == "xray") m_state.modeling.xray = !m_state.modeling.xray;
        else if (s == "new") addMeshPart();
        else if (s.rfind("chain:", 0) == 0) { if (SceneNode* p = armaturePart()) addBone(p, std::atoi(s.c_str() + 6)); m_showArmature = true; }
        else if (s == "bind") { if (SceneNode* p = armaturePart()) { Skin::bindAutomatic(*p); MeshEdit::refresh(*p); } }
        else if (s.rfind("turn:", 0) == 0) {
            // turn:Bone2:x:y:z  turns a bone (degrees), like the Rotate tool.
            char name[64] = {0};
            float x = 0, y = 0, z = 0;
            SceneNode* p = armaturePart();
            if (!p || std::sscanf(s.c_str() + 5, "%63[^:]:%f:%f:%f", name, &x, &y, &z) != 4) continue;
            for (SceneNode* b : Skin::bones(p)) if (b->name == name) b->transform.rotation += glm::vec3(x, y, z);
        }
        else if (s == "skinreport") {
            SceneNode* p = armaturePart();
            if (!p || !p->editMesh) { Log::warn("skin: no MeshPart"); continue; }
            std::vector<glm::vec3> bent;
            bool ok = Skin::deform(*p, bent);
            glm::vec3 lo(1e30f), hi(-1e30f);
            for (auto& v : bent) { lo = glm::min(lo, v * p->transform.scale); hi = glm::max(hi, v * p->transform.scale); }
            char line[256];
            std::snprintf(line, sizeof line, "skin: %s, %zu bones, %zu verts, bent box x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f",
                          ok ? "bound" : "NOT bound", Skin::bones(p).size(), p->editMesh->verts.size(), lo.x, hi.x, lo.y, hi.y, lo.z, hi.z);
            Log::info(line);
        }
        else if (s.rfind("save:", 0) == 0) saveFile(s.substr(5));
        else if (s == "new") addMeshPart();
        else if (s == "join") joinSelected();
        else if (s.rfind("pick:", 0) == 0) {   // add a part (by name) to the selection
            m_scene->forEach([&](SceneNode* n) { if (n->name == s.substr(5)) m_scene->addToSelection(n); });
        } else if (s == "parts") {
            m_scene->forEach([](SceneNode* n) {
                if (n->editMesh) Log::info("part: " + n->name + " " + std::to_string(n->editMesh->verts.size()) + " corners, " +
                                           std::to_string(n->editMesh->faces.size()) + " faces");
            });
        }
        else if (s.rfind("op:", 0) == 0) { if (!meshOpNamed(s.substr(3))) Log::warn("--test-mesh: no tool called '" + s.substr(3) + "'"); }
        else if (s.rfind("add:", 0) == 0) {
            for (int i = 0; i < MeshEdit::kAddShapeCount; ++i)
                if (s.substr(4) == MeshEdit::kAddShapeNames[i]) addMeshShape(i);
        } else if (s.rfind("loopcut:", 0) == 0) {
            // Cut through the first picked edge (or the mesh's first edge).
            SceneNode* n = m_scene->findById(m_state.modeling.node);
            if (!n || !n->editMesh) continue;
            EditMesh work = *n->editMesh;
            auto& ms = m_state.modeling;
            ms.sel.resize(work.verts.size(), 0);
            auto es = MeshEdit::edges(work);
            std::pair<uint32_t, uint32_t> e = es.front();
            for (auto [a, b] : es) if (ms.sel[a] && ms.sel[b]) { e = {a, b}; break; }
            if (MeshEdit::loopCut(work, ms.sel, e.first, e.second, std::atoi(s.c_str() + 8))) {
                Skin::carry(*n->editMesh, work);
                MeshEdit::own(*n) = std::move(work);
                ms.hidden.assign(ms.sel.size(), 0);
                MeshEdit::fit(*n);
                MeshEdit::refresh(*n);
            }
        } else if (s == "report") {
            SceneNode* n = m_scene->findById(m_state.modeling.node);
            if (!n || !n->editMesh) { Log::info("report: not modeling"); continue; }
            int tris = 0, quads = 0, ngons = 0;
            for (auto& f : n->editMesh->faces) (f.size() == 3 ? tris : f.size() == 4 ? quads : ngons)++;
            Log::info("report: " + std::to_string(n->editMesh->verts.size()) + " corners, " + std::to_string(n->editMesh->faces.size()) +
                      " faces (" + std::to_string(tris) + " tris, " + std::to_string(quads) + " quads, " + std::to_string(ngons) +
                      " bigger), " + std::to_string(MeshEdit::countSelected(m_state.modeling.sel)) + " picked, " +
                      std::to_string(MeshEdit::countSelected(m_state.modeling.hidden)) + " hidden, size " +
                      std::to_string(n->transform.scale.x) + " x " + std::to_string(n->transform.scale.y) + " x " +
                      std::to_string(n->transform.scale.z) + " at " + std::to_string(n->transform.position.x) + ", " +
                      std::to_string(n->transform.position.y) + ", " + std::to_string(n->transform.position.z));
        }
        else if (s == "roundtrip") {
            // Save and load the whole place, then report every MeshPart.
            std::string saved = Serializer::saveScene(*m_scene);
            restore(saved);
            m_scene->forEach([](SceneNode* n) {
                if (n->editMesh)
                    Log::info("roundtrip: " + n->name + " has " + std::to_string(n->editMesh->verts.size()) + " corners, " +
                              std::to_string(n->editMesh->faces.size()) + " faces" + (n->mesh ? "" : " (NO GPU MESH)"));
            });
        }
        else if (s.rfind("top", 0) == 0 || s == "bottom") {
            // Pick the vertices on the top (or bottom) of the mesh.
            SceneNode* n = m_scene->findById(m_state.modeling.node);
            if (!n || !n->editMesh) continue;
            auto& ms = m_state.modeling;
            ms.sel.assign(n->editMesh->verts.size(), 0);
            float best = s == "bottom" ? 1e9f : -1e9f;
            for (auto& p : n->editMesh->verts) best = s == "bottom" ? std::min(best, p.y) : std::max(best, p.y);
            for (size_t i = 0; i < ms.sel.size(); ++i)
                if (std::abs(n->editMesh->verts[i].y - best) < 1e-4f) ms.sel[i] = 1;
        } else if (s.rfind("move:", 0) == 0) {
            // move:x:y:z in studs (world), like dragging the gizmo.
            SceneNode* n = m_scene->findById(m_state.modeling.node);
            float x = 0, y = 0, z = 0;
            if (!n || std::sscanf(s.c_str() + 5, "%f:%f:%f", &x, &y, &z) != 3) continue;
            glm::vec3 d = glm::vec3(glm::inverse(n->worldMatrix()) * glm::vec4(x, y, z, 0.0f));
            EditMesh& m = MeshEdit::own(*n);
            for (size_t i = 0; i < m.verts.size() && i < m_state.modeling.sel.size(); ++i)
                if (m_state.modeling.sel[i]) m.verts[i] += d;
            MeshEdit::fit(*n);
            MeshEdit::refresh(*n);
        } else Log::warn("--test-mesh: unknown step '" + s + "'");
    }
}
