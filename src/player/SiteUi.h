#pragma once
#include "../renderer/Textures.h"
// The Guts&Bolts site's look (2011-style): colours, buttons and drawings shared
// by the site's pages (PlayerApp.cpp, PlayerOnline.cpp).
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include "../game/Catalog.h"
#include "../online/AssetCache.h"
#include <set>

namespace Site {

inline const ImVec4 kAccent   = {0.26f, 0.55f, 0.96f, 1.0f};
inline const ImVec4 kGreen    = {0.20f, 0.68f, 0.32f, 1.0f};
inline const ImVec4 kCardBg   = {0.16f, 0.17f, 0.20f, 1.0f};
inline const ImVec4 kTopBarBg = {0.086f, 0.094f, 0.114f, 1.0f};

inline bool bigButton(const char* label, ImVec4 col, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, col);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x + 0.08f, col.y + 0.08f, col.z + 0.08f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(col.x - 0.05f, col.y - 0.05f, col.z - 0.05f, 1));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return r;
}

// Inside a popup: was it just tapped / clicked outside? (Phones have no Esc key, so
// tapping the dark area around a popup should close it.) Only for the top popup,
// and not on the frame it opened (that click is the one that opened it).
inline bool tappedOutside() {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (ImGui::IsWindowAppearing() || g.OpenPopupStack.empty() || g.OpenPopupStack.back().Window != w) return false;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;
    ImVec2 m = ImGui::GetIO().MousePos;
    return m.x < w->Pos.x || m.y < w->Pos.y || m.x > w->Pos.x + w->Size.x || m.y > w->Pos.y + w->Size.y;
}

// Tall (portrait) phone screen?
inline bool portraitScreen() { const ImVec2 d = ImGui::GetIO().DisplaySize; return d.y > d.x; }
// A dialog width that still fits on a narrow phone screen.
inline float fitWidth(float want) { return std::min(want, ImGui::GetIO().DisplaySize.x - 24.0f); }


