#include "Hud.h"
#include "../core/I18n.h"
#include "../scene/Scene.h"
#include "../scene/Player.h"
#include "../scripting/ScriptEngine.h"
#include "Badges.h"
#include "../core/Voice.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Hud {

namespace {
void shadowText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text, float wrap = 0.0f) {
    dl->AddText(font, size, ImVec2(pos.x + 2, pos.y + 2), IM_COL32(0, 0, 0, 160), text, nullptr, wrap);
    dl->AddText(font, size, pos, col, text, nullptr, wrap);
}
} // namespace

void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const GuiState& gui, float topOffset, bool character) {
    I18n::Pause keep;   // text the game made stays as it is
    ImFont* font = ImGui::GetFont();
    float   base = ImGui::GetFontSize();

    // --- Health bar (top-right) ---
    if (Player* p = character ? scene.player() : nullptr) {
        const Humanoid& h = p->humanoid();
        float frac = h.maxHealth > 0 ? std::clamp(h.health / h.maxHealth, 0.0f, 1.0f) : 0.0f;
        ImVec2 size(180, 16);
        ImVec2 a(max.x - size.x - 16, min.y + 16), b(a.x + size.x, a.y + size.y);
        dl->AddRectFilled(ImVec2(a.x - 3, a.y - 3), ImVec2(b.x + 3, b.y + 3), IM_COL32(0, 0, 0, 140), 6);
        ImU32 col = frac > 0.5f ? IM_COL32(80, 200, 90, 255)
                  : frac > 0.2f ? IM_COL32(230, 190, 50, 255) : IM_COL32(220, 60, 60, 255);
        dl->AddRectFilled(a, ImVec2(a.x + size.x * frac, b.y), col, 4);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f / %.0f", h.health, h.maxHealth);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(a.x + (size.x - ts.x) * 0.5f, a.y + (size.y - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 230), buf);

        if (p->isDead()) {
            const char* oof = I18n::tr("You died!");
            float big = base * 3.0f;
            ImVec2 s = font->CalcTextSizeA(big, FLT_MAX, 0, oof);
            ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.4f);
            shadowText(dl, font, big, ImVec2(c.x - s.x * 0.5f, c.y - s.y * 0.5f), IM_COL32(255, 90, 80, 255), oof);
            char r[48];
            std::snprintf(r, sizeof(r), I18n::tr("Respawning in %.0f..."), std::max(0.0f, p->respawnIn()) + 0.49f);
            ImVec2 rs = ImGui::CalcTextSize(r);
            shadowText(dl, font, base, ImVec2(c.x - rs.x * 0.5f, c.y + s.y * 0.6f), IM_COL32(255, 255, 255, 230), r);
        }
    }

    // --- Script labels (top-left) ---
    float y = min.y + 14 + topOffset;
    for (const auto& [key, text] : gui.labels) {
        ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        dl->AddRectFilled(ImVec2(min.x + 12, y - 4), ImVec2(min.x + 24 + ts.x, y + ts.y + 4),
                          IM_COL32(0, 0, 0, 130), 5);
        dl->AddText(ImVec2(min.x + 18, y), IM_COL32(255, 255, 255, 240), text.c_str());
        y += ts.y + 12;
    }

    // --- Big message (centre) ---
    if (!gui.message.empty()) {
        // Big, but never wider than the screen: long messages wrap onto more
        // lines, and very long ones get smaller (phones held sideways are narrow).
        const float wrap = std::max(120.0f, (max.x - min.x) - 80.0f);
        float big = base * 2.0f;
        ImVec2 s = font->CalcTextSizeA(big, FLT_MAX, wrap, gui.message.c_str());
        if (s.y > big * 2.6f) { big = base * 1.4f; s = font->CalcTextSizeA(big, FLT_MAX, wrap, gui.message.c_str()); }
        // Touch screens: a bit lower, clear of the buttons and the player list.
        ImVec2 c((min.x + max.x) * 0.5f, min.y + (max.y - min.y) * (topOffset > 0.0f ? 0.34f : 0.22f));
        dl->AddRectFilled(ImVec2(c.x - s.x * 0.5f - 18, c.y - s.y * 0.5f - 10),
                          ImVec2(c.x + s.x * 0.5f + 18, c.y + s.y * 0.5f + 10), IM_COL32(0, 0, 0, 140), 8);
        shadowText(dl, font, big, ImVec2(c.x - s.x * 0.5f, c.y - s.y * 0.5f), IM_COL32(255, 255, 255, 255),
                   gui.message.c_str(), wrap);
    }
}

