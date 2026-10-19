#include "ViewportPanel.h"
#include "../../core/Pad.h"
#include "../../core/Settings.h"
#include "../../game/GameGui.h"
#include <stb_image_write.h>   // (its code is in renderer/Textures.cpp)
#include <string>
#include <vector>
#include "../EditorState.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../scene/Physics.h"
#include "../../scene/EditMesh.h"
#include "../../game/GameSession.h"
#include "../../game/ClickDetectors.h"
#include "../../game/PlayCamera.h"
#include "../../core/AppWindow.h"
#include "../../scene/Player.h"
#include "../../game/Hud.h"
#include "../../game/EmoteMenu.h"
#include "../../core/Audio.h"
#include "../TeamCreate.h"
#include "../Icons.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

ViewportPanel::ViewportPanel(GLFWwindow* window, Scene* scene, EditorState* state)
    : m_window(window), m_scene(scene), m_state(state) {
    resetCamera();
}

ViewportPanel::~ViewportPanel() = default;

void ViewportPanel::resetCamera() {
    m_camera.yaw      = 45.0f;
    m_camera.pitch    = 25.0f;
    m_camera.distance = 12.0f;
    m_camera.pivot    = {0, 1, 0};
}

float ViewportPanel::cameraYaw() const { return m_camera.yaw; }
float ViewportPanel::swimLook() const { return PlayCamera::swimLook(m_camera); }

namespace {

// "Collisions" (MODEL tab): parts being moved stop against others.
void partsUnder(SceneNode* n, std::vector<SceneNode*>& out) {
    if (n->isPart()) out.push_back(n);
    if (n->kind == NodeKind::Part || n->kind == NodeKind::Model || n->kind == NodeKind::Tool)
        for (auto& c : n->children) partsUnder(c.get(), out);
}

// The other parts the moving ones go into (touching / just flush doesn't count).
std::vector<uint64_t> hitsOf(Scene& scene, const std::vector<SceneNode*>& moving) {
    std::vector<uint64_t> hits;
    std::vector<OBB> boxes;
    std::vector<AABB> bounds;
    AABB all{glm::vec3(1e9f), glm::vec3(-1e9f)};
    for (SceneNode* m : moving) {
        if (!m->canCollide || !m->visible) continue;
        boxes.push_back(Physics::worldOBB(m));
        bounds.push_back(Physics::worldBounds(m));
        all.min = glm::min(all.min, bounds.back().min);
        all.max = glm::max(all.max, bounds.back().max);
    }
    if (boxes.empty()) return hits;
    scene.forEach([&](SceneNode* o) {
        if (!o->isPart() || !o->visible || !o->canCollide) return;
        if (std::find(moving.begin(), moving.end(), o) != moving.end()) return;
        AABB ob = Physics::worldBounds(o);
        if (!ob.overlaps(all, 0.0f)) return;
        OBB oo = Physics::worldOBB(o);
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (!ob.overlaps(bounds[i], 0.0f)) continue;
            glm::vec3 nrm;
            float depth = 0.0f;
            if (Physics::obbOverlap(boxes[i], oo, nrm, depth) && depth > 0.002f) { hits.push_back(o->id); return; }
        }
    });
    return hits;
}

bool newHit(const std::vector<uint64_t>& now, const std::vector<uint64_t>& before) {
    for (uint64_t id : now) if (std::find(before.begin(), before.end(), id) == before.end()) return true;
    return false;
}

} // namespace

std::vector<uint64_t> ViewportPanel::collisionsOf(Scene& scene, const std::vector<SceneNode*>& movers) {
    std::vector<SceneNode*> parts;
    for (SceneNode* o : movers) partsUnder(o, parts);
    return hitsOf(scene, parts);
}

void ViewportPanel::stopAtCollisions(Scene& scene, const std::vector<SceneNode*>& movers,
                                     const std::vector<Transform>& before, const std::vector<uint64_t>& hitBefore,
                                     bool sliding) {
    std::vector<SceneNode*> parts;
    for (SceneNode* o : movers) partsUnder(o, parts);
    if (parts.empty() || !newHit(hitsOf(scene, parts), hitBefore)) return;
    if (!sliding) {
        // Turning / resizing into something: don't.
        for (size_t i = 0; i < movers.size(); ++i) movers[i]->transform = before[i];
        return;
    }
    std::vector<Transform> after;
    for (SceneNode* o : movers) after.push_back(o->transform);
    auto place = [&](float f) {
        for (size_t i = 0; i < movers.size(); ++i)
            movers[i]->transform.position = glm::mix(before[i].position, after[i].position, f);
    };
    // Slide as far as it goes: it ends up flush against what it hit.
    float lo = 0.0f, hi = 1.0f;
    for (int it = 0; it < 16; ++it) {
        float mid = (lo + hi) * 0.5f;
        place(mid);
        if (newHit(hitsOf(scene, parts), hitBefore)) hi = mid; else lo = mid;
    }
    place(lo);
}

void ViewportPanel::frameOn(const glm::vec3& target) { m_camera.pivot = target; }

void ViewportPanel::followPlayer(Player& p, float dt) {
    PlayCamera::follow(m_camera, p, dt, m_shiftLock);
    PlayCamera::fade(*m_scene, p, m_camera);
}

bool ViewportPanel::gizmoInUse() const { return ImGuizmo::IsUsing() || m_sculpting; }

bool ViewportPanel::focusSelected() {
    // The box around every part in the selection (and inside selected models / tools).
    glm::vec3 lo(1e9f), hi(-1e9f);
    bool any = false;
    std::vector<SceneNode*> stack;
    for (SceneNode* n : m_scene->selectionRoots()) if (n != m_scene->root()) stack.push_back(n);
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n->isPart() && n->visible) {
            AABB b = Physics::worldBounds(n);
            lo = glm::min(lo, b.min);
            hi = glm::max(hi, b.max);
            any = true;
        }
        if (n->kind == NodeKind::Part || n->kind == NodeKind::Model || n->kind == NodeKind::Tool)
            for (auto& c : n->children) stack.push_back(c.get());
    }
    if (!any) return false;
    // Back off far enough that the whole box fits on screen.
    float radius = std::max(0.5f, glm::length(hi - lo) * 0.5f);
    float fit = radius / std::sin(glm::radians(m_camera.fov * 0.5f)) * 1.15f;
    m_glideFrom = m_camera.pivot;
    m_glideDistFrom = m_camera.distance;
    m_glideTo = (lo + hi) * 0.5f;
    m_glideDistTo = std::clamp(fit, 3.0f, 800.0f);
    m_glide = 0.0f;
    return true;
}