// --- 2011-style look -----------------------------------------------------------
namespace Classic {
// The colours change with the season (seasonal() below), so they aren't constants.
inline ImU32  kSkyTop    = IM_COL32(22, 70, 148, 255);
inline ImU32  kSkyBottom = IM_COL32(110, 170, 232, 255);
inline ImU32  kNavTop    = IM_COL32(64, 146, 232, 255);
inline ImU32  kNavBottom = IM_COL32(16, 96, 186, 255);
inline ImU32  kNavLine   = IM_COL32(10, 60, 130, 255);
inline ImU32  kStripeA   = IM_COL32(255, 255, 255, 255);
inline ImU32  kStripeB   = IM_COL32(236, 239, 244, 255);
inline ImU32  kLogo      = IM_COL32(222, 34, 28, 255);
inline const ImVec4 kInk       = {0.16f, 0.17f, 0.20f, 1.0f};
inline const ImVec4 kInkDim    = {0.42f, 0.44f, 0.50f, 1.0f};
inline ImVec4 kLink      = {0.02f, 0.33f, 0.74f, 1.0f};
inline const ImVec4 kPlay      = {0.02f, 0.66f, 0.30f, 1.0f};
inline ImVec4 kBlue      = {0.10f, 0.45f, 0.82f, 1.0f};
inline ImVec4 kTitle     = {0.06f, 0.38f, 0.73f, 1.0f};
inline ImVec4 kTitleOn   = {0.10f, 0.45f, 0.82f, 1.0f};
// Window title bars: light, so the window's dark text stays readable on them.
inline ImVec4 kTitleBar   = {0.84f, 0.89f, 0.96f, 1.0f};
inline ImVec4 kTitleBarOn = {0.74f, 0.84f, 0.96f, 1.0f};
inline bool   gutstober  = false;   // dressed up for Halloween right now

// Gutstober (October, like the website): orange and purple, unless turned off in Settings.
inline void seasonal(bool on) {
    if (on == gutstober) return;
    gutstober = on;
    if (on) {
        kSkyTop = IM_COL32(26, 10, 40, 255);    kSkyBottom = IM_COL32(92, 40, 96, 255);
        kNavTop = IM_COL32(255, 173, 77, 255);  kNavBottom = IM_COL32(201, 94, 5, 255);
        kNavLine = IM_COL32(122, 53, 0, 255);
        kStripeA = IM_COL32(255, 250, 243, 255); kStripeB = IM_COL32(251, 239, 224, 255);
        kLogo = IM_COL32(255, 138, 28, 255);
        kLink = {0.42f, 0.18f, 0.70f, 1.0f};    kBlue = {0.48f, 0.25f, 0.77f, 1.0f};
        kTitle = {0.29f, 0.11f, 0.41f, 1.0f};   kTitleOn = {0.42f, 0.18f, 0.58f, 1.0f};
        kTitleBar = {1.0f, 0.78f, 0.50f, 1.0f}; kTitleBarOn = {1.0f, 0.68f, 0.30f, 1.0f};
    } else {
        kSkyTop = IM_COL32(22, 70, 148, 255);   kSkyBottom = IM_COL32(110, 170, 232, 255);
        kNavTop = IM_COL32(64, 146, 232, 255);  kNavBottom = IM_COL32(16, 96, 186, 255);
        kNavLine = IM_COL32(10, 60, 130, 255);
        kStripeA = IM_COL32(255, 255, 255, 255); kStripeB = IM_COL32(236, 239, 244, 255);
        kLogo = IM_COL32(222, 34, 28, 255);
        kLink = {0.02f, 0.33f, 0.74f, 1.0f};    kBlue = {0.10f, 0.45f, 0.82f, 1.0f};
        kTitle = {0.06f, 0.38f, 0.73f, 1.0f};   kTitleOn = {0.10f, 0.45f, 0.82f, 1.0f};
        kTitleBar = {0.84f, 0.89f, 0.96f, 1.0f}; kTitleBarOn = {0.74f, 0.84f, 0.96f, 1.0f};
    }
}
// Is it Gutstober (October, your own clock)?
inline bool isOctober() {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    return tm.tm_mon == 9;
}

// A pumpkin (an orange ball with ridges, a stem and a glowing face), drawn, not a picture.
inline void pumpkin(ImDrawList* dl, ImVec2 c, float r) {
    dl->AddEllipseFilled(c, ImVec2(r * 1.25f, r), IM_COL32(227, 106, 5, 255), 0.0f, 28);
    for (float k : {-0.55f, 0.0f, 0.55f})
        dl->AddEllipse(ImVec2(c.x + k * r * 0.8f, c.y), ImVec2(r * 0.45f, r * 0.98f), IM_COL32(150, 60, 0, 200), 0.0f, 20, 1.5f);
    dl->AddEllipseFilled(ImVec2(c.x - r * 0.25f, c.y - r * 0.35f), ImVec2(r * 0.45f, r * 0.3f), IM_COL32(255, 170, 70, 90), 0.0f, 16);
    dl->AddRectFilled(ImVec2(c.x - r * 0.1f, c.y - r * 1.35f), ImVec2(c.x + r * 0.12f, c.y - r * 0.8f), IM_COL32(79, 122, 30, 255), 2.0f);
    const ImU32 glow = IM_COL32(255, 224, 102, 255);
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.55f, c.y - r * 0.05f), ImVec2(c.x - r * 0.3f, c.y - r * 0.45f), ImVec2(c.x - r * 0.1f, c.y - r * 0.05f), glow);
    dl->AddTriangleFilled(ImVec2(c.x + r * 0.1f, c.y - r * 0.05f), ImVec2(c.x + r * 0.3f, c.y - r * 0.45f), ImVec2(c.x + r * 0.55f, c.y - r * 0.05f), glow);
    dl->PathArcTo(ImVec2(c.x, c.y + r * 0.05f), r * 0.6f, 0.35f, 2.8f, 12);
    dl->PathStroke(glow, 0, r * 0.16f);
}
// A bat, flapping (t = seconds).
inline void bat(ImDrawList* dl, ImVec2 c, float s, float t) {
    const float flap = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * 9.0f));
    const ImU32 col = IM_COL32(20, 8, 28, 255);
    dl->AddCircleFilled(c, s * 0.18f, col);
    for (int side : {-1, 1}) {
        ImVec2 tip(c.x + side * s, c.y - s * 0.5f * flap), mid(c.x + side * s * 0.55f, c.y + s * 0.15f);
        dl->AddTriangleFilled(c, tip, mid, col);
        dl->AddTriangleFilled(mid, tip, ImVec2(c.x + side * s * 0.8f, c.y + s * 0.05f), col);
    }
}

