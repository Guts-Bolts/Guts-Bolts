#pragma once

#include <imgui.h>
#include <string>
#include <unordered_map>

// Records the screen rectangle of a real ImGui item so Studio Learn can outline
// the exact control after the editor panels have finished drawing this frame.
namespace StudioLearnHighlight {
struct ItemRect {
    ImVec2 min;
    ImVec2 max;
    int frame = -1;
    bool interacted = false;
};

inline std::unordered_map<std::string, ItemRect>& items() {
    static std::unordered_map<std::string, ItemRect> value;
    return value;
}

inline void markRect(const std::string& id, const ImVec2& min, const ImVec2& max, bool interacted = false) {
    if (!ImGui::GetCurrentContext() || id.empty() || max.x <= min.x || max.y <= min.y) return;
    items()[id] = {min, max, ImGui::GetFrameCount(), interacted};
}

inline void mark(const std::string& id) {
    if (!ImGui::GetCurrentContext() || id.empty()) return;
    const bool interacted = ImGui::IsItemClicked() || ImGui::IsItemEdited() || ImGui::IsItemDeactivatedAfterEdit();
    markRect(id, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), interacted);
}

inline bool interacted(const std::string& id) {
    const auto it = items().find(id);
    return it != items().end() && it->second.frame == ImGui::GetFrameCount() && it->second.interacted;
}

inline bool find(const std::string& id, ImVec2& min, ImVec2& max) {
    const auto it = items().find(id);
    if (it == items().end() || it->second.frame != ImGui::GetFrameCount()) return false;
    min = it->second.min;
    max = it->second.max;
    return max.x > min.x && max.y > min.y;
}
} // namespace StudioLearnHighlight