void ViewportPanel::handleInput(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    bool playing = m_session != nullptr && !m_session->runOnly();   // Run: fly around like when editing
    bool focused = ImGui::IsWindowFocused();

    if (!playing) m_shiftLock = false;
    if (playing) {
        if (!m_hovered) return;
        const bool firstPerson = PlayCamera::firstPerson(m_camera);
        // Shift toggles Shift Lock, same as in the Player app.
        if (!GraphicsSettings::get().shiftLockSwitch) m_shiftLock = false;
        else if (!io.WantTextInput && !firstPerson &&
                 (ImGui::IsKeyPressed(ImGuiKey_LeftShift, false) || ImGui::IsKeyPressed(ImGuiKey_RightShift, false)))
            m_shiftLock = !m_shiftLock;
        if ((firstPerson || (m_shiftLock && !m_camera.noInput)) && !io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            // First person / Shift Lock: the hidden mouse looks around (Esc / F5 stops playing as usual).
            ImVec2 mid(m_viewMin.x + (m_viewMax.x - m_viewMin.x) * 0.5f, m_viewMin.y + (m_viewMax.y - m_viewMin.y) * 0.5f);
            AppWindow::lockMouse(mid.x, mid.y);
            PlayCamera::turn(m_camera, AppWindow::mouseLookX(), AppWindow::mouseLookY());
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            if (m_shiftLock && !firstPerson) {
                fg->AddCircle(mid, 11.0f, IM_COL32(0, 0, 0, 120), 24, 4.0f);
                fg->AddCircle(mid, 11.0f, IM_COL32(255, 255, 255, 235), 24, 2.0f);
                fg->AddCircleFilled(mid, 2.5f, IM_COL32(255, 255, 255, 235));
            } else {
                fg->AddCircleFilled(mid, 3.5f, IM_COL32(0, 0, 0, 160));
                fg->AddCircleFilled(mid, 2.0f, IM_COL32(255, 255, 255, 230));
            }
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            PlayCamera::turn(m_camera, io.MouseDelta.x, io.MouseDelta.y);
        }
        if (!GameGui::overScroller(*m_scene, ImGui::GetMousePos())) PlayCamera::zoom(m_camera, io.MouseWheel, m_scene->cameraOptions());   // (else the wheel scrolls the UI)
        if (Pad::connected() && !io.WantTextInput) {   // a controller: right stick looks, D-pad zooms
            const glm::vec2 rs = Pad::rightStick();
            if (rs.x != 0.0f || rs.y != 0.0f) PlayCamera::turn(m_camera, rs.x * 900.0f * dt, -rs.y * 600.0f * dt);
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true))   PlayCamera::zoom(m_camera, 1.0f, m_scene->cameraOptions());
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true)) PlayCamera::zoom(m_camera, -1.0f, m_scene->cameraOptions());
        }
        return;
    }

    // Roblox Studio camera: right-drag looks around, middle-drag pans, the
    // wheel zooms, and WASD / Q E fly (hold Shift to go faster).
    static bool looking = false;
    if (m_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) looking = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) looking = false;
    if (looking) m_camera.look(io.MouseDelta.x, io.MouseDelta.y);
    if (m_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        if (io.KeyShift) m_camera.orbit(io.MouseDelta.x, io.MouseDelta.y);   // Shift+middle: orbit
        else             m_camera.pan(io.MouseDelta.x, io.MouseDelta.y);
    }
    if (m_hovered && io.MouseWheel != 0.0f) m_camera.zoom(io.MouseWheel);

    // Numpad 5: orthographic / perspective, like Blender.
    if ((focused || m_hovered) && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Keypad5, false))
        m_state->orthographic = !m_state->orthographic;

    // Modeling mode: Home (or numpad .) looks at the picked points, like Blender.
    if (m_state->mode == StudioMode::Modeling && (focused || m_hovered) && !io.WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Home, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false))) {
        SceneNode* n = m_scene->findById(m_state->modeling.node);
        if (n && n->editMesh) {
            glm::mat4 M = n->worldMatrix();
            glm::vec3 c(0.0f);
            int count = 0;
            const auto& sel = m_state->modeling.sel;
            for (size_t i = 0; i < n->editMesh->verts.size(); ++i)
                if (i < sel.size() && sel[i]) { c += glm::vec3(M * glm::vec4(n->editMesh->verts[i], 1.0f)); ++count; }
            m_camera.pivot = count ? c / (float)count : glm::vec3(M[3]);
        }
    }
    // In Modeling mode the letter keys are tools (E extrude, ...), so fly only while right-dragging.
    bool keysFly = m_state->mode == StudioMode::Modeling ? looking : (focused || looking);
    if (keysFly && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt) {
        float speed = (io.KeyShift ? 60.0f : 18.0f) * dt;
        float f = 0, r = 0, u = 0;
        if (ImGui::IsKeyDown(ImGuiKey_W)) f += speed;
        if (ImGui::IsKeyDown(ImGuiKey_S)) f -= speed;
        if (ImGui::IsKeyDown(ImGuiKey_D)) r += speed;
        if (ImGui::IsKeyDown(ImGuiKey_A)) r -= speed;
        if (ImGui::IsKeyDown(ImGuiKey_E)) u += speed;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) u -= speed;
        if (f != 0 || r != 0 || u != 0) { m_camera.fly(f, r, u); m_glide = -1.0f; }
    }
    // Gliding to the selection (F); any camera move of your own stops it.
    if (looking || (m_hovered && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || io.MouseWheel != 0.0f))) m_glide = -1.0f;
    if (m_glide >= 0.0f) {
        m_glide += dt;
        float t = std::min(1.0f, m_glide / 0.35f);
        float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);   // quick, then gentle
        m_camera.pivot = glm::mix(m_glideFrom, m_glideTo, e);
        m_camera.distance = glm::mix(m_glideDistFrom, m_glideDistTo, e);
        if (t >= 1.0f) m_glide = -1.0f;
    }
}

void ViewportPanel::mouseRay(const glm::vec2& mouse, const glm::vec2& imgMin, const glm::vec2& imgSize,
                             const glm::mat4& view, const glm::mat4& proj,
                             glm::vec3& ro, glm::vec3& rd) const {
    // Mouse position in the image -> normalised device coords -> world ray.
    float nx = (mouse.x - imgMin.x) / imgSize.x * 2.0f - 1.0f;
    float ny = 1.0f - (mouse.y - imgMin.y) / imgSize.y * 2.0f;
    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 pNear = invVP * glm::vec4(nx, ny, -1.0f, 1.0f);
    glm::vec4 pFar  = invVP * glm::vec4(nx, ny,  1.0f, 1.0f);
    pNear /= pNear.w;
    pFar  /= pFar.w;
    ro = glm::vec3(pNear);
    rd = glm::normalize(glm::vec3(pFar - pNear));
}

bool ViewportPanel::pointAt(ImVec2 mouse, glm::vec3& point, SceneNode*& part) {
    part = nullptr;
    const glm::vec2 lo(m_viewMin.x, m_viewMin.y), size(m_viewMax.x - m_viewMin.x, m_viewMax.y - m_viewMin.y);
    if (size.x < 1.0f || size.y < 1.0f || mouse.x < lo.x || mouse.y < lo.y || mouse.x > lo.x + size.x || mouse.y > lo.y + size.y)
        return false;
    glm::vec3 ro, rd;
    mouseRay({mouse.x, mouse.y}, lo, size, m_camera.view(), m_camera.projection(), ro, rd);
    float dist = 0.0f;
    if ((part = Physics::raycast(*m_scene, ro, rd, &dist))) { point = ro + rd * dist; return true; }
    // Nothing there: the ground (y = 0), or in front of the camera if looking up.
    point = rd.y < -1e-3f && ro.y > 0.0f ? ro + rd * std::min(-ro.y / rd.y, 500.0f) : m_camera.pivot;
    return true;
}

