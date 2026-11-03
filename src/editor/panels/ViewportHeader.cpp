// The buttons in a Viewport's top-right corner, like Blender's viewport header:
// X-ray, the four view styles (Wireframe, Solid, Flat colours, Material) and a View
// menu to look from a side. Also Z's view style menu.
#include "ViewportPanel.h"
#include "../Icons.h"
#include "../../core/I18n.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
using Shading = SceneRenderer::Shading;

struct StyleInfo { Shading style; const char* name; const char* tip; };
const StyleInfo kStyles[] = {
    {Shading::Wireframe, "Wireframe", "Only the edges of everything, see-through (Shift+Z)"},
    {Shading::Solid,     "Solid",     "Plain colours in a soft light, no shadows or sky light: easy to see shapes"},
    {Shading::Flat,      "Flat Colors", "Just the colours, no light at all"},
    {Shading::Material,  "Material",  "The real look: lighting, shadows, materials (what players see)"},
};

ImU32 rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

// A little picture of each view style (a ball drawn that way).
void styleIcon(ImDrawList* dl, ImVec2 c, float r, Shading s, bool on) {
    const ImU32 line = on ? rgb(255, 255, 255) : rgb(205, 208, 215);
    switch (s) {
    case Shading::Wireframe:
        dl->AddCircle(c, r, line, 20, 1.3f);
        dl->AddEllipse(c, ImVec2(r, r * 0.38f), line, 0.0f, 20, 1.0f);
        dl->AddEllipse(c, ImVec2(r * 0.38f, r), line, 0.0f, 20, 1.0f);
        break;
    case Shading::Solid:
        dl->AddCircleFilled(c, r, rgb(150, 154, 162), 20);
        dl->AddCircleFilled(ImVec2(c.x - r * 0.3f, c.y - r * 0.3f), r * 0.45f, rgb(215, 218, 225), 16);
        break;
    case Shading::Flat:
        dl->PathArcTo(c, r, 3.14159f * 0.5f, 3.14159f * 1.5f, 16);
        dl->PathFillConvex(rgb(235, 120, 60));
        dl->PathArcTo(c, r, -3.14159f * 0.5f, 3.14159f * 0.5f, 16);
        dl->PathFillConvex(rgb(80, 160, 235));
        break;
    case Shading::Material:
        dl->AddCircleFilled(c, r, rgb(190, 90, 60), 20);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.15f, c.y + r * 0.15f), r * 0.75f, rgb(120, 50, 35), 20);
        dl->AddCircleFilled(ImVec2(c.x - r * 0.35f, c.y - r * 0.35f), r * 0.3f, rgb(255, 230, 200), 12);
        break;
    }
}
} // namespace

void ViewportPanel::shadingMenuItems() {
    for (const StyleInfo& st : kStyles) {
        if (ImGui::MenuItem(I18n::tr(st.name), st.style == Shading::Wireframe ? "Shift+Z" : nullptr, shading == st.style))
            shading = st.style;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", I18n::tr(st.tip));
    }
    ImGui::Separator();
    if (ImGui::MenuItem(I18n::tr("X-Ray (see through)"), "Alt+Z", xray)) xray = !xray;
}