// Dark text and light widgets for the white striped panel.
inline void pushLight() {
    ImGui::PushStyleColor(ImGuiCol_Text, kInk);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, kInkDim);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.93f, 0.96f, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.88f, 0.93f, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.93f, 0.93f, 0.94f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.91f, 0.98f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78f, 0.86f, 0.97f, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.72f, 0.74f, 0.78f, 1));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.78f, 0.8f, 0.84f, 1));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, kBlue);
#if IMGUI_VERSION_NUM >= 19301
    ImGui::PushStyleColor(ImGuiCol_CheckboxSelectedBg, ImVec4(0.93f, 0.95f, 1, 1));   // ticked boxes stay light too
#else
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 1));   // (keeps the count the same on older ImGui)
#endif
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.9f, 0.91f, 0.93f, 1));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.7f, 0.72f, 0.76f, 1));
    // Popups, dropdowns and tooltips opened on the light pages are light too (the
    // dark default boxes with this dark text were unreadable).
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1, 1, 1, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.86f, 0.91f, 0.98f, 1));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.80f, 0.88f, 0.98f, 1));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.72f, 0.83f, 0.97f, 1));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kTitleBar);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kTitleBarOn);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, kTitleBar);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, kBlue);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.45f));
    // Windows opened from the light pages (the Terms window, the leaderboard menu)
    // get a white box too: dark text on the dark default window was unreadable.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.97f, 0.97f, 0.98f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
}
inline void popLight() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(25);
}

inline bool button(const char* label, ImVec4 col, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(col.x * 0.7f, col.y * 0.7f, col.z * 0.7f, 1));
    bool r = bigButton(label, col, size);
    ImGui::PopStyleColor(2);
    return r;
}

inline void stripes(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    dl->AddRectFilled(a, b, kStripeB);
    dl->PushClipRect(a, b, true);
    float h = b.y - a.y;
    for (float x = a.x - h; x < b.x; x += 18.0f)
        dl->AddLine(ImVec2(x, b.y), ImVec2(x + h, a.y), kStripeA, 9.0f);
    dl->PopClipRect();
    dl->AddRect(a, b, IM_COL32(150, 160, 180, 255));
}

// Big chunky logo text with an outline, like the old logo.
inline void logo(ImDrawList* dl, ImVec2 p, float size, const char* text) {
    ImFont* f = ImGui::GetFont();
    for (int dx = -3; dx <= 3; ++dx)
        for (int dy = -3; dy <= 3; ++dy)
            if (dx * dx + dy * dy >= 4)
                dl->AddText(f, size, ImVec2(p.x + dx, p.y + dy + 2), IM_COL32(40, 10, 10, 255), text);
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            dl->AddText(f, size, ImVec2(p.x + dx, p.y + dy), IM_COL32(255, 255, 255, 255), text);
    dl->AddText(f, size, p, kLogo, text);
}
} // namespace Classic
// An item's picture ("gb:<id>"): asks the server for it the first time (it shows
// up by itself once it has downloaded).
inline unsigned itemPicture(const std::string& image) {
    if (image.empty()) return 0;
    unsigned tex = Textures::get(image);
    static std::set<std::string> asked;
    if (!tex && image.rfind("gb:", 0) == 0 && asked.insert(image).second) Online::download(image.substr(3));
    return tex;
}

} // namespace Site
