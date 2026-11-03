// Build mode's Blender keys (Object Mode): G moves, R rotates and S scales what's
// selected by following the mouse, until you click (or press Enter). X / Y / Z lock
// it to an axis (twice = the object's own axis, Shift+X = everything but X), a typed
// number sets the amount, Ctrl goes in steps, and right-click or Esc puts it back.
#include "ViewportPanel.h"
#include "../EditorState.h"
#include "../../scene/Animation.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

// Where on the line P + s*A the mouse ray passes closest (the s).
float objLineParam(glm::vec3 P, glm::vec3 A, glm::vec3 ro, glm::vec3 rd) {
    glm::vec3 w0 = P - ro;
    float b = glm::dot(A, rd), d = glm::dot(A, w0), e = glm::dot(rd, w0);
    float denom = 1.0f - b * b;
    return std::abs(denom) < 1e-6f ? 0.0f : (b * e - d) / denom;
}

glm::vec3 objRayPlane(glm::vec3 ro, glm::vec3 rd, glm::vec3 P, glm::vec3 N) {
    float denom = glm::dot(rd, N);
    if (std::abs(denom) < 1e-6f) return P;
    return ro + rd * (glm::dot(P - ro, N) / denom);
}

bool objTyped(const std::string& s, float& out) {
    if (s.empty() || s == "-" || s == "." || s == "-.") return false;
    out = std::strtof(s.c_str(), nullptr);
    return true;
}

// Put a node where `world` says (keeping its size): back into its parent's space.
void placeAt(SceneNode& n, const glm::mat4& world) {
    const glm::mat4 local = n.parent ? glm::inverse(n.parentFrame()) * world : world;
    n.transform.position = glm::vec3(local[3]);
    glm::mat3 r(local);
    for (int i = 0; i < 3; ++i) {
        const float len = glm::length(r[i]);
        if (len > 1e-9f) r[i] /= len;
    }
    n.transform.rotation = Anim::quatToEuler(glm::quat_cast(r));
}

// The parts that make up a selection (for scaling): parts themselves, and the parts in
// models and folders (not the ones inside other parts: they come along with their part).
void bodyParts(SceneNode* n, std::vector<SceneNode*>& out) {
    if (n->isPart()) { out.push_back(n); return; }
    for (auto& c : n->children) bodyParts(c.get(), out);
}

} // namespace

