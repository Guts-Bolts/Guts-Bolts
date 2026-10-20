// Bones (armatures inside MeshParts) drawn over the 3D view: a line from each
// bone's joint to its tip, with a dot on the joint you can click to pick it.
#include "ViewportPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/Skin.h"

#include <imgui.h>

SceneNode* ViewportPanel::drawBones(const glm::mat4& view, const glm::mat4& proj, const glm::vec2& imgMin,
                                    const glm::vec2& imgSize, uint64_t onlyPart) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const glm::mat4 vp = proj * view;
    auto toScreen = [&](glm::vec3 p, ImVec2& out) {
        glm::vec4 c = vp * glm::vec4(p, 1.0f);
        if (c.w <= 0.05f) return false;
        out = ImVec2(imgMin.x + (c.x / c.w * 0.5f + 0.5f) * imgSize.x, imgMin.y + (0.5f - c.y / c.w * 0.5f) * imgSize.y);
        return true;
    };
    // Which parts' bones show: the one being modeled, else parts picked (or with a bone
    // picked), and every part of the rig in the Animation Editor.
    std::vector<SceneNode*> parts;
    auto add = [&](SceneNode* p) {
        if (p && p->isPart() && std::find(parts.begin(), parts.end(), p) == parts.end() && !Skin::bones(p).empty()) parts.push_back(p);
    };
    if (onlyPart) add(m_scene->findById(onlyPart));
    else {
        for (SceneNode* s : m_scene->selection()) add(s->isBone() ? Skin::partOf(s) : s);
        if (SceneNode* rig = m_state->animRig ? m_scene->findById(m_state->animRig) : nullptr)
            m_scene->forEach([&](SceneNode* n) { if (rig->isAncestorOf(n)) add(n); });
    }
    SceneNode* hit = nullptr;
    const ImVec2 mouse = ImGui::GetMousePos();
    for (SceneNode* part : parts) {
        const glm::mat4 base = Skin::bones(part).empty() ? glm::mat4(1.0f) : Skin::bones(part).front()->parentFrame();
        for (SceneNode* b : Skin::bones(part)) {
            glm::vec3 head = glm::vec3(base * Skin::frame(b, part)[3]);
            glm::vec3 tail = glm::vec3(base * glm::vec4(Skin::tip(b, part), 1.0f));
            ImVec2 h, t;
            if (!toScreen(head, h) || !toScreen(tail, t)) continue;
            const bool picked = b->selected;
            const ImU32 col = picked ? IM_COL32(255, 150, 40, 255) : IM_COL32(120, 200, 255, 230);
            // A long thin diamond, like Blender's bones.
            ImVec2 d(t.x - h.x, t.y - h.y);
            float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len > 1.0f) {
                ImVec2 n(-d.y / len, d.x / len);
                float w = std::min(8.0f, len * 0.15f);
                ImVec2 mid(h.x + d.x * 0.2f, h.y + d.y * 0.2f);
                ImVec2 a(mid.x + n.x * w, mid.y + n.y * w), c(mid.x - n.x * w, mid.y - n.y * w);
                dl->AddQuadFilled(h, a, t, c, picked ? IM_COL32(255, 150, 40, 70) : IM_COL32(120, 200, 255, 50));
                dl->AddQuad(h, a, t, c, col, 1.6f);
            }
            dl->AddCircleFilled(h, 4.5f, col);
            dl->AddCircle(h, 5.5f, IM_COL32(20, 20, 24, 220), 0, 1.2f);
            float dx = mouse.x - h.x, dy = mouse.y - h.y;
            if (dx * dx + dy * dy < 9.0f * 9.0f) hit = b;
        }
    }
    return hit;
}
