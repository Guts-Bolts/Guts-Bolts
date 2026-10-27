#include "GameGui.h"
#include "../core/I18n.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../renderer/Textures.h"
#include "../renderer/ViewportRender.h"
#include "../renderer/Mesh.h"
#include "../scene/Physics.h"

#include <imgui_internal.h>   // ImGui::ShadeVertsLinearUV (pictures in odd shapes)
#include <misc/cpp/imgui_stdlib.h>   // InputText into a std::string (TextBox)

#include <algorithm>
#include <cmath>
#include <optional>
#include <glm/glm.hpp>
#include <unordered_map>

namespace GameGui {

namespace {

// One object to draw: where it goes and what it's clipped to.
struct Item {
    SceneNode* node;
    ImVec2     a, b;        // its rectangle
    ImVec2     clipA, clipB;
    bool       bars = false;   // a ScrollingFrame's scroll bars (drawn after what's inside it)
    int        surface = -1;   // on a SurfaceGui (g_surfaces[surface]): a, b and the clip are canvas pixels
};

ImU32 rgba(const glm::vec3& c, float transparency) {
    float alpha = std::clamp(1.0f - transparency, 0.0f, 1.0f);
    return IM_COL32((int)(std::clamp(c.r, 0.0f, 1.0f) * 255), (int)(std::clamp(c.g, 0.0f, 1.0f) * 255),
                    (int)(std::clamp(c.b, 0.0f, 1.0f) * 255), (int)(alpha * 255));
}

const SceneNode* childOfType(const SceneNode* n, GuiType t) {
    // (UICorner, UIScale and UIAspectRatioConstraint have no Enabled: always on)
    const bool always = t == GuiType::UICorner || t == GuiType::UIScale || t == GuiType::UIAspectRatioConstraint;
    for (auto& c : n->children)
        if (c->isGui() && c->gui.type == t && (always || c->enabled)) return c.get();
    return nullptr;
}

// UIScale: how much bigger it (and everything inside it) is drawn.
float ownScale(const SceneNode* n) {
    const SceneNode* s = childOfType(n, GuiType::UIScale);
    return s ? std::clamp(s->gui.scale, 0.0f, 100.0f) : 1.0f;
}

// How round each corner is (pixels): top-left, top-right, bottom-right, bottom-left.
struct Radii {
    float r[4] = {0, 0, 0, 0};
    bool same() const { return r[0] == r[1] && r[1] == r[2] && r[2] == r[3]; }
};

Radii radiiOf(const SceneNode* n, float w, float h) {
    Radii out;
    const SceneNode* c = childOfType(n, GuiType::UICorner);
    if (!c) return out;
    const GuiProps& k = c->gui;
    const float px = n->gui.absScale;   // (pixels grow with UIScale)
    const float most = std::max(0.0f, std::min(w, h) * 0.5f);
    for (int i = 0; i < 4; ++i) {
        const float v = k.corners[i] < 0 ? k.corner.xs * std::min(w, h) + k.corner.xo * px
                                         : k.cornerScales[i] * std::min(w, h) + k.corners[i] * px;
        out.r[i] = std::clamp(v, 0.0f, most);
    }
    return out;
}

// The outline of a box with its own roundness at each corner, as ImGui's current path
// (clockwise from the top-left, like ImGui's own rounded boxes).
void pathRounded(ImDrawList* dl, ImVec2 a, ImVec2 b, const Radii& r) {
    const float pi = 3.14159265f;
    const ImVec2 c[4] = {ImVec2(a.x + r.r[0], a.y + r.r[0]), ImVec2(b.x - r.r[1], a.y + r.r[1]),
                         ImVec2(b.x - r.r[2], b.y - r.r[2]), ImVec2(a.x + r.r[3], b.y - r.r[3])};
    dl->PathClear();
    for (int i = 0; i < 4; ++i) {
        if (r.r[i] < 0.5f) dl->PathLineTo(c[i]);
        else dl->PathArcTo(c[i], r.r[i], pi + i * pi * 0.5f, pi + (i + 1) * pi * 0.5f);
    }
}

void fillRounded(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, const Radii& r) {
    if (r.same()) { dl->AddRectFilled(a, b, col, r.r[0]); return; }
    pathRounded(dl, a, b, r);
    dl->PathFillConvex(col);
}

void strokeRounded(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, const Radii& r, float thickness) {
    if (r.same()) { dl->AddRect(a, b, col, r.r[0], 0, thickness); return; }
    pathRounded(dl, a, b, r);
    dl->PathStroke(col, ImDrawFlags_Closed, thickness);
}

// A picture stretched over [a, b] (uv0..uv1 across it), cut to the rounded shape.
void imageRounded(ImDrawList* dl, unsigned tex, ImVec2 a, ImVec2 b, ImVec2 uv0, ImVec2 uv1, ImU32 col, const Radii& r) {
    const ImTextureID id = (ImTextureID)(intptr_t)tex;
    if (r.same()) { dl->AddImageRounded(id, a, b, uv0, uv1, col, r.r[0]); return; }
    dl->PushTexture(id);
    const int start = dl->VtxBuffer.Size;
    pathRounded(dl, a, b, r);
    dl->PathFillConvex(col);
    ImGui::ShadeVertsLinearUV(dl, start, dl->VtxBuffer.Size, a, b, uv0, uv1, true);
    dl->PopTexture();
}

// Points around a rounded box, kArc + 1 per corner, so two boxes always have the same
// number (the shadow joins an inner box to an outer one, point by point).
constexpr int kArc = 8;
void roundedPoints(ImVec2 a, ImVec2 b, const float r[4], ImVec2* out) {
    const float pi = 3.14159265f;
    const ImVec2 c[4] = {ImVec2(a.x + r[0], a.y + r[0]), ImVec2(b.x - r[1], a.y + r[1]),
                         ImVec2(b.x - r[2], b.y - r[2]), ImVec2(a.x + r[3], b.y - r[3])};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k <= kArc; ++k) {
            const float ang = pi + (i + (float)k / kArc) * pi * 0.5f;
            *out++ = ImVec2(c[i].x + std::cos(ang) * r[i], c[i].y + std::sin(ang) * r[i]);
        }
}

// UIShadow: a soft shadow under the box. One mesh: solid in the middle, fading to
// nothing over Blur pixels at the edge (no pictures, no extra passes).
void drawShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, const Radii& radii, const GuiProps& s) {
    if (s.bgTransparency >= 1.0f) return;
    const float spread = s.shadowSpread, half = std::max(0.0f, s.shadowBlur) * 0.5f;
    a = ImVec2(a.x - spread + s.shadowOffset.x, a.y - spread + s.shadowOffset.y);
    b = ImVec2(b.x + spread + s.shadowOffset.x, b.y + spread + s.shadowOffset.y);
    if (b.x <= a.x || b.y <= a.y) return;
    ImVec2 ia(a.x + half, a.y + half), ib(b.x - half, b.y - half);   // where it's fully dark
    if (ib.x < ia.x) ia.x = ib.x = (a.x + b.x) * 0.5f;
    if (ib.y < ia.y) ia.y = ib.y = (a.y + b.y) * 0.5f;
    const ImVec2 oa(a.x - half, a.y - half), ob(b.x + half, b.y + half);   // where it's gone
    float ri[4], ro[4];
    const float innerMost = std::min(ib.x - ia.x, ib.y - ia.y) * 0.5f, outerMost = std::min(ob.x - oa.x, ob.y - oa.y) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        const float r = std::max(0.0f, radii.r[i] + spread);
        ri[i] = std::clamp(r - half, 0.0f, innerMost);
        ro[i] = std::clamp(r + half, 0.0f, outerMost);
    }
    constexpr int kPts = 4 * (kArc + 1);
    ImVec2 in[kPts], out[kPts];
    roundedPoints(ia, ib, ri, in);
    roundedPoints(oa, ob, ro, out);
    const ImU32 col = rgba(s.bg, s.bgTransparency), clear = col & ~IM_COL32_A_MASK;
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    // The middle (a fan from the first point) and the fading ring around it.
    dl->PrimReserve((kPts - 2) * 3 + kPts * 6, kPts * 2);
    const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    for (int i = 0; i < kPts; ++i) dl->PrimWriteVtx(in[i], uv, col);
    for (int i = 0; i < kPts; ++i) dl->PrimWriteVtx(out[i], uv, clear);
    for (int i = 1; i + 1 < kPts; ++i) {
        dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base + i)); dl->PrimWriteIdx((ImDrawIdx)(base + i + 1));
    }
    for (int i = 0; i < kPts; ++i) {
        const int j = (i + 1) % kPts;
        const ImDrawIdx i0 = (ImDrawIdx)(base + i), i1 = (ImDrawIdx)(base + j);
        const ImDrawIdx o0 = (ImDrawIdx)(base + kPts + i), o1 = (ImDrawIdx)(base + kPts + j);
        dl->PrimWriteIdx(i0); dl->PrimWriteIdx(o0); dl->PrimWriteIdx(o1);
        dl->PrimWriteIdx(i0); dl->PrimWriteIdx(o1); dl->PrimWriteIdx(i1);
    }
}

