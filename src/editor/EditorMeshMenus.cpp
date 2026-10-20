// Modeling mode's menus, laid out like Blender's: the right-click menu (it
// changes with vertex / edge / face picking), X Delete, M Merge, Shift+A Add,
// Ctrl+V / Ctrl+E / Ctrl+F, Alt+N Normals, Ctrl+M Mirror and F3 search.
// Also Ctrl+J in Build mode (join mesh parts into one).
#include "Editor.h"
#include "panels/ViewportPanel.h"
#include "../scene/Scene.h"
#include "../scene/EditMesh.h"
#include "../core/Log.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>
#include <iterator>

namespace {

// Which menus an action shows up in.
enum : unsigned {
    kVert = 1u << 0,    // right-click while picking vertices, and Ctrl+V
    kEdge = 1u << 1,    // ... edges, and Ctrl+E
    kFace = 1u << 2,    // ... faces, and Ctrl+F
    kSel = 1u << 3,     // Select
    kDel = 1u << 4,     // X
    kMerge = 1u << 5,   // M
    kMirror = 1u << 6,  // Ctrl+M
    kNormals = 1u << 7, // Alt+N
    kMesh = 1u << 8,    // Mesh (the ribbon's menu)
};

struct Action { const char* name; const char* keys; Editor::MeshOp op; unsigned menus; const char* tip; };
using Op = Editor::MeshOp;

const Action kActions[] = {
    {"Move", "G", Op::Grab, kMesh | kVert | kEdge | kFace, "Follows the mouse. X / Y / Z lock an axis, type a number, click to finish"},
    {"Rotate", "R", Op::Rotate, kMesh, "Turns round the middle of what's picked"},
    {"Scale", "S", Op::Scale, kMesh, "Grows or shrinks from the middle of what's picked"},
    {"Extrude", "E", Op::ExtrudeMove, kEdge | kFace, "Pull out new faces and move them with the mouse"},
    {"Extrude 1 Stud Out", "Shift+E", Op::Extrude, kFace, "Pull the faces straight out by one stud"},
    {"Inset Faces", "I", Op::InsetMove, kFace, "A smaller face inside each picked face (move the mouse in)"},
    {"Loop Cut", "Ctrl+R", Op::LoopCut, kEdge | kMesh, "Cut a new loop of edges round the mesh (wheel = how many)"},
    {"Subdivide", "", Op::Subdivide, kEdge | kFace, "Cut the picked faces (or all) into smaller ones"},
    {"Subdivide Smooth", "Ctrl+1", Op::SubdivideSmooth, kFace | kMesh, "Smaller faces AND rounder (Catmull-Clark, the whole mesh)"},
    {"Bridge Edge Loops", "", Op::Bridge, kEdge, "Join two picked loops of edges with a tube of faces"},
    {"Fill", "F", Op::Fill, kVert | kEdge, "Make a face between the picked corners"},
    {"Spin", "", Op::Spin, kEdge, "Spin the picked edges round the part's middle (like a lathe)"},
    {"Merge at Center", "M", Op::Merge, kVert | kMerge, "All picked corners become one, in the middle"},
    {"Collapse", "", Op::Collapse, kMerge, "Each joined group of picked corners becomes one"},
    {"Merge by Distance", "", Op::MergeByDistance, kMerge | kMesh | kVert, "Join corners sitting on top of each other"},
    {"Smooth Vertices", "", Op::SmoothVerts, kVert, "Round off the picked corners"},
    {"Duplicate", "Shift+D", Op::DuplicateMove, kVert | kEdge | kFace | kMesh, "Copy the picked faces and move the copy"},
    {"Separate", "P", Op::Separate, kVert | kEdge | kFace | kMesh, "Move the picked faces into a new part of their own"},
    {"Triangulate Faces", "Ctrl+T", Op::Triangulate, kFace, "Split faces into triangles"},
    {"Tris to Quads", "Alt+J", Op::TrisToQuads, kFace, "Join pairs of triangles into four-sided faces"},
    {"Shade Smooth", "", Op::ShadeSmooth, kFace, "Smooth shading (no hard edges)"},
    {"Shade Flat", "", Op::ShadeFlat, kFace, "Flat shading (every face shows)"},
    {"Flip", "", Op::Flip, kFace | kNormals, "Turn the picked faces (or all) inside out"},
    {"Recalculate Outside", "Shift+N", Op::NormalsOut, kNormals | kMesh, "Make every face point outwards"},
    {"Recalculate Inside", "", Op::NormalsIn, kNormals, "Make every face point inwards"},
    {"Mirror X", "", Op::MirrorX, kMirror | kMesh, "Flip what's picked across its middle (the part's X)"},
    {"Mirror Y", "", Op::MirrorY, kMirror, "Flip what's picked across its middle (the part's Y)"},
    {"Mirror Z", "", Op::MirrorZ, kMirror, "Flip what's picked across its middle (the part's Z)"},
    {"Delete Vertices", "", Op::DeleteVerts, kDel, "The picked corners and every face touching them"},
    {"Delete Edges", "", Op::DeleteEdges, kDel, "The faces on the picked edges"},
    {"Delete Faces", "", Op::DeleteFaces, kDel, "Just the picked faces"},
    {"Dissolve Vertices", "Ctrl+X", Op::DissolveVerts, kDel | kVert, "Take the corners out; the faces round them become one"},
    {"Dissolve Edges", "Ctrl+X", Op::DissolveEdges, kDel | kEdge, "Take the edges out; the faces each side become one"},
    {"Dissolve Faces", "Ctrl+X", Op::DissolveFaces, kDel | kFace, "Faces side by side become one big face"},
    {"Select All", "A", Op::SelectAll, kSel, "Pick everything (again = nothing)"},
    {"Select None", "Alt+A", Op::SelectNone, kSel, ""},
    {"Invert", "Ctrl+I", Op::Invert, kSel, "Swap picked and not picked"},
    {"Select More", "Ctrl+Plus", Op::SelectGrow, kSel, "Grow the picked area by one step"},
    {"Select Less", "Ctrl+Minus", Op::SelectShrink, kSel, "Shrink the picked area by one step"},
    {"Select Linked", "Ctrl+L", Op::SelectLinked, kSel, "Everything joined to what's picked (L picks under the mouse)"},
    {"Select Edge Loop", "Alt+Click", Op::SelectLoop, kSel | kEdge, "The loop of edges running through the picked edge"},
    {"Select Edge Ring", "Ctrl+Alt+Click", Op::SelectRing, kSel | kEdge, "The edges running across the picked edge's faces"},
    {"Select Boundary Loop", "", Op::SelectBoundary, kSel, "Just the outline of the picked faces"},
    {"Select Non-Manifold", "", Op::SelectNonManifold, kSel, "Holes and odd edges (what stops a mesh being solid)"},
    {"Hide", "H", Op::Hide, kMesh, "Hide what's picked, to work round it"},
    {"Hide Unselected", "Shift+H", Op::HideOthers, kMesh, "Hide everything else"},
    {"Reveal Hidden", "Alt+H", Op::Reveal, kMesh, "Show everything again"},
    {"Proportional Editing", "O", Op::Proportional, kMesh, "Moving corners drags nearby ones along (wheel = how far)"},
};

unsigned menuBits(const std::string& name, int selectMode) {
    if (name == "Vertex") return kVert;
    if (name == "Edge") return kEdge;
    if (name == "Face") return kFace;
    if (name == "Select") return kSel;
    if (name == "Delete") return kDel;
    if (name == "Merge") return kMerge;
    if (name == "Mirror") return kMirror;
    if (name == "Normals") return kNormals;
    if (name == "Mesh") return kMesh;
    if (name == "Context") return selectMode == 0 ? kVert : selectMode == 1 ? kEdge : kFace;
    return 0;
}

const char* const kMenuNames[] = {"Context", "Vertex", "Edge", "Face", "Select", "Delete", "Merge", "Mirror", "Normals", "Mesh", "Add", "Search"};

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

} // namespace

