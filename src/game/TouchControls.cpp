#include "TouchControls.h"

#include <algorithm>
#include <cmath>

namespace {
float dist(ImVec2 a, ImVec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
bool inRect(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.y >= a.y && p.x <= b.x && p.y <= b.y; }
} // namespace

void TouchControls::begin(ImVec2 min, ImVec2 max, float scale) {
    m_min = min;
    m_max = max;
    m_scale = std::clamp(scale, 0.5f, 2.0f);
    m_dt = ImGui::GetIO().DeltaTime;
    m_look = ImVec2(0, 0);
    m_zoom = 0.0f;
    m_tap = m_chat = m_menu = m_mic = m_emote = false;
}

// The thumbstick stays put in the bottom-left corner (it doesn't jump to your thumb).
ImVec2 TouchControls::stickHome() const { return ImVec2(m_min.x + 120 * m_scale, m_max.y - 120 * m_scale); }

ImVec2 TouchControls::jumpCenter() const { return ImVec2(m_max.x - 105 * m_scale, m_max.y - 105 * m_scale); }
float  TouchControls::jumpRadius() const { return 48 * m_scale; }

bool TouchControls::inStickZone(ImVec2 p) const {
    // On (or near) the stick: a thumb that lands a bit off it still grabs it.
    ImVec2 h = stickHome();
    float dx = p.x - h.x, dy = p.y - h.y;
    float r = 62 * m_scale * 2.0f;
    return dx * dx + dy * dy < r * r;
}

void TouchControls::buttonRects(ImVec2& chatA, ImVec2& chatB, ImVec2& menuA, ImVec2& menuB) const {
    float s = 46 * m_scale;
    menuA = ImVec2(m_min.x + 12, m_min.y + 12);
    menuB = ImVec2(menuA.x + s, menuA.y + s);
    chatA = ImVec2(menuB.x + 10, menuA.y);
    chatB = ImVec2(chatA.x + s, chatA.y + s);
}

void TouchControls::micRect(ImVec2& a, ImVec2& b) const {
    ImVec2 chatA, chatB, menuA, menuB;
    buttonRects(chatA, chatB, menuA, menuB);
    a = ImVec2(chatB.x + 10, chatA.y);
    b = ImVec2(a.x + (chatB.x - chatA.x), chatB.y);
}

void TouchControls::emoteRect(ImVec2& a, ImVec2& b) const {
    const float s = 46 * m_scale;
    const ImVec2 j = jumpCenter();
    a = ImVec2(j.x - s * 0.5f, j.y - jumpRadius() - 18 * m_scale - s);
    b = ImVec2(a.x + s, a.y + s);
}

void TouchControls::feed(const std::vector<Finger>& fingers, bool allowNew) {
    ImVec2 chatA, chatB, menuA, menuB, micA, micB, emoteA, emoteB;
    buttonRects(chatA, chatB, menuA, menuB);
    micRect(micA, micB);
    emoteRect(emoteA, emoteB);

    // Fingers that lifted (or vanished).
    for (auto it = m_touches.begin(); it != m_touches.end();) {
        const Finger* f = nullptr;
        for (const Finger& g : fingers) if (g.id == it->id && g.down) f = &g;
        if (f) { ++it; continue; }
        switch (it->role) {
            case Role::Look:
                if (it->age < 0.3f && it->travel < 12.0f) { m_tap = true; m_tapPos = it->pos; }
                break;
            case Role::Chat: if (inRect(it->pos, chatA, chatB)) m_chat = true; break;
            case Role::Menu: if (inRect(it->pos, menuA, menuB)) m_menu = true; break;
            case Role::Mic:  if (inRect(it->pos, micA, micB)) m_mic = true; break;
            case Role::Emote: if (inRect(it->pos, emoteA, emoteB)) m_emote = true; break;
            case Role::Stick: m_stickActive = false; break;
            default: break;
        }
        it = m_touches.erase(it);
    }

    // Fingers that are down: move the ones we know, give new ones a job.
    for (const Finger& f : fingers) {
        if (!f.down) continue;
        auto it = std::find_if(m_touches.begin(), m_touches.end(), [&](const Touch& t) { return t.id == f.id; });
        if (it != m_touches.end()) {
            it->travel += dist(it->pos, f.pos);
            it->pos = f.pos;
            it->age += m_dt;
            continue;
        }
        if (!allowNew || !inRect(f.pos, m_min, m_max)) continue;
        bool blocked = false;
        for (const auto& [a, b] : m_blocked) if (inRect(f.pos, a, b)) blocked = true;
        if (blocked) continue;
        Touch t;
        t.id = f.id;
        t.start = t.pos = t.last = f.pos;
        if (inRect(f.pos, menuA, menuB))                           t.role = Role::Menu;
        else if (inRect(f.pos, chatA, chatB))                      t.role = Role::Chat;
        else if (m_micShown && inRect(f.pos, micA, micB))          t.role = Role::Mic;
        else if (m_emoteShown && inRect(f.pos, emoteA, emoteB))    t.role = Role::Emote;
        else if (dist(f.pos, jumpCenter()) < jumpRadius() * 1.25f) t.role = Role::Jump;
        else if (!m_stickActive && inStickZone(f.pos)) {
            t.role = Role::Stick;
            m_stickActive = true;
            m_stickCenter = stickHome();   // always the same place, wherever your thumb lands
        } else                                                     t.role = Role::Look;
        m_touches.push_back(t);
    }

    // Work out what the fingers mean.
    m_move = glm::vec2(0.0f);
    m_jump = false;
    const float stickR = 62 * m_scale;
    std::vector<Touch*> looks;
    for (Touch& t : m_touches) {
        switch (t.role) {
            case Role::Stick: {
                glm::vec2 d(t.pos.x - m_stickCenter.x, t.pos.y - m_stickCenter.y);
                float len = glm::length(d);
                if (len > stickR) {   // past the edge: full speed that way (the stick stays where it is)
                    d *= stickR / len;
                    len = stickR;
                }
                float k = len / stickR;
                if (k > 0.12f) m_move = glm::vec2(d.x, -d.y) / stickR;
                break;
            }
            case Role::Jump: m_jump = true; break;
            case Role::Look: looks.push_back(&t); break;
            default: break;
        }
    }
    if (looks.size() == 1) {
        m_look = ImVec2(looks[0]->pos.x - looks[0]->last.x, looks[0]->pos.y - looks[0]->last.y);
        m_pinchDist = 0.0f;
    } else if (looks.size() >= 2) {
        float d = dist(looks[0]->pos, looks[1]->pos);
        if (m_pinchDist > 0.0f) m_zoom = (d - m_pinchDist) / 40.0f;
        m_pinchDist = d;
    } else {
        m_pinchDist = 0.0f;
    }
    for (Touch& t : m_touches) t.last = t.pos;
}

void TouchControls::feedMouse(bool allowed) {
    std::vector<Finger> f;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) f.push_back({0, ImGui::GetMousePos(), true});
    feed(f, allowed);
}