// UIGradient: where a point of the box [a, b] is along the gradient (0 = its first
// colour, 1 = its last). Like Roblox's, it works in the box's own 0..1 space: turned
// Rotation degrees clockwise around the middle and moved by Offset.
struct Gradient {
    const GuiProps* g = nullptr;
    ImVec2 a, b;
    glm::vec2 dir{1.0f, 0.0f};
    Gradient(const GuiProps* gp, ImVec2 a_, ImVec2 b_) : g(gp), a(a_), b(b_) {
        const float r = glm::radians(gp->gradRotation);
        dir = {std::cos(r), std::sin(r)};   // (screen y points down: clockwise)
    }
    float at(ImVec2 p) const {
        const float w = std::max(1e-3f, b.x - a.x), h = std::max(1e-3f, b.y - a.y);
        const glm::vec2 uv((p.x - a.x) / w - 0.5f - g->gradOffset.x, (p.y - a.y) / h - 0.5f - g->gradOffset.y);
        return glm::dot(uv, dir) + 0.5f;
    }
    // The colour times the gradient at t.
    ImU32 tint(ImU32 col, float t) const {
        const ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
        const glm::vec3 k = sampleSequence(g->gradColor, t);
        const float alpha = 1.0f - std::clamp(sampleSequence(g->gradTransparency, t), 0.0f, 1.0f);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x * k.r, c.y * k.g, c.z * k.b, c.w * alpha));
    }
    // Every vertex added to `dl` since `start` takes the gradient's colour where it is.
    void recolor(ImDrawList* dl, int start) const {
        for (int i = start; i < dl->VtxBuffer.Size; ++i) dl->VtxBuffer[i].col = tint(dl->VtxBuffer[i].col, at(dl->VtxBuffer[i].pos));
    }
};

// The box's outline as points (clockwise from the top-left): 4 corners, or rounded.
constexpr int kMaxShape = 4 * (kArc + 1);
int shapePoints(ImVec2 a, ImVec2 b, const Radii& r, ImVec2* out) {
    if (r.r[0] < 0.5f && r.r[1] < 0.5f && r.r[2] < 0.5f && r.r[3] < 0.5f) {
        out[0] = a; out[1] = ImVec2(b.x, a.y); out[2] = b; out[3] = ImVec2(a.x, b.y);
        return 4;
    }
    roundedPoints(a, b, r.r, out);
    return kMaxShape;
}

// The box filled with `col` (and the picture `tex` over uv0..uv1, if any) times the
// gradient. The shape is cut into strips at each of the gradient's keypoints, so a
// gradient with many colours is exact (not just its two ends).
void fillGradient(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, const Radii& radii, const Gradient& gr,
                  unsigned tex = 0, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1)) {
    if (b.x <= a.x || b.y <= a.y) return;
    struct P { ImVec2 p; float t; };
    ImVec2 pts[kMaxShape];
    const int n = shapePoints(a, b, radii, pts);
    std::vector<P> shape(n);
    for (int i = 0; i < n; ++i) shape[i] = {pts[i], gr.at(pts[i])};
    std::vector<float> cuts;
    for (const ColorKey& k : gr.g->gradColor) cuts.push_back(k.t);
    for (const NumberKey& k : gr.g->gradTransparency) cuts.push_back(k.t);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    cuts.insert(cuts.begin(), -1e9f);
    cuts.push_back(1e9f);
    auto clip = [](const std::vector<P>& in, float lim, bool above) {
        std::vector<P> out;
        for (size_t i = 0; i < in.size(); ++i) {
            const P& p = in[i];
            const P& q = in[(i + 1) % in.size()];
            const float dp = above ? p.t - lim : lim - p.t, dq = above ? q.t - lim : lim - q.t;
            if (dp >= 0) out.push_back(p);
            if ((dp >= 0) != (dq >= 0)) {
                const float f = dp / (dp - dq);
                out.push_back({ImVec2(p.p.x + (q.p.x - p.p.x) * f, p.p.y + (q.p.y - p.p.y) * f), p.t + (q.t - p.t) * f});
            }
        }
        return out;
    };
    const ImVec2 white = dl->_Data->TexUvWhitePixel;
    if (tex) dl->PushTexture((ImTextureID)(intptr_t)tex);
    const float w = b.x - a.x, h = b.y - a.y;
    for (size_t c = 0; c + 1 < cuts.size(); ++c) {
        const std::vector<P> strip = clip(clip(shape, cuts[c], true), cuts[c + 1], false);
        if (strip.size() < 3) continue;
        dl->PrimReserve(((int)strip.size() - 2) * 3, (int)strip.size());
        const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
        for (const P& v : strip) {
            ImVec2 uv = white;
            if (tex) {
                const float fx = (v.p.x - a.x) / w, fy = (v.p.y - a.y) / h;
                uv = ImVec2(uv0.x + (uv1.x - uv0.x) * fx, uv0.y + (uv1.y - uv0.y) * fy);
            }
            dl->PrimWriteVtx(v.p, uv, gr.tint(col, v.t));
        }
        for (int i = 1; i + 1 < (int)strip.size(); ++i) {
            dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base + i)); dl->PrimWriteIdx((ImDrawIdx)(base + i + 1));
        }
    }
    if (tex) dl->PopTexture();
}

uint64_t g_focus = 0;           // the TextBox being typed in
uint64_t g_wantFocus = 0;       // CaptureFocus asked for this one (~0 = ReleaseFocus)
bool     g_releaseEnter = false;
int      g_focusAge = 0;        // frames since it got focus (ImGui's box takes a frame to wake up)

unsigned g_backdrop[8] = {};
int      g_backdropCount = 0;

bool anyBlur(const SceneNode* n) {
    for (auto& c : n->children) {
        if (!c->isGui()) continue;
        if (c->gui.type == GuiType::UIBlur && c->enabled && c->gui.blurSize > 0) return true;
        if (c->isGuiObject() && c->visible && anyBlur(c.get())) return true;
    }
    return false;
}

struct Placed { SceneNode* node; ImVec2 a, b; };

const SceneNode* firstLayout(const SceneNode* n) {
    for (auto& c : n->children)
        if (c->isGui() && isGuiLayout(c->gui.type)) return c.get();
    return nullptr;
}

// How UI inside `parent` is scaled by UIScales: `px` multiplies pixel sizes and
// positions, `whole` its whole size (a UIScale in a ScreenGui scales each thing in it).
struct Scaling { float px = 1.0f, whole = 1.0f; };
Scaling scalingIn(const SceneNode* parent) {
    if (isGuiLayer(parent->gui.type)) return {1.0f, parent->gui.absScale};
    return {parent->gui.absScale, 1.0f};
}

// Its size in a parent pw x ph pixels: Size (its pixels grown by the UIScales above it),
// kept to the shape of a UIAspectRatioConstraint inside it, times its own UIScale.
ImVec2 sizeIn(const SceneNode* n, float pw, float ph, Scaling sc) {
    const GuiProps& g = n->gui;
    float w = std::max(0.0f, pw * g.size.xs + g.size.xo * sc.px), h = std::max(0.0f, ph * g.size.ys + g.size.yo * sc.px);
    if (const SceneNode* c = childOfType(n, GuiType::UIAspectRatioConstraint)) {
        const float r = std::clamp(c->gui.aspectRatio, 0.001f, 1000.0f);
        if (c->gui.aspectType == 0) {   // FitWithinMaxSize: the biggest box of that shape that fits in Size
            if (w / r <= h) h = w / r; else w = h * r;
        } else if (c->gui.dominantAxis == 0) h = w / r;   // ScaleWithParentSize: Width decides the height
        else w = h * r;                                    // ... or Height decides the width
    }
    const float k = ownScale(n) * sc.whole;
    return ImVec2(w * k, h * k);
}

