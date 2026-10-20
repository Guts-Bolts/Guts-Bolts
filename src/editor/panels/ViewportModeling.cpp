// The Viewport in Modeling mode: pick corners (vertices), edges or faces of
// one part's mesh and move, turn or stretch them with the gizmo, like
// Blender's Edit Mode. G / R / S (and E, I, Shift+D, Ctrl+R) run "modal"
// tools here that follow the mouse until you click.
#include "ViewportPanel.h"
#include "../EditorState.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../scene/EditMesh.h"
#include "../../core/Log.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace {

const ImU32 kPicked   = IM_COL32(255, 150, 40, 255);
const ImU32 kWire     = IM_COL32(20, 20, 24, 220);
const ImU32 kWireBack = IM_COL32(20, 20, 24, 70);
const ImU32 kSeam     = IM_COL32(235, 40, 40, 255);
const ImU32 kSeamBack = IM_COL32(235, 40, 40, 80);

float segmentDistance(ImVec2 p, ImVec2 a, ImVec2 b) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0f, 1.0f) : 0.0f;
    float ex = a.x + dx * t - p.x, ey = a.y + dy * t - p.y;
    return std::sqrt(ex * ex + ey * ey);
}

// Ray against a triangle (Moller-Trumbore). Returns the distance along the ray.
bool rayTriangle(glm::vec3 ro, glm::vec3 rd, glm::vec3 a, glm::vec3 b, glm::vec3 c, float& t) {
    glm::vec3 e1 = b - a, e2 = c - a, p = glm::cross(rd, e2);
    float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-10f) return false;
    float inv = 1.0f / det;
    glm::vec3 s = ro - a;
    float u = glm::dot(s, p) * inv;
    if (u < 0 || u > 1) return false;
    glm::vec3 q = glm::cross(s, e1);
    float v = glm::dot(rd, q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = glm::dot(e2, q) * inv;
    return t > 0;
}

// Where on the line P + s*A the mouse ray passes closest (the s).
float lineParam(glm::vec3 P, glm::vec3 A, glm::vec3 ro, glm::vec3 rd) {
    glm::vec3 w0 = P - ro;
    float b = glm::dot(A, rd), d = glm::dot(A, w0), e = glm::dot(rd, w0);
    float denom = 1.0f - b * b;
    return std::abs(denom) < 1e-6f ? 0.0f : (b * e - d) / denom;
}

// Where the ray hits the flat plane through P facing N (P if it runs along it).
glm::vec3 rayPlane(glm::vec3 ro, glm::vec3 rd, glm::vec3 P, glm::vec3 N) {
    float denom = glm::dot(rd, N);
    if (std::abs(denom) < 1e-6f) return P;
    return ro + rd * (glm::dot(P - ro, N) / denom);
}

// A number typed while moving ("", "-", "." don't count yet).
bool typedNumber(const std::string& s, float& out) {
    if (s.empty() || s == "-" || s == "." || s == "-.") return false;
    out = std::strtof(s.c_str(), nullptr);
    return true;
}

} // namespace

