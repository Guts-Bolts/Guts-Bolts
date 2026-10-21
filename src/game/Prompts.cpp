// ProximityPrompts, like Roblox's: walk up to a door and a little card says
// "[E] Open  Door". Press E (or click / tap the card) and the prompt's Triggered
// event fires. With a HoldDuration you hold the key while a ring fills up.
#include "GameGui.h"
#include "../core/I18n.h"
#include "../core/Pad.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <unordered_map>

namespace GameGui {

namespace {

// One prompt being shown this frame.
struct Shown {
    uint64_t id = 0;
    bool     onScreen = false;
    ImVec2   a, b;          // its card on screen
    ImVec2   at;            // the point in the world, on screen
    bool     active = false; // its key works (the nearest prompt with that key)
    bool     padActive = false; // a controller's X works it (the nearest prompt of all)
    std::string action, object, key;
    float    hold = 0.0f;
};

// What's happening with a prompt while it shows (kept between frames).
struct State {
    bool  down = false;       // its key / the pointer is held on it
    bool  triggered = false;  // Triggered fired for this press
    float held = 0.0f;        // seconds held (HoldDuration)
    float fade = 0.0f;        // 0..1 as the card pops in
};

std::vector<Shown> g_shown;                 // (this frame, nearest first)
std::unordered_map<uint64_t, State> g_state;
uint64_t g_pressedCard = 0;                 // the card the pointer went down on

constexpr int   kMaxShown = 4;
constexpr float kPi = 3.14159265f;
constexpr float kCardH = 52.0f, kBadge = 40.0f;

// Roblox's KeyCode names -> ImGui's keys.
ImGuiKey keyFor(const std::string& name) {
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') return (ImGuiKey)(ImGuiKey_A + (name[0] - 'A'));
    static const char* digits[] = {"Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"};
    for (int i = 0; i < 10; ++i) if (name == digits[i]) return (ImGuiKey)(ImGuiKey_0 + i);
    if (name == "Space")       return ImGuiKey_Space;
    if (name == "Return")      return ImGuiKey_Enter;
    if (name == "Tab")         return ImGuiKey_Tab;
    if (name == "LeftShift")   return ImGuiKey_LeftShift;
    if (name == "LeftControl") return ImGuiKey_LeftCtrl;
    if (name == "LeftAlt")     return ImGuiKey_LeftAlt;
    if (name.size() >= 2 && name[0] == 'F') {
        const int f = std::atoi(name.c_str() + 1);
        if (f >= 1 && f <= 12) return (ImGuiKey)(ImGuiKey_F1 + f - 1);
    }
    return ImGuiKey_None;
}

// What the key badge says.
std::string keyLabel(const std::string& name) {
    static const char* digits[] = {"Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"};
    for (int i = 0; i < 10; ++i) if (name == digits[i]) return std::to_string(i);
    if (name == "Return") return "Enter";
    if (name == "LeftShift") return "Shift";
    if (name == "LeftControl") return "Ctrl";
    if (name == "LeftAlt") return "Alt";
    return name;
}

// Where a prompt sits: the part it's in, an Attachment's spot, or a model's first part.
bool placeOf(const SceneNode* prompt, glm::vec3& out, const SceneNode*& part) {
    const SceneNode* p = prompt->parent;
    part = nullptr;
    if (!p) return false;
    if (p->kind == NodeKind::Model) {
        for (auto& c : p->children) if (c->isPart()) { p = c.get(); break; }
        if (p->kind == NodeKind::Model) return false;
    }
    if (!p->isPart() && p->kind != NodeKind::Attachment) return false;
    out = glm::vec3(p->worldMatrix()[3]);
    part = p->isPart() ? p : (p->parent && p->parent->isPart() ? p->parent : nullptr);
    return true;
}

} // namespace

bool prompts(Scene& scene, ImVec2 min, ImVec2 max, const glm::vec3* player, ImVec2 pointer, bool acceptInput,
             float dt, std::vector<Event>& events) {
    // Which prompts are close enough (and in view, if they need to be)?
    struct Near { SceneNode* n; float dist; glm::vec3 at; };
    std::vector<Near> near;
    if (player)
        scene.forEach([&](SceneNode* n) {
            if (!n->isPrompt() || !n->enabled || scene.isCharacterPart(n)) return;
            glm::vec3 at;
            const SceneNode* part;
            if (!placeOf(n, at, part)) return;
            const float d = glm::length(at - *player);
            if (d > n->prompt.range) return;
            if (n->prompt.lineOfSight && part && !canSee(scene, n->id ^ 0x5052u, at, part)) return;
            near.push_back({n, d, at});
        });
    std::sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.dist < b.dist; });
    if ((int)near.size() > kMaxShown) near.resize(kMaxShown);

    // Gone: hidden (and any press on it ends).
    std::set<uint64_t> now;
    for (const Near& e : near) now.insert(e.n->id);
    for (auto it = g_state.begin(); it != g_state.end();) {
        if (now.count(it->first)) { ++it; continue; }
        if (it->second.down) {
            if (scene.findById(it->first) && scene.findById(it->first)->prompt.hold > 0.0f)
                events.push_back({EventKind::PromptHoldEnded, it->first});
            if (it->second.triggered) events.push_back({EventKind::PromptTriggerEnded, it->first});
        }
        events.push_back({EventKind::PromptHidden, it->first});
        it = g_state.erase(it);
    }
    if (g_pressedCard && !now.count(g_pressedCard)) g_pressedCard = 0;

    // Lay out the cards, and give each key to the nearest prompt that uses it.
    g_shown.clear();
    std::set<ImGuiKey> keysTaken;
    ImFont* font = ImGui::GetFont();
    for (const Near& e : near) {
        const PromptProps& p = e.n->prompt;
        if (!g_state.count(e.n->id)) events.push_back({EventKind::PromptShown, e.n->id});
        State& st = g_state[e.n->id];
        st.fade = std::min(1.0f, st.fade + dt * 8.0f);
        Shown s;
        s.id = e.n->id;
        const ImGuiKey key = keyFor(p.key);
        s.active = key != ImGuiKey_None && keysTaken.insert(key).second;
        s.padActive = g_shown.empty();   // (nearest first)
        s.action = p.action;
        s.object = p.object;
        s.key = keyLabel(p.key);
        if (Pad::inUse()) { s.key = "X"; s.active = s.padActive; }   // playing with a controller: its button
        s.hold = p.hold;
        if (worldToScreen(e.at, min, max, s.at)) {
            const float tw = std::max(font->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, p.action.c_str()).x,
                                      font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, p.object.c_str()).x);
            const float w = kBadge + 22.0f + tw + (p.action.empty() && p.object.empty() ? -12.0f : 0.0f);
            s.a = ImVec2(std::round(s.at.x - w * 0.5f), std::round(s.at.y - kCardH * 0.5f));
            s.b = ImVec2(s.a.x + w, s.a.y + kCardH);
            s.onScreen = s.b.x > min.x && s.a.x < max.x && s.b.y > min.y && s.a.y < max.y;
        }
        g_shown.push_back(s);
    }

    // Pressing: the key (its nearest prompt), or the pointer on a card.
    bool overCard = false;
    for (const Shown& s : g_shown) {
        SceneNode* n = scene.findById(s.id);
        if (!n) continue;
        const PromptProps& p = n->prompt;
        const bool onCard = s.onScreen && p.clickable && pointer.x >= s.a.x && pointer.x < s.b.x &&
                            pointer.y >= s.a.y && pointer.y < s.b.y;
        if (onCard && acceptInput) overCard = true;
        if (onCard && acceptInput && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) g_pressedCard = s.id;
        const ImGuiKey key = keyFor(p.key);
        const bool keyHeld = acceptInput && !ImGui::GetIO().WantTextInput &&
                             ((s.active && key != ImGuiKey_None && ImGui::IsKeyDown(key)) ||
                              (s.padActive && Pad::connected() && ImGui::IsKeyDown(ImGuiKey_GamepadFaceLeft)));
        const bool pointerHeld = g_pressedCard == s.id && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool held = keyHeld || pointerHeld;
        State& st = g_state[s.id];
        if (held && !st.down) {
            st.down = true;
            st.held = 0.0f;
            if (p.hold > 0.0f) events.push_back({EventKind::PromptHoldBegan, s.id});
            else { events.push_back({EventKind::PromptTriggered, s.id}); st.triggered = true; }
        } else if (held && !st.triggered && p.hold > 0.0f) {
            st.held += dt;
            if (st.held >= p.hold) { events.push_back({EventKind::PromptTriggered, s.id}); st.triggered = true; }
        } else if (!held && st.down) {
            st.down = false;
            if (p.hold > 0.0f) events.push_back({EventKind::PromptHoldEnded, s.id});
            if (st.triggered) events.push_back({EventKind::PromptTriggerEnded, s.id});
            st.triggered = false;
            st.held = 0.0f;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) g_pressedCard = 0;
    return overCard || g_pressedCard != 0;
}