// Its rectangle inside [pa, pb] from Position, Size and AnchorPoint (a UIScale grows
// it around its AnchorPoint).
void rectIn(const SceneNode* n, ImVec2 pa, ImVec2 pb, ImVec2& a, ImVec2& b, Scaling sc) {
    const GuiProps& g = n->gui;
    const float pw = pb.x - pa.x, ph = pb.y - pa.y;
    const ImVec2 s = sizeIn(n, pw, ph, sc);
    const float x = pa.x + pw * g.pos.xs + g.pos.xo * sc.px - g.anchor.x * s.x;
    const float y = pa.y + ph * g.pos.ys + g.pos.yo * sc.px - g.anchor.y * s.y;
    a = ImVec2(x, y);
    b = ImVec2(x + s.x, y + s.y);
}

// UIListLayout / UIGridLayout: line `kids` up inside [pa, pb] (instead of their Position).
void arrange(SceneNode* lay, std::vector<SceneNode*> kids, ImVec2 pa, ImVec2 pb, std::vector<Placed>& out, Scaling sc) {
    GuiProps& L = lay->gui;
    const float px = sc.px * sc.whole;   // (gaps and cells grow with UIScale too)
    std::stable_sort(kids.begin(), kids.end(), [&](SceneNode* l, SceneNode* r) {
        return L.sortByName ? l->name < r->name : l->gui.layoutOrder < r->gui.layoutOrder;
    });
    const float W = pb.x - pa.x, H = pb.y - pa.y;
    const bool across = L.fill == 1;   // Horizontal
    auto put = [&](SceneNode* n, float x, float y, float w, float h) {
        out.push_back(Placed{n, ImVec2(x, y), ImVec2(x + w, y + h)});
    };
    if (L.type == GuiType::UIListLayout) {
        const float gap = across ? W * L.padding.xs + L.padding.xo * px : H * L.padding.xs + L.padding.xo * px;
        std::vector<ImVec2> sizes;
        float total = 0.0f, widest = 0.0f;
        for (SceneNode* k : kids) {
            ImVec2 a, b;
            rectIn(k, pa, pb, a, b, sc);
            sizes.emplace_back(b.x - a.x, b.y - a.y);
            total += across ? sizes.back().x : sizes.back().y;
            widest = std::max(widest, across ? sizes.back().y : sizes.back().x);
        }
        if (!kids.empty()) total += gap * (float)(kids.size() - 1);
        L.contentSize = across ? glm::vec2(total, widest) : glm::vec2(widest, total);
        float at = across ? pa.x + (W - total) * 0.5f * L.hAlign : pa.y + (H - total) * 0.5f * L.vAlign;
        for (size_t i = 0; i < kids.size(); ++i) {
            const ImVec2 s = sizes[i];
            if (across) { put(kids[i], at, pa.y + (H - s.y) * 0.5f * L.vAlign, s.x, s.y); at += s.x + gap; }
            else        { put(kids[i], pa.x + (W - s.x) * 0.5f * L.hAlign, at, s.x, s.y); at += s.y + gap; }
        }
        return;
    }
    // A grid of CellSize cells, CellPadding apart, filling rows (or columns) in turn.
    const float cw = W * L.cellSize.xs + L.cellSize.xo * px, ch = H * L.cellSize.ys + L.cellSize.yo * px;
    const float gx = W * L.padding.xs + L.padding.xo * px, py = H * L.padding.ys + L.padding.yo * px;
    int per = across ? (int)std::floor((W + gx) / std::max(1.0f, cw + gx)) : (int)std::floor((H + py) / std::max(1.0f, ch + py));
    per = std::max(1, per);
    if (L.maxCells > 0) per = std::min(per, L.maxCells);
    const int n = (int)kids.size();
    const int lines = n ? (n + per - 1) / per : 0, inLine = std::min(n, per);
    const int cols = across ? inLine : lines, rows = across ? lines : inLine;
    const float bw = cols ? cols * (cw + gx) - gx : 0.0f, bh = rows ? rows * (ch + py) - py : 0.0f;
    L.contentSize = {bw, bh};
    const float x0 = pa.x + (W - bw) * 0.5f * L.hAlign, y0 = pa.y + (H - bh) * 0.5f * L.vAlign;
    for (int i = 0; i < n; ++i) {
        const int c = across ? i % per : i / per, r = across ? i / per : i % per;
        put(kids[i], x0 + c * (cw + gx), y0 + r * (ch + py), cw, ch);
    }
}

void place(SceneNode* n, ImVec2 a, ImVec2 b, ImVec2 clipA, ImVec2 clipB, std::vector<Item>& out);

// Everything inside `parent`, laid out in [pa, pb] (its inside), in drawing order.
void layoutChildren(SceneNode* parent, ImVec2 pa, ImVec2 pb, ImVec2 clipA, ImVec2 clipB, std::vector<Item>& out) {
    std::vector<SceneNode*> kids;
    for (auto& c : parent->children) if (c->isGuiObject() && c->visible) kids.push_back(c.get());
    std::vector<Placed> rects;
    const Scaling sc = scalingIn(parent);
    if (SceneNode* lay = const_cast<SceneNode*>(firstLayout(parent))) {
        arrange(lay, kids, pa, pb, rects, sc);
    } else {
        for (SceneNode* k : kids) {
            Placed p{k, {}, {}};
            rectIn(k, pa, pb, p.a, p.b, sc);
            rects.push_back(p);
        }
    }
    // Drawn by ZIndex (ties: the order they're in).
    std::vector<size_t> order(rects.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t l, size_t r) {
        if (rects[l].node->gui.zIndex != rects[r].node->gui.zIndex) return rects[l].node->gui.zIndex < rects[r].node->gui.zIndex;
        return std::find(kids.begin(), kids.end(), rects[l].node) < std::find(kids.begin(), kids.end(), rects[r].node);
    });
    for (size_t i : order) place(rects[i].node, rects[i].a, rects[i].b, clipA, clipB, out);
}

// The inside of an object: its rectangle less any UIPadding.
void insideOf(const SceneNode* n, ImVec2& a, ImVec2& b) {
    for (auto& c : n->children) {
        if (!c->isGui() || c->gui.type != GuiType::UIPadding) continue;
        const GuiProps& p = c->gui;
        const float w = b.x - a.x, h = b.y - a.y, k = n->gui.absScale;
        a = ImVec2(a.x + w * p.padScale.x + p.padPx.x * k, a.y + h * p.padScale.y + p.padPx.y * k);
        b = ImVec2(b.x - w * p.padScale.z - p.padPx.z * k, b.y - h * p.padScale.w - p.padPx.w * k);
        if (b.x < a.x) b.x = a.x;
        if (b.y < a.y) b.y = a.y;
        return;
    }
}

// ScrollingFrame: how big its canvas is (pixels) for a view of `view` pixels.
glm::vec2 canvasFor(SceneNode* n, ImVec2 ia, ImVec2 ib) {
    GuiProps& g = n->gui;
    const float vw = ib.x - ia.x, vh = ib.y - ia.y;
    glm::vec2 c(vw * g.canvasSize.xs + g.canvasSize.xo * g.absScale, vh * g.canvasSize.ys + g.canvasSize.yo * g.absScale);
    if (g.autoCanvas) {   // AutomaticCanvasSize: big enough for what's inside
        std::vector<Item> trial;
        layoutChildren(n, ia, ImVec2(ia.x + std::max(c.x, vw), ia.y + std::max(c.y, vh)), ia, ib, trial);
        glm::vec2 most(0.0f);
        for (const Item& it : trial)
            if (it.node->parent == n) most = glm::max(most, glm::vec2(it.b.x - ia.x, it.b.y - ia.y));
        if (const SceneNode* lay = firstLayout(n)) most = glm::max(most, lay->gui.contentSize);
        if (g.autoCanvas & 1) c.x = std::max(c.x, most.x);
        if (g.autoCanvas & 2) c.y = std::max(c.y, most.y);
    }
    return glm::max(c, glm::vec2(0.0f));
}