void ViewportPanel::modelingView(const glm::mat4& view, const glm::mat4& proj,
                                 const glm::vec2& imgMin, const glm::vec2& imgSize) {
    ModelingState& ms = m_state->modeling;
    SceneNode* node = m_scene->findById(ms.node);
    if (!node || !node->editMesh) return;
    if (ms.sel.size() != node->editMesh->verts.size()) ms.sel.assign(node->editMesh->verts.size(), 0);   // e.g. after undo
    ms.hidden.resize(node->editMesh->verts.size(), 0);
    // A tool following the mouse (G, R, S...) changes the mesh first; then it's drawn as usual.
    const bool modal = modelingModal(*node, view, proj, imgMin, imgSize);
    const EditMesh& m = *node->editMesh;
    if (ms.sel.size() != m.verts.size()) ms.sel.resize(m.verts.size(), 0);
    if (ms.hidden.size() != m.verts.size()) ms.hidden.resize(m.verts.size(), 0);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const glm::mat4 M = node->worldMatrix();
    const glm::mat4 vp = proj * view;
    const glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);

    // Where everything is, in the world and on screen.
    const size_t nv = m.verts.size();
    std::vector<glm::vec3> world(nv);
    std::vector<ImVec2> screen(nv);
    std::vector<char> onScreen(nv, 0);
    for (size_t i = 0; i < nv; ++i) {
        world[i] = glm::vec3(M * glm::vec4(m.verts[i], 1.0f));
        glm::vec4 c = vp * glm::vec4(world[i], 1.0f);
        if (c.w <= 0.01f) continue;
        screen[i] = ImVec2(imgMin.x + (c.x / c.w * 0.5f + 0.5f) * imgSize.x, imgMin.y + (0.5f - c.y / c.w * 0.5f) * imgSize.y);
        onScreen[i] = 1;
    }
    // Which faces point at the camera (the others are round the back).
    std::vector<char> front(m.faces.size(), 1);
    std::vector<char> vertFront(nv, 0);
    for (size_t f = 0; f < m.faces.size(); ++f) {
        const auto& face = m.faces[f];
        glm::vec3 n(0.0f), c(0.0f);
        for (size_t i = 0; i < face.size(); ++i) {
            n += glm::cross(world[face[i]], world[face[(i + 1) % face.size()]]);
            c += world[face[i]];
        }
        c /= (float)face.size();
        front[f] = glm::dot(n, eye - c) > 0.0f;
        if (front[f]) for (uint32_t v : face) vertFront[v] = 1;
    }
    auto visibleVert = [&](uint32_t v) { return onScreen[v] && !ms.hidden[v] && (ms.xray || vertFront[v]); };
    auto hiddenFace = [&](const std::vector<uint32_t>& face) {
        for (uint32_t v : face) if (ms.hidden[v]) return true;
        return false;
    };

    // --- Draw: picked faces, edges, then corners ---
    for (size_t f = 0; f < m.faces.size(); ++f) {
        const auto& face = m.faces[f];
        bool all = true, shown = true;
        for (uint32_t v : face) { if (!ms.sel[v]) all = false; if (!onScreen[v]) shown = false; }
        if (!shown || (!ms.xray && !front[f]) || hiddenFace(face)) continue;
        if (all) {
            std::vector<ImVec2> pts;
            for (uint32_t v : face) pts.push_back(screen[v]);
            for (size_t k = 1; k + 1 < pts.size(); ++k)   // fan: works for concave faces too
                dl->AddTriangleFilled(pts[0], pts[k], pts[k + 1], IM_COL32(255, 150, 40, 70));
        }
        if (ms.selectMode == 2) {
            glm::vec3 c(0.0f);
            for (uint32_t v : face) c += world[v];
            glm::vec4 cc = vp * glm::vec4(c / (float)face.size(), 1.0f);
            if (cc.w > 0.01f) {
                ImVec2 sp(imgMin.x + (cc.x / cc.w * 0.5f + 0.5f) * imgSize.x, imgMin.y + (0.5f - cc.y / cc.w * 0.5f) * imgSize.y);
                dl->AddRectFilled(ImVec2(sp.x - 2.5f, sp.y - 2.5f), ImVec2(sp.x + 2.5f, sp.y + 2.5f), all ? kPicked : kWire);
            }
        }
    }
    auto edgeList = MeshEdit::edges(m);
    std::vector<char> edgeFront(edgeList.size(), 0);
    {
        // An edge shows if one of its faces faces us.
        std::vector<std::pair<uint64_t, char>> keys;
        for (size_t f = 0; f < m.faces.size(); ++f) {
            const auto& face = m.faces[f];
            for (size_t i = 0; i < face.size(); ++i) {
                uint32_t a = face[i], b = face[(i + 1) % face.size()];
                if (a > b) std::swap(a, b);
                keys.push_back({((uint64_t)a << 32) | b, front[f]});
            }
        }
        std::sort(keys.begin(), keys.end());
        for (size_t e = 0; e < edgeList.size(); ++e) {
            uint64_t k = ((uint64_t)edgeList[e].first << 32) | edgeList[e].second;
            auto it = std::lower_bound(keys.begin(), keys.end(), std::make_pair(k, (char)0));
            for (; it != keys.end() && it->first == k; ++it) if (it->second) edgeFront[e] = 1;
        }
    }
    for (size_t e = 0; e < edgeList.size(); ++e) {
        auto [a, b] = edgeList[e];
        if (!onScreen[a] || !onScreen[b] || ms.hidden[a] || ms.hidden[b]) continue;
        bool back = !edgeFront[e];
        if (back && !ms.xray) continue;
        bool picked = ms.sel[a] && ms.sel[b];
        bool seam = !m.seams.empty() && MeshEdit::isSeam(m, a, b);   // UV seams show red, like Blender
        ImU32 col = picked ? kPicked : seam ? (back ? kSeamBack : kSeam) : (back ? kWireBack : kWire);
        dl->AddLine(screen[a], screen[b], col, picked || seam ? 2.2f : 1.4f);
    }
    if (ms.selectMode == 0)
        for (size_t i = 0; i < nv; ++i)
            if (visibleVert((uint32_t)i)) dl->AddCircleFilled(screen[i], ms.sel[i] ? 4.0f : 3.0f, ms.sel[i] ? kPicked : kWire);

    if (modal || m_modalOn) return;   // (a modal tool has the mouse)

    // --- Gizmo on the picked points ---
    int picked = MeshEdit::countSelected(ms.sel);
    bool gizmoBusy = false;
    if (picked > 0 && m_state->tool != GizmoTool::Select) {
        glm::vec3 pivot(0.0f);
        for (size_t i = 0; i < nv; ++i) if (ms.sel[i]) pivot += world[i];
        pivot /= (float)picked;
        if (!ImGuizmo::IsUsing()) {
            glm::mat4 g(1.0f);
            if (m_state->gizmoLocal)
                for (int k = 0; k < 3; ++k) g[k] = glm::vec4(glm::normalize(glm::vec3(M[k])), 0.0f);
            g[3] = glm::vec4(pivot, 1.0f);
            m_meshGizmo = g;
        }
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSize.x, imgSize.y);
        ImGuizmo::OPERATION op = m_state->tool == GizmoTool::Rotate ? ImGuizmo::ROTATE
                               : m_state->tool == GizmoTool::Scale  ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;
        float snap[3] = {0, 0, 0};
        if (m_state->snapEnabled) {
            float s = op == ImGuizmo::TRANSLATE ? m_state->snapTranslate : op == ImGuizmo::ROTATE ? m_state->snapRotate
                                                                                                  : m_state->snapScale;
            snap[0] = snap[1] = snap[2] = s;
        }
        glm::mat4 before = m_meshGizmo;
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op,
                                 m_state->gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                                 glm::value_ptr(m_meshGizmo), nullptr, m_state->snapEnabled ? snap : nullptr)) {
            glm::mat4 delta = m_meshGizmo * glm::inverse(before);
            glm::mat4 toLocal = glm::inverse(M);
            EditMesh& em = MeshEdit::own(*node);
            for (size_t i = 0; i < nv; ++i)
                if (ms.sel[i]) em.verts[i] = glm::vec3(toLocal * delta * glm::vec4(world[i], 1.0f));
            MeshEdit::refresh(*node);
        }
        gizmoBusy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    }
    // Finished dragging: wrap the part's box around the new shape.
    bool using_ = ImGuizmo::IsUsing();
    if (m_meshDragging && !using_) MeshEdit::fit(*node);
    m_meshDragging = using_;

    // --- Picking: click, or drag a box ---
    ImVec2 mouse = io.MousePos;
    // The nearest edge to the mouse (for Alt+click loops and L), or -1.
    auto edgeUnderMouse = [&](float within) {
        int best = -1;
        float bestD = within;
        for (size_t e = 0; e < edgeList.size(); ++e) {
            auto [a, b] = edgeList[e];
            if (!onScreen[a] || !onScreen[b] || ms.hidden[a] || ms.hidden[b] || (!ms.xray && !edgeFront[e])) continue;
            float d = segmentDistance(mouse, screen[a], screen[b]);
            if (d < bestD) { bestD = d; best = (int)e; }
        }
        return best;
    };
    // L: pick everything joined to what's under the mouse (Shift+L: un-pick it).
    if (m_hovered && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_L, false) &&
        ms.modalEnded != ImGui::GetFrameCount()) {
        int e = edgeUnderMouse(40.0f);
        if (e >= 0) {
            MeshEdit::Selection one(nv, 0);
            one[edgeList[e].first] = 1;
            MeshEdit::selectLinked(m, one);
            for (size_t i = 0; i < nv; ++i) if (one[i] && !ms.hidden[i]) ms.sel[i] = io.KeyShift ? 0 : 1;
        }
    }
    if (m_hovered && !gizmoBusy && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_boxStart = mouse;
        m_boxing = true;
    }
    if (!m_boxing) return;
    float dragX = mouse.x - m_boxStart.x, dragY = mouse.y - m_boxStart.y;
    bool isBox = dragX * dragX + dragY * dragY > 25.0f;
    ImVec2 lo(std::min(mouse.x, m_boxStart.x), std::min(mouse.y, m_boxStart.y));
    ImVec2 hi(std::max(mouse.x, m_boxStart.x), std::max(mouse.y, m_boxStart.y));
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (isBox) {
            dl->AddRectFilled(lo, hi, IM_COL32(255, 255, 255, 25));
            dl->AddRect(lo, hi, IM_COL32(255, 255, 255, 170), 0, 0, 1.0f);
        }
        return;
    }
    m_boxing = false;

    // Alt+click: the edge loop through the edge under the mouse; Ctrl+Alt+click: its ring.
    // Shift too adds to what's picked (like Blender).
    if (io.KeyAlt && !isBox) {
        int e = edgeUnderMouse(14.0f);
        if (e < 0) return;
        auto [a, b] = edgeList[e];
        if (!io.KeyShift) std::fill(ms.sel.begin(), ms.sel.end(), 0);
        if (io.KeyCtrl) for (auto [x, y] : MeshEdit::edgeRing(m, a, b)) { ms.sel[x] = ms.sel[y] = 1; }
        else for (uint32_t v : MeshEdit::edgeLoop(m, a, b)) ms.sel[v] = 1;
        for (size_t i = 0; i < nv; ++i) if (ms.hidden[i]) ms.sel[i] = 0;
        return;
    }

    // Shift adds / toggles, Ctrl takes away, a plain click starts over.
    int how = io.KeyCtrl ? 2 : io.KeyShift ? 1 : 0;
    if (how == 0) std::fill(ms.sel.begin(), ms.sel.end(), 0);
    auto setVerts = [&](const std::vector<uint32_t>& vs, bool toggle) {
        bool allOn = true;
        for (uint32_t v : vs) if (!ms.sel[v]) allOn = false;
        char val = how == 2 ? 0 : (toggle && how == 1 && allOn ? 0 : 1);
        for (uint32_t v : vs) ms.sel[v] = val;
    };
    auto inBox = [&](ImVec2 p) { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y; };

    if (isBox) {
        if (ms.selectMode == 0) {
            for (uint32_t i = 0; i < nv; ++i) if (visibleVert(i) && inBox(screen[i])) setVerts({i}, false);
        } else if (ms.selectMode == 1) {
            for (size_t e = 0; e < edgeList.size(); ++e) {
                auto [a, b] = edgeList[e];
                if ((ms.xray || edgeFront[e]) && !ms.hidden[a] && !ms.hidden[b] && onScreen[a] && onScreen[b] && inBox(screen[a]) && inBox(screen[b]))
                    setVerts({a, b}, false);
            }
        } else {
            for (size_t f = 0; f < m.faces.size(); ++f) {
                if ((!ms.xray && !front[f]) || hiddenFace(m.faces[f])) continue;
                bool inside = true;
                for (uint32_t v : m.faces[f]) if (!onScreen[v] || !inBox(screen[v])) inside = false;
                if (inside) setVerts(m.faces[f], false);
            }
        }
        return;
    }

    // A single click: the nearest thing under the mouse.
    if (ms.selectMode == 0) {
        int best = -1;
        float bestD = 12.0f;
        for (uint32_t i = 0; i < nv; ++i) {
            if (!visibleVert(i)) continue;
            float d = std::hypot(screen[i].x - mouse.x, screen[i].y - mouse.y);
            if (d < bestD) { bestD = d; best = (int)i; }
        }
        if (best >= 0) setVerts({(uint32_t)best}, true);
    } else if (ms.selectMode == 1) {
        int best = -1;
        float bestD = 9.0f;
        for (size_t e = 0; e < edgeList.size(); ++e) {
            auto [a, b] = edgeList[e];
            if (!onScreen[a] || !onScreen[b] || ms.hidden[a] || ms.hidden[b] || (!ms.xray && !edgeFront[e])) continue;
            float d = segmentDistance(mouse, screen[a], screen[b]);
            if (d < bestD) { bestD = d; best = (int)e; }
        }
        if (best >= 0) setVerts({edgeList[best].first, edgeList[best].second}, true);
    } else {
        glm::vec3 ro, rd;
        mouseRay({mouse.x, mouse.y}, imgMin, imgSize, view, proj, ro, rd);
        int best = -1;
        float bestT = 1e30f;
        for (size_t f = 0; f < m.faces.size(); ++f) {
            if ((!ms.xray && !front[f]) || hiddenFace(m.faces[f])) continue;
            const auto& face = m.faces[f];
            for (size_t k = 1; k + 1 < face.size(); ++k) {
                float t;
                if (rayTriangle(ro, rd, world[face[0]], world[face[k]], world[face[k + 1]], t) && t < bestT) {
                    bestT = t;
                    best = (int)f;
                }
            }
        }
        if (best >= 0) setVerts(m.faces[best], true);
    }
}