void drawBubbles(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const glm::mat4& viewProj,
                 const std::unordered_map<std::string, std::pair<std::string, float>>& bubbles) {
    if (bubbles.empty()) return;
    I18n::Pause keep;   // what people said stays as it is
    auto drawFor = [&](SceneNode* root) {
        if (!root) return;
        auto it = bubbles.find(root->name);
        if (it == bubbles.end()) return;
        SceneNode* head = root->findChild("Head");
        glm::vec3 p = head ? glm::vec3(head->worldMatrix()[3]) : root->transform.position + glm::vec3(0, 2.4f, 0);
        glm::vec4 c = viewProj * glm::vec4(p + glm::vec3(0, 1.45f, 0), 1.0f);   // above the name tag
        if (c.w <= 0.1f) return;
        glm::vec2 ndc = glm::vec2(c) / c.w;
        ImVec2 sp(min.x + (ndc.x * 0.5f + 0.5f) * (max.x - min.x), min.y + (0.5f - ndc.y * 0.5f) * (max.y - min.y));
        const std::string& text = it->second.first;
        float wrap = 220.0f;
        ImVec2 ts = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
        float alpha = std::min(1.0f, it->second.second);
        ImVec2 a(sp.x - ts.x * 0.5f - 10, sp.y - ts.y - 16), b(sp.x + ts.x * 0.5f + 10, sp.y - 6);
        dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, (int)(235 * alpha)), 10.0f);
        dl->AddTriangleFilled(ImVec2(sp.x - 7, b.y), ImVec2(sp.x + 7, b.y), ImVec2(sp.x, b.y + 8),
                              IM_COL32(255, 255, 255, (int)(235 * alpha)));
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + 10, a.y + 5),
                    IM_COL32(20, 20, 25, (int)(255 * alpha)), text.c_str(), nullptr, wrap);
    };
    if (Player* p = scene.player()) drawFor(p->root());
    for (auto& rc : scene.remotes()) drawFor(scene.findById(rc.rootId));
}

void drawNameTags(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const glm::mat4& viewProj,
                  const glm::vec3& cameraPos) {
    I18n::Pause keep;   // text the game made stays as it is
    constexpr float kShowUntil = 60.0f, kFadeFrom = 45.0f;   // units away
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    auto drawFor = [&](SceneNode* root, const std::string& name, const Humanoid* hum, bool me) {
        if (!root || name.empty()) return;
        SceneNode* head = root->findChild("Head");
        if (!head || head->shownTransparency() > 0.5f) return;   // faded out (first person), or headless
        glm::vec3 top = glm::vec3(head->worldMatrix()[3]) + glm::vec3(0.0f, 0.75f, 0.0f);
        float dist = glm::length(top - cameraPos);
        if (dist > kShowUntil) return;
        float alpha = std::clamp((kShowUntil - dist) / (kShowUntil - kFadeFrom), 0.0f, 1.0f);
        glm::vec4 c = viewProj * glm::vec4(top, 1.0f);
        if (c.w <= 0.1f) return;
        glm::vec2 ndc = glm::vec2(c) / c.w;
        if (std::abs(ndc.x) > 1.2f || std::abs(ndc.y) > 1.2f) return;
        ImVec2 sp(min.x + (ndc.x * 0.5f + 0.5f) * (max.x - min.x), min.y + (0.5f - ndc.y * 0.5f) * (max.y - min.y));
        ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, name.c_str());
        ImVec2 at(std::floor(sp.x - ts.x * 0.5f), std::floor(sp.y - ts.y));
        // White with a dark outline, readable on any background.
        const ImU32 edge = IM_COL32(0, 0, 0, (int)(200 * alpha));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx || dy) dl->AddText(font, size, ImVec2(at.x + dx, at.y + dy), edge, name.c_str());
        dl->AddText(font, size, at, IM_COL32(255, 255, 255, (int)(255 * alpha)), name.c_str());
        // Someone else's health, when they've been hurt.
        if (!me && hum && hum->health < hum->maxHealth && hum->maxHealth > 0.0f) {
            float f = std::clamp(hum->health / hum->maxHealth, 0.0f, 1.0f);
            float w = std::max(40.0f, ts.x), y = at.y + ts.y + 2.0f;
            ImVec2 a(std::floor(sp.x - w * 0.5f), y), b(a.x + w, y + 5.0f);
            dl->AddRectFilled(a, b, IM_COL32(40, 0, 0, (int)(200 * alpha)), 2.0f);
            ImU32 col = f > 0.5f ? IM_COL32(60, 220, 80, (int)(255 * alpha))
                      : f > 0.25f ? IM_COL32(240, 200, 40, (int)(255 * alpha)) : IM_COL32(240, 60, 50, (int)(255 * alpha));
            dl->AddRectFilled(a, ImVec2(a.x + w * f, b.y), col, 2.0f);
        }
    };
    if (Player* p = scene.player(); p && !p->isDead()) drawFor(p->root(), p->root() ? p->root()->name : "", &p->humanoid(), true);
    for (auto& rc : scene.remotes())
        if (rc.alive) drawFor(scene.findById(rc.rootId), rc.name, &rc.humanoid, false);
    for (const auto& n : scene.npcs().all())
        if (!n->dead)
            if (SceneNode* r = scene.findById(n->rootId)) drawFor(r, r->name, &n->humanoid, false);
}