bool ViewportPanel::scaleHandles(SceneNode* sel, const glm::mat4& view, const glm::mat4& proj,
                                 const glm::vec2& imgMin, const glm::vec2& imgSize, bool move) {
    const glm::mat4 M = sel->worldMatrix();
    const glm::vec3 c(M[3]);
    glm::vec3 dir[3];
    float half[3];
    for (int i = 0; i < 3; ++i) {
        const glm::vec3 col(M[i]);
        half[i] = glm::length(col) * 0.5f;
        dir[i] = half[i] > 1e-6f ? col / (half[i] * 2.0f) : glm::vec3(i == 0, i == 1, i == 2);
    }
    if (move && !m_state->gizmoLocal) {   // World: the arrows point along the world's X / Y / Z
        float wh[3];
        for (int k = 0; k < 3; ++k) wh[k] = std::abs(dir[0][k]) * half[0] + std::abs(dir[1][k]) * half[1] + std::abs(dir[2][k]) * half[2];
        for (int k = 0; k < 3; ++k) { half[k] = wh[k]; dir[k] = glm::vec3(k == 0, k == 1, k == 2); }
    }
    const glm::mat4 vp = proj * view;
    auto toScreen = [&](glm::vec3 p, ImVec2& out) {
        glm::vec4 q = vp * glm::vec4(p, 1.0f);
        if (q.w <= 0.01f) return false;
        out = ImVec2(imgMin.x + (q.x / q.w * 0.5f + 0.5f) * imgSize.x, imgMin.y + (0.5f - q.y / q.w * 0.5f) * imgSize.y);
        return true;
    };
    const ImVec2 mouse = ImGui::GetMousePos();
    glm::vec3 ro, rd;
    mouseRay({mouse.x, mouse.y}, imgMin, imgSize, view, proj, ro, rd);
    // Where along the line through the part's middle (along `d`) the mouse ray passes closest.
    auto alongAxis = [&](const glm::vec3& d) {
        const glm::vec3 w0 = c - ro;
        const float b = glm::dot(d, rd), dd = glm::dot(d, w0), e = glm::dot(rd, w0);
        const float den = 1.0f - b * b;
        return den < 1e-5f ? 0.0f : (b * e - dd) / den;
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 cols[3] = {IM_COL32(230, 70, 60, 255), IM_COL32(80, 200, 80, 255), IM_COL32(60, 120, 240, 255)};
    for (int h = 0; h < 6; ++h) {
        const int ax = h / 2;
        const float side = (h % 2) ? -1.0f : 1.0f;
        const glm::vec3 at = c + dir[ax] * side * (half[ax] + 0.35f);
        ImVec2 sp;
        if (!toScreen(at, sp)) continue;
        const float r = 8.0f;
        const bool hot = m_scaleDrag == h || (m_scaleDrag < 0 && m_hovered &&
                         (mouse.x - sp.x) * (mouse.x - sp.x) + (mouse.y - sp.y) * (mouse.y - sp.y) < (r + 3) * (r + 3));
        if (hot && m_scaleDrag < 0) m_scaleHover = h;
        ImVec2 base = sp;
        if (toScreen(c + dir[ax] * side * half[ax], base)) dl->AddLine(base, sp, cols[ax], 1.5f);
        const ImU32 fill = hot ? IM_COL32(255, 230, 90, 255) : cols[ax];
        if (move) {
            // An arrow pointing away from the part.
            ImVec2 d(sp.x - base.x, sp.y - base.y);
            float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len < 1e-3f) { d = ImVec2(0, -1); len = 1; }
            d = ImVec2(d.x / len, d.y / len);
            const ImVec2 n(-d.y, d.x);
            const float s2 = hot ? 1.25f : 1.0f;
            ImVec2 tip(sp.x + d.x * 12 * s2, sp.y + d.y * 12 * s2);
            ImVec2 a(sp.x - d.x * 4 * s2 + n.x * 8 * s2, sp.y - d.y * 4 * s2 + n.y * 8 * s2);
            ImVec2 b(sp.x - d.x * 4 * s2 - n.x * 8 * s2, sp.y - d.y * 4 * s2 - n.y * 8 * s2);
            dl->AddTriangleFilled(tip, a, b, fill);
            dl->AddTriangle(tip, a, b, IM_COL32(0, 0, 0, 120), 1.5f);
        } else {
            dl->AddCircleFilled(sp, hot ? r + 2 : r, fill, 20);
            dl->AddCircle(sp, hot ? r + 2 : r, IM_COL32(0, 0, 0, 120), 20, 1.5f);
        }
    }

    // Start / continue / finish a drag.
    if (m_scaleDrag < 0 && m_scaleHover >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_scaleDrag = m_scaleHover;
        m_scaleStart = sel->transform;
        m_scaleT0 = alongAxis(dir[m_scaleDrag / 2]);
        if (move) {   // everything selected moves together
            m_moveNodes = {sel};
            for (SceneNode* o : m_scene->selectionRoots())
                if (o != sel && !m_scene->isProtected(o) && !m_scene->isCharacterPart(o)) m_moveNodes.push_back(o);
            m_moveStart.clear();
            for (SceneNode* o : m_moveNodes) m_moveStart.push_back(o->transform);
            m_moveHits = m_state->collisions ? collisionsOf(*m_scene, m_moveNodes) : std::vector<uint64_t>();
        }
    }
    if (move && m_scaleDrag >= 0) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { m_scaleDrag = -1; return true; }
        const int ax = m_scaleDrag / 2;
        float by = alongAxis(dir[ax]) - m_scaleT0;
        const float step = m_state->snapEnabled && m_state->snapTranslate > 0.01f ? m_state->snapTranslate : 0.0f;
        if (step > 0.0f) by = std::round(by / step) * step;
        for (size_t i = 0; i < m_moveNodes.size(); ++i) {
            SceneNode* o = m_moveNodes[i];
            if (!m_scene->findById(o->id)) continue;
            glm::vec3 d = dir[ax] * by;
            if (o->parent) d = glm::vec3(glm::inverse(o->parent->worldMatrix()) * glm::vec4(d, 0.0f));
            o->transform = m_moveStart[i];
            o->transform.position += d;
        }
        if (m_state->collisions) stopAtCollisions(*m_scene, m_moveNodes, m_moveStart, m_moveHits, true);
        m_scene->markDirty();
        char text[64];
        std::snprintf(text, sizeof text, "%+.2f studs", by);
        dl->AddText(ImVec2(mouse.x + 16, mouse.y + 10), IM_COL32(255, 255, 255, 255), text);
        return true;
    }
    if (m_scaleDrag >= 0) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { m_scaleDrag = -1; return true; }
        const int ax = m_scaleDrag / 2;
        const float side = (m_scaleDrag % 2) ? -1.0f : 1.0f;
        const bool both = ImGui::GetIO().KeyCtrl;   // Ctrl: both sides at once (like Roblox)
        float grow = (alongAxis(dir[ax]) - m_scaleT0) * side * (both ? 2.0f : 1.0f);
        const float step = m_state->snapEnabled && m_state->snapTranslate > 0.01f ? m_state->snapTranslate : 0.0f;
        if (step > 0.0f) grow = std::round(grow / step) * step;
        const float minSize = step > 0.0f ? step : 0.05f;
        const float startSize = m_scaleStart.scale[ax];
        const float newSize = std::max(minSize, startSize + grow);
        const float d = newSize - startSize;
        sel->transform = m_scaleStart;
        sel->transform.scale[ax] = newSize;
        if (!both) {   // the far side stays where it is: the middle moves half as far
            glm::vec3 shift = dir[ax] * side * (d * 0.5f);
            if (sel->parent) shift = glm::vec3(glm::inverse(sel->parent->worldMatrix()) * glm::vec4(shift, 0.0f));
            sel->transform.position += shift;
        }
        m_scene->markDirty();
        char text[64];
        std::snprintf(text, sizeof text, "%.2f studs", newSize);
        dl->AddText(ImVec2(mouse.x + 16, mouse.y + 10), IM_COL32(255, 255, 255, 255), text);
    }
    return true;
}

// The light blue box around each selected part, like Roblox Studio's.
void ViewportPanel::drawSelectionBoxes(const glm::mat4& view, const glm::mat4& proj,
                                       const glm::vec2& imgMin, const glm::vec2& imgSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const glm::mat4 vp = proj * view;
    auto screen = [&](const glm::vec3& p, ImVec2& out) {
        glm::vec4 q = vp * glm::vec4(p, 1.0f);
        if (q.w <= 0.05f) return false;
        out = ImVec2(imgMin.x + (q.x / q.w * 0.5f + 0.5f) * imgSize.x, imgMin.y + (0.5f - q.y / q.w * 0.5f) * imgSize.y);
        return true;
    };
    int drawn = 0;
    std::vector<SceneNode*> stack = m_scene->selectionRoots();
    while (!stack.empty() && drawn < 400) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n == m_scene->root()) continue;
        for (auto& c : n->children) stack.push_back(c.get());
        if (!n->isPart() || n->internal) continue;
        ++drawn;
        const glm::mat4 M = n->worldMatrix();
        ImVec2 corner[8];
        bool ok = true;
        for (int i = 0; i < 8 && ok; ++i) {
            // A hair bigger than the part, so the lines sit on its surface.
            glm::vec3 local((i & 1) ? 0.51f : -0.51f, (i & 2) ? 0.51f : -0.51f, (i & 4) ? 0.51f : -0.51f);
            ok = screen(glm::vec3(M * glm::vec4(local, 1.0f)), corner[i]);
        }
        if (!ok) continue;
        static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& e : edges) dl->AddLine(corner[e[0]], corner[e[1]], IM_COL32(25, 160, 255, 255), 1.6f);
    }
}