void TouchControls::draw(ImDrawList* dl) const {
    const float s = m_scale;
    // Thumbstick: faint at its resting place, solid while held.
    ImVec2 base = stickHome();
    ImVec2 knob = base;
    for (const Touch& t : m_touches)
        if (t.role == Role::Stick) {
            float r = 62 * s;
            glm::vec2 d(t.pos.x - base.x, t.pos.y - base.y);
            if (glm::length(d) > r) d *= r / glm::length(d);
            knob = ImVec2(base.x + d.x, base.y + d.y);
        }
    int a = m_stickActive ? 150 : 70;
    dl->AddCircleFilled(base, 62 * s, IM_COL32(0, 0, 0, a / 2), 40);
    dl->AddCircle(base, 62 * s, IM_COL32(255, 255, 255, a), 40, 3.0f);
    dl->AddCircleFilled(knob, 28 * s, IM_COL32(255, 255, 255, a + 40), 32);

    // Jump button with an up arrow.
    ImVec2 j = jumpCenter();
    float jr = jumpRadius();
    dl->AddCircleFilled(j, jr, m_jump ? IM_COL32(255, 255, 255, 120) : IM_COL32(0, 0, 0, 90), 40);
    dl->AddCircle(j, jr, IM_COL32(255, 255, 255, 200), 40, 3.0f);
    dl->AddTriangleFilled(ImVec2(j.x, j.y - jr * 0.45f), ImVec2(j.x - jr * 0.38f, j.y + jr * 0.05f),
                          ImVec2(j.x + jr * 0.38f, j.y + jr * 0.05f), IM_COL32(255, 255, 255, 230));
    dl->AddRectFilled(ImVec2(j.x - jr * 0.14f, j.y), ImVec2(j.x + jr * 0.14f, j.y + jr * 0.42f),
                      IM_COL32(255, 255, 255, 230));

    // Menu (three lines) and Chat (speech bubble) buttons.
    ImVec2 chatA, chatB, menuA, menuB;
    buttonRects(chatA, chatB, menuA, menuB);
    auto held = [&](Role r) {
        for (const Touch& t : m_touches) if (t.role == r) return true;
        return false;
    };
    dl->AddRectFilled(menuA, menuB, held(Role::Menu) ? IM_COL32(255, 255, 255, 110) : IM_COL32(0, 0, 0, 120), 8);
    float w = menuB.x - menuA.x;
    for (int i = 0; i < 3; ++i) {
        float y = menuA.y + w * (0.32f + 0.18f * i);
        dl->AddLine(ImVec2(menuA.x + w * 0.25f, y), ImVec2(menuB.x - w * 0.25f, y), IM_COL32(255, 255, 255, 235), 3.0f * s);
    }
    dl->AddRectFilled(chatA, chatB, held(Role::Chat) ? IM_COL32(255, 255, 255, 110) : IM_COL32(0, 0, 0, 120), 8);
    ImVec2 b0(chatA.x + w * 0.2f, chatA.y + w * 0.22f), b1(chatB.x - w * 0.2f, chatB.y - w * 0.34f);
    dl->AddRectFilled(b0, b1, IM_COL32(255, 255, 255, 235), 5 * s);
    dl->AddTriangleFilled(ImVec2(b0.x + w * 0.12f, b1.y), ImVec2(b0.x + w * 0.3f, b1.y),
                          ImVec2(b0.x + w * 0.1f, b1.y + w * 0.14f), IM_COL32(255, 255, 255, 235));
    if (m_emoteShown) {   // the emote menu's button: a smiley
        ImVec2 ea, eb;
        emoteRect(ea, eb);
        dl->AddRectFilled(ea, eb, held(Role::Emote) ? IM_COL32(255, 255, 255, 110) : IM_COL32(0, 0, 0, 120), 8);
        const ImVec2 c((ea.x + eb.x) * 0.5f, (ea.y + eb.y) * 0.5f);
        const float fr = w * 0.3f;
        const ImU32 col = IM_COL32(255, 255, 255, 235);
        dl->AddCircle(c, fr, col, 24, 2.2f * s);
        dl->AddCircleFilled(ImVec2(c.x - fr * 0.36f, c.y - fr * 0.25f), fr * 0.13f, col, 10);
        dl->AddCircleFilled(ImVec2(c.x + fr * 0.36f, c.y - fr * 0.25f), fr * 0.13f, col, 10);
        dl->PathArcTo(ImVec2(c.x, c.y + fr * 0.05f), fr * 0.55f, 0.35f, 2.79f, 12);
        dl->PathStroke(col, 0, 2.2f * s);
    }
    if (m_micShown) {   // voice chat's Talk button: a microphone, green while you're talking
        ImVec2 micA, micB;
        micRect(micA, micB);
        dl->AddRectFilled(micA, micB, held(Role::Mic) ? IM_COL32(255, 255, 255, 110)
                                      : m_micOn ? IM_COL32(40, 160, 70, 200) : IM_COL32(0, 0, 0, 120), 8);
        const ImVec2 c((micA.x + micB.x) * 0.5f, (micA.y + micB.y) * 0.5f);
        const ImU32 col = IM_COL32(255, 255, 255, 235);
        dl->AddRectFilled(ImVec2(c.x - w * 0.09f, c.y - w * 0.28f), ImVec2(c.x + w * 0.09f, c.y + w * 0.06f), col, w * 0.09f);
        dl->PathArcTo(ImVec2(c.x, c.y - w * 0.04f), w * 0.17f, 0.0f, 3.14159f, 12);
        dl->PathStroke(col, 0, 2.2f * s);
        dl->AddLine(ImVec2(c.x, c.y + w * 0.13f), ImVec2(c.x, c.y + w * 0.25f), col, 2.2f * s);
        dl->AddLine(ImVec2(c.x - w * 0.1f, c.y + w * 0.25f), ImVec2(c.x + w * 0.1f, c.y + w * 0.25f), col, 2.2f * s);
    }
}