namespace {
constexpr float kSlot = 58.0f, kGap = 6.0f;
bool g_backpackOpen = false;
float g_backpackScroll = 0.0f;
ImVec2 backpackButtonMin(ImVec2 max) { return ImVec2(max.x - 108.0f, max.y - 76.0f); }
ImVec2 backpackPanelMin(ImVec2 max) { return ImVec2(max.x - 302.0f, max.y - 374.0f); }
bool inRect(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y; }
ImVec2 hotbarStart(ImVec2 min, ImVec2 max, int n) {
    float w = n * kSlot + (n - 1) * kGap;
    return ImVec2(std::floor((min.x + max.x - w) * 0.5f), max.y - kSlot - 14.0f);
}
int hotbarCount(Scene& scene) {
    Player* p = scene.player();
    return p && !p->isDead() ? std::min((int)p->tools().size(), Player::kHotbarTools) : 0;
}

// The game creator's hammer: a wooden handle leaning right, with a steel head on top.
void drawHammer(ImDrawList* dl, ImVec2 c, float size) {
    const float u = size / 14.0f;
    auto P = [&](float x, float y) { return ImVec2(c.x + x * u, c.y + y * u); };
    dl->AddLine(P(-5, 6), P(2, -1), IM_COL32(0, 0, 0, 110), 3.6f * u);         // shadow so it reads on bright skies
    dl->AddLine(P(-5, 6), P(2, -1), IM_COL32(214, 150, 84, 255), 2.4f * u);     // handle
    ImVec2 head[] = {P(7.6f, -0.2f), P(4.8f, 2.6f), P(-1.6f, -3.8f), P(1.2f, -6.6f)};   // the head, across the handle
    dl->AddConvexPolyFilled(head, 4, IM_COL32(205, 212, 222, 255));
    dl->AddPolyline(head, 4, IM_COL32(0, 0, 0, 140), ImDrawFlags_Closed, 1.0f * u);
}
} // namespace