void ViewportPanel::drawGizmo(const glm::mat4& view, const glm::mat4& proj,
                              const glm::vec2& imgMin, const glm::vec2& imgSize) {
    SceneNode* sel = m_scene->selected();
    m_scaleHover = -1;
    if (!sel || sel == m_scene->root() || sel->isScript() || sel->isGui() || m_state->tool == GizmoTool::Select) {
        m_scaleDrag = -1;
        return;   // (game UI is moved by dragging it in the viewport)
    }

    if ((m_state->tool == GizmoTool::Scale || m_state->tool == GizmoTool::Translate) &&
        sel->isPart() && !m_state->animRig && !m_scene->isCharacterPart(sel) &&
        scaleHandles(sel, view, proj, imgMin, imgSize, m_state->tool == GizmoTool::Translate))
        return;
    m_scaleDrag = -1;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSize.x, imgSize.y);

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (m_state->tool == GizmoTool::Rotate)     op = ImGuizmo::ROTATE;
    else if (m_state->tool == GizmoTool::Scale) op = ImGuizmo::SCALE;
    ImGuizmo::MODE mode = m_state->gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    // Increments, like Roblox Studio: Move (studs) also sets how Scale grows, Rotate
    // turns in steps of degrees. Turned off, everything moves freely (Blender style).
    float snap[3] = {0, 0, 0};
    bool snapping = op == ImGuizmo::ROTATE ? m_state->rotSnapEnabled : m_state->snapEnabled;
    if (snapping) {
        float s = (op == ImGuizmo::TRANSLATE) ? m_state->snapTranslate : m_state->snapRotate;
        snap[0] = snap[1] = snap[2] = s;
    }
    const bool snapSize = snapping && op == ImGuizmo::SCALE && m_state->snapTranslate > 0.01f;   // (done below, in studs)

    // Models are handled around their middle (their "pivot"), like Roblox,
    // rather than their origin, which may be far away from their parts.
    glm::mat4 base = sel->worldMatrix();
    glm::vec3 localPivot(0.0f);
    if (sel->kind == NodeKind::Model) {
        glm::vec3 lo(1e9f), hi(-1e9f);
        std::vector<SceneNode*> stack{sel};
        while (!stack.empty()) {
            SceneNode* n = stack.back(); stack.pop_back();
            if (n->isPart()) { glm::vec3 p(n->worldMatrix()[3]); lo = glm::min(lo, p); hi = glm::max(hi, p); }
            for (auto& c : n->children) stack.push_back(c.get());
        }
        if (lo.x <= hi.x) localPivot = glm::vec3(glm::inverse(base) * glm::vec4((lo + hi) * 0.5f, 1.0f));
    }
    // Posing in the Animation Editor: parts turn about their joint (shoulder, hip, neck).
    if (m_state->animRig && sel->isPart())
        if (SceneNode* rig = m_scene->findById(m_state->animRig); rig && rig->isAncestorOf(sel))
            localPivot = Anim::jointPivot(sel);
    glm::mat4 world = base * glm::translate(glm::mat4(1.0f), localPivot);
    const glm::vec3 pivotBefore(world[3]);
    const Transform before0 = sel->transform;
    const glm::mat4 worldBefore = world;
    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, mode,
                                              glm::value_ptr(world), nullptr,
                                              snapping && op != ImGuizmo::SCALE ? snap : nullptr);
    // Started dragging: remember where from (for the readout).
    if (ImGuizmo::IsUsing() && !m_gizmoDragging) {
        m_gizmoDragging = true;
        m_dragStart = before0;
        m_dragStartPivot = pivotBefore;
    }
    if (!ImGuizmo::IsUsing()) m_gizmoDragging = false;
    if (changed) {
        glm::vec3 movedPivot = glm::vec3(world[3]) - pivotBefore;
        // Rotating several things: they all turn together around the gizmo, like Roblox
        // (before, only the last one picked turned).
        if (m_state->tool == GizmoTool::Rotate) {
            const glm::mat4 turn = world * glm::inverse(worldBefore);
            for (SceneNode* o : m_scene->selectionRoots()) {
                if (o == sel || m_scene->isProtected(o) || m_scene->isCharacterPart(o)) continue;
                glm::mat4 ow = turn * o->worldMatrix();
                if (o->parent) ow = glm::inverse(o->parent->worldMatrix()) * ow;
                float ot[3], orr[3], os[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(ow), ot, orr, os);
                o->transform.position = {ot[0], ot[1], ot[2]};
                o->transform.rotation = {orr[0], orr[1], orr[2]};
            }
        }
        world = world * glm::translate(glm::mat4(1.0f), -localPivot);
        // Convert the manipulated world matrix back into a local transform.
        glm::mat4 local = world;
        if (sel->parent)
            local = glm::inverse(sel->parent->worldMatrix()) * world;

        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local), t, r, s);

        glm::vec3 newRot{r[0], r[1], r[2]};
        // Accumulate the rotation delta to avoid Euler-angle flips at +/-90 deg.
        glm::vec3 deltaRot = newRot - sel->transform.rotation;
        // Collisions: remember where everything was and what it already touched.
        std::vector<SceneNode*> movers{sel};
        if (m_state->tool == GizmoTool::Translate)
            for (SceneNode* o : m_scene->selectionRoots())
                if (o != sel && !m_scene->isProtected(o) && !m_scene->isCharacterPart(o)) movers.push_back(o);
        std::vector<Transform> before;
        std::vector<uint64_t> hitBefore;
        const bool collide = m_state->collisions && !m_state->animRig;
        if (collide) {
            for (SceneNode* o : movers) before.push_back(o->transform);
            hitBefore = collisionsOf(*m_scene, movers);
        }
        // Moving several things: everything else selected slides along too.
        glm::vec3 moved = movedPivot;
        if (m_state->tool == GizmoTool::Translate && glm::length(moved) > 0.0f) {
            for (SceneNode* o : m_scene->selectionRoots()) {
                if (o == sel || m_scene->isProtected(o) || m_scene->isCharacterPart(o)) continue;
                glm::vec3 d = moved;
                if (o->parent) d = glm::vec3(glm::inverse(o->parent->worldMatrix()) * glm::vec4(moved, 0.0f));
                o->transform.position += d;
            }
        }
        sel->transform.position = {t[0], t[1], t[2]};
        sel->transform.rotation += deltaRot;
        sel->transform.scale    = {s[0], s[1], s[2]};
        if (snapSize) {   // sizes in whole steps of the Move increment (never smaller than one step)
            const float st = m_state->snapTranslate;
            for (int i = 0; i < 3; ++i)
                if (std::abs(sel->transform.scale[i] - m_dragStart.scale[i]) > 1e-5f) {
                    const float d = std::round((sel->transform.scale[i] - m_dragStart.scale[i]) / st) * st;
                    sel->transform.scale[i] = std::max(st, m_dragStart.scale[i] + d);
                }
        }

        if (collide) stopAtCollisions(*m_scene, movers, before, hitBefore, m_state->tool == GizmoTool::Translate);
    }

    // While dragging: how far, next to the mouse (like Roblox Studio's "4 studs").
    if (m_gizmoDragging) {
        char text[96] = "";
        if (op == ImGuizmo::TRANSLATE) {
            const glm::vec3 d = glm::vec3((sel->worldMatrix() * glm::translate(glm::mat4(1.0f), localPivot))[3]) - m_dragStartPivot;
            std::snprintf(text, sizeof text, "%.2f studs", glm::length(d));
            if (std::abs(d.x) > 1e-3f && std::abs(d.y) + std::abs(d.z) < 1e-3f) std::snprintf(text, sizeof text, "X  %+.2f studs", d.x);
            else if (std::abs(d.y) > 1e-3f && std::abs(d.x) + std::abs(d.z) < 1e-3f) std::snprintf(text, sizeof text, "Y  %+.2f studs", d.y);
            else if (std::abs(d.z) > 1e-3f && std::abs(d.x) + std::abs(d.y) < 1e-3f) std::snprintf(text, sizeof text, "Z  %+.2f studs", d.z);
        } else if (op == ImGuizmo::ROTATE) {
            glm::vec3 d = sel->transform.rotation - m_dragStart.rotation;
            int big = std::abs(d.x) >= std::abs(d.y) && std::abs(d.x) >= std::abs(d.z) ? 0 : std::abs(d.y) >= std::abs(d.z) ? 1 : 2;
            std::snprintf(text, sizeof text, "%+.1f\xC2\xB0", d[big]);
        } else {
            const glm::vec3 sz = sel->transform.scale, d = sz - m_dragStart.scale;
            std::snprintf(text, sizeof text, "%.2f x %.2f x %.2f  (%+.2f studs)", sz.x, sz.y, sz.z,
                          std::abs(d.x) >= std::abs(d.y) && std::abs(d.x) >= std::abs(d.z) ? d.x : std::abs(d.y) >= std::abs(d.z) ? d.y : d.z);
        }
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImVec2 m = ImGui::GetMousePos(), ts = ImGui::CalcTextSize(text);
        const ImVec2 p0(m.x + 18, m.y + 14), p1(p0.x + ts.x + 12, p0.y + ts.y + 8);
        dl->AddRectFilled(p0, p1, IM_COL32(30, 32, 38, 230), 4.0f);
        dl->AddRect(p0, p1, IM_COL32(90, 150, 240, 255), 4.0f);
        dl->AddText(ImVec2(p0.x + 6, p0.y + 4), IM_COL32(255, 255, 255, 255), text);
    }
}

