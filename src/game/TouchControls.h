#pragma once
#include <imgui.h>
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>

// On-screen controls for phones and tablets, laid out like Roblox mobile:
//   - a thumbstick on the left (it appears wherever your thumb lands),
//   - a big jump button on the right,
//   - Chat and Menu buttons along the top,
//   - drag anywhere else to turn the camera, pinch to zoom, tap to click.
// Fingers come in through feed(). On a computer, feedMouse() turns the mouse
// into one finger so you can try the controls without a phone.
class TouchControls {
public:
    struct Finger { int id; ImVec2 pos; bool down; };

    // Call once per frame, before feeding fingers: the game view's rectangle.
    void begin(ImVec2 min, ImVec2 max, float scale);
    // allowNew = false: fingers already down keep working, new ones are ignored
    // (e.g. while a menu is open).
    void feed(const std::vector<Finger>& fingers, bool allowNew = true);
    // Areas where a new finger isn't a control (the chat box, ...).
    void setBlocked(std::vector<std::pair<ImVec2, ImVec2>> rects) { m_blocked = std::move(rects); }
    void feedMouse(bool allowed);
    void draw(ImDrawList* dl) const;

    // Results for this frame.
    glm::vec2 move() const { return m_move; }        // x = right, y = forward, length 0..1
    bool      jump() const { return m_jump; }
    ImVec2    look() const { return m_look; }        // pixels dragged
    float     zoom() const { return m_zoom; }        // + = closer (like the mouse wheel)
    bool      tapped(ImVec2& where) const { where = m_tapPos; return m_tap; }
    bool      chatPressed() const { return m_chat; }
    bool      menuPressed() const { return m_menu; }
    // Voice chat's Talk button (next to Chat): tap to start talking, tap again to stop.
    void      setMic(bool shown, bool on) { m_micShown = shown; m_micOn = on; }
    bool      micPressed() const { return m_mic; }
    // True while a finger is on the controls (so the game ignores that mouse press).
    bool      busy() const { return !m_touches.empty(); }

    // Scripts' action buttons (ContextActionService:BindAction with createTouchButton):
    // round buttons stacked around the jump button, or where SetPosition put them
    // (a UDim2 on the game view). Set them before feed() each frame.
    struct ActionButton {
        std::string name, title, image;
        bool  hasPosition = false;
        float xScale = 0, xOffset = 0, yScale = 0, yOffset = 0;
    };
    void setActionButtons(std::vector<ActionButton> b) { m_actions = std::move(b); }
    // What happened this frame. Buttons: pressed / let go. Fingers on the game view
    // (UserInputType Touch, for scripts): state 0 = Begin, 1 = Change, 2 = End.
    struct ButtonEvent { std::string name; bool down; ImVec2 pos; };
    struct FingerEvent { int id; int state; ImVec2 pos; };
    const std::vector<ButtonEvent>& actionEvents() const { return m_actionEvents; }
    const std::vector<FingerEvent>& fingerEvents() const { return m_fingerEvents; }
    // A script's action sank this finger: it doesn't turn the camera or tap.
    void      sinkFinger(int id);
    // Where action button i goes (its centre and radius), for drawing and tests.
    void      actionButtonPlace(size_t i, ImVec2& center, float& radius) const;

private:
    enum class Role { Stick, Jump, Look, Chat, Menu, Mic, Action };
    struct Touch {
        int    id;
        Role   role;
        std::string action;   // Role::Action: which button
        bool   sunk = false;  // a script's action took this finger
        ImVec2 start, pos, last;
        float  age = 0.0f;
        float  travel = 0.0f;
    };

    ImVec2 jumpCenter() const;
    float  jumpRadius() const;
    bool   inStickZone(ImVec2 p) const;
    void   buttonRects(ImVec2& chatA, ImVec2& chatB, ImVec2& menuA, ImVec2& menuB) const;
    void   micRect(ImVec2& a, ImVec2& b) const;

    ImVec2 m_min{0, 0}, m_max{0, 0};
    std::vector<std::pair<ImVec2, ImVec2>> m_blocked;
    float  m_scale = 1.0f;
    float  m_dt = 0.0f;
    std::vector<Touch> m_touches;
    ImVec2 m_stickCenter{0, 0};
    ImVec2 stickHome() const;
    bool   m_stickActive = false;
    float  m_pinchDist = 0.0f;

    glm::vec2 m_move{0.0f};
    bool      m_jump = false;
    ImVec2    m_look{0, 0};
    float     m_zoom = 0.0f;
    bool      m_tap = false;
    ImVec2    m_tapPos{0, 0};
    bool      m_chat = false, m_menu = false, m_mic = false;
    bool      m_micShown = false, m_micOn = false;
    std::vector<ActionButton> m_actions;
    std::vector<ButtonEvent>  m_actionEvents;
    std::vector<FingerEvent>  m_fingerEvents;
};