void drawSpeaker(ImDrawList* dl, ImVec2 c, float size, bool talking, bool muted) {
    const float u = size / 14.0f;
    auto P = [&](float x, float y) { return ImVec2(c.x + x * u, c.y + y * u); };
    const ImU32 body = muted ? IM_COL32(170, 170, 175, 255) : talking ? IM_COL32(120, 235, 140, 255) : IM_COL32(235, 235, 240, 255);
    ImVec2 cone[] = {P(-6, -2.2f), P(-3, -2.2f), P(1, -6), P(1, 6), P(-3, 2.2f), P(-6, 2.2f)};
    dl->AddConvexPolyFilled(cone, 6, IM_COL32(0, 0, 0, 120));   // (a soft shadow first, so it reads on bright skies)
    for (ImVec2& v : cone) v.x -= 0.6f * u, v.y -= 0.6f * u;
    dl->AddConvexPolyFilled(cone, 6, body);
    if (talking && !muted) {   // waves that pulse while they talk
        const float t = (float)ImGui::GetTime();
        for (int i = 0; i < 2; ++i) {
            const float a = 0.55f + 0.45f * std::sin(t * 9.0f - i * 1.3f);
            dl->PathArcTo(P(1, 0), (3.5f + i * 3.0f) * u, -0.9f, 0.9f, 10);
            dl->PathStroke(IM_COL32(120, 235, 140, (int)(255 * a)), 0, 1.6f * u);
        }
    }
    if (muted) dl->AddLine(P(-7, 6), P(6, -6), IM_COL32(230, 70, 70, 255), 2.0f * u);
}

void drawVoiceTags(ImDrawList* dl, ImVec2 min, ImVec2 max, const glm::mat4& viewProj) {
    Voice::eachSpeaking([&](const std::string&, const glm::vec3& head) {
        glm::vec4 c = viewProj * glm::vec4(head + glm::vec3(0.0f, 2.0f, 0.0f), 1.0f);   // above the name tag
        if (c.w <= 0.1f) return;
        glm::vec2 ndc = glm::vec2(c) / c.w;
        if (std::abs(ndc.x) > 1.1f || std::abs(ndc.y) > 1.1f) return;
        ImVec2 sp(min.x + (ndc.x * 0.5f + 0.5f) * (max.x - min.x), min.y + (0.5f - ndc.y * 0.5f) * (max.y - min.y));
        dl->AddCircleFilled(sp, 13.0f, IM_COL32(0, 0, 0, 110), 20);
        drawSpeaker(dl, ImVec2(sp.x - 1, sp.y), 16.0f, true);
    });
}

bool overHotbar(ImVec2 min, ImVec2 max, Scene& scene, ImVec2 p) {
    const ImVec2 bp = backpackButtonMin(max), panel = backpackPanelMin(max);
    if (inRect(p, bp, ImVec2(max.x - 12.0f, max.y - 38.0f))) return true;
    if (g_backpackOpen && inRect(p, panel, ImVec2(max.x - 12.0f, max.y - 84.0f))) return true;
    int n = hotbarCount(scene);
    if (!n) return false;
    ImVec2 a = hotbarStart(min, max, n);
    return p.x >= a.x && p.x <= a.x + n * (kSlot + kGap) && p.y >= a.y && p.y <= a.y + kSlot;
}