void ViewportPanel::render(float dt) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (m_wantFocus) { ImGui::SetNextWindowFocus(); m_wantFocus = false; }
    // Playing: a controller plays the game here, it doesn't move between buttons.
    ImGui::Begin("Viewport", nullptr, m_session ? ImGuiWindowFlags_NoNav : 0);

    m_hovered = ImGui::IsWindowHovered();
    handleInput(dt);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x, h = (int)avail.y;
    if (w > 0 && h > 0) {
        if (w != m_viewW || h != m_viewH) {
            m_viewW = w; m_viewH = h;
            m_fbo.resize(w, h);
            m_camera.resize(w, h);
        }
        bool playing = m_session != nullptr;
        if (!playing) {   // (while playing, the game's own camera setting counts: PlayCamera)
            m_camera.orthographic = m_state->orthographic;
            m_camera.orthoSize = 0.0f;
        }
        m_renderer.setGridSpacing(m_state->snapEnabled && m_state->snapTranslate >= 0.25f ? m_state->snapTranslate : 1.0f);
        updateNavOverlay();
        m_renderer.render(*m_scene, m_camera, m_fbo, m_state->showGrid && (!playing || m_session->runOnly()));
        if (GameGui::needsBackdrop(*m_scene)) {   // UIBlur: the world behind the UI, blurred
            m_renderer.makeBackdrop(m_fbo);
            unsigned bd[SceneRenderer::kBackdropLevels];
            for (int i = 0; i < SceneRenderer::kBackdropLevels; ++i) bd[i] = m_renderer.backdrop(i);
            GameGui::setBackdrop(bd, SceneRenderer::kBackdropLevels);
        } else {
            GameGui::setBackdrop(nullptr, 0);
        }
        Audio::setListener(m_camera.position(), m_camera.forward());

        ImVec2 imgPos = ImGui::GetCursorScreenPos();
        // Flip V so the framebuffer texture is the right way up in ImGui.
        ImGui::Image((ImTextureID)(intptr_t)m_fbo.colorTexture(),
                     avail, ImVec2(0, 1), ImVec2(1, 0));
        m_viewMin = imgPos;
        m_viewMax = ImVec2(imgPos.x + avail.x, imgPos.y + avail.y);
        if (drawModeMenu(imgPos)) m_hovered = false;   // clicks on the menu aren't clicks in the world

        glm::mat4 view = m_camera.view();
        glm::mat4 proj = m_camera.projection();
        GameGui::setCamera(view, proj, m_camera.position());   // (BillboardGuis / SurfaceGuis sit on parts)
        glm::vec2 imgMin{imgPos.x, imgPos.y};
        glm::vec2 imgSize{avail.x, avail.y};
        ImVec2 imgMax(imgPos.x + avail.x, imgPos.y + avail.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        if (playing && m_session->runOnly()) {
            // Simulate: click things to look at them in Properties, and drag
            // them with the gizmo while the world keeps running.
            drawGizmo(view, proj, imgMin, imgSize);
            bool overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing() || m_scaleHover >= 0 || m_scaleDrag >= 0;
            if (ImGuizmo::IsUsing())
                for (SceneNode* n : m_scene->selectionRoots()) { n->velocity = glm::vec3(0.0f); n->angularVelocity = glm::vec3(0.0f); n->sleepTime = 0; }
            if (m_hovered && !overGizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 m = ImGui::GetMousePos();
                glm::vec3 ro, rd;
                mouseRay({m.x, m.y}, imgMin, imgSize, view, proj, ro, rd);
                SceneNode* hit = Physics::raycast(*m_scene, ro, rd);
                if (hit == m_scene->terrainNode()) hit = nullptr;
                if (hit && !ImGui::GetIO().KeyAlt && !m_scene->isCharacterPart(hit)) {
                    SceneNode* top = hit;
                    for (SceneNode* p = hit->parent; p && p != m_scene->root(); p = p->parent)
                        if (p->kind == NodeKind::Model) top = p;
                    hit = top;
                }
                if (hit) m_scene->select(hit); else m_scene->deselect();
            }
            GameGui::draw(dl, imgPos, imgMax, *m_scene);
            Hud::draw(dl, imgPos, imgMax, *m_scene, m_session->gui(), 0.0f, false);
            dl->AddRect(imgPos, imgMax, IM_COL32(60, 170, 230, 255), 0.0f, 0, 3.0f);   // blue frame = simulating
            const char* tip = m_state->simPaused
                ? "SIMULATE (paused)  -  F6 resume, F7 step one frame, Shift+F5 stop"
                : "SIMULATE  -  click things to inspect / drag them, right-drag + WASD fly, F6 pause, Shift+F5 stop";
            dl->AddText(ImVec2(imgPos.x + 12, imgMax.y - ImGui::GetFontSize() - 10), IM_COL32(255, 255, 255, 190), tip);
        } else if (playing) {
            // Clicks go to the game (part.Clicked / MouseButton1), not the editor. While the
            // emote menu is open, they're its.
            const bool emotesOpen = m_scene->coreGui().emotesOpen;
            const bool onHotbar = emotesOpen || Hud::overHotbar(imgPos, imgMax, *m_scene, ImGui::GetMousePos());
            std::vector<GameGui::Event> guiEvents;   // the game's own UI gets the mouse first
            const bool onGui = GameGui::handle(*m_scene, imgPos, imgMax, ImGui::GetMousePos(), m_hovered && !onHotbar,
                                               ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                                               ImGui::IsMouseReleased(ImGuiMouseButton_Left), false, m_guiInput, guiEvents);
            bool onPrompt = false;   // ProximityPrompts near the character
            {
                Player* me = m_scene->player();
                const glm::vec3 at = me && me->root() && !me->isDead() ? me->position() : glm::vec3(0.0f);
                onPrompt = GameGui::prompts(*m_scene, imgPos, imgMax, me && me->root() && !me->isDead() ? &at : nullptr,
                                            ImGui::GetMousePos(), m_hovered && !onGui && !onHotbar, ImGui::GetIO().DeltaTime, guiEvents);
            }
            m_session->guiEvents(guiEvents);
            const bool toWorld = m_hovered && !onHotbar && !onGui && !onPrompt;
            SceneNode* under = nullptr;   // the part under the mouse (not our own character)
            if (toWorld) {
                ImVec2 m = ImGui::GetMousePos();
                glm::vec3 ro, rd;
                mouseRay({m.x, m.y}, imgMin, imgSize, view, proj, ro, rd);
                const SceneNode* character = m_scene->player() ? m_scene->player()->root() : nullptr;
                under = Physics::raycast(*m_scene, ro, rd, nullptr, character);
            }
            // ClickDetectors: hovering one fires MouseHoverEnter / Leave and shows a hand.
            std::string cursorIcon;
            if (m_session->hoverPart(under ? under->id : 0, &cursorIcon)) ClickDetectors::showPointer(ImGui::GetMousePos(), cursorIcon);
            if (toWorld && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) m_session->click(under ? under->id : 0);
            // A right-click (not a right-drag turning the camera): RightMouseClick.
            if (toWorld && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 6.0f * 6.0f)
                m_session->rightClick(under ? under->id : 0);
            Hud::drawNameTags(dl, imgPos, imgMax, *m_scene, proj * view, m_camera.position());
            GameGui::draw(dl, imgPos, imgMax, *m_scene, &m_guiInput);
            GameGui::drawPrompts(dl);
            {
                std::vector<GameGui::Event> typed;   // a TextBox being typed in
                GameGui::textInput(*m_scene, typed);
                m_session->guiEvents(typed);
            }
            Hud::draw(dl, imgPos, imgMax, *m_scene, m_session->gui());
            if (int slot = Hud::drawHotbar(dl, imgPos, imgMax, *m_scene); slot >= 0 && m_hovered && !emotesOpen) m_session->selectToolSlot(slot);
            // The leaderboard, once the game gives the player some leaderstats.
            if (auto stats = m_session->scripts().leaderstats(m_session->scripts().playerName()); !stats.empty())
            {
                static bool listOpen = true;   // (the arrow on its title folds it away)
                Hud::drawPlayerList(dl, imgPos, imgMax, {{m_session->scripts().playerName(), false, false, stats, true}}, listOpen);
            }
            // The emote menu (B), over everything else.
            EmoteMenu::update(dl, imgPos, imgMax, *m_scene, !ImGui::GetIO().WantTextInput && !m_state->simPaused, nullptr, m_hovered);
            // Green frame = the game is running.
            dl->AddRect(imgPos, imgMax, IM_COL32(60, 200, 90, 255), 0.0f, 0, 3.0f);
            const char* tip = m_state->simPaused ? "PLAY (paused)  -  F6 resume, F7 step, Shift+F5 stop"
                                                 : "PLAYING  -  WASD move, Space jump, right-drag camera, B emotes, F6 pause, F5/Esc stop";
            dl->AddText(ImVec2(imgPos.x + 12, imgMax.y - ImGui::GetFontSize() - 10),
                        IM_COL32(255, 255, 255, 170), tip);
        } else if (m_state->mode == StudioMode::Modeling) {
            modelingView(view, proj, imgMin, imgSize);
            dl->AddRect(imgPos, imgMax, IM_COL32(255, 150, 40, 255), 0.0f, 0, 3.0f);   // orange frame = modeling
            static const char* kPickNames[] = {"vertices", "edges", "faces"};
            char tip[256];
            std::snprintf(tip, sizeof(tip),
                          "MODELING (picking %s)  -  1/2/3 vertex/edge/face, click / Shift+click / drag a box, A all, "
                          "E extrude, I inset, X delete, M merge, F fill, Tab done",
                          kPickNames[std::clamp(m_state->modeling.selectMode, 0, 2)]);
            dl->AddText(ImVec2(imgPos.x + 12, imgMax.y - ImGui::GetFontSize() - 10), IM_COL32(255, 255, 255, 200), tip);
        } else {
            drawSelectionBoxes(view, proj, imgMin, imgSize);
            drawGizmo(view, proj, imgMin, imgSize);

            // Team Create: a coloured box around what each other person has selected.
            if (m_team && m_team->active()) {
                for (const auto& mem : m_team->members()) {
                    if (mem.id == m_team->myId() || !mem.selected) continue;
                    SceneNode* n = m_scene->findById(mem.selected);
                    if (!n || !n->isPart()) continue;
                    AABB b = Physics::worldBounds(n);
                    ImVec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
                    bool behind = false;
                    for (int c = 0; c < 8; ++c) {
                        glm::vec3 p{(c & 1) ? b.max.x : b.min.x, (c & 2) ? b.max.y : b.min.y, (c & 4) ? b.max.z : b.min.z};
                        glm::vec4 clip = proj * view * glm::vec4(p, 1.0f);
                        if (clip.w <= 0.05f) { behind = true; break; }
                        float sx = imgMin.x + (clip.x / clip.w * 0.5f + 0.5f) * imgSize.x;
                        float sy = imgMin.y + (0.5f - clip.y / clip.w * 0.5f) * imgSize.y;
                        lo = ImVec2(std::min(lo.x, sx), std::min(lo.y, sy));
                        hi = ImVec2(std::max(hi.x, sx), std::max(hi.y, sy));
                    }
                    if (behind) continue;
                    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(mem.color.r, mem.color.g, mem.color.b, 1));
                    dl->AddRect(lo, hi, col, 3.0f, 0, 2.0f);
                    ImVec2 ts = ImGui::CalcTextSize(mem.name.c_str());
                    dl->AddRectFilled(ImVec2(lo.x, lo.y - ts.y - 6), ImVec2(lo.x + ts.x + 10, lo.y), col, 3.0f);
                    dl->AddText(ImVec2(lo.x + 5, lo.y - ts.y - 3), IM_COL32(20, 20, 25, 255), mem.name.c_str());
                }
            }

            // Light icons (lights have no shape, so draw a marker you can click).
            SceneNode* iconHit = nullptr;
            ImVec2 mouse = ImGui::GetMousePos();
            glm::mat4 vp = proj * view;
            m_scene->forEach([&](SceneNode* n) {
                if (!n->isLight()) return;
                glm::vec4 c = vp * glm::vec4(glm::vec3(n->worldMatrix()[3]), 1.0f);
                if (c.w <= 0.05f) return;
                glm::vec2 ndc = glm::vec2(c) / c.w;
                if (std::abs(ndc.x) > 1.05f || std::abs(ndc.y) > 1.05f) return;
                ImVec2 sp(imgMin.x + (ndc.x * 0.5f + 0.5f) * imgSize.x,
                          imgMin.y + (0.5f - ndc.y * 0.5f) * imgSize.y);
                glm::vec3 col = n->enabled ? n->color : glm::vec3(0.4f);
                ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(col.r, col.g, col.b, 0.9f));
                dl->AddCircleFilled(sp, 8.0f, fill);
                dl->AddCircle(sp, 10.0f, n->selected ? IM_COL32(255, 150, 40, 255) : IM_COL32(20, 20, 20, 200), 0, 2.0f);
                if (n->lightType == LightType::Spot)
                    dl->AddLine(sp, ImVec2(sp.x, sp.y + 16), IM_COL32(20, 20, 20, 200), 2.0f);
                float dx = mouse.x - sp.x, dy = mouse.y - sp.y;
                if (dx * dx + dy * dy < 12.0f * 12.0f) iconHit = n;
            });

            // Terrain brushes: drag on the ground.
            if (m_state->terrainBrush >= 0) {
                terrainView(view, proj, imgMin, imgSize, dt);
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }
            m_sculpting = false;

            // Connect tool: click part A, then part B.
            if (m_state->connectTool >= 0) {
                static const char* names[] = {"rope", "rod", "spring", "weld", "hinge", "motor", "ball socket", "prismatic slider", "cylindrical joint"};
                const char* what = names[std::clamp(m_state->connectTool, 0, 8)];
                char hint[160];
                if (!m_state->connectFirst)
                    std::snprintf(hint, sizeof(hint), "Adding a %s: click the FIRST part   (Esc to cancel)", what);
                else
                    std::snprintf(hint, sizeof(hint), "Adding a %s: now click the SECOND part   (Esc to cancel)", what);
                ImVec2 ts = ImGui::CalcTextSize(hint);
                ImVec2 hp(imgMin.x + (imgSize.x - ts.x) * 0.5f, imgMin.y + 14);
                dl->AddRectFilled(ImVec2(hp.x - 10, hp.y - 6), ImVec2(hp.x + ts.x + 10, hp.y + ts.y + 6), IM_COL32(20, 90, 200, 230), 6);
                dl->AddText(hp, IM_COL32(255, 255, 255, 255), hint);
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_state->connectTool = -1; m_state->connectFirst = 0; }
                if (m_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    ImVec2 m = ImGui::GetMousePos();
                    glm::vec3 ro, rd;
                    mouseRay({m.x, m.y}, imgMin, imgSize, view, proj, ro, rd);
                    float dist = 0.0f;
                    if (SceneNode* hit = Physics::raycast(*m_scene, ro, rd, &dist); hit && hit != m_scene->terrainNode()) {
                        glm::vec3 point = ro + rd * dist;
                        if (!m_state->connectFirst) {
                            m_state->connectFirst = hit->id;
                            m_state->connectPoint[0] = point.x; m_state->connectPoint[1] = point.y; m_state->connectPoint[2] = point.z;
                            m_scene->select(hit);
                        } else if (hit->id != m_state->connectFirst) {
                            SceneNode* first = m_scene->findById(m_state->connectFirst);
                            glm::vec3 p0(m_state->connectPoint[0], m_state->connectPoint[1], m_state->connectPoint[2]);
                            if (first && onConnect) onConnect(first, p0, hit, point);
                            m_state->connectTool = -1;
                            m_state->connectFirst = 0;
                        }
                    }
                }
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }

            // Game UI shows on top while you build; click it to pick, drag to move,
            // drag the blue corner to resize (Roblox Studio works the same way).
            {
                SceneNode* sel = m_scene->selected();
                const uint64_t selGui = sel && sel->isGuiObject() ? sel->id : 0;
                GameGui::draw(dl, imgPos, imgMax, *m_scene, nullptr, selGui);
                ImVec2 mp = ImGui::GetMousePos();
                if (m_guiDrag && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    if (SceneNode* g = m_scene->findById(m_guiDragId); g && g->isGuiObject()) {
                        float dx = std::round(mp.x - m_guiDragFrom.x), dy = std::round(mp.y - m_guiDragFrom.y);
                        if (m_guiDrag == 1) { g->gui.pos = m_guiDragStart; g->gui.pos.xo += dx; g->gui.pos.yo += dy; }
                        else {
                            g->gui.size = m_guiDragStart;
                            g->gui.size.xo += dx; g->gui.size.yo += dy;
                            // keep at least 4 pixels on screen
                            float w = g->gui.absSize.x, h = g->gui.absSize.y;
                            if (w < 4) g->gui.size.xo += 4 - w;
                            if (h < 4) g->gui.size.yo += 4 - h;
                        }
                    }
                    ImGui::End();
                    ImGui::PopStyleVar();
                    return;
                }
                m_guiDrag = 0;
                bool overGizmoNow = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
                if (m_hovered && !overGizmoNow && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    ImVec2 a, b;
                    SceneNode* hitGui = nullptr;
                    if (selGui && GameGui::rectOf(*m_scene, imgPos, imgMax, sel, a, b) &&
                        std::abs(mp.x - b.x) < 8 && std::abs(mp.y - b.y) < 8) {
                        m_guiDrag = 2;   // resize from the corner
                        hitGui = sel;
                    } else if ((hitGui = GameGui::pick(*m_scene, imgPos, imgMax, mp))) {
                        m_guiDrag = 1;
                        m_scene->select(hitGui);
                    }
                    if (hitGui) {
                        m_guiDragId = hitGui->id;
                        m_guiDragFrom = mp;
                        m_guiDragStart = m_guiDrag == 1 ? hitGui->gui.pos : hitGui->gui.size;
                        ImGui::End();
                        ImGui::PopStyleVar();
                        return;
                    }
                }
            }

            // Left-click to pick — but not while interacting with the gizmo.
            bool overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing() || m_scaleHover >= 0 || m_scaleDrag >= 0;
            // Ctrl+click adds / removes, Shift+click adds.
            auto pick = [&](SceneNode* hit) {
                ImGuiIO& io = ImGui::GetIO();
                if (io.KeyCtrl)       m_scene->toggleSelection(hit);
                else if (io.KeyShift) m_scene->addToSelection(hit);
                else if (hit)         m_scene->select(hit);
                else                  m_scene->deselect();
            };
            if (m_hovered && !overGizmo && iconHit && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                pick(iconHit);
            } else if (m_hovered && !overGizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 m = ImGui::GetMousePos();
                glm::vec3 ro, rd;
                mouseRay({m.x, m.y}, imgMin, imgSize, view, proj, ro, rd);
                SceneNode* hit = Physics::raycast(*m_scene, ro, rd);
                if (hit == m_scene->terrainNode()) hit = nullptr;   // (the ground isn't something you pick)
                if (hit && hit->locked) hit = nullptr;            // Locked parts can't be clicked (Alt+L)
                // Like Roblox: clicking a part inside a Model picks the whole Model
                // (the top one under the Workspace). Alt+click picks just the part,
                // and so does any click on the rig open in the Animation Editor.
                SceneNode* animRig = m_state->animRig ? m_scene->findById(m_state->animRig) : nullptr;
                bool inAnimRig = hit && animRig && animRig->isAncestorOf(hit);
                if (hit && !inAnimRig && !ImGui::GetIO().KeyAlt && !m_scene->isCharacterPart(hit)) {
                    SceneNode* top = hit;
                    for (SceneNode* p = hit->parent; p && p != m_scene->root(); p = p->parent)
                        if (p->kind == NodeKind::Model) top = p;
                    hit = top;
                }
                if (hit || !(ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift)) pick(hit);
                // Clicked empty space: hold and drag to box-select, like Roblox Studio.
                if (!hit) { m_partBox = true; m_boxFrom = m; }
            }
            if (m_partBox) {
                const ImVec2 m = ImGui::GetMousePos();
                const ImVec2 lo(std::min(m.x, m_boxFrom.x), std::min(m.y, m_boxFrom.y));
                const ImVec2 hi(std::max(m.x, m_boxFrom.x), std::max(m.y, m_boxFrom.y));
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(lo, hi, IM_COL32(80, 160, 255, 40));
                dl->AddRect(lo, hi, IM_COL32(80, 160, 255, 220));
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    m_partBox = false;
                    if (hi.x - lo.x > 4 && hi.y - lo.y > 4) {
                        // Everything whose middle is inside the box (whole Models, like clicking).
                        const glm::mat4 vp = proj * view;
                        std::vector<SceneNode*> found;
                        m_scene->forEach([&](SceneNode* n) {
                            if (!n->isPart() || n->locked || n->internal || !n->visible || m_scene->isCharacterPart(n)) return;
                            glm::vec4 c = vp * glm::vec4(glm::vec3(n->worldMatrix()[3]), 1.0f);
                            if (c.w <= 0.01f) return;   // behind the camera
                            const float sx = imgMin.x + (c.x / c.w * 0.5f + 0.5f) * imgSize.x;
                            const float sy = imgMin.y + (0.5f - c.y / c.w * 0.5f) * imgSize.y;
                            if (sx < lo.x || sx > hi.x || sy < lo.y || sy > hi.y) return;
                            SceneNode* top = n;
                            for (SceneNode* p = n->parent; p && p != m_scene->root(); p = p->parent)
                                if (p->kind == NodeKind::Model) top = p;
                            if (m_scene->isProtected(top)) return;
                            if (std::find(found.begin(), found.end(), top) == found.end()) found.push_back(top);
                        });
                        ImGuiIO& io = ImGui::GetIO();
                        if (!io.KeyShift && !io.KeyCtrl) m_scene->deselect();
                        for (SceneNode* n : found) m_scene->addToSelection(n);
                    }
                }
            }
        }
    }

    // A right-click that didn't turn the camera: a menu for what's selected (the Explorer's
    // right-click menu, with Publish to Guts&Bolts), like Roblox Studio's.
    if (!m_session && contextMenu) {
        if (m_hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 25.0f)
            ImGui::OpenPopup("##viewportMenu");
        if (ImGui::BeginPopup("##viewportMenu")) {
            contextMenu();
            ImGui::EndPopup();
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

// Blender-style mode menu in the Viewport's top-left corner.
bool ViewportPanel::drawModeMenu(ImVec2 imgPos) {
    static const Icons::Id kIcons[] = {Icons::Id::Build, Icons::Id::Mesh, Icons::Id::Simulate, Icons::Id::Play};
    static const char* kKeys[] = {"Shift+F5", "Tab", "F8", "F5"};
    static const char* kTips[] = {"Place and change objects", "Reshape the selected part's mesh (corners, edges, faces)",
                                  "Physics and scripts run live; fly around, inspect and drag things",
                                  "Playtest with your character"};
    int cur = (int)m_state->mode;
    char label[64];
    std::snprintf(label, sizeof(label), "%s Mode", kStudioModeNames[cur]);
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 a(imgPos.x + 8, imgPos.y + 8), b(a.x + ts.x + 50, a.y + ts.y + 12);
    ImGui::SetCursorScreenPos(a);
    bool clicked = ImGui::InvisibleButton("##mode", ImVec2(b.x - a.x, b.y - a.y));
    bool hover = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, hover ? IM_COL32(70, 70, 76, 235) : IM_COL32(40, 40, 44, 220), 5.0f);
    dl->AddRect(a, b, IM_COL32(90, 90, 96, 255), 5.0f);
    Icons::draw(dl, ImVec2(a.x + 14, (a.y + b.y) * 0.5f), 16.0f, kIcons[cur]);
    dl->AddText(ImVec2(a.x + 27, a.y + 6), IM_COL32(235, 235, 235, 255), label);
    float ax = b.x - 13, ay = (a.y + b.y) * 0.5f;
    dl->AddTriangleFilled(ImVec2(ax - 4, ay - 2), ImVec2(ax + 4, ay - 2), ImVec2(ax, ay + 3), IM_COL32(200, 200, 200, 255));
    if (hover) ImGui::SetTooltip("Switch mode (like Blender)");
    if (clicked) ImGui::OpenPopup("##modes");
    ImGui::SetNextWindowPos(ImVec2(a.x, b.y + 2));
    if (ImGui::BeginPopup("##modes")) {
        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            char row[64];
            std::snprintf(row, sizeof(row), "      %s", kStudioModeNames[i]);
            if (ImGui::Selectable(row, cur == i, 0, ImVec2(230, 0)) && onMode) onMode((StudioMode)i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kTips[i]);
            Icons::draw(ImGui::GetWindowDrawList(), ImVec2(p.x + 10, p.y + ImGui::GetTextLineHeight() * 0.5f), 16.0f, kIcons[i]);
            ImVec2 ks = ImGui::CalcTextSize(kKeys[i]);
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 230 - ks.x, p.y), IM_COL32(140, 140, 140, 255), kKeys[i]);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    return hover;
}

