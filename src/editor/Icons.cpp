#include "Icons.h"
#include "../scene/SceneNode.h"

#include <cmath>

namespace Icons {

namespace {
constexpr float kPi = 3.14159265f;

ImU32 rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

// An isometric-ish cube: three faces.
void cube(ImDrawList* dl, ImVec2 c, float s, ImU32 top, ImU32 left, ImU32 right) {
    float h = s * 0.5f;
    ImVec2 t(c.x, c.y - h), tl(c.x - h * 0.87f, c.y - h * 0.5f), tr(c.x + h * 0.87f, c.y - h * 0.5f),
           m(c.x, c.y), bl(c.x - h * 0.87f, c.y + h * 0.5f), br(c.x + h * 0.87f, c.y + h * 0.5f), b(c.x, c.y + h);
    dl->AddQuadFilled(t, tr, m, tl, top);
    dl->AddQuadFilled(tl, m, b, bl, left);
    dl->AddQuadFilled(m, tr, br, b, right);
}

void arrowHead(ImDrawList* dl, ImVec2 tip, ImVec2 dir, float s, ImU32 col) {
    ImVec2 n(-dir.y, dir.x);
    dl->AddTriangleFilled(tip, ImVec2(tip.x - dir.x * s + n.x * s * 0.6f, tip.y - dir.y * s + n.y * s * 0.6f),
                          ImVec2(tip.x - dir.x * s - n.x * s * 0.6f, tip.y - dir.y * s - n.y * s * 0.6f), col);
}

void paper(ImDrawList* dl, ImVec2 c, float s, ImU32 fill, ImU32 line) {
    ImVec2 a(c.x - s * 0.32f, c.y - s * 0.42f), b(c.x + s * 0.32f, c.y + s * 0.42f);
    dl->AddRectFilled(a, b, fill, s * 0.06f);
    float th = std::max(1.0f, s * 0.07f);
    for (int i = 0; i < 4; ++i) {
        float y = a.y + s * (0.2f + 0.16f * i);
        dl->AddLine(ImVec2(a.x + s * 0.12f, y), ImVec2(b.x - s * (i == 3 ? 0.25f : 0.12f), y), line, th);
    }
}
} // namespace

void draw(ImDrawList* dl, ImVec2 c, float s, Id id, ImU32 tint) {
    float th = std::max(1.2f, s * 0.09f);
    switch (id) {
    case Id::Select: {
        ImVec2 p[] = {{c.x - s * 0.3f, c.y - s * 0.42f}, {c.x + s * 0.3f, c.y + s * 0.1f}, {c.x + s * 0.02f, c.y + s * 0.12f},
                      {c.x + s * 0.18f, c.y + s * 0.42f}, {c.x + s * 0.05f, c.y + s * 0.47f}, {c.x - s * 0.1f, c.y + s * 0.18f},
                      {c.x - s * 0.3f, c.y + s * 0.36f}};
        dl->AddConvexPolyFilled(p, 3, tint);
        dl->AddTriangleFilled(p[0], p[2], p[6], tint);
        dl->AddQuadFilled(p[5], p[2], p[3], p[4], tint);
        break;
    }
    case Id::Move:
        dl->AddLine(ImVec2(c.x - s * 0.4f, c.y), ImVec2(c.x + s * 0.4f, c.y), tint, th);
        dl->AddLine(ImVec2(c.x, c.y - s * 0.4f), ImVec2(c.x, c.y + s * 0.4f), tint, th);
        arrowHead(dl, ImVec2(c.x + s * 0.46f, c.y), ImVec2(1, 0), s * 0.18f, rgb(230, 80, 80));
        arrowHead(dl, ImVec2(c.x - s * 0.46f, c.y), ImVec2(-1, 0), s * 0.18f, rgb(230, 80, 80));
        arrowHead(dl, ImVec2(c.x, c.y - s * 0.46f), ImVec2(0, -1), s * 0.18f, rgb(90, 200, 90));
        arrowHead(dl, ImVec2(c.x, c.y + s * 0.46f), ImVec2(0, 1), s * 0.18f, rgb(90, 200, 90));
        break;
    case Id::Scale:
        dl->AddRect(ImVec2(c.x - s * 0.22f, c.y - s * 0.22f), ImVec2(c.x + s * 0.22f, c.y + s * 0.22f), tint, 0, 0, th);
        dl->AddLine(ImVec2(c.x + s * 0.12f, c.y - s * 0.12f), ImVec2(c.x + s * 0.4f, c.y - s * 0.4f), rgb(80, 150, 240), th);
        arrowHead(dl, ImVec2(c.x + s * 0.45f, c.y - s * 0.45f), ImVec2(0.707f, -0.707f), s * 0.18f, rgb(80, 150, 240));
        dl->AddLine(ImVec2(c.x - s * 0.12f, c.y + s * 0.12f), ImVec2(c.x - s * 0.4f, c.y + s * 0.4f), rgb(80, 150, 240), th);
        arrowHead(dl, ImVec2(c.x - s * 0.45f, c.y + s * 0.45f), ImVec2(-0.707f, 0.707f), s * 0.18f, rgb(80, 150, 240));
        break;
    case Id::Rotate:
        dl->PathArcTo(c, s * 0.34f, kPi * 0.15f, kPi * 1.75f, 24);
        dl->PathStroke(tint, 0, th);
        arrowHead(dl, ImVec2(c.x + s * 0.34f * std::cos(kPi * 0.15f), c.y + s * 0.34f * std::sin(kPi * 0.15f) + s * 0.05f),
                  ImVec2(-0.3f, 0.95f), s * 0.2f, rgb(240, 200, 60));
        break;
    case Id::Transform:
        draw(dl, ImVec2(c.x - s * 0.18f, c.y - s * 0.18f), s * 0.55f, Id::Move, tint);
        draw(dl, ImVec2(c.x + s * 0.2f, c.y + s * 0.2f), s * 0.5f, Id::Rotate, tint);
        break;
    case Id::Part:
        cube(dl, c, s * 0.9f, rgb(190, 192, 200), rgb(140, 142, 150), rgb(110, 112, 120));
        break;
    case Id::Sphere:
        dl->AddCircleFilled(c, s * 0.4f, rgb(150, 152, 160), 24);
        dl->AddCircleFilled(ImVec2(c.x - s * 0.12f, c.y - s * 0.12f), s * 0.16f, rgb(210, 212, 220), 16);
        break;
    case Id::Cylinder:
        dl->AddRectFilled(ImVec2(c.x - s * 0.3f, c.y - s * 0.28f), ImVec2(c.x + s * 0.3f, c.y + s * 0.3f), rgb(140, 142, 150));
        dl->AddEllipseFilled(ImVec2(c.x, c.y + s * 0.3f), ImVec2(s * 0.3f, s * 0.1f), rgb(140, 142, 150));
        dl->AddEllipseFilled(ImVec2(c.x, c.y - s * 0.28f), ImVec2(s * 0.3f, s * 0.1f), rgb(200, 202, 210));
        break;
    case Id::Plane:
        dl->AddQuadFilled(ImVec2(c.x - s * 0.45f, c.y + s * 0.12f), ImVec2(c.x, c.y - s * 0.12f),
                          ImVec2(c.x + s * 0.45f, c.y + s * 0.12f), ImVec2(c.x, c.y + s * 0.36f), rgb(170, 172, 180));
        break;
    case Id::Model:
        cube(dl, ImVec2(c.x - s * 0.16f, c.y + s * 0.1f), s * 0.52f, rgb(250, 200, 80), rgb(210, 160, 50), rgb(180, 130, 40));
        cube(dl, ImVec2(c.x + s * 0.18f, c.y + s * 0.12f), s * 0.46f, rgb(120, 180, 250), rgb(80, 140, 220), rgb(60, 110, 190));
        cube(dl, ImVec2(c.x + s * 0.02f, c.y - s * 0.2f), s * 0.44f, rgb(130, 220, 120), rgb(90, 180, 80), rgb(70, 150, 60));
        break;
    case Id::Folder:
        dl->AddRectFilled(ImVec2(c.x - s * 0.42f, c.y - s * 0.3f), ImVec2(c.x - s * 0.02f, c.y - s * 0.12f), rgb(220, 170, 60), s * 0.05f);
        dl->AddRectFilled(ImVec2(c.x - s * 0.42f, c.y - s * 0.18f), ImVec2(c.x + s * 0.42f, c.y + s * 0.32f), rgb(240, 195, 80), s * 0.05f);
        break;
    case Id::Script:
        paper(dl, c, s, rgb(235, 238, 245), rgb(70, 130, 220));
        break;
    case Id::ModuleScript:
        paper(dl, c, s, rgb(235, 238, 245), rgb(160, 90, 220));
        break;
    case Id::LocalScript:
        paper(dl, c, s, rgb(235, 238, 245), rgb(60, 180, 110));
        break;
    case Id::Prompt:   // a key badge
        dl->AddRectFilled(ImVec2(c.x - s * 0.36f, c.y - s * 0.36f), ImVec2(c.x + s * 0.36f, c.y + s * 0.36f), rgb(240, 240, 245), s * 0.12f);
        dl->AddRect(ImVec2(c.x - s * 0.36f, c.y - s * 0.36f), ImVec2(c.x + s * 0.36f, c.y + s * 0.36f), rgb(60, 60, 70), s * 0.12f, 0, s * 0.07f);
        dl->AddText(nullptr, s * 0.55f, ImVec2(c.x - s * 0.15f, c.y - s * 0.3f), rgb(30, 30, 36), "E");
        break;
    case Id::Highlight:   // a cube, filled in and outlined
        dl->AddRectFilled(ImVec2(c.x - s * 0.3f, c.y - s * 0.3f), ImVec2(c.x + s * 0.3f, c.y + s * 0.3f), rgb(240, 70, 70), s * 0.06f);
        dl->AddRect(ImVec2(c.x - s * 0.38f, c.y - s * 0.38f), ImVec2(c.x + s * 0.38f, c.y + s * 0.38f), rgb(255, 255, 255), s * 0.1f, 0, s * 0.09f);
        break;
    case Id::Ribbon:   // a wavy streak, fading out
        for (int i = 0; i < 6; ++i) {
            const float x0 = c.x - s * 0.4f + s * 0.8f * i / 6.0f, x1 = c.x - s * 0.4f + s * 0.8f * (i + 1) / 6.0f;
            const float y0 = c.y + std::sin(i * 1.1f) * s * 0.18f, y1 = c.y + std::sin((i + 1) * 1.1f) * s * 0.18f;
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), rgb(255, 170 - i * 15, 60, 255 - i * 30), s * (0.22f - i * 0.025f));
        }
        break;
    case Id::Remote:   // a little envelope
        dl->AddRectFilled(ImVec2(c.x - s * 0.4f, c.y - s * 0.26f), ImVec2(c.x + s * 0.4f, c.y + s * 0.26f), rgb(250, 200, 80), s * 0.05f);
        dl->AddLine(ImVec2(c.x - s * 0.4f, c.y - s * 0.24f), ImVec2(c.x, c.y + s * 0.06f), rgb(150, 100, 30), s * 0.07f);
        dl->AddLine(ImVec2(c.x + s * 0.4f, c.y - s * 0.24f), ImVec2(c.x, c.y + s * 0.06f), rgb(150, 100, 30), s * 0.07f);
        break;
    case Id::Light:
        dl->AddCircleFilled(ImVec2(c.x, c.y - s * 0.08f), s * 0.28f, rgb(255, 220, 90), 20);
        dl->AddRectFilled(ImVec2(c.x - s * 0.13f, c.y + s * 0.16f), ImVec2(c.x + s * 0.13f, c.y + s * 0.38f), rgb(170, 172, 180), s * 0.04f);
        break;
    case Id::Sound: {
        ImVec2 p[] = {{c.x - s * 0.38f, c.y - s * 0.12f}, {c.x - s * 0.18f, c.y - s * 0.12f}, {c.x + s * 0.05f, c.y - s * 0.36f},
                      {c.x + s * 0.05f, c.y + s * 0.36f}, {c.x - s * 0.18f, c.y + s * 0.12f}, {c.x - s * 0.38f, c.y + s * 0.12f}};
        dl->AddConvexPolyFilled(p, 6, rgb(190, 150, 250));
        dl->PathArcTo(ImVec2(c.x + s * 0.05f, c.y), s * 0.25f, -0.8f, 0.8f, 10);
        dl->PathStroke(rgb(190, 150, 250), 0, th);
        dl->PathArcTo(ImVec2(c.x + s * 0.05f, c.y), s * 0.38f, -0.8f, 0.8f, 12);
        dl->PathStroke(rgb(190, 150, 250), 0, th);
        break;
    }
    case Id::Attachment:
        dl->AddCircleFilled(c, s * 0.2f, rgb(90, 230, 120), 16);
        dl->AddCircle(c, s * 0.36f, rgb(90, 230, 120), 20, th);
        break;
    case Id::Constraint:
        dl->AddEllipse(ImVec2(c.x - s * 0.14f, c.y + s * 0.1f), ImVec2(s * 0.22f, s * 0.13f), rgb(240, 170, 90), -0.7f, 16, th);
        dl->AddEllipse(ImVec2(c.x + s * 0.14f, c.y - s * 0.1f), ImVec2(s * 0.22f, s * 0.13f), rgb(240, 170, 90), -0.7f, 16, th);
        break;
    case Id::ForceField:
        dl->AddCircle(c, s * 0.38f, rgb(120, 220, 255), 24, th * 1.4f);
        dl->AddCircleFilled(c, s * 0.3f, rgb(120, 220, 255, 60), 24);
        break;
    case Id::Value: {   // a little tag with a number on it
        dl->AddRectFilled(ImVec2(c.x - s * 0.36f, c.y - s * 0.26f), ImVec2(c.x + s * 0.36f, c.y + s * 0.26f), rgb(90, 170, 120), s * 0.08f);
        dl->AddText(nullptr, s * 0.5f, ImVec2(c.x - s * 0.14f, c.y - s * 0.26f), rgb(255, 255, 255), "1");
        break;
    }
    case Id::Decal: {   // a little picture: sky, sun and a hill
        ImVec2 a(c.x - s * 0.38f, c.y - s * 0.3f), b(c.x + s * 0.38f, c.y + s * 0.3f);
        dl->AddRectFilled(a, b, rgb(120, 180, 240), s * 0.05f);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.18f, c.y - s * 0.1f), s * 0.08f, rgb(255, 220, 90), 12);
        dl->AddTriangleFilled(ImVec2(a.x, b.y), ImVec2(c.x - s * 0.08f, c.y - s * 0.06f), ImVec2(c.x + s * 0.2f, b.y), rgb(80, 170, 90));
        dl->AddRect(a, b, rgb(235, 238, 245), s * 0.05f, 0, th * 0.8f);
        break;
    }
    case Id::ScreenGui: {   // a screen with a little window on it
        ImVec2 a(c.x - s * 0.42f, c.y - s * 0.32f), b(c.x + s * 0.42f, c.y + s * 0.26f);
        dl->AddRectFilled(a, b, rgb(52, 60, 78), s * 0.05f);
        dl->AddRect(a, b, rgb(150, 190, 240), s * 0.05f, 0, th);
        dl->AddRectFilled(ImVec2(c.x - s * 0.22f, c.y - s * 0.16f), ImVec2(c.x + s * 0.22f, c.y + s * 0.1f), rgb(110, 170, 250), s * 0.03f);
        dl->AddLine(ImVec2(c.x - s * 0.15f, b.y + s * 0.1f), ImVec2(c.x + s * 0.15f, b.y + s * 0.1f), rgb(150, 190, 240), th);
        break;
    }
    case Id::GuiFrame: {   // a box with a title bar
        ImVec2 a(c.x - s * 0.38f, c.y - s * 0.3f), b(c.x + s * 0.38f, c.y + s * 0.3f);
        dl->AddRectFilled(a, b, rgb(230, 234, 242), s * 0.04f);
        dl->AddRectFilled(a, ImVec2(b.x, a.y + s * 0.16f), rgb(90, 150, 230), s * 0.04f, ImDrawFlags_RoundCornersTop);
        dl->AddRect(a, b, rgb(90, 150, 230), s * 0.04f, 0, th * 0.8f);
        break;
    }
    case Id::GuiText: {   // a big "T"
        ImU32 col = rgb(235, 238, 245);
        dl->AddRectFilled(ImVec2(c.x - s * 0.3f, c.y - s * 0.32f), ImVec2(c.x + s * 0.3f, c.y - s * 0.2f), col);
        dl->AddRectFilled(ImVec2(c.x - s * 0.07f, c.y - s * 0.32f), ImVec2(c.x + s * 0.07f, c.y + s * 0.34f), col);
        break;
    }
    case Id::GuiButton: {   // a rounded button with a pointer
        ImVec2 a(c.x - s * 0.4f, c.y - s * 0.2f), b(c.x + s * 0.4f, c.y + s * 0.14f);
        dl->AddRectFilled(a, b, rgb(70, 180, 100), s * 0.12f);
        ImVec2 p(c.x + s * 0.05f, c.y);
        dl->AddTriangleFilled(p, ImVec2(p.x, p.y + s * 0.38f), ImVec2(p.x + s * 0.26f, p.y + s * 0.26f), rgb(255, 255, 255));
        break;
    }
    case Id::GuiImage: {   // a framed picture
        ImVec2 a(c.x - s * 0.36f, c.y - s * 0.3f), b(c.x + s * 0.36f, c.y + s * 0.3f);
        dl->AddRectFilled(a, b, rgb(250, 190, 70), s * 0.03f);
        dl->AddTriangleFilled(ImVec2(a.x, b.y), ImVec2(c.x - s * 0.05f, c.y - s * 0.08f), ImVec2(c.x + s * 0.22f, b.y), rgb(200, 90, 50));
        dl->AddRect(a, b, rgb(235, 238, 245), s * 0.03f, 0, th);
        break;
    }
    case Id::GuiCorner: {   // a rounded corner
        dl->PathArcTo(ImVec2(c.x + s * 0.12f, c.y + s * 0.12f), s * 0.36f, 3.14159f, 4.71239f, 12);
        dl->PathStroke(rgb(150, 190, 240), 0, th * 1.6f);
        dl->AddLine(ImVec2(c.x - s * 0.24f, c.y + s * 0.12f), ImVec2(c.x - s * 0.24f, c.y + s * 0.38f), rgb(150, 190, 240), th * 1.6f);
        dl->AddLine(ImVec2(c.x + s * 0.12f, c.y - s * 0.24f), ImVec2(c.x + s * 0.38f, c.y - s * 0.24f), rgb(150, 190, 240), th * 1.6f);
        break;
    }
    case Id::Animation: {   // a film strip with a keyframe diamond
        ImVec2 a(c.x - s * 0.4f, c.y - s * 0.28f), b(c.x + s * 0.4f, c.y + s * 0.28f);
        dl->AddRectFilled(a, b, rgb(70, 76, 92), s * 0.05f);
        for (int i = 0; i < 4; ++i) {
            float x = a.x + s * (0.1f + i * 0.2f);
            dl->AddRectFilled(ImVec2(x - s * 0.04f, a.y + s * 0.04f), ImVec2(x + s * 0.04f, a.y + s * 0.1f), rgb(220, 224, 232));
            dl->AddRectFilled(ImVec2(x - s * 0.04f, b.y - s * 0.1f), ImVec2(x + s * 0.04f, b.y - s * 0.04f), rgb(220, 224, 232));
        }
        float d = s * 0.13f;
        dl->AddQuadFilled(ImVec2(c.x, c.y - d), ImVec2(c.x + d, c.y), ImVec2(c.x, c.y + d), ImVec2(c.x - d, c.y), rgb(255, 200, 60));
        break;
    }
    case Id::Rig: {   // a little stick-figure dummy
        ImU32 col = rgb(160, 200, 255);
        dl->AddCircleFilled(ImVec2(c.x, c.y - s * 0.3f), s * 0.1f, col, 12);
        dl->AddRectFilled(ImVec2(c.x - s * 0.13f, c.y - s * 0.18f), ImVec2(c.x + s * 0.13f, c.y + s * 0.08f), col);
        dl->AddLine(ImVec2(c.x - s * 0.15f, c.y - s * 0.15f), ImVec2(c.x - s * 0.35f, c.y + s * 0.02f), col, th * 1.6f);
        dl->AddLine(ImVec2(c.x + s * 0.15f, c.y - s * 0.15f), ImVec2(c.x + s * 0.35f, c.y - s * 0.3f), col, th * 1.6f);
        dl->AddLine(ImVec2(c.x - s * 0.07f, c.y + s * 0.08f), ImVec2(c.x - s * 0.12f, c.y + s * 0.4f), col, th * 1.8f);
        dl->AddLine(ImVec2(c.x + s * 0.07f, c.y + s * 0.08f), ImVec2(c.x + s * 0.12f, c.y + s * 0.4f), col, th * 1.8f);
        break;
    }
    case Id::Tool: {   // a little sword
        ImVec2 tip(c.x + s * 0.38f, c.y - s * 0.38f), guard(c.x - s * 0.12f, c.y + s * 0.12f);
        dl->AddLine(guard, tip, rgb(200, 210, 225), th * 2.0f);
        dl->AddLine(ImVec2(c.x - s * 0.3f, c.y - s * 0.02f), ImVec2(c.x + s * 0.02f, c.y + s * 0.3f), rgb(230, 180, 60), th * 1.6f);
        dl->AddLine(guard, ImVec2(c.x - s * 0.36f, c.y + s * 0.36f), rgb(150, 100, 50), th * 2.0f);
        break;
    }
    case Id::Workspace:
        dl->AddCircleFilled(c, s * 0.4f, rgb(70, 140, 230), 24);
        dl->AddEllipse(c, ImVec2(s * 0.18f, s * 0.4f), rgb(200, 230, 255), 0, 20, th * 0.8f);
        dl->AddLine(ImVec2(c.x - s * 0.4f, c.y), ImVec2(c.x + s * 0.4f, c.y), rgb(200, 230, 255), th * 0.8f);
        break;
    case Id::Player:
        dl->AddCircleFilled(ImVec2(c.x, c.y - s * 0.22f), s * 0.16f, rgb(250, 210, 80), 16);
        dl->AddRectFilled(ImVec2(c.x - s * 0.22f, c.y - s * 0.04f), ImVec2(c.x + s * 0.22f, c.y + s * 0.4f), rgb(70, 140, 230), s * 0.05f);
        break;
    case Id::Play:
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.26f, c.y - s * 0.36f), ImVec2(c.x + s * 0.36f, c.y), ImVec2(c.x - s * 0.26f, c.y + s * 0.36f), rgb(80, 210, 110));
        break;
    case Id::PlayHere:
        draw(dl, ImVec2(c.x - s * 0.08f, c.y), s * 0.9f, Id::Play, tint);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.3f, c.y + s * 0.28f), s * 0.14f, rgb(250, 200, 60), 12);
        break;
    case Id::Run:
        dl->AddCircle(c, s * 0.3f, rgb(80, 210, 110), 20, th * 1.3f);
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.1f, c.y - s * 0.16f), ImVec2(c.x + s * 0.16f, c.y), ImVec2(c.x - s * 0.1f, c.y + s * 0.16f), rgb(80, 210, 110));
        break;
    case Id::Stop:
        dl->AddRectFilled(ImVec2(c.x - s * 0.3f, c.y - s * 0.3f), ImVec2(c.x + s * 0.3f, c.y + s * 0.3f), rgb(230, 80, 80), s * 0.05f);
        break;
    case Id::Paste:
        dl->AddRectFilled(ImVec2(c.x - s * 0.32f, c.y - s * 0.34f), ImVec2(c.x + s * 0.2f, c.y + s * 0.4f), rgb(190, 150, 100), s * 0.05f);
        dl->AddRectFilled(ImVec2(c.x - s * 0.05f, c.y - s * 0.14f), ImVec2(c.x + s * 0.38f, c.y + s * 0.44f), rgb(235, 238, 245), s * 0.04f);
        break;
    case Id::Copy:
        dl->AddRect(ImVec2(c.x - s * 0.36f, c.y - s * 0.36f), ImVec2(c.x + s * 0.12f, c.y + s * 0.2f), tint, s * 0.05f, 0, th);
        dl->AddRectFilled(ImVec2(c.x - s * 0.12f, c.y - s * 0.12f), ImVec2(c.x + s * 0.36f, c.y + s * 0.4f), tint, s * 0.05f);
        break;
    case Id::Cut:
        dl->AddCircle(ImVec2(c.x - s * 0.18f, c.y + s * 0.26f), s * 0.12f, tint, 12, th);
        dl->AddCircle(ImVec2(c.x + s * 0.18f, c.y + s * 0.26f), s * 0.12f, tint, 12, th);
        dl->AddLine(ImVec2(c.x - s * 0.1f, c.y + s * 0.16f), ImVec2(c.x + s * 0.2f, c.y - s * 0.4f), tint, th);
        dl->AddLine(ImVec2(c.x + s * 0.1f, c.y + s * 0.16f), ImVec2(c.x - s * 0.2f, c.y - s * 0.4f), tint, th);
        break;
    case Id::Duplicate:
        cube(dl, ImVec2(c.x - s * 0.12f, c.y + s * 0.1f), s * 0.5f, rgb(190, 192, 200), rgb(140, 142, 150), rgb(110, 112, 120));
        cube(dl, ImVec2(c.x + s * 0.14f, c.y - s * 0.12f), s * 0.5f, rgb(160, 200, 250), rgb(110, 160, 220), rgb(80, 130, 200));
        break;
    case Id::Undo:
    case Id::Redo: {
        float d = id == Id::Undo ? -1.0f : 1.0f;
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.1f), s * 0.28f, id == Id::Undo ? kPi * 1.1f : -kPi * 0.1f,
                      id == Id::Undo ? kPi * 2.05f : kPi * 0.95f - kPi, 16);
        dl->PathStroke(tint, 0, th);
        arrowHead(dl, ImVec2(c.x + d * s * 0.34f, c.y + s * 0.18f), ImVec2(0, 1), s * 0.18f, tint);
        break;
    }
    case Id::Delete:
        dl->AddLine(ImVec2(c.x - s * 0.3f, c.y - s * 0.3f), ImVec2(c.x + s * 0.3f, c.y + s * 0.3f), rgb(230, 80, 80), th * 1.4f);
        dl->AddLine(ImVec2(c.x + s * 0.3f, c.y - s * 0.3f), ImVec2(c.x - s * 0.3f, c.y + s * 0.3f), rgb(230, 80, 80), th * 1.4f);
        break;
    case Id::Group:
        dl->AddRect(ImVec2(c.x - s * 0.42f, c.y - s * 0.42f), ImVec2(c.x + s * 0.42f, c.y + s * 0.42f), tint, 0, 0, th * 0.8f);
        draw(dl, c, s * 0.7f, Id::Model, tint);
        break;
    case Id::Ungroup:
        cube(dl, ImVec2(c.x - s * 0.22f, c.y + s * 0.16f), s * 0.4f, rgb(250, 200, 80), rgb(210, 160, 50), rgb(180, 130, 40));
        cube(dl, ImVec2(c.x + s * 0.24f, c.y - s * 0.18f), s * 0.4f, rgb(120, 180, 250), rgb(80, 140, 220), rgb(60, 110, 190));
        break;
    case Id::Lock:
        dl->PathArcTo(ImVec2(c.x, c.y - s * 0.08f), s * 0.2f, kPi, kPi * 2, 12);
        dl->PathStroke(tint, 0, th * 1.2f);
        dl->AddRectFilled(ImVec2(c.x - s * 0.3f, c.y - s * 0.08f), ImVec2(c.x + s * 0.3f, c.y + s * 0.38f), rgb(240, 190, 70), s * 0.05f);
        break;
    case Id::Anchor:
        dl->AddLine(ImVec2(c.x, c.y - s * 0.3f), ImVec2(c.x, c.y + s * 0.36f), tint, th * 1.2f);
        dl->AddCircle(ImVec2(c.x, c.y - s * 0.36f), s * 0.09f, tint, 10, th);
        dl->AddLine(ImVec2(c.x - s * 0.18f, c.y - s * 0.12f), ImVec2(c.x + s * 0.18f, c.y - s * 0.12f), tint, th);
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.04f), s * 0.32f, kPi * 0.15f, kPi * 0.85f, 14);
        dl->PathStroke(tint, 0, th * 1.2f);
        break;
    case Id::Snap:
        for (int i = -1; i <= 1; ++i) {
            dl->AddLine(ImVec2(c.x + i * s * 0.25f, c.y - s * 0.4f), ImVec2(c.x + i * s * 0.25f, c.y + s * 0.4f), tint, th * 0.6f);
            dl->AddLine(ImVec2(c.x - s * 0.4f, c.y + i * s * 0.25f), ImVec2(c.x + s * 0.4f, c.y + i * s * 0.25f), tint, th * 0.6f);
        }
        dl->AddCircleFilled(ImVec2(c.x + s * 0.25f, c.y - s * 0.25f), s * 0.1f, rgb(80, 150, 240), 10);
        break;
    case Id::NavMesh: {   // a blue floor with a dotted route across it
        ImVec2 q[4] = {ImVec2(c.x - s * 0.45f, c.y + s * 0.25f), ImVec2(c.x - s * 0.15f, c.y - s * 0.3f),
                       ImVec2(c.x + s * 0.45f, c.y - s * 0.3f), ImVec2(c.x + s * 0.15f, c.y + s * 0.25f)};
        dl->AddConvexPolyFilled(q, 4, IM_COL32(60, 140, 240, 150));
        dl->AddPolyline(q, 4, rgb(80, 150, 240), ImDrawFlags_Closed, th * 0.6f);
        for (int i = 0; i < 4; ++i)
            dl->AddCircleFilled(ImVec2(c.x - s * 0.22f + i * s * 0.15f, c.y + s * 0.1f - i * s * 0.12f), s * 0.05f, rgb(255, 220, 60), 8);
        break;
    }
    case Id::Bake: {   // the floor plus a little oven flame
        ImVec2 q[4] = {ImVec2(c.x - s * 0.45f, c.y + s * 0.35f), ImVec2(c.x - s * 0.2f, c.y + s * 0.05f),
                       ImVec2(c.x + s * 0.45f, c.y + s * 0.05f), ImVec2(c.x + s * 0.2f, c.y + s * 0.35f)};
        dl->AddConvexPolyFilled(q, 4, IM_COL32(60, 140, 240, 170));
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.12f, c.y - s * 0.02f), ImVec2(c.x + s * 0.12f, c.y - s * 0.02f),
                              ImVec2(c.x, c.y - s * 0.45f), rgb(255, 140, 40));
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.06f, c.y - s * 0.02f), ImVec2(c.x + s * 0.06f, c.y - s * 0.02f),
                              ImVec2(c.x, c.y - s * 0.25f), rgb(255, 230, 90));
        break;
    }
    // Terrain: green hills (with a snowy peak), and what each brush does to them.
    case Id::Terrain: case Id::TerrainRaise: case Id::TerrainLower: case Id::TerrainSmooth:
    case Id::TerrainFlatten: case Id::TerrainPaint: case Id::TerrainClear: {
        const float base = c.y + s * 0.38f;
        if (id == Id::TerrainFlatten) {
            dl->AddRectFilled(ImVec2(c.x - s * 0.45f, c.y + s * 0.05f), ImVec2(c.x + s * 0.45f, base), rgb(90, 160, 70));
            dl->AddLine(ImVec2(c.x - s * 0.45f, c.y + s * 0.05f), ImVec2(c.x + s * 0.45f, c.y + s * 0.05f), rgb(255, 150, 40), th);
            break;
        }
        const bool smooth = id == Id::TerrainSmooth;
        ImU32 hill = id == Id::TerrainClear ? rgb(120, 120, 120, 150) : id == Id::TerrainPaint ? rgb(200, 170, 90) : rgb(90, 160, 70);
        // Two hills (round ones for Smooth).
        if (smooth) {
            dl->PathArcTo(ImVec2(c.x - s * 0.12f, base), s * 0.36f, kPi, kPi * 2.0f, 16);
            dl->PathFillConvex(hill);
            dl->PathArcTo(ImVec2(c.x + s * 0.22f, base), s * 0.24f, kPi, kPi * 2.0f, 12);
            dl->PathFillConvex(hill);
        } else {
            dl->AddTriangleFilled(ImVec2(c.x - s * 0.48f, base), ImVec2(c.x - s * 0.08f, c.y - s * 0.3f), ImVec2(c.x + s * 0.3f, base), hill);
            dl->AddTriangleFilled(ImVec2(c.x - s * 0.05f, base), ImVec2(c.x + s * 0.24f, c.y - s * 0.05f), ImVec2(c.x + s * 0.48f, base), hill);
            if (id == Id::Terrain)
                dl->AddTriangleFilled(ImVec2(c.x - s * 0.19f, c.y - s * 0.12f), ImVec2(c.x - s * 0.08f, c.y - s * 0.3f),
                                      ImVec2(c.x + s * 0.03f, c.y - s * 0.12f), rgb(240, 242, 248));
        }
        if (id == Id::TerrainRaise) arrowHead(dl, ImVec2(c.x + s * 0.3f, c.y - s * 0.38f), ImVec2(0, -1), s * 0.2f, rgb(255, 150, 40));
        if (id == Id::TerrainLower) arrowHead(dl, ImVec2(c.x + s * 0.3f, c.y - s * 0.2f), ImVec2(0, 1), s * 0.2f, rgb(255, 150, 40));
        if (id == Id::TerrainPaint) {   // a brush
            dl->AddLine(ImVec2(c.x + s * 0.42f, c.y - s * 0.45f), ImVec2(c.x + s * 0.12f, c.y - s * 0.12f), rgb(160, 110, 60), th * 1.4f);
            dl->AddCircleFilled(ImVec2(c.x + s * 0.08f, c.y - s * 0.08f), s * 0.1f, rgb(90, 200, 90), 10);
        }
        if (id == Id::TerrainClear) {
            dl->AddLine(ImVec2(c.x - s * 0.3f, c.y - s * 0.3f), ImVec2(c.x + s * 0.3f, c.y + s * 0.3f), rgb(230, 80, 80), th * 1.3f);
            dl->AddLine(ImVec2(c.x + s * 0.3f, c.y - s * 0.3f), ImVec2(c.x - s * 0.3f, c.y + s * 0.3f), rgb(230, 80, 80), th * 1.3f);
        }
        break;
    }
    // Solid modeling: a grey block and a round shape overlapping, joined / cut / overlapped / apart.
    case Id::Union:
        dl->AddRectFilled(ImVec2(c.x - s * 0.42f, c.y - s * 0.1f), ImVec2(c.x + s * 0.1f, c.y + s * 0.4f), rgb(150, 152, 160));
        dl->AddCircleFilled(ImVec2(c.x + s * 0.12f, c.y - s * 0.08f), s * 0.3f, rgb(150, 152, 160));
        break;
    case Id::Negate:
        dl->AddRectFilled(ImVec2(c.x - s * 0.35f, c.y - s * 0.35f), ImVec2(c.x + s * 0.35f, c.y + s * 0.35f), IM_COL32(255, 110, 110, 150));
        dl->AddRect(ImVec2(c.x - s * 0.35f, c.y - s * 0.35f), ImVec2(c.x + s * 0.35f, c.y + s * 0.35f), rgb(230, 80, 80), 0, 0, th);
        break;
    case Id::Intersect:
        dl->AddRect(ImVec2(c.x - s * 0.42f, c.y - s * 0.1f), ImVec2(c.x + s * 0.1f, c.y + s * 0.4f), tint, 0, 0, th * 0.6f);
        dl->AddCircle(ImVec2(c.x + s * 0.12f, c.y - s * 0.08f), s * 0.3f, tint, 0, th * 0.6f);
        dl->AddRectFilled(ImVec2(c.x - s * 0.16f, c.y - s * 0.1f), ImVec2(c.x + s * 0.1f, c.y + s * 0.2f), rgb(80, 150, 240));
        break;
    case Id::Separate:
        dl->AddRectFilled(ImVec2(c.x - s * 0.45f, c.y - s * 0.05f), ImVec2(c.x - s * 0.05f, c.y + s * 0.4f), rgb(150, 152, 160));
        dl->AddCircleFilled(ImVec2(c.x + s * 0.2f, c.y - s * 0.15f), s * 0.25f, rgb(150, 152, 160));
        break;
    case Id::Collide:
        dl->AddRectFilled(ImVec2(c.x - s * 0.42f, c.y - s * 0.2f), ImVec2(c.x - s * 0.02f, c.y + s * 0.2f), rgb(140, 142, 150));
        dl->AddRectFilled(ImVec2(c.x + s * 0.02f, c.y - s * 0.3f), ImVec2(c.x + s * 0.42f, c.y + s * 0.1f), rgb(80, 150, 240));
        break;
    case Id::Align:
        dl->AddLine(ImVec2(c.x - s * 0.4f, c.y - s * 0.4f), ImVec2(c.x - s * 0.4f, c.y + s * 0.4f), tint, th);
        dl->AddRectFilled(ImVec2(c.x - s * 0.34f, c.y - s * 0.3f), ImVec2(c.x + s * 0.3f, c.y - s * 0.06f), rgb(80, 150, 240));
        dl->AddRectFilled(ImVec2(c.x - s * 0.34f, c.y + s * 0.06f), ImVec2(c.x + s * 0.06f, c.y + s * 0.3f), rgb(140, 142, 150));
        break;
    case Id::Insert:
        dl->AddLine(ImVec2(c.x - s * 0.34f, c.y), ImVec2(c.x + s * 0.34f, c.y), rgb(80, 210, 110), th * 1.5f);
        dl->AddLine(ImVec2(c.x, c.y - s * 0.34f), ImVec2(c.x, c.y + s * 0.34f), rgb(80, 210, 110), th * 1.5f);
        break;
    case Id::Toolbox:
        dl->AddRectFilled(ImVec2(c.x - s * 0.4f, c.y - s * 0.1f), ImVec2(c.x + s * 0.4f, c.y + s * 0.36f), rgb(220, 90, 70), s * 0.05f);
        dl->AddRect(ImVec2(c.x - s * 0.16f, c.y - s * 0.3f), ImVec2(c.x + s * 0.16f, c.y - s * 0.1f), tint, s * 0.04f, 0, th);
        break;
    case Id::Explorer:
        for (int i = 0; i < 3; ++i) {
            float y = c.y - s * 0.3f + i * s * 0.3f;
            float x = c.x - s * 0.35f + (i ? s * 0.18f : 0);
            dl->AddRectFilled(ImVec2(x, y - s * 0.08f), ImVec2(x + s * 0.14f, y + s * 0.08f), rgb(80, 150, 240));
            dl->AddLine(ImVec2(x + s * 0.2f, y), ImVec2(c.x + s * 0.4f, y), tint, th * 0.8f);
        }
        break;
    case Id::Properties:
        for (int i = 0; i < 3; ++i) {
            float y = c.y - s * 0.28f + i * s * 0.28f;
            dl->AddLine(ImVec2(c.x - s * 0.38f, y), ImVec2(c.x - s * 0.02f, y), tint, th * 0.8f);
            dl->AddRectFilled(ImVec2(c.x + s * 0.06f, y - s * 0.07f), ImVec2(c.x + s * 0.38f, y + s * 0.07f), rgb(80, 150, 240), s * 0.03f);
        }
        break;
    case Id::Output:
        dl->AddRectFilled(ImVec2(c.x - s * 0.4f, c.y - s * 0.34f), ImVec2(c.x + s * 0.4f, c.y + s * 0.34f), rgb(40, 42, 48), s * 0.05f);
        dl->AddLine(ImVec2(c.x - s * 0.28f, c.y - s * 0.14f), ImVec2(c.x + s * 0.2f, c.y - s * 0.14f), rgb(120, 220, 120), th * 0.8f);
        dl->AddLine(ImVec2(c.x - s * 0.28f, c.y + s * 0.06f), ImVec2(c.x + s * 0.28f, c.y + s * 0.06f), tint, th * 0.8f);
        dl->AddLine(ImVec2(c.x - s * 0.28f, c.y + s * 0.2f), ImVec2(c.x, c.y + s * 0.2f), rgb(230, 90, 90), th * 0.8f);
        break;
    case Id::CommandBar:
        dl->AddRectFilled(ImVec2(c.x - s * 0.42f, c.y - s * 0.22f), ImVec2(c.x + s * 0.42f, c.y + s * 0.22f), rgb(40, 42, 48), s * 0.05f);
        dl->AddLine(ImVec2(c.x - s * 0.3f, c.y - s * 0.1f), ImVec2(c.x - s * 0.16f, c.y), rgb(120, 220, 120), th);
        dl->AddLine(ImVec2(c.x - s * 0.16f, c.y), ImVec2(c.x - s * 0.3f, c.y + s * 0.1f), rgb(120, 220, 120), th);
        dl->AddLine(ImVec2(c.x - s * 0.08f, c.y + s * 0.1f), ImVec2(c.x + s * 0.2f, c.y + s * 0.1f), tint, th);
        break;
    case Id::Settings:
        for (int i = 0; i < 8; ++i) {
            float a = i * kPi / 4;
            dl->AddLine(ImVec2(c.x + std::cos(a) * s * 0.2f, c.y + std::sin(a) * s * 0.2f),
                        ImVec2(c.x + std::cos(a) * s * 0.42f, c.y + std::sin(a) * s * 0.42f), tint, th * 1.6f);
        }
        dl->AddCircleFilled(c, s * 0.28f, tint, 16);
        dl->AddCircleFilled(c, s * 0.12f, rgb(46, 46, 46), 12);
        break;
    case Id::Team:
        draw(dl, ImVec2(c.x - s * 0.16f, c.y), s * 0.7f, Id::Player, tint);
        draw(dl, ImVec2(c.x + s * 0.18f, c.y + s * 0.05f), s * 0.7f, Id::Player, tint);
        break;
    case Id::Keyboard:
        dl->AddRect(ImVec2(c.x - s * 0.44f, c.y - s * 0.26f), ImVec2(c.x + s * 0.44f, c.y + s * 0.26f), tint, s * 0.06f, 0, th);
        for (int r = 0; r < 2; ++r)
            for (int k = 0; k < 4; ++k)
                dl->AddRectFilled(ImVec2(c.x - s * 0.34f + k * s * 0.18f, c.y - s * 0.16f + r * s * 0.16f),
                                  ImVec2(c.x - s * 0.24f + k * s * 0.18f, c.y - s * 0.06f + r * s * 0.16f), tint);
        break;
    case Id::Import:
    case Id::Export: {
        dl->AddRect(ImVec2(c.x - s * 0.38f, c.y - s * 0.1f), ImVec2(c.x + s * 0.38f, c.y + s * 0.4f), tint, s * 0.05f, 0, th);
        float dir = id == Id::Import ? 1.0f : -1.0f;
        dl->AddLine(ImVec2(c.x, c.y - s * 0.42f), ImVec2(c.x, c.y + s * 0.16f), rgb(80, 150, 240), th * 1.3f);
        arrowHead(dl, ImVec2(c.x, id == Id::Import ? c.y + s * 0.2f : c.y - s * 0.46f), ImVec2(0, dir), s * 0.2f, rgb(80, 150, 240));
        break;
    }
    // --- Modes ---
    case Id::Build:     // a brick with a hammer handle
        cube(dl, ImVec2(c.x - s * 0.08f, c.y + s * 0.08f), s * 0.7f, rgb(120, 170, 240), rgb(70, 120, 200), rgb(50, 95, 170));
        dl->AddLine(ImVec2(c.x + s * 0.1f, c.y - s * 0.12f), ImVec2(c.x + s * 0.42f, c.y - s * 0.42f), rgb(200, 150, 90), th * 1.3f);
        dl->AddRectFilled(ImVec2(c.x + s * 0.2f, c.y - s * 0.48f), ImVec2(c.x + s * 0.48f, c.y - s * 0.3f), tint, s * 0.03f);
        break;
    case Id::Mesh: {    // a wireframe cube with orange corner points
        float h = s * 0.3f;
        ImVec2 f0(c.x - h, c.y - h * 0.6f), f1(c.x + h * 0.6f, c.y - h * 0.6f), f2(c.x + h * 0.6f, c.y + h), f3(c.x - h, c.y + h);
        ImVec2 o(h * 0.45f, -h * 0.45f);
        ImVec2 b0(f0.x + o.x, f0.y + o.y), b1(f1.x + o.x, f1.y + o.y), b2(f2.x + o.x, f2.y + o.y);
        ImU32 line = tint;
        dl->AddQuad(f0, f1, f2, f3, line, th * 0.8f);
        dl->AddLine(f0, b0, line, th * 0.8f); dl->AddLine(f1, b1, line, th * 0.8f); dl->AddLine(f2, b2, line, th * 0.8f);
        dl->AddLine(b0, b1, line, th * 0.8f); dl->AddLine(b1, b2, line, th * 0.8f);
        for (ImVec2 p : {f0, f1, f2, f3, b0, b1, b2}) dl->AddCircleFilled(p, s * 0.07f, rgb(255, 150, 40));
        break;
    }
    case Id::Simulate:  // a ball bouncing
        dl->AddCircleFilled(ImVec2(c.x + s * 0.12f, c.y - s * 0.12f), s * 0.2f, rgb(90, 200, 120), 16);
        dl->AddLine(ImVec2(c.x - s * 0.42f, c.y + s * 0.38f), ImVec2(c.x + s * 0.42f, c.y + s * 0.38f), tint, th);
        dl->PathArcTo(ImVec2(c.x - s * 0.12f, c.y + s * 0.3f), s * 0.28f, kPi * 1.05f, kPi * 1.6f, 10);
        dl->PathStroke(rgb(90, 200, 120, 150), 0, th * 0.8f);
        break;
    case Id::Pause:
        dl->AddRectFilled(ImVec2(c.x - s * 0.26f, c.y - s * 0.32f), ImVec2(c.x - s * 0.08f, c.y + s * 0.32f), rgb(240, 200, 60), s * 0.04f);
        dl->AddRectFilled(ImVec2(c.x + s * 0.08f, c.y - s * 0.32f), ImVec2(c.x + s * 0.26f, c.y + s * 0.32f), rgb(240, 200, 60), s * 0.04f);
        break;
    case Id::Step:
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.3f, c.y - s * 0.3f), ImVec2(c.x + s * 0.15f, c.y), ImVec2(c.x - s * 0.3f, c.y + s * 0.3f), rgb(90, 200, 120));
        dl->AddRectFilled(ImVec2(c.x + s * 0.18f, c.y - s * 0.3f), ImVec2(c.x + s * 0.3f, c.y + s * 0.3f), rgb(90, 200, 120));
        break;
    // --- Modeling ---
    case Id::Vertex:
    case Id::Edge:
    case Id::Face: {    // a square showing which part is picked
        ImVec2 a(c.x - s * 0.32f, c.y - s * 0.32f), b(c.x + s * 0.32f, c.y + s * 0.32f);
        ImU32 hi = rgb(255, 150, 40), dim = rgb(150, 150, 150);
        if (id == Id::Face) dl->AddRectFilled(a, b, rgb(255, 150, 40, 110));
        dl->AddRect(a, b, dim, 0, 0, th * 0.8f);
        if (id == Id::Edge) dl->AddLine(ImVec2(a.x, b.y), b, hi, th * 1.6f);
        for (ImVec2 p : {a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y)})
            dl->AddCircleFilled(p, s * 0.08f, id == Id::Vertex && p.x == b.x && p.y == b.y ? hi : dim);
        break;
    }
    case Id::Extrude:
        dl->AddRectFilled(ImVec2(c.x - s * 0.36f, c.y + s * 0.12f), ImVec2(c.x + s * 0.36f, c.y + s * 0.4f), rgb(130, 132, 140));
        dl->AddRect(ImVec2(c.x - s * 0.36f, c.y - s * 0.3f), ImVec2(c.x + s * 0.36f, c.y + s * 0.12f), rgb(255, 150, 40), 0, 0, th);
        arrowHead(dl, ImVec2(c.x, c.y - s * 0.46f), ImVec2(0, -1), s * 0.16f, tint);
        break;
    case Id::Inset:
        dl->AddRect(ImVec2(c.x - s * 0.4f, c.y - s * 0.4f), ImVec2(c.x + s * 0.4f, c.y + s * 0.4f), tint, 0, 0, th);
        dl->AddRectFilled(ImVec2(c.x - s * 0.2f, c.y - s * 0.2f), ImVec2(c.x + s * 0.2f, c.y + s * 0.2f), rgb(255, 150, 40, 170));
        break;
    case Id::Subdivide:
        dl->AddRect(ImVec2(c.x - s * 0.4f, c.y - s * 0.4f), ImVec2(c.x + s * 0.4f, c.y + s * 0.4f), tint, 0, 0, th);
        dl->AddLine(ImVec2(c.x, c.y - s * 0.4f), ImVec2(c.x, c.y + s * 0.4f), rgb(255, 150, 40), th);
        dl->AddLine(ImVec2(c.x - s * 0.4f, c.y), ImVec2(c.x + s * 0.4f, c.y), rgb(255, 150, 40), th);
        break;
    case Id::Merge:
        for (ImVec2 p : {ImVec2(c.x - s * 0.36f, c.y - s * 0.3f), ImVec2(c.x + s * 0.36f, c.y - s * 0.3f), ImVec2(c.x, c.y + s * 0.4f)}) {
            dl->AddLine(p, c, tint, th * 0.8f);
            dl->AddCircleFilled(p, s * 0.07f, tint);
        }
        dl->AddCircleFilled(c, s * 0.12f, rgb(255, 150, 40));
        break;
    case Id::Fill: {
        ImVec2 p[] = {{c.x - s * 0.38f, c.y + s * 0.3f}, {c.x, c.y - s * 0.4f}, {c.x + s * 0.38f, c.y + s * 0.3f}};
        dl->AddTriangleFilled(p[0], p[1], p[2], rgb(255, 150, 40, 140));
        for (ImVec2 q : p) dl->AddCircleFilled(q, s * 0.08f, tint);
        break;
    }
    case Id::Flip:
        dl->PathArcTo(c, s * 0.3f, kPi * 0.9f, kPi * 2.1f, 16);
        dl->PathStroke(tint, 0, th);
        arrowHead(dl, ImVec2(c.x + s * 0.3f, c.y + s * 0.12f), ImVec2(0, 1), s * 0.16f, tint);
        arrowHead(dl, ImVec2(c.x - s * 0.3f, c.y + s * 0.12f), ImVec2(0, 1), s * 0.16f, rgb(255, 150, 40));
        break;
    case Id::Smooth:
        dl->AddCircleFilled(c, s * 0.36f, rgb(150, 155, 170), 24);
        dl->AddCircleFilled(ImVec2(c.x - s * 0.12f, c.y - s * 0.12f), s * 0.14f, rgb(230, 232, 240, 200), 16);
        break;
    case Id::XRay:
        dl->AddRect(ImVec2(c.x - s * 0.36f, c.y - s * 0.36f), ImVec2(c.x + s * 0.2f, c.y + s * 0.2f), tint, 0, 0, th * 0.8f);
        dl->AddRectFilled(ImVec2(c.x - s * 0.2f, c.y - s * 0.2f), ImVec2(c.x + s * 0.36f, c.y + s * 0.36f), rgb(120, 170, 240, 130));
        break;
    case Id::Done:
        dl->AddLine(ImVec2(c.x - s * 0.34f, c.y), ImVec2(c.x - s * 0.08f, c.y + s * 0.28f), rgb(90, 200, 120), th * 1.5f);
        dl->AddLine(ImVec2(c.x - s * 0.08f, c.y + s * 0.28f), ImVec2(c.x + s * 0.38f, c.y - s * 0.3f), rgb(90, 200, 120), th * 1.5f);
        break;
    case Id::Lighting:
        dl->AddCircleFilled(c, s * 0.2f, rgb(255, 210, 80), 16);
        for (int i = 0; i < 8; ++i) {
            float a = i * kPi / 4;
            dl->AddLine(ImVec2(c.x + std::cos(a) * s * 0.28f, c.y + std::sin(a) * s * 0.28f),
                        ImVec2(c.x + std::cos(a) * s * 0.42f, c.y + std::sin(a) * s * 0.42f), rgb(255, 210, 80), th);
        }
        break;
    }
}