bool ViewportPanel::modelingModal(SceneNode& node, const glm::mat4& view, const glm::mat4& proj,
                                  const glm::vec2& imgMin, const glm::vec2& imgSize) {
    ModelingState& ms = m_state->modeling;
    ImGuiIO& io = ImGui::GetIO();
    if (ms.modal == MeshModal::None || !node.editMesh) { m_modalOn = false; return false; }
    const glm::mat4 M = node.worldMatrix();
    const glm::mat4 vp = proj * view;
    const glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
    auto toScreen = [&](glm::vec3 w, ImVec2& out) {
        glm::vec4 c = vp * glm::vec4(w, 1.0f);
        if (c.w <= 0.01f) return false;
        out = ImVec2(imgMin.x + (c.x / c.w * 0.5f + 0.5f) * imgSize.x, imgMin.y + (0.5f - c.y / c.w * 0.5f) * imgSize.y);
        return true;
    };

    // --- Starting: remember how everything was ---
    if (!m_modalOn) {
        m_modalOn = true;
        m_modalBefore = std::make_shared<EditMesh>(*node.editMesh);
        m_modalSel = ms.sel;
        m_modalSel.resize(m_modalBefore->verts.size(), 0);
        m_modalMouse0 = io.MousePos;
        glm::vec3 c(0.0f);
        int n = 0;
        for (size_t i = 0; i < m_modalBefore->verts.size(); ++i)
            if (m_modalSel[i]) { c += glm::vec3(M * glm::vec4(m_modalBefore->verts[i], 1.0f)); ++n; }
        m_modalPivot = n ? c / (float)n : glm::vec3(M[3]);
        m_modalAxis = glm::length(ms.modalAxis) > 0.5f ? 3 : -1;
        m_modalPlane = m_modalLocal = false;
        m_modalTyped.clear();
        m_modalAngle = 0.0f;
        ImVec2 sp;
        m_modalLastAngle = toScreen(m_modalPivot, sp) ? std::atan2(io.MousePos.y - sp.y, io.MousePos.x - sp.x) : 0.0f;
        m_modalWeightRadius = -1.0f;
        if (ms.modal == MeshModal::LoopCut) m_loopCuts = 1;
    }
    const EditMesh& before = *m_modalBefore;
    const size_t nv = before.verts.size();
    auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };

    auto finish = [&](bool keep) {
        if (!keep) {
            MeshEdit::own(node) = *m_modalBefore;
            ms.sel = m_modalSel;
        }
        MeshEdit::fit(node);
        MeshEdit::refresh(node);
        ms.modal = MeshModal::None;
        ms.modalAxis = glm::vec3(0.0f);
        ms.modalEnded = ImGui::GetFrameCount();
        m_modalOn = false;
        m_modalBefore.reset();
        m_boxing = false;
    };
    const bool cancel = key(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) m_eatRightClick = true;
    const bool confirm = key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter) || ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (cancel) { finish(false); return true; }

    // --- Typing a number ---
    for (int k = 0; k < 10; ++k)
        if (key((ImGuiKey)(ImGuiKey_0 + k)) || key((ImGuiKey)(ImGuiKey_Keypad0 + k))) m_modalTyped += (char)('0' + k);
    if ((key(ImGuiKey_Period) || key(ImGuiKey_KeypadDecimal)) && m_modalTyped.find('.') == std::string::npos) m_modalTyped += '.';
    if (key(ImGuiKey_Minus) || key(ImGuiKey_KeypadSubtract)) {
        if (!m_modalTyped.empty() && m_modalTyped[0] == '-') m_modalTyped.erase(0, 1);
        else m_modalTyped.insert(0, "-");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !m_modalTyped.empty()) m_modalTyped.pop_back();
    float typed = 0.0f;
    const bool hasTyped = typedNumber(m_modalTyped, typed);

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->PushClipRect(ImVec2(imgMin.x, imgMin.y), ImVec2(imgMin.x + imgSize.x, imgMin.y + imgSize.y), true);
    char header[256] = "";
    const char* help = "";

    // ======================= Loop cut =======================
    if (ms.modal == MeshModal::LoopCut) {
        if (io.MouseWheel > 0.0f || ImGui::IsKeyPressed(ImGuiKey_PageUp)) m_loopCuts = std::min(64, m_loopCuts + 1);
        if (io.MouseWheel < 0.0f || ImGui::IsKeyPressed(ImGuiKey_PageDown)) m_loopCuts = std::max(1, m_loopCuts - 1);
        if (hasTyped) m_loopCuts = std::clamp((int)typed, 1, 64);
        // The edge under the mouse.
        int bestA = -1, bestB = -1;
        float bestD = 30.0f;
        std::vector<ImVec2> scr(nv);
        std::vector<char> ok(nv, 0);
        for (size_t i = 0; i < nv; ++i) ok[i] = toScreen(glm::vec3(M * glm::vec4(before.verts[i], 1.0f)), scr[i]);
        for (auto [a, b] : MeshEdit::edges(before)) {
            if (!ok[a] || !ok[b] || (a < ms.hidden.size() && ms.hidden[a]) || (b < ms.hidden.size() && ms.hidden[b])) continue;
            float d = segmentDistance(io.MousePos, scr[a], scr[b]);
            if (d < bestD) { bestD = d; bestA = (int)a; bestB = (int)b; }
        }
        if (bestA >= 0) {
            // Show where the new loops will go: lines across every face of the ring.
            auto ring = MeshEdit::edgeRing(before, (uint32_t)bestA, (uint32_t)bestB);
            std::set<uint64_t> ringKeys;
            for (auto [u, v] : ring) ringKeys.insert(((uint64_t)std::min(u, v) << 32) | std::max(u, v));
            auto isRing = [&](uint32_t u, uint32_t v) { return ringKeys.count(((uint64_t)std::min(u, v) << 32) | std::max(u, v)) != 0; };
            for (const auto& f : before.faces) {
                if (f.size() != 4) continue;
                for (int i = 0; i < 2; ++i) {
                    if (!isRing(f[i], f[i + 1]) || !isRing(f[i + 2], f[(i + 3) % 4])) continue;
                    for (int k = 1; k <= m_loopCuts; ++k) {
                        float t = (float)k / (m_loopCuts + 1);
                        glm::vec3 pa = glm::mix(before.verts[f[i]], before.verts[f[i + 1]], t);
                        glm::vec3 pb = glm::mix(before.verts[f[(i + 3) % 4]], before.verts[f[i + 2]], t);
                        ImVec2 sa, sb;
                        if (toScreen(glm::vec3(M * glm::vec4(pa, 1.0f)), sa) && toScreen(glm::vec3(M * glm::vec4(pb, 1.0f)), sb))
                            fg->AddLine(sa, sb, IM_COL32(255, 220, 40, 255), 2.0f);
                    }
                }
            }
            fg->AddLine(scr[bestA], scr[bestB], IM_COL32(255, 255, 255, 200), 3.0f);
        }
        std::snprintf(header, sizeof(header), "Loop Cut   %d cut%s", m_loopCuts, m_loopCuts == 1 ? "" : "s");
        help = "point at an edge, wheel / type = how many, click = cut, right-click = cancel";
        if (confirm && bestA >= 0) {
            EditMesh work = before;
            MeshEdit::Selection sel = ms.sel;
            if (MeshEdit::loopCut(work, sel, (uint32_t)bestA, (uint32_t)bestB, m_loopCuts)) {
                MeshEdit::own(node) = std::move(work);
                ms.sel = std::move(sel);
                ms.hidden.resize(ms.sel.size(), 0);
                ms.selectMode = 1;
                finish(true);
                fg->PopClipRect();
                return true;
            }
        } else if (confirm && !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            finish(false);   // Enter with nothing under the mouse
            fg->PopClipRect();
            return true;
        }
    }

    // ======================= Inset =======================
    else if (ms.modal == MeshModal::Inset) {
        ImVec2 sp;
        float amount = 0.0f;
        if (toScreen(m_modalPivot, sp)) {
            float d0 = std::max(60.0f, std::hypot(m_modalMouse0.x - sp.x, m_modalMouse0.y - sp.y));
            float d = std::hypot(io.MousePos.x - sp.x, io.MousePos.y - sp.y);
            amount = 0.25f + (d0 - d) / d0;   // moving in = a thicker rim
            fg->AddLine(sp, io.MousePos, IM_COL32(255, 255, 255, 120), 1.0f);
        }
        if (hasTyped) amount = typed;
        if (io.KeyCtrl) amount = std::round(amount * 20.0f) / 20.0f;
        amount = std::clamp(amount, 0.0f, 0.99f);
        EditMesh work = before;
        MeshEdit::Selection sel = m_modalSel;
        MeshEdit::inset(work, sel, amount);
        MeshEdit::own(node) = std::move(work);
        ms.sel = std::move(sel);
        ms.hidden.resize(ms.sel.size(), 0);
        MeshEdit::refresh(node);
        std::snprintf(header, sizeof(header), "Inset   %.2f%s", amount, hasTyped ? " (typed)" : "");
        help = "mouse in = thicker rim, type 0 - 1, Ctrl = steps, click = done, right-click = cancel";
        if (confirm) { finish(true); fg->PopClipRect(); return true; }
    }

    // ======================= Grab / Rotate / Scale =======================
    else {
        // Axis keys: X / Y / Z lock to that axis (again = the part's own axis, again = free);
        // Shift+X / Y / Z keeps it off that axis (moves in the other two).
        const ImGuiKey axisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
        for (int a = 0; a < 3; ++a) {
            if (!key(axisKeys[a])) continue;
            if (io.KeyShift) { m_modalAxis = a; m_modalPlane = true; m_modalLocal = false; }
            else if (m_modalAxis == a && !m_modalPlane) {
                if (!m_modalLocal) m_modalLocal = true;
                else { m_modalAxis = -1; m_modalLocal = false; }
            } else { m_modalAxis = a; m_modalPlane = false; m_modalLocal = false; }
        }
        // Proportional editing: the wheel sets how far it reaches.
        if (ms.proportional) {
            if (io.MouseWheel > 0.0f || ImGui::IsKeyPressed(ImGuiKey_PageUp)) ms.propRadius *= 1.15f;
            if (io.MouseWheel < 0.0f || ImGui::IsKeyPressed(ImGuiKey_PageDown)) ms.propRadius /= 1.15f;
            ms.propRadius = std::clamp(ms.propRadius, 0.05f, 500.0f);
        }
        if (key(ImGuiKey_O)) ms.proportional = !ms.proportional;

        std::vector<glm::vec3> W(nv);
        for (size_t i = 0; i < nv; ++i) W[i] = glm::vec3(M * glm::vec4(before.verts[i], 1.0f));
        // How much each corner follows: picked ones fully, others by distance (proportional).
        if (!ms.proportional) m_modalWeight.assign(nv, 0.0f);
        else if (m_modalWeight.size() != nv || m_modalWeightRadius != ms.propRadius) {
            m_modalWeight.assign(nv, 0.0f);
            std::vector<size_t> picked;
            for (size_t i = 0; i < nv; ++i) if (m_modalSel[i]) picked.push_back(i);
            for (size_t i = 0; i < nv; ++i) {
                if (m_modalSel[i] || (i < ms.hidden.size() && ms.hidden[i])) continue;
                float d = 1e30f;
                for (size_t j : picked) d = std::min(d, glm::length(W[i] - W[j]));
                if (d >= ms.propRadius) continue;
                float t = 1.0f - d / ms.propRadius;
                m_modalWeight[i] = t * t * (3.0f - 2.0f * t);
            }
            m_modalWeightRadius = ms.propRadius;
        }
        auto weight = [&](size_t i) { return m_modalSel[i] ? 1.0f : m_modalWeight[i]; };

        glm::vec3 A(0.0f);
        if (m_modalAxis == 3) A = ms.modalAxis;
        else if (m_modalAxis >= 0) A = m_modalLocal ? glm::normalize(glm::vec3(M[m_modalAxis])) : glm::vec3(m_modalAxis == 0, m_modalAxis == 1, m_modalAxis == 2);
        const glm::vec3 V = glm::normalize(m_modalPivot - eye);   // the way the camera looks at it
        static const char* kAxisName[4] = {"X", "Y", "Z", "out"};
        char axisText[48] = "";
        if (m_modalAxis >= 0)
            std::snprintf(axisText, sizeof(axisText), "  along %s%s%s", m_modalPlane ? "not " : "", kAxisName[m_modalAxis],
                          m_modalAxis < 3 ? (m_modalLocal ? " (local)" : " (world)") : "");
        // Draw the axis it's locked to.
        if (m_modalAxis >= 0) {
            ImVec2 a, b;
            const ImU32 col = m_modalAxis == 0 ? IM_COL32(230, 70, 70, 220) : m_modalAxis == 1 ? IM_COL32(110, 210, 80, 220)
                            : m_modalAxis == 2 ? IM_COL32(80, 140, 240, 220) : IM_COL32(255, 210, 60, 220);
            if (toScreen(m_modalPivot - A * 500.0f, a) && toScreen(m_modalPivot + A * 500.0f, b)) fg->AddLine(a, b, col, 1.5f);
            else if (toScreen(m_modalPivot - A * 5.0f, a) && toScreen(m_modalPivot + A * 5.0f, b)) fg->AddLine(a, b, col, 1.5f);
        }

        glm::vec3 delta(0.0f);
        float angle = 0.0f, factor = 1.0f;
        glm::vec3 rotAxis = V;
        ImVec2 sp;
        const bool pivotOn = toScreen(m_modalPivot, sp);
        if (ms.modal == MeshModal::Grab) {
            glm::vec3 ro, rd, ro0, rd0;
            mouseRay({io.MousePos.x, io.MousePos.y}, imgMin, imgSize, view, proj, ro, rd);
            mouseRay({m_modalMouse0.x, m_modalMouse0.y}, imgMin, imgSize, view, proj, ro0, rd0);
            const float step = m_state->snapTranslate > 0.0f ? m_state->snapTranslate : 0.5f;
            if (m_modalAxis >= 0 && !m_modalPlane) {
                float s = hasTyped ? typed : lineParam(m_modalPivot, A, ro, rd) - lineParam(m_modalPivot, A, ro0, rd0);
                if (io.KeyCtrl && !hasTyped) s = std::round(s / step) * step;
                delta = A * s;
            } else {
                glm::vec3 N = m_modalPlane ? A : V;
                delta = rayPlane(ro, rd, m_modalPivot, N) - rayPlane(ro0, rd0, m_modalPivot, N);
                if (m_modalPlane) delta -= A * glm::dot(delta, A);
                if (hasTyped) delta = glm::vec3(typed, 0.0f, 0.0f);
                if (io.KeyCtrl && !hasTyped) delta = glm::round(delta / step) * step;
            }
            std::snprintf(header, sizeof(header), "Move   D: %.3f  (%.3f, %.3f, %.3f)%s%s", glm::length(delta), delta.x, delta.y, delta.z,
                          axisText, hasTyped ? "  [typed]" : "");
        } else if (ms.modal == MeshModal::Rotate) {
            if (pivotOn) {
                float a = std::atan2(io.MousePos.y - sp.y, io.MousePos.x - sp.x);
                float d = a - m_modalLastAngle;
                while (d > 3.14159265f) d -= 6.2831853f;
                while (d < -3.14159265f) d += 6.2831853f;
                m_modalAngle += d;
                m_modalLastAngle = a;
                fg->AddLine(sp, io.MousePos, IM_COL32(255, 255, 255, 120), 1.0f);
            }
            angle = m_modalAngle;
            if (m_modalAxis >= 0) { rotAxis = A; if (glm::dot(A, V) < 0.0f) angle = -angle; }
            if (hasTyped) angle = glm::radians(typed);
            if (io.KeyCtrl && !hasTyped) angle = glm::radians(std::round(glm::degrees(angle) / 5.0f) * 5.0f);
            std::snprintf(header, sizeof(header), "Rotate   %.1f deg%s%s", glm::degrees(m_modalAxis >= 0 && glm::dot(A, V) < 0.0f && !hasTyped ? -angle : angle),
                          axisText, hasTyped ? "  [typed]" : "");
        } else {
            if (pivotOn) {
                float d0 = std::max(1.0f, std::hypot(m_modalMouse0.x - sp.x, m_modalMouse0.y - sp.y));
                factor = std::hypot(io.MousePos.x - sp.x, io.MousePos.y - sp.y) / d0;
                fg->AddLine(sp, io.MousePos, IM_COL32(255, 255, 255, 120), 1.0f);
            }
            if (hasTyped) factor = typed;
            if (io.KeyCtrl && !hasTyped) factor = std::round(factor * 10.0f) / 10.0f;
            std::snprintf(header, sizeof(header), "Scale   %.3f%s%s", factor, axisText, hasTyped ? "  [typed]" : "");
        }
        help = "X/Y/Z = axis (twice = local), Shift+X = not X, type a number, Ctrl = steps, O = proportional,\n"
               "click / Enter = done, right-click / Esc = cancel";

        // Put every corner where the move takes it.
        const glm::mat4 toLocal = glm::inverse(M);
        EditMesh& em = MeshEdit::own(node);
        em.verts = before.verts;
        for (size_t i = 0; i < nv; ++i) {
            float w = weight(i);
            if (w <= 0.0f) continue;
            glm::vec3 p = W[i];
            if (ms.modal == MeshModal::Grab) p += delta * w;
            else if (ms.modal == MeshModal::Rotate) {
                glm::mat4 R = glm::rotate(glm::mat4(1.0f), angle * w, rotAxis);
                p = m_modalPivot + glm::vec3(R * glm::vec4(p - m_modalPivot, 0.0f));
            } else {
                float f = 1.0f + (factor - 1.0f) * w;
                glm::mat3 S(f);
                if (m_modalAxis >= 0 && !m_modalPlane) S = glm::mat3(1.0f) + (f - 1.0f) * glm::outerProduct(A, A);
                else if (m_modalAxis >= 0) S = glm::mat3(f) + (1.0f - f) * glm::outerProduct(A, A);
                p = m_modalPivot + S * (p - m_modalPivot);
            }
            em.verts[i] = glm::vec3(toLocal * glm::vec4(p, 1.0f));
        }
        MeshEdit::refresh(node);
        // Proportional editing: a circle showing how far it reaches.
        if (ms.proportional && pivotOn) {
            glm::vec3 right = glm::normalize(glm::cross(V, glm::vec3(0, 1, 0)));
            if (glm::length(right) < 0.5f) right = glm::vec3(1, 0, 0);
            ImVec2 edge;
            if (toScreen(m_modalPivot + right * ms.propRadius, edge))
                fg->AddCircle(sp, std::hypot(edge.x - sp.x, edge.y - sp.y), IM_COL32(255, 255, 255, 150), 48, 1.5f);
        }
        if (confirm) { finish(true); fg->PopClipRect(); return true; }
    }

    // The header along the top of the view, like Blender's.
    {
        ImVec2 at(imgMin.x + 12.0f, imgMin.y + 44.0f);
        ImVec2 hs = ImGui::CalcTextSize(header), ts = ImGui::CalcTextSize(help);
        float w = std::max(hs.x, ts.x) + 20.0f;
        fg->AddRectFilled(at, ImVec2(at.x + w, at.y + hs.y + ts.y + 16.0f), IM_COL32(25, 25, 28, 225), 5.0f);
        fg->AddText(ImVec2(at.x + 10, at.y + 5), IM_COL32(255, 200, 90, 255), header);
        fg->AddText(ImVec2(at.x + 10, at.y + 9 + hs.y), IM_COL32(210, 210, 210, 220), help);
    }
    fg->PopClipRect();
    return true;
}