int drawHotbar(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const ImVec2* tap) {
    Player* p = scene.player();
    int n = hotbarCount(scene);
    if (!n) return -1;
    std::vector<SceneNode*> list = p->tools();
    SceneNode* held = p->equippedTool();
    ImVec2 a = hotbarStart(min, max, n);
    ImVec2 mouse = ImGui::GetIO().MousePos;
    int clicked = -1;
    ImFont* font = ImGui::GetFont();
    for (int i = 0; i < n; ++i) {
        SceneNode* t = list[(size_t)i];
        ImVec2 s0(a.x + i * (kSlot + kGap), a.y), s1(s0.x + kSlot, s0.y + kSlot);
        bool on = t == held;
        bool hover = mouse.x >= s0.x && mouse.x < s1.x && mouse.y >= s0.y && mouse.y < s1.y;
        dl->AddRectFilled(s0, s1, on ? IM_COL32(245, 245, 245, 215) : IM_COL32(20, 22, 28, hover ? 200 : 160), 7.0f);
        dl->AddRect(s0, s1, on ? IM_COL32(40, 140, 255, 255) : IM_COL32(255, 255, 255, 60), 7.0f, 0, on ? 3.0f : 1.0f);
        // A little picture: the Handle's colour as a diagonal stick.
        if (SceneNode* h = t->findChild("Handle")) {
            ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(h->color.r, h->color.g, h->color.b, 1.0f));
            dl->AddLine(ImVec2(s0.x + kSlot * 0.3f, s1.y - kSlot * 0.3f), ImVec2(s1.x - kSlot * 0.28f, s0.y + kSlot * 0.26f), col, 6.0f);
        }
        const char* num = i == 9 ? "0" : (i == 8 ? "9" : nullptr);
        char numBuf[4];
        if (i < 8) { std::snprintf(numBuf, sizeof(numBuf), "%d", i + 1); num = numBuf; }
        dl->AddText(ImVec2(s0.x + 5, s0.y + 3), on ? IM_COL32(30, 30, 40, 255) : IM_COL32(255, 255, 255, 200), num);
        // The name along the bottom, cut to fit.
        std::string name = t->name;
        while (name.size() > 1 && ImGui::CalcTextSize(name.c_str()).x > kSlot - 6) name.pop_back();
        ImVec2 ts = ImGui::CalcTextSize(name.c_str());
        dl->AddText(font, ImGui::GetFontSize(), ImVec2(s0.x + (kSlot - ts.x) * 0.5f, s1.y - ts.y - 3),
                    on ? IM_COL32(20, 20, 30, 255) : IM_COL32(255, 255, 255, 235), name.c_str());
        if (hover) {
            const std::string& tip = t->toolTip.empty() ? t->name : t->toolTip;
            ImVec2 tt = ImGui::CalcTextSize(tip.c_str());
            ImVec2 b0(s0.x + (kSlot - tt.x) * 0.5f - 6, s0.y - tt.y - 12);
            dl->AddRectFilled(b0, ImVec2(b0.x + tt.x + 12, b0.y + tt.y + 6), IM_COL32(20, 22, 28, 220), 4.0f);
            dl->AddText(ImVec2(b0.x + 6, b0.y + 3), IM_COL32(255, 255, 255, 255), tip.c_str());
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked = i;
        }
        if (tap && tap->x >= s0.x && tap->x < s1.x && tap->y >= s0.y && tap->y < s1.y) clicked = i;
    }
    return clicked;
}