Id forNode(const SceneNode& n) {
    switch (n.kind) {
        case NodeKind::Model:      return n.parent ? Id::Model : Id::Workspace;
        case NodeKind::Script:     return n.isModule ? Id::ModuleScript : n.isLocal ? Id::LocalScript : Id::Script;
        case NodeKind::Remote:     return Id::Remote;
        case NodeKind::Prompt:     return Id::Prompt;
        case NodeKind::Highlight:  return Id::Highlight;
        case NodeKind::Trail:
        case NodeKind::Beam:       return Id::Ribbon;
        case NodeKind::Light:      return Id::Light;
        case NodeKind::Sound:
        case NodeKind::SoundGroup:
        case NodeKind::SoundEffect: return Id::Sound;
        case NodeKind::Attachment: return Id::Attachment;
        case NodeKind::Constraint: return Id::Constraint;
        case NodeKind::ForceField: return Id::ForceField;
        case NodeKind::Tool:       return Id::Tool;
        case NodeKind::Value:      return Id::Value;
        case NodeKind::Decal:      return Id::Decal;
        case NodeKind::Animation:  return Id::Animation;
        case NodeKind::FluidSystem:  return Id::Value;    // (a liquid: plain icons for now)
        case NodeKind::FluidEmitter: return Id::Sound;
        case NodeKind::Mover:        return Id::Constraint;
        case NodeKind::Gui:
            switch (n.gui.type) {
                case GuiType::ScreenGui: case GuiType::BillboardGui: case GuiType::SurfaceGui: return Id::ScreenGui;
                case GuiType::TextLabel: case GuiType::TextBox: return Id::GuiText;
                case GuiType::TextButton: case GuiType::ImageButton: return Id::GuiButton;
                case GuiType::ImageLabel: return Id::GuiImage;
                case GuiType::UICorner: case GuiType::UIStroke: case GuiType::UIShadow: case GuiType::UIBlur:
                case GuiType::UIListLayout: case GuiType::UIGridLayout: case GuiType::UIPadding: return Id::GuiCorner;
                default:                  return Id::GuiFrame;
            }
        default: break;
    }
    switch (n.primitiveType) {
        case PrimitiveType::Sphere:   return Id::Sphere;
        case PrimitiveType::Cylinder: return Id::Cylinder;
        case PrimitiveType::Plane:    return Id::Plane;
        case PrimitiveType::Mesh:     return Id::Mesh;
        default:                      return Id::Part;
    }
}

void inlineIcon(Id id, float size) {
    if (size <= 0.0f) size = ImGui::GetTextLineHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    draw(ImGui::GetWindowDrawList(), ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size, id);
}

} // namespace Icons
