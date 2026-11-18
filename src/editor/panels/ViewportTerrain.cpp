// The Viewport with a TERRAIN brush picked: drag on the ground to raise, lower,
// smooth, flatten, paint or erode it. A circle on the ground shows where the brush is.
#include "ViewportPanel.h"
#include "../EditorState.h"
#include "../../scene/Scene.h"
#include "../../scene/Terrain.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

void ViewportPanel::terrainView(const glm::mat4& view, const glm::mat4& proj, const glm::vec2& imgMin,
                                const glm::vec2& imgSize, float dt) {
    Terrain& terrain = m_scene->terrain();
    const auto brush = (Terrain::Brush)std::clamp(m_state->terrainBrush, 0, Terrain::kBrushCount - 1);
    static const char* kNames[] = {"Raise", "Lower", "Smooth", "Flatten", "Paint", "Erode"};
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // What to do, at the top.
    char hint[200];
    std::snprintf(hint, sizeof(hint), "Terrain: %s  -  drag on the ground   (size and strength on the TERRAIN tab, Esc to stop)",
                  kNames[(int)brush]);
    ImVec2 ts = ImGui::CalcTextSize(hint);
    ImVec2 hp(imgMin.x + (imgSize.x - ts.x) * 0.5f, imgMin.y + 46);   // (under the mode menu)
    dl->AddRectFilled(ImVec2(hp.x - 10, hp.y - 6), ImVec2(hp.x + ts.x + 10, hp.y + ts.y + 6), IM_COL32(60, 120, 50, 230), 6);
    dl->AddText(hp, IM_COL32(255, 255, 255, 255), hint);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_state->terrainBrush = -1; m_sculpting = false; return; }

    // Where the mouse points: on the terrain, or on the flat ground (y = 0).
    const ImVec2 mouse = ImGui::GetMousePos();
    glm::vec3 ro, rd;
    mouseRay({mouse.x, mouse.y}, imgMin, imgSize, view, proj, ro, rd);
    glm::vec3 at;
    float t;
    bool onGround = terrain.raycast(ro, rd, 5000.0f, t);
    if (onGround) at = ro + rd * t;
    else if (rd.y < -1e-3f && ro.y > 0.0f) { at = ro + rd * (-ro.y / rd.y); onGround = true; }
    const float radius = std::clamp(m_state->terrainSize, 1.0f, 128.0f);

    if (!m_hovered || !onGround) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_sculpting = false;
        return;
    }

    // The brush's circle, lying on the ground.
    const glm::mat4 vp = proj * view;
    auto screen = [&](const glm::vec3& p, ImVec2& out) {
        glm::vec4 c = vp * glm::vec4(p, 1.0f);
        if (c.w <= 0.05f) return false;
        out = ImVec2(imgMin.x + (c.x / c.w * 0.5f + 0.5f) * imgSize.x, imgMin.y + (0.5f - c.y / c.w * 0.5f) * imgSize.y);
        return true;
    };
    ImVec2 pts[64];
    int n = 0;
    for (int i = 0; i < 64; ++i) {
        const float a = i / 64.0f * 6.2831853f;
        glm::vec3 p(at.x + std::cos(a) * radius, at.y, at.z + std::sin(a) * radius);
        float h;
        if (terrain.heightAt(p.x, p.z, h)) p.y = h;
        if (screen(p + glm::vec3(0, 0.15f, 0), pts[n])) ++n;
    }
    const ImU32 ring = brush == Terrain::Brush::Lower ? IM_COL32(255, 120, 80, 230) : IM_COL32(120, 230, 120, 230);
    if (n > 2) dl->AddPolyline(pts, n, ring, ImDrawFlags_Closed, 2.0f);
    ImVec2 mid;
    if (screen(at, mid)) dl->AddCircleFilled(mid, 3.0f, ring);

    // Sculpt while the button is held (it all counts as one undo step).
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (terrain.empty())   // nothing to sculpt yet: start a flat 512-stud square of grass (just under the Baseplate's top)
            terrain.create(128, Terrain::kDefaultCell, -0.5f, TerrainMaterial::Grass);
        float h;
        m_sculptLevel = terrain.heightAt(at.x, at.z, h) ? h : at.y;
        m_sculpting = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { m_sculpting = false; return; }
    if (!m_sculpting) return;
    const float strength = std::clamp(m_state->terrainStrength, 0.05f, 1.0f);
    const auto mat = (TerrainMaterial)std::clamp(m_state->terrainMaterial, 0, kTerrainMaterialCount - 1);
    float amount = 0.0f;
    switch (brush) {
        case Terrain::Brush::Raise:
        case Terrain::Brush::Lower:   amount = strength * 12.0f * dt; break;    // studs a second, in the middle
        case Terrain::Brush::Smooth:
        case Terrain::Brush::Flatten: amount = std::min(1.0f, strength * 8.0f * dt); break;
        case Terrain::Brush::Paint:   amount = 1.0f; break;
        case Terrain::Brush::Erode:   amount = std::min(1.0f, strength * 6.0f * dt); break;   // (rain a dab: Terrain::brush)
    }
    terrain.brush(brush, glm::vec3(at.x, m_sculptLevel, at.z), radius, amount, mat);
}