bool ViewportPanel::objectModal(const glm::mat4& view, const glm::mat4& proj, const glm::vec2& imgMin, const glm::vec2& imgSize) {
    EditorState& st = *m_state;
    ImGuiIO& io = ImGui::GetIO();
    if (st.objectModal == ObjectModal::None) { m_objOn = false; return false; }
    const glm::mat4 vp = proj * view;
    const glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
    auto toScreen = [&](glm::vec3 w, ImVec2& out) {
        glm::vec4 c = vp * glm::vec4(w, 1.0f);
        if (c.w <= 0.01f) return false;
        out = ImVec2(imgMin.x + (c.x / c.w * 0.5f + 0.5f) * imgSize.x, imgMin.y + (0.5f - c.y / c.w * 0.5f) * imgSize.y);
        return true;
    };
    auto finish = [&](bool keep) {
        if (!keep)
            for (const ObjectModalItem& it : m_objItems)
                if (SceneNode* n = m_scene->findById(it.id)) n->transform = it.start;
        st.objectModal = ObjectModal::None;
        st.objectModalEnded = ImGui::GetFrameCount();
        m_objOn = false;
        m_objItems.clear();
    };

    // --- Starting: what moves, and where it all was ---
    if (!m_objOn) {
        m_objOn = true;
        m_objItems.clear();
        std::vector<SceneNode*> movers;
        for (SceneNode* n : m_scene->selectionRoots()) {
            if (n == m_scene->root() || n->isScript() || n->isGui() || m_scene->isServiceFolder(n) ||
                m_scene->isProtected(n) || m_scene->isCharacterPart(n))
                continue;
            if (st.objectModal == ObjectModal::Scale) bodyParts(n, movers);
            else movers.push_back(n);
        }
        glm::vec3 sum(0.0f);
        for (SceneNode* n : movers) {
            m_objItems.push_back({n->id, n->transform, n->worldMatrix()});
            sum += glm::vec3(n->worldMatrix()[3]);
        }
        if (m_objItems.empty()) { finish(false); return false; }
        m_modalPivot = sum / (float)m_objItems.size();
        m_modalMouse0 = io.MousePos;
        m_modalAxis = -1;
        m_modalPlane = m_modalLocal = false;
        m_modalTyped.clear();
        m_modalAngle = 0.0f;
        ImVec2 sp;
        m_modalLastAngle = toScreen(m_modalPivot, sp) ? std::atan2(io.MousePos.y - sp.y, io.MousePos.x - sp.x) : 0.0f;
    }

    auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (key(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) m_eatRightClick = true;   // (no menu)
        finish(false);
        return true;
    }
    const bool confirm = key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter) || (m_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left));

    // A number typed while moving.
    for (int k = 0; k < 10; ++k)
        if (key((ImGuiKey)(ImGuiKey_0 + k)) || key((ImGuiKey)(ImGuiKey_Keypad0 + k))) m_modalTyped += (char)('0' + k);
    if ((key(ImGuiKey_Period) || key(ImGuiKey_KeypadDecimal)) && m_modalTyped.find('.') == std::string::npos) m_modalTyped += '.';
    if (key(ImGuiKey_Minus) || key(ImGuiKey_KeypadSubtract)) {
        if (!m_modalTyped.empty() && m_modalTyped[0] == '-') m_modalTyped.erase(0, 1);
        else m_modalTyped.insert(0, "-");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !m_modalTyped.empty()) m_modalTyped.pop_back();
    float typed = 0.0f;
    const bool hasTyped = objTyped(m_modalTyped, typed);

    // X / Y / Z: lock to that axis; again = the first object's own axis; again = free.
    const ImGuiKey axisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
    for (int a = 0; a < 3; ++a) {
        if (!key(axisKeys[a])) continue;
        if (io.KeyShift) { m_modalAxis = a; m_modalPlane = true; m_modalLocal = false; }
        else if (m_modalAxis == a && !m_modalPlane) {
            if (!m_modalLocal) m_modalLocal = true;
            else { m_modalAxis = -1; m_modalLocal = false; }
        } else { m_modalAxis = a; m_modalPlane = false; m_modalLocal = false; }
    }
    glm::vec3 A(0.0f);
    if (m_modalAxis >= 0) {
        A = glm::vec3(m_modalAxis == 0, m_modalAxis == 1, m_modalAxis == 2);
        if (m_modalLocal) A = glm::normalize(glm::vec3(m_objItems.front().world[m_modalAxis]));
    }
    const glm::vec3 V = glm::normalize(m_modalPivot - eye);   // the way the camera looks at it

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->PushClipRect(ImVec2(imgMin.x, imgMin.y), ImVec2(imgMin.x + imgSize.x, imgMin.y + imgSize.y), true);
    if (m_modalAxis >= 0) {   // the axis it's locked to
        ImVec2 a, b;
        const ImU32 col = m_modalAxis == 0 ? IM_COL32(230, 70, 70, 220) : m_modalAxis == 1 ? IM_COL32(110, 210, 80, 220) : IM_COL32(80, 140, 240, 220);
        if (toScreen(m_modalPivot - A * 500.0f, a) && toScreen(m_modalPivot + A * 500.0f, b)) fg->AddLine(a, b, col, 1.5f);
        else if (toScreen(m_modalPivot - A * 5.0f, a) && toScreen(m_modalPivot + A * 5.0f, b)) fg->AddLine(a, b, col, 1.5f);
    }
    static const char* kAxisName[3] = {"X", "Y", "Z"};
    char axisText[48] = "";
    if (m_modalAxis >= 0)
        std::snprintf(axisText, sizeof(axisText), "  along %s%s%s", m_modalPlane ? "not " : "", kAxisName[m_modalAxis],
                      m_modalLocal ? " (its own)" : " (world)");

    char header[200] = "";
    ImVec2 sp;
    const bool pivotOn = toScreen(m_modalPivot, sp);
    const bool steps = io.KeyCtrl || (st.objectModal == ObjectModal::Rotate ? st.rotSnapEnabled : st.snapEnabled);

    if (st.objectModal == ObjectModal::Grab) {
        glm::vec3 ro, rd, ro0, rd0;
        mouseRay({io.MousePos.x, io.MousePos.y}, imgMin, imgSize, view, proj, ro, rd);
        mouseRay({m_modalMouse0.x, m_modalMouse0.y}, imgMin, imgSize, view, proj, ro0, rd0);
        const float step = st.snapTranslate > 0.0f ? st.snapTranslate : 0.5f;
        glm::vec3 delta(0.0f);
        if (m_modalAxis >= 0 && !m_modalPlane) {
            float s = hasTyped ? typed : objLineParam(m_modalPivot, A, ro, rd) - objLineParam(m_modalPivot, A, ro0, rd0);
            if (steps && !hasTyped) s = std::round(s / step) * step;
            delta = A * s;
        } else {
            const glm::vec3 N = m_modalPlane ? A : V;
            delta = objRayPlane(ro, rd, m_modalPivot, N) - objRayPlane(ro0, rd0, m_modalPivot, N);
            if (m_modalPlane) delta -= A * glm::dot(delta, A);
            if (hasTyped) delta = glm::vec3(typed, 0.0f, 0.0f);
            if (steps && !hasTyped) delta = glm::round(delta / step) * step;
        }
        for (const ObjectModalItem& it : m_objItems)
            if (SceneNode* n = m_scene->findById(it.id)) {
                n->transform = it.start;
                placeAt(*n, glm::translate(glm::mat4(1.0f), delta) * it.world);
            }
        std::snprintf(header, sizeof(header), "Move   %.2f studs  (%.2f, %.2f, %.2f)%s%s", glm::length(delta), delta.x, delta.y, delta.z,
                      axisText, hasTyped ? "  [typed]" : "");
    } else if (st.objectModal == ObjectModal::Rotate) {
        if (pivotOn) {
            const float a = std::atan2(io.MousePos.y - sp.y, io.MousePos.x - sp.x);
            float d = a - m_modalLastAngle;
            while (d > 3.14159265f) d -= 6.2831853f;
            while (d < -3.14159265f) d += 6.2831853f;
            m_modalAngle += d;
            m_modalLastAngle = a;
            fg->AddLine(sp, io.MousePos, IM_COL32(255, 255, 255, 120), 1.0f);
        }
        // Turning the mouse clockwise on screen turns it clockwise as you look at it.
        float angle = m_modalAngle;
        glm::vec3 axis = V;
        if (m_modalAxis >= 0) { axis = A; if (glm::dot(A, V) < 0.0f) angle = -angle; }
        if (hasTyped) angle = glm::radians(typed);
        const float stepDeg = io.KeyCtrl ? 5.0f : std::max(1.0f, st.snapRotate);
        if (steps && !hasTyped) angle = glm::radians(std::round(glm::degrees(angle) / stepDeg) * stepDeg);
        const glm::mat4 turn = glm::translate(glm::mat4(1.0f), m_modalPivot) * glm::rotate(glm::mat4(1.0f), angle, axis) *
                               glm::translate(glm::mat4(1.0f), -m_modalPivot);
        for (const ObjectModalItem& it : m_objItems)
            if (SceneNode* n = m_scene->findById(it.id)) {
                n->transform = it.start;
                placeAt(*n, turn * it.world);
            }
        std::snprintf(header, sizeof(header), "Rotate   %.1f degrees%s%s", glm::degrees(angle), axisText, hasTyped ? "  [typed]" : "");
    } else {
        float factor = 1.0f;
        if (pivotOn) {
            const float d0 = std::max(1.0f, std::hypot(m_modalMouse0.x - sp.x, m_modalMouse0.y - sp.y));
            factor = std::hypot(io.MousePos.x - sp.x, io.MousePos.y - sp.y) / d0;
            fg->AddLine(sp, io.MousePos, IM_COL32(255, 255, 255, 120), 1.0f);
        }
        if (hasTyped) factor = typed;
        if (steps && !hasTyped) factor = std::round(factor * 10.0f) / 10.0f;
        factor = std::max(factor, 0.01f);
        glm::mat3 S(factor);   // what happens to positions around the middle
        if (m_modalAxis >= 0 && !m_modalPlane) S = glm::mat3(1.0f) + (factor - 1.0f) * glm::outerProduct(A, A);
        else if (m_modalAxis >= 0) S = glm::mat3(factor) + (1.0f - factor) * glm::outerProduct(A, A);
        for (const ObjectModalItem& it : m_objItems) {
            SceneNode* n = m_scene->findById(it.id);
            if (!n) continue;
            n->transform = it.start;
            const glm::vec3 p(it.world[3]);
            glm::mat4 w = it.world;
            w[3] = glm::vec4(m_modalPivot + S * (p - m_modalPivot), 1.0f);
            placeAt(*n, w);
            // Its size: each of its own sides stretches as much as the way it points.
            glm::vec3 size = it.start.scale;
            for (int k = 0; k < 3; ++k) {
                const glm::vec3 side = glm::normalize(glm::vec3(it.world[k]));
                size[k] = std::max(0.001f, it.start.scale[k] * glm::length(S * side));
            }
            n->transform.scale = size;
        }
        std::snprintf(header, sizeof(header), "Scale   %.2fx%s%s", factor, axisText, hasTyped ? "  [typed]" : "");
    }
    m_scene->markDirty();

    // The header along the top of the view, like Blender's.
    {
        const char* help = "X/Y/Z = axis (twice = its own), Shift+X = not X, type a number, Ctrl = steps,\n"
                           "click / Enter = done, right-click / Esc = cancel";
        ImVec2 at(imgMin.x + 12.0f, imgMin.y + 44.0f);
        ImVec2 hs = ImGui::CalcTextSize(header), ts = ImGui::CalcTextSize(help);
        float w = std::max(hs.x, ts.x) + 20.0f;
        fg->AddRectFilled(at, ImVec2(at.x + w, at.y + hs.y + ts.y + 16.0f), IM_COL32(25, 25, 28, 225), 5.0f);
        fg->AddText(ImVec2(at.x + 10, at.y + 5), IM_COL32(255, 200, 90, 255), header);
        fg->AddText(ImVec2(at.x + 10, at.y + 9 + hs.y), IM_COL32(210, 210, 210, 220), help);
    }
    fg->PopClipRect();
    if (confirm) finish(true);
    return true;
}
