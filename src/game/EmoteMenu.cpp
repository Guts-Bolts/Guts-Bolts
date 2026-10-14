#include "EmoteMenu.h"
#include "../scene/Scene.h"
#include "../scene/Emotes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace EmoteMenu {

namespace {
float dist2(ImVec2 a, ImVec2 b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }

// Text centred on `c`, broken onto two lines at a space if it's too wide.
void centredText(ImDrawList* dl, ImVec2 c, float width, const std::string& text, ImU32 col) {
    ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    if (ts.x <= width || text.find(' ') == std::string::npos) {
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, text.c_str());
        return;
    }
    const size_t mid = text.size() / 2;
    size_t cut = text.rfind(' ', mid);
    if (cut == std::string::npos) cut = text.find(' ');
    const std::string a = text.substr(0, cut), b = text.substr(cut + 1);
    ImVec2 sa = ImGui::CalcTextSize(a.c_str()), sb = ImGui::CalcTextSize(b.c_str());
    dl->AddText(ImVec2(c.x - sa.x * 0.5f, c.y - sa.y), col, a.c_str());
    dl->AddText(ImVec2(c.x - sb.x * 0.5f, c.y), col, b.c_str());
}
} // namespace

bool available(Scene& scene) {
    Player* p = scene.player();
    return scene.coreGui().enabled("EmotesMenu") && p && p->root() && !p->isDead();
}

void toggle(Scene& scene) {
    CoreGuiState& g = scene.coreGui();
    g.emotesOpen = !g.emotesOpen && available(scene);
}

bool update(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, bool keys, const ImVec2* tap, bool mouse) {
    CoreGuiState& g = scene.coreGui();
    if (!available(scene)) { g.emotesOpen = false; return false; }
    if (keys && ImGui::IsKeyPressed(ImGuiKey_B, false)) toggle(scene);
    if (!g.emotesOpen) return false;

    // The wheel: up to 8 emotes around a middle that says what this is.
    const std::vector<Emotes::MenuEntry> entries = Emotes::menu();
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float R = std::clamp(std::min(max.x - min.x, max.y - min.y) * 0.3f, 95.0f, 210.0f);
    const float r = R * 0.34f, hub = R * 0.42f;
    const ImVec2 mousePos = ImGui::GetMousePos();
    const bool click = mouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mousePos.x >= min.x && mousePos.y >= min.y &&
                       mousePos.x <= max.x && mousePos.y <= max.y;
    const ImVec2* at = tap ? tap : click ? &mousePos : nullptr;

    dl->AddRectFilled(min, max, IM_COL32(0, 0, 0, 70));
    dl->AddCircleFilled(c, R + r + 10.0f, IM_COL32(20, 24, 32, 150), 64);
    std::string picked;
    for (int i = 0; i < Emotes::kSlots; ++i) {
        const float a = -1.5707963f + i * (6.2831853f / Emotes::kSlots);   // from the top, clockwise
        const ImVec2 s(c.x + std::cos(a) * R, c.y + std::sin(a) * R);
        const Emotes::MenuEntry* e = i < (int)entries.size() && !entries[i].name.empty() ? &entries[i] : nullptr;
        const bool hover = e && dist2(mousePos, s) < r * r;
        dl->AddCircleFilled(s, r, !e ? IM_COL32(255, 255, 255, 25) : hover ? IM_COL32(255, 255, 255, 235) : IM_COL32(255, 255, 255, 170), 40);
        dl->AddCircle(s, r, IM_COL32(255, 255, 255, e ? 230 : 70), 40, 2.0f);
        // The slot's number (1-8), small, at its edge.
        char num[4];
        std::snprintf(num, sizeof(num), "%d", i + 1);
        dl->AddText(ImVec2(s.x - 3.0f, s.y - r + 3.0f), e ? IM_COL32(40, 50, 70, 200) : IM_COL32(255, 255, 255, 90), num);
        if (e) centredText(dl, s, r * 1.8f, e->title, IM_COL32(20, 25, 35, 255));
        else centredText(dl, s, r * 1.8f, "Empty", IM_COL32(255, 255, 255, 110));
        if (e && at && dist2(*at, s) < r * r) picked = e->name;
    }
    dl->AddCircleFilled(c, hub, IM_COL32(30, 110, 220, 235), 48);
    centredText(dl, ImVec2(c.x, c.y - 8.0f), hub * 1.8f, "Emotes", IM_COL32(255, 255, 255, 255));
    centredText(dl, ImVec2(c.x, c.y + 12.0f), hub * 1.9f, tap ? "Tap one to play it" : "B to close",
                IM_COL32(210, 225, 255, 255));

    if (!at) return false;
    // A click or tap anywhere: play the emote it's on, and close the menu either way.
    if (!picked.empty())
        if (Player* p = scene.player()) p->playEmote(picked);
    g.emotesOpen = false;
    return true;
}

} // namespace EmoteMenu