void place(SceneNode* n, ImVec2 a, ImVec2 b, ImVec2 clipA, ImVec2 clipB, std::vector<Item>& out) {
    GuiProps& g = n->gui;
    g.absScale = (n->parent && n->parent->isGui() ? n->parent->gui.absScale : 1.0f) * ownScale(n);
    g.absPos = {a.x, a.y};
    g.absSize = {b.x - a.x, b.y - a.y};
    out.push_back(Item{n, a, b, clipA, clipB});
    ImVec2 ca = clipA, cb = clipB;
    const bool scroller = g.type == GuiType::ScrollingFrame;
    if (g.clips || scroller) { ca = ImVec2(std::max(ca.x, a.x), std::max(ca.y, a.y)); cb = ImVec2(std::min(cb.x, b.x), std::min(cb.y, b.y)); }
    ImVec2 ia = a, ib = b;
    insideOf(n, ia, ib);
    if (!scroller) { layoutChildren(n, ia, ib, ca, cb, out); return; }
    // What's inside sits on the canvas, moved by how far it's scrolled.
    const glm::vec2 canvas = canvasFor(n, ia, ib);
    g.contentSize = canvas;   // (AbsoluteCanvasSize)
    const glm::vec2 most = glm::max(glm::vec2(0.0f), canvas - glm::vec2(ib.x - ia.x, ib.y - ia.y));
    g.canvasPos = glm::clamp(g.canvasPos, glm::vec2(0.0f), most);
    const ImVec2 o(ia.x - g.canvasPos.x, ia.y - g.canvasPos.y);
    layoutChildren(n, o, ImVec2(o.x + std::max(canvas.x, ib.x - ia.x), o.y + std::max(canvas.y, ib.y - ia.y)), ca, cb, out);
    Item bars{n, a, b, clipA, clipB};
    bars.bars = true;
    out.push_back(bars);
}

// Where a ScrollingFrame's bars are: false if it has none that way (everything fits).
bool scrollThumb(const SceneNode* n, bool vertical, ImVec2& ta, ImVec2& tb) {
    const GuiProps& g = n->gui;
    if (g.scrollBar <= 0 || !(g.scrollDir & (vertical ? 2 : 1))) return false;
    const ImVec2 a(g.absPos.x, g.absPos.y), b(a.x + g.absSize.x, a.y + g.absSize.y);
    const float view = vertical ? g.absSize.y : g.absSize.x, canvas = vertical ? g.contentSize.y : g.contentSize.x;
    if (canvas <= view + 0.5f || view <= 0) return false;
    const float len = std::max(16.0f, view * view / canvas), travel = view - len;
    const float at = travel * std::clamp((vertical ? g.canvasPos.y : g.canvasPos.x) / (canvas - view), 0.0f, 1.0f);
    const float t = (float)g.scrollBar;
    if (vertical) { ta = ImVec2(b.x - t, a.y + at); tb = ImVec2(b.x, a.y + at + len); }
    else          { ta = ImVec2(a.x + at, b.y - t); tb = ImVec2(a.x + at + len, b.y); }
    return true;
}

// --- UI on parts (BillboardGui, SurfaceGui) ---------------------------------

glm::mat4 g_view(1.0f), g_viewProj(1.0f), g_invViewProj(1.0f);
glm::vec3 g_camPos(0.0f);
bool      g_haveCam = false;

// A SurfaceGui this frame: its canvas lies on one side of a part.
struct Surface {
    glm::vec3 origin, ax, ay;   // canvas (0, 0) in the world, and one canvas pixel right / down
    glm::vec2 canvas;
    size_t    first = 0, last = 0;   // its items
};
std::vector<Surface> g_surfaces;   // (from the last items())
ImVec2 g_itemsMin(0, 0), g_itemsMax(1, 1);

// Which side of the camera's view things are hidden behind, worked out once a frame.
int g_seenFrame = -1;
std::unordered_map<uint64_t, bool> g_seen;

void findLayers(SceneNode* n, std::vector<SceneNode*>& out) {
    for (auto& c : n->children) {
        if (c->isGui()) {
            if (isGuiLayer(c->gui.type)) out.push_back(c.get());
            continue;   // a ScreenGui doesn't hold other ScreenGuis
        }
        findLayers(c.get(), out);
    }
}

// The part a BillboardGui / SurfaceGui sits on: its Adornee, else the part it's in
// (in a model: its Head, else its first part).
SceneNode* adorneeOf(Scene& scene, const SceneNode* gui) {
    if (gui->gui.adornee)
        if (SceneNode* a = scene.findById(gui->gui.adornee); a && a->isPart()) return a;
    SceneNode* p = gui->parent;
    if (!p) return nullptr;
    if (p->isPart()) return p;
    if (p->kind == NodeKind::Model) {
        if (SceneNode* h = p->findChild("Head"); h && h->isPart()) return h;
        for (auto& c : p->children) if (c->isPart()) return c.get();
    }
    return nullptr;
}

bool toScreen(const glm::vec3& p, ImVec2 min, ImVec2 max, ImVec2& out) {
    const glm::vec4 c = g_viewProj * glm::vec4(p, 1.0f);
    if (c.w < 0.05f) return false;   // behind the camera
    out = ImVec2(min.x + (c.x / c.w * 0.5f + 0.5f) * (max.x - min.x), min.y + (0.5f - c.y / c.w * 0.5f) * (max.y - min.y));
    return true;
}

// Can the camera see `p`, or is something in the way? (`part` and its model, and
// characters, don't count.) Cached for the frame under `key`.
bool seen(Scene& scene, uint64_t key, const glm::vec3& p, const SceneNode* part) {
    const int frame = ImGui::GetFrameCount();
    if (frame != g_seenFrame) { g_seen.clear(); g_seenFrame = frame; }
    if (auto it = g_seen.find(key); it != g_seen.end()) return it->second;
    const glm::vec3 d = p - g_camPos;
    const float len = glm::length(d);
    bool vis = true;
    if (len > 0.01f) {
        const SceneNode* model = part->parent && part->parent != scene.root() && part->parent->kind == NodeKind::Model
                                     ? part->parent : nullptr;   // (a character or other model it's part of)
        float hit = 0.0f;
        SceneNode* h = Physics::raycastIf(scene, g_camPos, d / len, &hit, [&](const SceneNode* n) {
            return n != part && n != model && !(model && n->parent == model) && !scene.isCharacterPart(n);
        });
        vis = !h || hit >= len - 0.3f;
    }
    g_seen[key] = vis;
    return vis;
}

// One side of a part's unit box: which way is right, up and out (like decals).
void faceAxes(int face, glm::vec3& R, glm::vec3& U, glm::vec3& N) {
    switch ((Face)face) {
        case Face::Front:  R = {-1, 0, 0}; U = {0, 1, 0};  N = {0, 0, -1}; break;
        case Face::Back:   R = {1, 0, 0};  U = {0, 1, 0};  N = {0, 0, 1};  break;
        case Face::Right:  R = {0, 0, -1}; U = {0, 1, 0};  N = {1, 0, 0};  break;
        case Face::Left:   R = {0, 0, 1};  U = {0, 1, 0};  N = {-1, 0, 0}; break;
        case Face::Top:    R = {1, 0, 0};  U = {0, 0, -1}; N = {0, 1, 0};  break;
        default:           R = {1, 0, 0};  U = {0, 0, 1};  N = {0, -1, 0}; break;   // Bottom
    }
}

