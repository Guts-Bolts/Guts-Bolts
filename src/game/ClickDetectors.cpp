#include "ClickDetectors.h"
#include "../scene/Physics.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../renderer/Textures.h"
#include <algorithm>
#include <vector>

namespace ClickDetectors {

namespace {
SceneNode* detectorIn(const SceneNode* n) {
    for (auto& c : n->children) if (c->isClickDetector()) return c.get();
    return nullptr;
}
} // namespace

SceneNode* find(Scene& scene, SceneNode* part) {
    if (!part || !part->isPart()) return nullptr;
    if (SceneNode* d = detectorIn(part)) return d;
    for (SceneNode* p = part->parent; p && p != scene.root(); p = p->parent)
        if (p->kind == NodeKind::Model)
            if (SceneNode* d = detectorIn(p)) return d;
    return nullptr;
}

float distanceToPart(const SceneNode* part, const glm::vec3& from) {
    const AABB b = Physics::worldBounds(part);
    return glm::length(from - glm::clamp(from, b.min, b.max));
}

float distance(const SceneNode* detector, const glm::vec3& from) {
    float best = 1e30f;
    const SceneNode* in = detector ? detector->parent : nullptr;
    if (!in) return best;
    if (in->isPart()) return distanceToPart(in, from);
    if (in->kind != NodeKind::Model) return best;
    std::vector<const SceneNode*> stack{in};
    while (!stack.empty()) {
        const SceneNode* n = stack.back();
        stack.pop_back();
        for (auto& c : n->children) {
            if (c->isPart()) best = std::min(best, distanceToPart(c.get(), from));
            stack.push_back(c.get());
        }
    }
    return best;
}

void showPointer(ImVec2 at, const std::string& cursorIcon) {
    const unsigned tex = cursorIcon.empty() ? 0 : Textures::get(cursorIcon);
    if (!tex) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); return; }
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    const float s = 32.0f;   // its middle is the point that clicks, like Roblox's
    ImGui::GetForegroundDrawList()->AddImage((ImTextureID)(intptr_t)tex, ImVec2(at.x - s * 0.5f, at.y - s * 0.5f),
                                             ImVec2(at.x + s * 0.5f, at.y + s * 0.5f), ImVec2(0, 1), ImVec2(1, 0));
}

void drawAim(ImDrawList* dl, ImVec2 c, bool onClickable) {
    if (onClickable) dl->AddCircleFilled(c, 7.0f, IM_COL32(255, 255, 255, 200), 20);
    dl->AddCircle(c, 7.0f, IM_COL32(0, 0, 0, 150), 20, 2.5f);
    dl->AddCircle(c, 7.0f, IM_COL32(255, 255, 255, 220), 20, 1.2f);
}

} // namespace ClickDetectors