// ---------------------------------------------------------------------------
// A picture of the game for the site (Studio sends it when you publish)
// ---------------------------------------------------------------------------

std::string ViewportPanel::snapshotAround(int width, int height, glm::vec3 center, float radius) {
    m_aimSnapshot = true;
    m_aimCenter = center;
    m_aimRadius = std::max(0.5f, radius);
    std::string png = snapshotPng(width, height, false);
    m_aimSnapshot = false;
    return png;
}

std::string ViewportPanel::snapshotPng(int width, int height, bool fromView) {
    // Look at the spawn point, like the Player's game cards do.
    Camera cam;
    if (fromView) cam = m_camera;
    cam.resize(width, height);
    if (m_aimSnapshot) {
        cam.pivot = m_aimCenter;
        cam.yaw = 35.0f;
        cam.pitch = 22.0f;
        cam.distance = std::max(2.0f, m_aimRadius / std::sin(glm::radians(cam.fov * 0.5f)) * 1.1f);
    } else if (!fromView) {
        glm::vec3 target(0.0f, 1.0f, 0.0f);
        if (SceneNode* spawn = m_scene->root()->findChild("SpawnLocation", true))
            target = glm::vec3(spawn->worldMatrix()[3]) + glm::vec3(0.0f, 1.5f, 0.0f);
        cam.pivot = target;
        cam.yaw = 45.0f;
        cam.pitch = 28.0f;
        cam.distance = 22.0f;
    }

    // No selection outlines in the picture.
    std::vector<SceneNode*> selected;
    m_scene->forEach([&](SceneNode* n) { if (n->selected) { selected.push_back(n); n->selected = false; } });
    Framebuffer fb;
    fb.resize(width, height);
    m_renderer.render(*m_scene, cam, fb, false);
    for (SceneNode* n : selected) n->selected = true;

    std::vector<unsigned char> px((size_t)width * height * 4);
    fb.bind();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    fb.unbind();
    // OpenGL's rows go bottom-up; pictures go top-down. And no see-through pixels.
    std::vector<unsigned char> img(px.size());
    const size_t row = (size_t)width * 4;
    for (int y = 0; y < height; ++y)
        std::copy(px.begin() + (size_t)(height - 1 - y) * row, px.begin() + (size_t)(height - y) * row, img.begin() + (size_t)y * row);
    for (size_t i = 3; i < img.size(); i += 4) img[i] = 255;
    std::string png;
    stbi_write_png_to_func([](void* ctx, void* data, int size) {
        static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), (size_t)size);
    }, &png, width, height, 4, img.data(), (int)row);
    return png;
}