// Where on a SurfaceGui's canvas the pointer at `p` (screen) is. False if it misses the plane.
bool canvasPoint(const Surface& s, ImVec2 p, ImVec2& out) {
    const float w = g_itemsMax.x - g_itemsMin.x, h = g_itemsMax.y - g_itemsMin.y;
    if (w <= 0 || h <= 0) return false;
    const float nx = (p.x - g_itemsMin.x) / w * 2.0f - 1.0f, ny = 1.0f - (p.y - g_itemsMin.y) / h * 2.0f;
    const glm::vec4 a = g_invViewProj * glm::vec4(nx, ny, -1, 1), b = g_invViewProj * glm::vec4(nx, ny, 1, 1);
    const glm::vec3 ro = glm::vec3(a) / a.w, rd = glm::normalize(glm::vec3(b) / b.w - ro);
    const glm::vec3 n = glm::cross(s.ax, s.ay);
    const float den = glm::dot(rd, n);
    if (std::abs(den) < 1e-9f) return false;
    const float t = glm::dot(s.origin - ro, n) / den;
    if (t <= 0.0f) return false;
    const glm::vec3 rel = ro + rd * t - s.origin;
    out = ImVec2(glm::dot(rel, s.ax) / glm::dot(s.ax, s.ax), glm::dot(rel, s.ay) / glm::dot(s.ay, s.ay));
    return true;
}

// Every visible UI object, in the order they're drawn (so the last one is on top):
// SurfaceGuis and BillboardGuis (furthest first), then ScreenGuis by DisplayOrder.
std::vector<Item> items(Scene& scene, ImVec2 min, ImVec2 max) {
    std::vector<SceneNode*> layers;
    findLayers(scene.root(), layers);
    std::vector<Item> out;
    g_surfaces.clear();
    g_itemsMin = min;
    g_itemsMax = max;
    if (g_haveCam) {
        struct OnPart { SceneNode* gui; float dist; ImVec2 a, b; Surface s; };
        std::vector<OnPart> onParts;
        const glm::vec3 camRight(g_view[0][0], g_view[1][0], g_view[2][0]), camUp(g_view[0][1], g_view[1][1], g_view[2][1]),
                        camBack(g_view[0][2], g_view[1][2], g_view[2][2]);
        for (SceneNode* l : layers) {
            if (!l->enabled || l->gui.type == GuiType::ScreenGui) continue;
            SceneNode* part = adorneeOf(scene, l);
            if (!part) continue;
            const GuiProps& g = l->gui;
            const glm::mat4 W = part->worldMatrix();
            if (g.type == GuiType::BillboardGui) {
                // Floats over the part, facing the camera. Size: pixels, plus studs (scale).
                const glm::vec3 at = glm::vec3(W[3]) + g.worldOffset + camRight * g.studsOffset.x + camUp * g.studsOffset.y +
                                     camBack * g.studsOffset.z;
                const float dist = glm::length(at - g_camPos);
                if (g.maxDistance > 0 && dist > g.maxDistance) continue;
                ImVec2 c, up;
                if (!toScreen(at, min, max, c) || !toScreen(at + camUp, min, max, up)) continue;
                if (!g.alwaysOnTop && !seen(scene, l->id * 8, at, part)) continue;
                const float perStud = std::abs(c.y - up.y);
                const float w = g.size.xo + g.size.xs * perStud, h = g.size.yo + g.size.ys * perStud;
                if (w < 1.0f || h < 1.0f) continue;
                onParts.push_back({l, dist, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), {}});
            } else {
                // Painted on one side of the part, facing out (only seen from the front).
                glm::vec3 R, U, N;
                faceAxes(g.face, R, U, N);
                const glm::vec3 Rw(W * glm::vec4(R, 0)), Uw(W * glm::vec4(U, 0)), Nw(W * glm::vec4(N, 0));
                const float nl = glm::length(Nw), wS = glm::length(Rw), hS = glm::length(Uw);
                if (nl < 1e-6f || wS < 1e-4f || hS < 1e-4f) continue;
                const glm::vec3 normal = Nw / nl;
                const glm::vec3 center = glm::vec3(W * glm::vec4(N * 0.5f, 1)) + normal * 0.02f;
                if (glm::dot(g_camPos - center, normal) <= 0.0f) continue;   // looking at its back
                const float dist = glm::length(center - g_camPos);
                if (g.maxDistance > 0 && dist > g.maxDistance) continue;
                if (!g.alwaysOnTop) {   // hidden only if its middle and all four corners are
                    bool any = false;
                    const glm::vec3 pts[5] = {center, center + Rw * 0.4f + Uw * 0.4f, center - Rw * 0.4f + Uw * 0.4f,
                                              center + Rw * 0.4f - Uw * 0.4f, center - Rw * 0.4f - Uw * 0.4f};
                    for (int i = 0; i < 5 && !any; ++i) any = seen(scene, l->id * 8 + 1 + i, pts[i], part);
                    if (!any) continue;
                }
                Surface s;
                s.canvas = g.perStud ? glm::vec2(wS, hS) * g.pixelsPerStud : g.surfaceCanvas;
                s.canvas = glm::max(s.canvas, glm::vec2(1.0f));
                s.origin = center - Rw * 0.5f + Uw * 0.5f;
                s.ax = Rw / s.canvas.x;
                s.ay = -Uw / s.canvas.y;
                onParts.push_back({l, dist, ImVec2(0, 0), ImVec2(s.canvas.x, s.canvas.y), s});
            }
        }
        std::stable_sort(onParts.begin(), onParts.end(), [](const OnPart& l, const OnPart& r) { return l.dist > r.dist; });
        for (OnPart& o : onParts) {
            o.gui->gui.absScale = ownScale(o.gui);
            o.gui->gui.absPos = {o.a.x, o.a.y};
            o.gui->gui.absSize = {o.b.x - o.a.x, o.b.y - o.a.y};
            ImVec2 ia = o.a, ib = o.b;
            insideOf(o.gui, ia, ib);
            if (o.gui->gui.type == GuiType::SurfaceGui) {
                Surface s = o.s;
                s.first = out.size();
                layoutChildren(o.gui, ia, ib, o.a, o.b, out);
                s.last = out.size();
                for (size_t i = s.first; i < s.last; ++i) out[i].surface = (int)g_surfaces.size();
                g_surfaces.push_back(s);
            } else {   // (a billboard clips what's inside it)
                const ImVec2 ca(std::max(o.a.x, min.x), std::max(o.a.y, min.y)), cb(std::min(o.b.x, max.x), std::min(o.b.y, max.y));
                layoutChildren(o.gui, ia, ib, ca, cb, out);
            }
        }
    }
    std::vector<SceneNode*> screens;
    for (SceneNode* l : layers) if (l->gui.type == GuiType::ScreenGui) screens.push_back(l);
    std::stable_sort(screens.begin(), screens.end(),
                     [](SceneNode* a, SceneNode* b) { return a->gui.displayOrder < b->gui.displayOrder; });
    for (SceneNode* s : screens) {
        if (!s->enabled) continue;
        s->gui.absScale = ownScale(s);
        s->gui.absPos = {min.x, min.y};
        s->gui.absSize = {max.x - min.x, max.y - min.y};
        ImVec2 ia = min, ib = max;
        insideOf(s, ia, ib);
        layoutChildren(s, ia, ib, min, max, out);
    }
    return out;
}

ImVec2 g_lastMin(0, 0), g_lastMax(1280, 720);   // the screen we last drew on

bool inside(const Item& it, ImVec2 p) {
    if (it.bars) return false;
    if (it.surface >= 0 && (it.surface >= (int)g_surfaces.size() || !canvasPoint(g_surfaces[it.surface], p, p))) return false;
    ImVec2 a(std::max(it.a.x, it.clipA.x), std::max(it.a.y, it.clipA.y));
    ImVec2 b(std::min(it.b.x, it.clipB.x), std::min(it.b.y, it.clipB.y));
    return p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y;
}

// Does this object catch the pointer (so clicks don't go through to the world)?
bool blocks(const SceneNode* n) {
    if (n->isGuiButton() || n->gui.type == GuiType::TextBox || n->gui.type == GuiType::ScrollingFrame) return true;
    return n->gui.bgTransparency < 0.95f || ((n->gui.type == GuiType::ImageLabel) && !n->gui.image.empty());
}