bool ViewportPanel::drawViewHeader(ImVec2 imgPos, ImVec2 imgMax) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float bw = 26.0f, bh = 24.0f, gap = 6.0f;
    const char* viewLabel = I18n::tr("View");
    const float viewW = ImGui::CalcTextSize(viewLabel).x + 30.0f;
    // From the right: View menu, the 4 styles, then X-ray.
    const float total = bw + gap + bw * 4 + gap + viewW;
    bool over = false;
    // Z: the view style menu, at the mouse.
    if (m_openShadingMenu) { ImGui::OpenPopup("##shadingMenu"); m_openShadingMenu = false; }
    if (ImGui::BeginPopup("##shadingMenu")) {
        over = true;
        ImGui::TextDisabled("%s", I18n::tr("View style"));
        shadingMenuItems();
        ImGui::EndPopup();
    }
    if (imgMax.x - imgPos.x < total + 165.0f) return over;   // (too narrow: the mode menu needs the room)
    ImVec2 at(imgMax.x - 8.0f - total, imgPos.y + 8.0f);

    auto box = [&](ImVec2 a, ImVec2 b, bool on, bool hover, ImDrawFlags corners) {
        dl->AddRectFilled(a, b, on ? IM_COL32(60, 110, 190, 240) : hover ? IM_COL32(70, 70, 76, 235) : IM_COL32(40, 40, 44, 220), 5.0f, corners);
    };

    // X-ray
    {
        ImVec2 a = at, b(at.x + bw, at.y + bh);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton("##xray", ImVec2(bw, bh));
        const bool hover = ImGui::IsItemHovered();
        box(a, b, xray, hover, ImDrawFlags_RoundCornersAll);
        Icons::draw(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), 18.0f, Icons::Id::XRay);
        if (hover) ImGui::SetTooltip("%s", I18n::tr("X-Ray: see through everything, and box-select what's behind (Alt+Z)"));
        if (clicked) xray = !xray;
        over = over || hover;
        at.x = b.x + gap;
    }
    // The 4 view styles, joined together like Blender's.
    for (int i = 0; i < 4; ++i) {
        const StyleInfo& st = kStyles[i];
        ImVec2 a = at, b(at.x + bw, at.y + bh);
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton("##style", ImVec2(bw, bh));
        const bool hover = ImGui::IsItemHovered();
        ImGui::PopID();
        const ImDrawFlags corners = i == 0 ? ImDrawFlags_RoundCornersLeft : i == 3 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone;
        box(a, b, shading == st.style, hover, corners);
        styleIcon(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), 7.5f, st.style, shading == st.style);
        if (hover) ImGui::SetTooltip("%s\n%s", I18n::tr(st.name), I18n::tr(st.tip));
        if (clicked) shading = st.style;
        over = over || hover;
        at.x = b.x;
    }
    dl->AddRect(ImVec2(at.x - bw * 4, at.y), ImVec2(at.x, at.y + bh), IM_COL32(90, 90, 96, 255), 5.0f);
    at.x += gap;
    // View: look from a side, perspective / orthographic.
    {
        ImVec2 a = at, b(at.x + viewW, at.y + bh);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton("##viewmenu", ImVec2(viewW, bh));
        const bool hover = ImGui::IsItemHovered();
        box(a, b, false, hover, ImDrawFlags_RoundCornersAll);
        dl->AddRect(a, b, IM_COL32(90, 90, 96, 255), 5.0f);
        dl->AddText(ImVec2(a.x + 9, a.y + (bh - ImGui::GetFontSize()) * 0.5f), IM_COL32(235, 235, 235, 255), viewLabel);
        const float ax = b.x - 12, ay = (a.y + b.y) * 0.5f;
        dl->AddTriangleFilled(ImVec2(ax - 4, ay - 2), ImVec2(ax + 4, ay - 2), ImVec2(ax, ay + 3), IM_COL32(200, 200, 200, 255));
        if (hover) ImGui::SetTooltip("%s", I18n::tr("Look from the front, top, side... (numpad 1 / 3 / 7)"));
        if (clicked) ImGui::OpenPopup("##viewSides");
        over = over || hover;
        ImGui::SetNextWindowPos(ImVec2(b.x, b.y + 2), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        if (ImGui::BeginPopup("##viewSides")) {
            over = true;
            static const char* kSides[6] = {"Front", "Back", "Right", "Left", "Top", "Bottom"};
            static const char* kKeys[6] = {"Numpad 1", "Ctrl+Numpad 1", "Numpad 3", "Ctrl+Numpad 3", "Numpad 7", "Ctrl+Numpad 7"};
            for (int i = 0; i < 6; ++i)
                if (ImGui::MenuItem(I18n::tr(kSides[i]), kKeys[i])) lookFrom(i);
            ImGui::Separator();
            if (ImGui::MenuItem(I18n::tr("Orthographic (flat, no perspective)"), "Numpad 5", orthographic())) setOrthographic(!orthographic());
            if (ImGui::MenuItem(I18n::tr("Zoom to Selection"), "F")) focusSelected();
            if (ImGui::MenuItem(I18n::tr("Reset Camera"))) { resetCamera(); setOrthographic(false); }
            ImGui::Separator();
            shadingMenuItems();
            ImGui::EndPopup();
        }
    }

    return over;
}