bool Editor::meshMenuItems(int menu) {
    const std::string name = kMenuNames[std::clamp(menu, 0, (int)std::size(kMenuNames) - 1)];
    if (name == "Add") {
        for (int i = 0; i < MeshEdit::kAddShapeCount; ++i)
            if (ImGui::MenuItem(MeshEdit::kAddShapeNames[i])) { m_deferred = [this, i] { addMeshShape(i); }; return true; }
        return false;
    }
    unsigned bits = menuBits(name, m_state.modeling.selectMode);
    for (const Action& a : kActions) {
        if (!(a.menus & bits)) continue;
        bool checked = a.op == Op::Proportional && m_state.modeling.proportional;
        if (ImGui::MenuItem(a.name, a.keys, checked)) {
            Op op = a.op;
            m_deferred = [this, op] { meshOp(op); };
            return true;
        }
        if (*a.tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a.tip);
    }
    return false;
}

bool Editor::meshOpNamed(const std::string& name) {
    for (const Action& a : kActions) {
        std::string n = lower(a.name);
        std::replace(n.begin(), n.end(), ' ', '-');
        if (n == name) { meshOp(a.op); return true; }
    }
    return false;
}

// The right-click menu in Modeling mode: the vertex, edge or face menu (by what you're
// picking), then the menus everything else lives in.
void Editor::modelingContextMenu() {
    static const char* kTitles[] = {"Vertex", "Edge", "Face"};
    const int mode = std::clamp(m_state.modeling.selectMode, 0, 2);
    ImGui::TextDisabled("%s Context Menu", kTitles[mode]);
    ImGui::Separator();
    if (meshMenuItems(0)) return;
    ImGui::Separator();
    if (mode != 0 && ImGui::BeginMenu("Vertex")) { meshMenuItems(1); ImGui::EndMenu(); }
    if (mode != 1 && ImGui::BeginMenu("Edge")) { meshMenuItems(2); ImGui::EndMenu(); }
    if (mode != 2 && ImGui::BeginMenu("Face")) { meshMenuItems(3); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Select")) { meshMenuItems(4); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Add")) { meshMenuItems(10); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Mesh")) { meshMenuItems(9); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Normals")) { meshMenuItems(8); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Mirror")) { meshMenuItems(7); ImGui::EndMenu(); }
    if (ImGui::BeginMenu("Delete")) { meshMenuItems(5); ImGui::EndMenu(); }
    ImGui::Separator();
    if (ImGui::MenuItem("Search...", "F3")) m_meshMenu = "Search";
    if (ImGui::MenuItem("Done (Build mode)", "Tab")) m_deferred = [this] { setMode(StudioMode::Build); };
}