void drawPrompts(ImDrawList* dl) {
    I18n::Pause keep;   // text the game made stays as it is
    ImFont* font = ImGui::GetFont();
    // Farthest first, so the nearest card is on top.
    for (auto it = g_shown.rbegin(); it != g_shown.rend(); ++it) {
        const Shown& s = *it;
        auto st = g_state.find(s.id);
        if (!s.onScreen || st == g_state.end()) continue;
        const State& state = st->second;
        const float fade = state.fade;
        auto alpha = [&](float v) { return (int)(v * fade); };
        // It pops in (and squashes a little while held, like a button).
        const float pop = (0.85f + 0.15f * fade) * (state.down ? 0.96f : 1.0f);
        const ImVec2 c((s.a.x + s.b.x) * 0.5f, (s.a.y + s.b.y) * 0.5f);
        auto at = [&](ImVec2 p) { return ImVec2(c.x + (p.x - c.x) * pop, c.y + (p.y - c.y) * pop); };
        const ImVec2 a = at(s.a), b = at(s.b);
        dl->AddRectFilled(a, b, IM_COL32(18, 18, 22, alpha(175)), 8.0f * pop);

        // The key badge (grey when a nearer prompt has the same key: click that one instead).
        const float bs = kBadge * pop;
        const ImVec2 ba(a.x + 6.0f * pop, c.y - bs * 0.5f), bb(ba.x + bs, ba.y + bs);
        const ImVec2 bc((ba.x + bb.x) * 0.5f, (ba.y + bb.y) * 0.5f);
        dl->AddRectFilled(ba, bb, s.active ? IM_COL32(245, 245, 245, alpha(240)) : IM_COL32(140, 140, 145, alpha(210)), 6.0f * pop);
        float ks = (s.key.size() <= 2 ? 22.0f : s.key.size() <= 4 ? 15.0f : 12.0f) * pop;
        ImVec2 kt = font->CalcTextSizeA(ks, FLT_MAX, 0.0f, s.key.c_str());
        dl->AddText(font, ks, ImVec2(bc.x - kt.x * 0.5f, bc.y - kt.y * 0.5f), IM_COL32(20, 20, 24, alpha(255)), s.key.c_str());
        // Holding: a ring around the badge fills up clockwise from the top.
        if (s.hold > 0.0f && state.down) {
            const float f = state.triggered ? 1.0f : std::clamp(state.held / s.hold, 0.0f, 1.0f);
            const float r = bs * 0.5f + 5.0f * pop;
            const float start = -kPi * 0.5f;
            dl->PathArcTo(bc, r, start, start + kPi * 2.0f * f, 40);
            dl->PathStroke(IM_COL32(255, 255, 255, alpha(255)), 0, 4.0f * pop);
        }

        // ObjectText (small, grey) over ActionText.
        const float tx = bb.x + 10.0f * pop;
        const float as = 18.0f * pop, os = 14.0f * pop;
        if (s.object.empty()) {
            dl->AddText(font, as, ImVec2(tx, c.y - as * 0.55f), IM_COL32(255, 255, 255, alpha(255)), s.action.c_str());
        } else {
            dl->AddText(font, os, ImVec2(tx, c.y - os - 2.0f * pop), IM_COL32(200, 200, 205, alpha(255)), s.object.c_str());
            dl->AddText(font, as, ImVec2(tx, c.y), IM_COL32(255, 255, 255, alpha(255)), s.action.c_str());
        }
    }
}

} // namespace GameGui