void ViewportPanel::updateNavOverlay() {
    static bool testFlag = std::getenv("GB_TEST_NAVMESH") != nullptr;   // (tests: start with it showing)
    if (testFlag) { m_state->showNavMesh = true; testFlag = false; }
    if (!m_state->showNavMesh && !m_state->bakeNavMesh) {
        if (m_navShown) { m_renderer.setOverlay({}, {}); m_navShown = false; }
        return;
    }
    // Look at the parts again now and then (twice a second); the navmesh rebakes
    // itself when they've changed.
    const double now = ImGui::GetTime();
    if (now - m_navGather > 0.5 || m_state->bakeNavMesh) {
        m_navGather = now;
        m_navPhysics.gather(*m_scene);
        if (m_state->bakeNavMesh) { m_navPhysics.rebakeNavMesh(); m_state->bakeNavMesh = 0; }
    }
    const NavMesh& nav = m_navPhysics.navMesh();
    char info[96];
    std::snprintf(info, sizeof info, "%zu floor cells, baked in %.0f ms", nav.spanCount(), nav.bakeMs());
    m_state->navInfo = info;
    if (!m_state->showNavMesh) return;
    if (m_navShown && m_navDrawn == nav.version()) return;
    std::vector<NavMesh::DrawVertex> tris, lines;
    nav.buildDrawing(tris, lines, NavMesh::Agent{});
    auto conv = [](const std::vector<NavMesh::DrawVertex>& in) {
        std::vector<SceneRenderer::OverlayVertex> out;
        out.reserve(in.size());
        for (const auto& v : in) out.push_back({v.pos, v.color});
        return out;
    };
    m_renderer.setOverlay(conv(tris), conv(lines));
    m_navDrawn = nav.version();
    m_navShown = true;
}