void drawText(ImDrawList* dl, const Item& it, const GuiProps& gIn, float radius, const Gradient* grad) {
    GuiProps shown;
    const GuiProps* gp = &gIn;
    if (gIn.type == GuiType::TextBox && gIn.text.empty() && !gIn.placeholder.empty()) {   // PlaceholderText, greyed out
        shown = gIn;
        shown.text = gIn.placeholder;
        shown.textColor = gIn.placeholderColor;
        gp = &shown;
    }
    const GuiProps& g = *gp;
    if (g.text.empty() || g.textTransparency >= 1.0f) return;
    ImFont* font = ImGui::GetFont();
    const float w = it.b.x - it.a.x, h = it.b.y - it.a.y;
    const float k = it.node->gui.absScale;   // (text grows with UIScale)
    const float pad = std::min(4.0f * k, radius * 0.5f + 2.0f * k);
    float wrap = g.textWrapped || g.textScaled ? std::max(1.0f, w - pad * 2) : 0.0f;
    float size = g.textSize * k;
    if (g.textScaled) {   // as big as fits
        size = std::max(6.0f, h * 0.85f);
        for (int i = 0; i < 24; ++i) {
            ImVec2 s = font->CalcTextSizeA(size, FLT_MAX, g.textWrapped ? wrap : 0.0f, g.text.c_str());
            if (s.x <= w - pad * 2 && s.y <= h) break;
            size *= 0.88f;
        }
        if (!g.textWrapped) wrap = 0.0f;
    }
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, wrap, g.text.c_str());
    float x = g.xAlign == 0 ? it.a.x + pad : g.xAlign == 2 ? it.b.x - pad - ts.x : it.a.x + (w - ts.x) * 0.5f;
    float y = g.yAlign == 0 ? it.a.y : g.yAlign == 2 ? it.b.y - ts.y : it.a.y + (h - ts.y) * 0.5f;
    const ImU32 col = rgba(g.textColor, g.textTransparency);
    if (g.strokeTransparency < 1.0f) {   // an outline around the letters
        const ImU32 sc = rgba(g.strokeColor, std::max(g.strokeTransparency, g.textTransparency));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx || dy) dl->AddText(font, size, ImVec2(x + dx * k, y + dy * k), sc, g.text.c_str(), nullptr, wrap);
    }
    const int start = dl->VtxBuffer.Size;
    dl->AddText(font, size, ImVec2(x, y), col, g.text.c_str(), nullptr, wrap);
    if (g.bold) dl->AddText(font, size, ImVec2(x + k, y), col, g.text.c_str(), nullptr, wrap);
    if (grad) grad->recolor(dl, start);   // UIGradient colours the letters too
}

} // namespace

void refresh(Scene& scene) { items(scene, g_lastMin, g_lastMax); }
void lastScreen(ImVec2& min, ImVec2& max) { min = g_lastMin; max = g_lastMax; }

Scene* g_drawScene = nullptr;   // (ViewportFrames draw the parts in it)

bool needsBackdrop(Scene& scene) {
    std::vector<SceneNode*> screens;
    findLayers(scene.root(), screens);
    for (SceneNode* s : screens) if (s->gui.type == GuiType::ScreenGui && s->enabled && anyBlur(s)) return true;
    return false;
}

void setBackdrop(const unsigned* levels, int count) {
    g_backdropCount = levels ? std::clamp(count, 0, 8) : 0;
    for (int i = 0; i < g_backdropCount; ++i) g_backdrop[i] = levels[i];
}

// Draw one object (`min`, `max`: the area it's drawn in). `flat`: on a SurfaceGui's
// canvas, where the world behind can't be blurred.
void drawItem(ImDrawList* dl, const Item& it, ImVec2 min, ImVec2 max, const Input* input, bool flat) {
    const SceneNode* n = it.node;
    const GuiProps& g = n->gui;
    ImVec2 ca(std::max(it.clipA.x, min.x), std::max(it.clipA.y, min.y));
    ImVec2 cb(std::min(it.clipB.x, max.x), std::min(it.clipB.y, max.y));
    if (cb.x <= ca.x || cb.y <= ca.y) return;
    dl->PushClipRect(ca, cb, true);
    if (it.bars) {   // a ScrollingFrame's bars, over what's inside it
        const ImU32 col = rgba(g.scrollColor, std::max(g.scrollTransparency, 0.35f));
        for (bool vertical : {true, false}) {
            ImVec2 ta, tb;
            if (scrollThumb(n, vertical, ta, tb)) dl->AddRectFilled(ta, tb, col, (float)g.scrollBar * 0.5f);
        }
        dl->PopClipRect();
        return;
    }
    const float w = it.b.x - it.a.x, h = it.b.y - it.a.y;
    const Radii radii = radiiOf(n, w, h);
    const float radius = std::max(std::max(radii.r[0], radii.r[1]), std::max(radii.r[2], radii.r[3]));
    const float k = g.absScale;   // UIScale
    const SceneNode* gradNode = childOfType(n, GuiType::UIGradient);
    std::optional<Gradient> grad;
    if (gradNode) grad.emplace(&gradNode->gui, it.a, it.b);
    const Gradient* gr = grad ? &*grad : nullptr;

    // A soft shadow under it, then the world behind it blurred (frosted glass).
    if (const SceneNode* s = childOfType(n, GuiType::UIShadow)) {
        GuiProps sh = s->gui;
        sh.shadowOffset *= k; sh.shadowBlur *= k; sh.shadowSpread *= k;
        drawShadow(dl, it.a, it.b, radii, sh);
    }
    if (const SceneNode* b = childOfType(n, GuiType::UIBlur); b && !flat && b->gui.blurSize > 0 && g_backdropCount > 0) {
        const float sz = b->gui.blurSize;
        const int level = std::min(g_backdropCount - 1, sz <= 6 ? 0 : sz <= 12 ? 1 : sz <= 24 ? 2 : 3);
        const float sw = max.x - min.x, sh = max.y - min.y;
        if (sw > 0 && sh > 0 && g_backdrop[level]) {   // (the world's picture is upside down, like the scene's)
            const ImVec2 uv0((it.a.x - min.x) / sw, 1.0f - (it.a.y - min.y) / sh);
            const ImVec2 uv1((it.b.x - min.x) / sw, 1.0f - (it.b.y - min.y) / sh);
            imageRounded(dl, g_backdrop[level], it.a, it.b, uv0, uv1, IM_COL32_WHITE, radii);
        }
    }

    // Background (buttons get darker when you point at or press them).
    glm::vec3 bg = g.bg;
    if (n->isGuiButton() && g.autoButtonColor && input) {
        if (input->pressed == n->id && input->hovered == n->id) bg *= 0.7f;
        else if (input->hovered == n->id) bg *= 0.87f;
    }
    if (g.bgTransparency < 1.0f) {   // (a UIGradient colours it along a line)
        if (gr) fillGradient(dl, it.a, it.b, rgba(bg, g.bgTransparency), radii, *gr);
        else fillRounded(dl, it.a, it.b, rgba(bg, g.bgTransparency), radii);
    }

    // A picture.
    if ((g.type == GuiType::ImageLabel || g.type == GuiType::ImageButton) && !g.image.empty() && g.imageTransparency < 1.0f)
        if (unsigned tex = Textures::get(g.image)) {
            glm::vec3 tint = g.imageColor;
            if (n->isGuiButton() && g.autoButtonColor && input && input->hovered == n->id && g.bgTransparency >= 1.0f)
                tint *= input->pressed == n->id ? 0.7f : 0.87f;
            if (gr) fillGradient(dl, it.a, it.b, rgba(tint, g.imageTransparency), radii, *gr, tex, ImVec2(0, 1), ImVec2(1, 0));
            else imageRounded(dl, tex, it.a, it.b, ImVec2(0, 1), ImVec2(1, 0), rgba(tint, g.imageTransparency), radii);
        }

    // A ViewportFrame: the parts inside it, drawn into a picture of their own (only
    // while it's on screen, at the size it's shown).
    if (g.type == GuiType::ViewportFrame && g_drawScene && !Mesh::headless && g.imageTransparency < 1.0f) {
        const ImVec2 va(std::max(it.a.x, ca.x), std::max(it.a.y, ca.y)), vb(std::min(it.b.x, cb.x), std::min(it.b.y, cb.y));
        if (vb.x > va.x && vb.y > va.y && w >= 1.0f && h >= 1.0f)
            if (unsigned tex = ViewportRender::render(*g_drawScene, *n, (int)std::ceil(w), (int)std::ceil(h))) {
                const ImU32 tint = rgba(g.imageColor, g.imageTransparency);
                if (gr) fillGradient(dl, it.a, it.b, tint, radii, *gr, tex, ImVec2(0, 1), ImVec2(1, 0));
                else imageRounded(dl, tex, it.a, it.b, ImVec2(0, 1), ImVec2(1, 0), tint, radii);
            }
    }

    if (!input || n->id != g_focus) drawText(dl, it, g, radius, gr);   // (the TextBox being typed in shows its own)

    // The edge: a UIStroke inside it, else the border.
    if (const SceneNode* s = childOfType(n, GuiType::UIStroke)) {
        if (s->gui.thickness > 0 && s->gui.bgTransparency < 1.0f)
            strokeRounded(dl, it.a, it.b, rgba(s->gui.borderColor, s->gui.bgTransparency), radii, s->gui.thickness * k);
    } else if (g.border > 0 && g.bgTransparency < 1.0f) {
        const float bw = (float)g.border * k;
        strokeRounded(dl, ImVec2(it.a.x - bw * 0.5f, it.a.y - bw * 0.5f), ImVec2(it.b.x + bw * 0.5f, it.b.y + bw * 0.5f),
                      rgba(g.borderColor, g.bgTransparency), radii, bw);
    }
    dl->PopClipRect();
}