int drawBackpack(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const ImVec2* tap) {
    Player* p = scene.player();
    if (!p || p->isDead()) { g_backpackOpen = false; return -1; }
    const auto tools = p->tools();
    const ImVec2 button = backpackButtonMin(max);
    const ImVec2 buttonMax(max.x - 12.0f, max.y - 38.0f);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool buttonHover = inRect(mouse, button, buttonMax);
    const bool buttonTap = tap && inRect(*tap, button, buttonMax);
    dl->AddRectFilled(button, buttonMax, g_backpackOpen ? IM_COL32(45, 112, 190, 235) : IM_COL32(20, 22, 28, buttonHover ? 230 : 185), 7.0f);
    dl->AddRect(button, buttonMax, IM_COL32(255, 255, 255, 95), 7.0f);
    const char* label = "Backpack";
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(button.x + (buttonMax.x - button.x - labelSize.x) * 0.5f,
                       button.y + (buttonMax.y - button.y - labelSize.y) * 0.5f), IM_COL32(255,255,255,255), label);
    if ((buttonHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || buttonTap) g_backpackOpen = !g_backpackOpen;
    if (!g_backpackOpen) return -1;

    const ImVec2 panel = backpackPanelMin(max), panelMax(max.x - 12.0f, max.y - 84.0f);
    dl->AddRectFilled(panel, panelMax, IM_COL32(20, 22, 28, 242), 9.0f);
    dl->AddRect(panel, panelMax, IM_COL32(255, 255, 255, 95), 9.0f);
    dl->AddText(ImVec2(panel.x + 12.0f, panel.y + 10.0f), IM_COL32(255,255,255,255), "Backpack");
    char count[64]; std::snprintf(count, sizeof(count), "%zu tools", tools.size());
    dl->AddText(ImVec2(panelMax.x - 80.0f, panel.y + 10.0f), IM_COL32(190,195,205,255), count);
    const float rowH = 31.0f, top = panel.y + 40.0f, bottom = panelMax.y - 10.0f;
    const float visibleH = bottom - top;
    const float extraCount = (float)(tools.size() > Player::kHotbarTools ? tools.size() - Player::kHotbarTools : 0);
    const float maxScroll = std::max(0.0f, extraCount * rowH - visibleH);
    if (inRect(mouse, ImVec2(panel.x, top), ImVec2(panelMax.x, bottom)) && ImGui::GetIO().MouseWheel != 0.0f)
        g_backpackScroll = std::clamp(g_backpackScroll - ImGui::GetIO().MouseWheel * rowH * 2.0f, 0.0f, maxScroll);
    else g_backpackScroll = std::clamp(g_backpackScroll, 0.0f, maxScroll);
    int selected = -1;
    for (size_t i = Player::kHotbarTools; i < tools.size(); ++i) {
        const float y = top + (float)(i - Player::kHotbarTools) * rowH - g_backpackScroll;
        if (y + rowH < top || y > bottom) continue;
        ImVec2 a(panel.x + 8.0f, y), b(panelMax.x - 8.0f, y + rowH - 2.0f);
        const bool hover = inRect(mouse, a, b);
        if (hover) dl->AddRectFilled(a, b, IM_COL32(70, 90, 120, 180), 4.0f);
        char index[16]; std::snprintf(index, sizeof(index), "%zu.", i + 1);
        dl->AddText(ImVec2(a.x + 7.0f, a.y + 6.0f), IM_COL32(180,190,205,255), index);
        std::string name = tools[i]->name;
        while (name.size() > 1 && ImGui::CalcTextSize(name.c_str()).x > b.x - a.x - 55.0f) name.pop_back();
        dl->AddText(ImVec2(a.x + 38.0f, a.y + 6.0f), IM_COL32(255,255,255,255), name.c_str());
        if ((hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || (tap && inRect(*tap, a, b))) selected = (int)i;
    }
    return selected;
}

std::string drawPlayerList(ImDrawList* dl, ImVec2 min, ImVec2 max, const std::vector<PlayerEntry>& players,
                           bool& open, ImVec2* clickedAt, const ImVec2* tap) {
    std::string picked;
    if (players.empty()) return picked;
    // leaderstats columns (like Roblox's leaderboard): every stat name anyone has, in order.
    std::vector<std::string> cols;
    for (const auto& p : players)
        for (const auto& [k, v] : p.stats)
            if (std::find(cols.begin(), cols.end(), k) == cols.end() && cols.size() < 4) cols.push_back(k);
    const float rowH = 22.0f, nameW = 170.0f, colW = 64.0f;
    const float w = nameW + colW * (float)cols.size() + (cols.empty() ? 30.0f : 0.0f);
    float y = min.y + 50;
    float x = max.x - w - 16;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto inside = [&](ImVec2 a, ImVec2 b) {
        bool m = mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y;
        bool t = tap && tap->x >= a.x && tap->x < b.x && tap->y >= a.y && tap->y < b.y;
        return std::make_pair(m, t || (m && ImGui::IsMouseClicked(ImGuiMouseButton_Left)));
    };

    // The title bar, with the fold arrow on its left. Folded, only a small tab shows.
    const float headW = open ? w + 8 : 92.0f;
    const ImVec2 h0(max.x - headW - 12, y - 4), h1(max.x - 12, y + 20);
    auto [headHover, headClick] = inside(h0, h1);
    dl->AddRectFilled(h0, h1, IM_COL32(0, 0, 0, headHover ? 165 : 130), 6.0f,
                      open ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
    {   // the arrow: pointing down when open, left when folded away
        const ImVec2 c(h0.x + 11, y + 8);
        const ImU32 ac = IM_COL32(255, 200, 120, 255);
        if (open) dl->AddTriangleFilled(ImVec2(c.x - 5, c.y - 3), ImVec2(c.x + 5, c.y - 3), ImVec2(c.x, c.y + 4), ac);
        else dl->AddTriangleFilled(ImVec2(c.x + 3, c.y - 5), ImVec2(c.x + 3, c.y + 5), ImVec2(c.x - 4, c.y), ac);
    }
    dl->AddText(ImVec2(h0.x + 22, y), IM_COL32(255, 200, 120, 255), "Players");
    if (headHover) {
        const char* tip = open ? "Hide (Tab)" : "Show players (Tab)";
        ImVec2 ts = ImGui::CalcTextSize(tip);
        dl->AddRectFilled(ImVec2(h1.x - ts.x - 12, h1.y + 4), ImVec2(h1.x, h1.y + ts.y + 10), IM_COL32(20, 22, 28, 220), 4.0f);
        dl->AddText(ImVec2(h1.x - ts.x - 6, h1.y + 7), IM_COL32(255, 255, 255, 255), tip);
    }
    if (headClick) open = !open;
    if (!open) return picked;

    dl->AddRectFilled(ImVec2(x - 4, y + 20), ImVec2(x + w + 4, y + 24 + players.size() * rowH),
                      IM_COL32(0, 0, 0, 120), 6.0f, ImDrawFlags_RoundCornersBottom);
    auto rightText = [&](float colRight, float ty, const std::string& text, ImU32 col) {
        std::string t = text;
        while (t.size() > 1 && ImGui::CalcTextSize(t.c_str()).x > colW - 6) t.pop_back();
        dl->AddText(ImVec2(colRight - ImGui::CalcTextSize(t.c_str()).x, ty), col, t.c_str());
    };
    for (size_t c = 0; c < cols.size(); ++c)
        rightText(x + nameW + colW * (float)(c + 1), y, cols[c], IM_COL32(255, 200, 120, 255));
    y += 24;
    float t = (float)ImGui::GetTime();
    for (size_t i = 0; i < players.size(); ++i) {
        const PlayerEntry& p = players[i];
        // The whole row is a button: click someone's name to friend or follow them.
        auto [hover, click] = inside(ImVec2(x - 4, y - 1), ImVec2(x + w + 4, y + rowH - 1));
        if (hover) dl->AddRectFilled(ImVec2(x - 2, y - 1), ImVec2(x + w + 2, y + rowH - 2), IM_COL32(255, 255, 255, 40), 4.0f);
        if (click) { picked = p.name; if (clickedAt) *clickedAt = ImVec2(x - 4, y + rowH); }
        float tx = x + 4;
        if (p.admin) {
            // The Administrator badge bobs gently next to the name.
            float bob = std::sin(t * 2.4f + (float)i) * 2.0f;
            ImVec2 c(x + 12, y + 8 + bob);
            dl->AddCircleFilled(c, 11.0f, IM_COL32(255, 60, 60, 40), 20);   // soft glow
            Badges::drawIcon(dl, c, 17.0f, Badges::Id::Administrator);
            tx = x + 26;
        }
        dl->AddText(ImVec2(tx, y), p.admin ? IM_COL32(255, 225, 120, 255) : IM_COL32(255, 255, 255, 230),
                    p.name.c_str());
        float after = tx + ImGui::CalcTextSize(p.name.c_str()).x + 3;   // where the next little icon goes
        if (p.verified || p.admin) {   // blue check after Verified names (staff are always verified)
            Badges::drawCheck(dl, ImVec2(after + 6, y + ImGui::GetFontSize() * 0.5f + 1), 13.0f);
            after += 15;
        }
        if (p.creator) {   // a little hammer: they made this game
            const ImVec2 c(after + 8, y + ImGui::GetFontSize() * 0.5f + 1);
            drawHammer(dl, c, 14.0f);
            if (ImGui::IsMouseHoveringRect(ImVec2(c.x - 7, c.y - 7), ImVec2(c.x + 7, c.y + 7))) {
                const char* tip = "Made this game";
                ImVec2 ts = ImGui::CalcTextSize(tip);
                ImVec2 t0(c.x - ts.x * 0.5f - 6, c.y - ts.y - 18);
                dl->AddRectFilled(t0, ImVec2(t0.x + ts.x + 12, t0.y + ts.y + 6), IM_COL32(20, 22, 28, 230), 4.0f);
                dl->AddText(ImVec2(t0.x + 6, t0.y + 3), IM_COL32(255, 255, 255, 255), tip);
            }
            after += 18;
        }
        if (p.voice) {   // voice chat: talking now, or muted by us
            drawSpeaker(dl, ImVec2(after + 10, y + ImGui::GetFontSize() * 0.5f + 1), 13.0f, p.voice == 1, p.voice == 2);
            after += 18;
        }
        for (size_t c = 0; c < cols.size(); ++c)
            for (const auto& [k, v] : p.stats)
                if (k == cols[c]) rightText(x + nameW + colW * (float)(c + 1), y, v, IM_COL32(255, 255, 255, 230));
        y += rowH;
    }
    return picked;
}

} // namespace Hud