void Editor::renderModelingMenus() {
    if (m_state.mode != StudioMode::Modeling) { m_meshMenu.clear(); return; }
    if (!m_meshMenu.empty()) {
        ImGui::OpenPopup(("##meshmenu_" + m_meshMenu).c_str());
        if (m_meshMenu == "Search") m_meshSearch.clear();
        m_meshMenu.clear();
    }
    for (int i = 0; i < (int)std::size(kMenuNames); ++i) {
        const std::string name = kMenuNames[i];
        if (!ImGui::BeginPopup(("##meshmenu_" + name).c_str())) continue;
        if (name == "Search") {
            // F3: type part of a tool's name, Enter runs the first one that matches.
            ImGui::TextDisabled("Search the modeling tools");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(260);
            bool enter = ImGui::InputTextWithHint("##find", "extrude, loop cut, mirror...", &m_meshSearch,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            const std::string q = lower(m_meshSearch);
            int shown = 0;
            for (const Action& a : kActions) {
                if (!q.empty() && lower(a.name).find(q) == std::string::npos) continue;
                if (ImGui::MenuItem(a.name, a.keys) || (enter && shown == 0)) {
                    Op op = a.op;
                    m_deferred = [this, op] { meshOp(op); };
                    ImGui::CloseCurrentPopup();
                    enter = false;
                }
                if (++shown >= 14) break;
            }
            for (int s = 0; s < MeshEdit::kAddShapeCount && shown < 14; ++s) {
                std::string label = std::string("Add ") + MeshEdit::kAddShapeNames[s];
                if (!q.empty() && lower(label).find(q) == std::string::npos) continue;
                if (ImGui::MenuItem(label.c_str(), "Shift+A") || (enter && shown == 0)) {
                    m_deferred = [this, s] { addMeshShape(s); };
                    ImGui::CloseCurrentPopup();
                    enter = false;
                }
                ++shown;
            }
            if (!shown) ImGui::TextDisabled("Nothing called that.");
        } else if (name == "Context") {
            modelingContextMenu();
        } else {
            ImGui::TextDisabled("%s", name.c_str());
            ImGui::Separator();
            meshMenuItems(i);
        }
        ImGui::EndPopup();
    }
}

// Shift+A: a new shape, one stud across, in the middle of what's picked (or of the mesh).
void Editor::addMeshShape(int shapeIndex) {
    SceneNode* n = m_scene->findById(m_state.modeling.node);
    if (!n || !n->editMesh) return;
    ModelingState& ms = m_state.modeling;
    auto src = MeshEdit::shape((MeshEdit::AddShape)std::clamp(shapeIndex, 0, MeshEdit::kAddShapeCount - 1));
    const glm::mat4 M = n->worldMatrix();
    glm::vec3 at(0.0f);
    int count = 0;
    for (size_t i = 0; i < n->editMesh->verts.size() && i < ms.sel.size(); ++i)
        if (ms.sel[i]) { at += glm::vec3(M * glm::vec4(n->editMesh->verts[i], 1.0f)); ++count; }
    at = count ? at / (float)count : glm::vec3(M[3]);
    // One stud across in the world, upright in the part's own directions.
    glm::mat4 rot(1.0f);
    for (int k = 0; k < 3; ++k) rot[k] = glm::vec4(glm::normalize(glm::vec3(M[k])), 0.0f);
    glm::mat4 place = glm::inverse(M) * glm::translate(glm::mat4(1.0f), at) * rot;
    EditMesh& m = MeshEdit::own(*n);
    std::vector<char> hidden = ms.hidden;
    hidden.resize(m.verts.size(), 0);
    MeshEdit::append(m, ms.sel, *src, place);
    hidden.resize(m.verts.size(), 0);
    ms.hidden = std::move(hidden);
    MeshEdit::fit(*n);
    MeshEdit::refresh(*n);
    Log::info(std::string("Added: ") + MeshEdit::kAddShapeNames[shapeIndex] + " (it's picked: G moves it, S scales it).");
}

// Ctrl+J in Build mode: every selected part becomes part of the first one's mesh.
void Editor::joinSelected() {
    std::vector<SceneNode*> parts;
    for (SceneNode* s : m_scene->selection())
        if (s->isPart() && canEdit(s) && !m_scene->isCharacterPart(s) && s->primitiveType != PrimitiveType::None) parts.push_back(s);
    if (parts.size() < 2) { Log::warn("Join: select two or more parts (the first one picked keeps its name)."); return; }
    SceneNode* keep = parts.front();
    if (keep->primitiveType != PrimitiveType::Mesh) MeshEdit::attach(*keep, MeshEdit::fromPrimitive(keep->primitiveType));
    const glm::mat4 toKeep = glm::inverse(keep->worldMatrix());
    EditMesh& m = MeshEdit::own(*keep);
    MeshEdit::Selection sel;
    for (size_t i = 1; i < parts.size(); ++i) {
        SceneNode* p = parts[i];
        std::shared_ptr<EditMesh> src = p->primitiveType == PrimitiveType::Mesh && p->editMesh ? p->editMesh
                                                                                               : MeshEdit::fromPrimitive(p->primitiveType);
        MeshEdit::append(m, sel, *src, toKeep * p->worldMatrix());
    }
    for (size_t i = 1; i < parts.size(); ++i) m_scene->removeNode(parts[i]);
    MeshEdit::fit(*keep);
    MeshEdit::refresh(*keep);
    m_scene->select(keep);
    Log::info("Joined " + std::to_string(parts.size()) + " parts into '" + keep->name + "' (Tab to model it).");
}