// A SurfaceGui: its canvas was drawn flat into `src`; put every triangle on the part's
// side, in perspective, into `dl` (cut to each command's clip and to the camera's near plane).
struct SV { glm::vec4 p; ImVec2 uv; glm::vec4 col; };
SV lerpSV(const SV& a, const SV& b, float t) {
    return {a.p + (b.p - a.p) * t, ImVec2(a.uv.x + (b.uv.x - a.uv.x) * t, a.uv.y + (b.uv.y - a.uv.y) * t), a.col + (b.col - a.col) * t};
}
template <class F> int clipPoly(const SV* in, int n, SV* out, F dist) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const SV& a = in[i];
        const SV& b = in[(i + 1) % n];
        const float da = dist(a), db = dist(b);
        if (da >= 0) out[m++] = a;
        if ((da >= 0) != (db >= 0)) out[m++] = lerpSV(a, b, da / (da - db));
    }
    return m;
}
void emitSurface(ImDrawList* dl, const ImDrawList& src, const Surface& s, ImVec2 min, ImVec2 max) {
    dl->PushClipRect(min, max, true);
    for (const ImDrawCmd& cmd : src.CmdBuffer) {
        if (cmd.UserCallback || cmd.ElemCount == 0) continue;
        dl->PushTexture(cmd.TexRef);
        const ImVec4 cr = cmd.ClipRect;
        for (unsigned k = 0; k + 2 < cmd.ElemCount; k += 3) {
            SV a[12], b[12];
            for (int v = 0; v < 3; ++v) {
                const ImDrawVert& dv = src.VtxBuffer[cmd.VtxOffset + src.IdxBuffer[cmd.IdxOffset + k + v]];
                const ImVec4 c = ImGui::ColorConvertU32ToFloat4(dv.col);
                a[v] = {glm::vec4(dv.pos.x, dv.pos.y, 0, 1), dv.uv, glm::vec4(c.x, c.y, c.z, c.w)};
            }
            int n = 3;
            n = clipPoly(a, n, b, [&](const SV& v) { return v.p.x - cr.x; });
            n = clipPoly(b, n, a, [&](const SV& v) { return cr.z - v.p.x; });
            n = clipPoly(a, n, b, [&](const SV& v) { return v.p.y - cr.y; });
            n = clipPoly(b, n, a, [&](const SV& v) { return cr.w - v.p.y; });
            if (n < 3) continue;
            for (int v = 0; v < n; ++v)   // canvas pixels -> the world -> the camera
                a[v].p = g_viewProj * glm::vec4(s.origin + s.ax * a[v].p.x + s.ay * a[v].p.y, 1.0f);
            n = clipPoly(a, n, b, [](const SV& v) { return v.p.w - 0.05f; });
            if (n < 3) continue;
            dl->PrimReserve((n - 2) * 3, n);
            const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
            for (int v = 0; v < n; ++v) {
                const glm::vec3 ndc = glm::vec3(b[v].p) / b[v].p.w;
                const ImVec2 sp(min.x + (ndc.x * 0.5f + 0.5f) * (max.x - min.x), min.y + (0.5f - ndc.y * 0.5f) * (max.y - min.y));
                dl->PrimWriteVtx(sp, b[v].uv, ImGui::ColorConvertFloat4ToU32(ImVec4(b[v].col.r, b[v].col.g, b[v].col.b, b[v].col.a)));
            }
            for (int v = 1; v + 1 < n; ++v) {
                dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base + v)); dl->PrimWriteIdx((ImDrawIdx)(base + v + 1));
            }
        }
        dl->PopTexture();
    }
    dl->PopClipRect();
}

void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const Input* input, uint64_t selected) {
    I18n::Pause keep;   // text the game made stays as it is
    g_lastMin = min;
    g_lastMax = max;
    g_drawScene = &scene;
    ViewportRender::collect();
    const std::vector<Item> list = items(scene, min, max);
    size_t i = 0;
    while (i < list.size()) {
        const Item& it = list[i];
        if (it.surface < 0) { drawItem(dl, it, min, max, input, false); ++i; continue; }
        // A SurfaceGui's objects: drawn flat on its canvas, then put on the part.
        const Surface& s = g_surfaces[it.surface];
        ImDrawList flat(ImGui::GetDrawListSharedData());
        flat._ResetForNewFrame();
        flat.PushTexture(ImGui::GetIO().Fonts->TexRef);
        const ImVec2 cmin(0, 0), cmax(s.canvas.x, s.canvas.y);
        flat.PushClipRect(cmin, cmax);
        for (; i < list.size() && list[i].surface == it.surface; ++i) drawItem(&flat, list[i], cmin, cmax, input, true);
        emitSurface(dl, flat, s, min, max);
    }
    if (selected)   // Studio: show which one is selected
        for (const Item& it : list)
            if (it.node->id == selected && it.surface < 0) {
                dl->AddRect(ImVec2(it.a.x - 2, it.a.y - 2), ImVec2(it.b.x + 2, it.b.y + 2), IM_COL32(40, 150, 255, 255), 0, 0, 2.0f);
                dl->AddRectFilled(ImVec2(it.b.x - 4, it.b.y - 4), ImVec2(it.b.x + 4, it.b.y + 4), IM_COL32(40, 150, 255, 255));
            }
}

SceneNode* pick(Scene& scene, ImVec2 min, ImVec2 max, ImVec2 p, bool buttonsOnly) {
    const std::vector<Item> list = items(scene, min, max);
    for (auto it = list.rbegin(); it != list.rend(); ++it)   // (Studio doesn't move things on a SurfaceGui by dragging)
        if ((buttonsOnly || it->surface < 0) && inside(*it, p) && (!buttonsOnly || it->node->isGuiButton())) return it->node;
    return nullptr;
}

bool rectOf(Scene& scene, ImVec2 min, ImVec2 max, const SceneNode* node, ImVec2& a, ImVec2& b) {
    for (const Item& it : items(scene, min, max))
        if (it.node == node && it.surface < 0) { a = it.a; b = it.b; return true; }
    return false;
}

bool handle(Scene& scene, ImVec2 min, ImVec2 max, ImVec2 pointer, bool in, bool down, bool up, bool tapped,
            Input& input, std::vector<Event>& events) {
    SceneNode* top = nullptr;
    bool topOnSurface = false;   // (typing into a TextBox on a part isn't supported)
    if (in) {
        const std::vector<Item> list = items(scene, min, max);
        for (auto it = list.rbegin(); it != list.rend(); ++it)
            if (inside(*it, pointer) && blocks(it->node)) { top = it->node; topOnSurface = it->surface >= 0; break; }
    }
    // Scrolling: the wheel scrolls the innermost ScrollingFrame under the pointer that can
    // still go that way; dragging its bar (or a finger on it) moves it too.
    ImGuiIO& io = ImGui::GetIO();
    if (in && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) {
        const std::vector<Item> list = items(scene, min, max);
        for (auto it = list.rbegin(); it != list.rend(); ++it) {
            SceneNode* s = it->node;
            if (it->bars || s->gui.type != GuiType::ScrollingFrame || !s->gui.scrolling || !inside(*it, pointer)) continue;
            GuiProps& g = s->gui;
            const glm::vec2 old = g.canvasPos;
            const glm::vec2 most = glm::max(glm::vec2(0.0f), g.contentSize - g.absSize);
            const float step = 48.0f;
            float dy = -io.MouseWheel * step, dx = -io.MouseWheelH * step;
            if (!(g.scrollDir & 2) || most.y <= 0.0f) { dx += dy; dy = 0.0f; }   // only sideways: the wheel scrolls that way
            if (g.scrollDir & 1) g.canvasPos.x = std::clamp(g.canvasPos.x + dx, 0.0f, most.x);
            if (g.scrollDir & 2) g.canvasPos.y = std::clamp(g.canvasPos.y + dy, 0.0f, most.y);
            if (g.canvasPos != old) break;
        }
    }
    // (ImGui's own press, not `down`: on phones `down` is off, but a finger is a mouse to ImGui)
    if (in && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && top && top->gui.type == GuiType::ScrollingFrame && top->gui.scrolling) {
        input.dragging = top->id;
        input.dragBar = false;
        for (bool vertical : {true, false}) {
            ImVec2 ta, tb;
            if (scrollThumb(top, vertical, ta, tb) && pointer.x >= ta.x - 4 && pointer.x <= tb.x + 4 && pointer.y >= ta.y - 4 && pointer.y <= tb.y + 4) {
                input.dragBar = true;
                input.dragFrom = vertical ? 1.0f : 0.0f;   // (which bar)
            }
        }
    }
    if (input.dragging) {
        SceneNode* s = scene.findById(input.dragging);
        if (!s || !s->isGui() || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) input.dragging = 0;
        else {
            GuiProps& g = s->gui;
            const glm::vec2 most = glm::max(glm::vec2(0.0f), g.contentSize - g.absSize);
            glm::vec2 d(io.MouseDelta.x, io.MouseDelta.y);
            if (input.dragBar) {   // the bar moves with the pointer: the canvas moves more
                const bool vertical = input.dragFrom > 0.5f;
                const float view = vertical ? g.absSize.y : g.absSize.x, canvas = vertical ? g.contentSize.y : g.contentSize.x;
                const float len = std::max(16.0f, view * view / std::max(canvas, 1.0f)), travel = std::max(1.0f, view - len);
                const float k = (canvas - view) / travel;
                d = vertical ? glm::vec2(0.0f, d.y * k) : glm::vec2(d.x * k, 0.0f);
            } else {
                d = -d;   // dragging the content: it follows the finger
            }
            if (g.scrollDir & 1) g.canvasPos.x = std::clamp(g.canvasPos.x + d.x, 0.0f, most.x);
            if (g.scrollDir & 2) g.canvasPos.y = std::clamp(g.canvasPos.y + d.y, 0.0f, most.y);
        }
    }

    // TextBoxes: pressing one starts typing in it; pressing anywhere else stops.
    if ((down || tapped) && in) {
        if (top && top->gui.type == GuiType::TextBox && top->gui.editable && !topOnSurface) { if (g_focus != top->id) focus(top->id); }
        else if (g_focus) focus(0);
    }

    const uint64_t button = top && top->isGuiButton() ? top->id : 0;
    if (button != input.hovered) {
        if (input.hovered) events.push_back({EventKind::Leave, input.hovered});
        if (button) events.push_back({EventKind::Enter, button});
        input.hovered = button;
    }
    if (tapped) {   // a finger: a tap on a button presses it straight away
        if (button) events.push_back({EventKind::Click, button});
        input.pressed = 0;
    } else {
        if (down) input.pressed = button;
        if (up) {
            if (input.pressed && input.pressed == button) events.push_back({EventKind::Click, button});
            input.pressed = 0;
        }
    }
    return top != nullptr;
}

void setCamera(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& position) {
    g_view = view;
    g_viewProj = proj * view;
    g_invViewProj = glm::inverse(g_viewProj);
    g_camPos = position;
    g_haveCam = true;
}

uint64_t focused() { return g_focus; }

void focus(uint64_t id, bool enter) {
    if (id == 0) { g_wantFocus = ~0ull; g_releaseEnter = enter; }
    else g_wantFocus = id;
}

void textInput(Scene& scene, std::vector<Event>& events) {
    auto lose = [&](bool enter) {
        if (g_focus) events.push_back({EventKind::FocusLost, g_focus, enter});
        g_focus = 0;
    };
    if (g_wantFocus) {
        const uint64_t want = g_wantFocus;
        g_wantFocus = 0;
        if (want == ~0ull) lose(g_releaseEnter);
        else if (want != g_focus) {
            SceneNode* n = scene.findById(want);
            if (n && n->isGui() && n->gui.type == GuiType::TextBox) {
                lose(false);
                g_focus = want;
                g_focusAge = 0;
                if (n->gui.clearOnFocus && !n->gui.text.empty()) {
                    n->gui.text.clear();
                    events.push_back({EventKind::TextChanged, want});
                }
                events.push_back({EventKind::Focused, want});
            }
        }
    }
    if (!g_focus) return;
    SceneNode* n = scene.findById(g_focus);
    bool shown = n && n->isGui() && n->gui.type == GuiType::TextBox && n->visible && n->gui.editable;
    for (SceneNode* p = n ? n->parent : nullptr; shown && p && p->isGui(); p = p->parent)
        shown = p->gui.type == GuiType::ScreenGui ? p->enabled : p->visible;
    if (!shown || n->gui.absSize.x < 1.0f || n->gui.absSize.y < 1.0f) { lose(false); return; }

    GuiProps& g = n->gui;
    const ImVec2 a(g.absPos.x, g.absPos.y), b(a.x + g.absSize.x, a.y + g.absSize.y);
    ImGui::PushFont(nullptr, std::clamp(g.textSize * g.absScale, 6.0f, 200.0f));
    const float fh = ImGui::GetFontSize();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, rgba(g.textColor, g.textTransparency));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, g.multiLine ? 4.0f : std::max(0.0f, (b.y - a.y - fh) * 0.5f)));
    ImGui::SetCursorScreenPos(a);
    if (g_focusAge == 0) ImGui::SetKeyboardFocusHere();
    std::string before = g.text;
    bool enter = false;
    ImGui::PushID((int)(g_focus & 0x7fffffff));
    if (g.multiLine) ImGui::InputTextMultiline("##textbox", &g.text, ImVec2(b.x - a.x, b.y - a.y));
    else {
        ImGui::SetNextItemWidth(b.x - a.x);
        enter = ImGui::InputText("##textbox", &g.text, ImGuiInputTextFlags_EnterReturnsTrue);
    }
    const bool active = ImGui::IsItemActive();
    ImGui::PopID();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopFont();
    if (g.text.size() > 10000) g.text.resize(10000);
    if (g.text != before) events.push_back({EventKind::TextChanged, g_focus});
    ++g_focusAge;
    if (enter) lose(true);
    else if (g_focusAge > 2 && !active) lose(false);   // clicked somewhere else, or Escape
}

bool worldToScreen(const glm::vec3& p, ImVec2 min, ImVec2 max, ImVec2& out) { return g_haveCam && toScreen(p, min, max, out); }
bool canSee(Scene& scene, uint64_t key, const glm::vec3& p, const SceneNode* part) { return !g_haveCam || seen(scene, key, p, part); }

bool overScroller(Scene& scene, ImVec2 p) {
    for (const Item& it : items(scene, g_lastMin, g_lastMax))
        if (!it.bars && it.node->gui.type == GuiType::ScrollingFrame && it.node->gui.scrolling && inside(it, p)) return true;
    return false;
}

std::vector<std::pair<ImVec2, ImVec2>> scrollerRects(Scene& scene) {
    std::vector<std::pair<ImVec2, ImVec2>> out;
    for (const Item& it : items(scene, g_lastMin, g_lastMax))
        if (!it.bars && it.node->gui.type == GuiType::ScrollingFrame && it.node->gui.scrolling)
            out.push_back({ImVec2(std::max(it.a.x, it.clipA.x), std::max(it.a.y, it.clipA.y)),
                           ImVec2(std::min(it.b.x, it.clipB.x), std::min(it.b.y, it.clipB.y))});
    return out;
}

} // namespace GameGui
