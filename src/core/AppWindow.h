#pragma once
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;
struct SDL_Window;

// A finger on a touch screen, in the same pixels ImGui uses.
struct TouchPoint {
    long long id;
    float     x, y;
};

// Window + OpenGL + Dear ImGui setup and the per-frame loop helpers, shared by
// the editor and the Player app. Also applies the VSync / FPS-limit settings.
class AppWindow {
public:
    AppWindow(const char* title, int width, int height, const char* layoutFile);
    ~AppWindow();

#ifndef GB_MOBILE
    GLFWwindow* handle() const { return m_window; }
#endif
    bool shouldClose() const;
    void close();
    void setTitle(const std::string& title);

    // Start a frame: poll input, start ImGui. `beforeImGui` runs right before
    // ImGui::NewFrame (used to inject test input). Returns seconds since last frame.
    float beginFrame(const std::function<void()>& beforeImGui = nullptr);
    // Finish a frame: draw ImGui, show it, and wait if an FPS limit is set.
    // `screenshotPath` (optional) saves this frame as an image first.
    void endFrame(const std::string& screenshotPath = {});

    void setFixedTimestep(bool on) { m_fixedDt = on; }   // deterministic test runs
    // A VR headset sets the pace (it waits for its own screens): no VSync or FPS cap on top.
    void setHeadsetPaced(bool on) { m_headsetPaced = on; }
    // Runs in endFrame once the menus and buttons are drawn, before the window
    // shows them (the VR headset draws them again on its floating screen).
    void setAfterUi(std::function<void()> f) { m_afterUi = std::move(f); }

    // Touch screens (phones / tablets): the fingers that are down right now.
    const std::vector<TouchPoint>& touches() const { return m_touches; }
    bool  hasTouchScreen() const { return m_touchScreen; }
    float uiScale() const { return m_uiScale; }         // bigger UI on small, dense screens
    // Tests: pretend a finger went down / moved / lifted (x, y from 0 to 1).
    void  injectTouch(long long id, float x, float y, bool down);
    // Phones: true = landscape only (games), false = follow the phone (the site).
    void  lockLandscape(bool on);
    void saveScreenshot(const std::string& path);         // binary PPM

    // First-person mouse look (like Roblox): the pointer is hidden and kept at
    // (x, y) in the window, and how far the mouse moved each frame is handed
    // back instead. Ask for it every frame you want it; stop asking to let go.
    static void  lockMouse(float x, float y);
    static float mouseLookX();   // pixels moved since last frame while locked
    static float mouseLookY();
    static bool  mouseLocked();

    // Files dragged from the computer and let go over the window, since the last
    // call (full paths). The mouse is where they were dropped.
    static std::vector<std::string> takeDroppedFiles();

private:
#ifdef GB_MOBILE
    SDL_Window* m_sdl = nullptr;
    void*       m_gl = nullptr;
    bool        m_quit = false;
    int         m_backPressed = 0;
    int         m_landscape = -1;
#else
    GLFWwindow* m_window = nullptr;
#endif
    std::vector<TouchPoint> m_touches;
    bool        m_touchScreen = false;
    float       m_uiScale = 1.0f;
    double      m_lastTime = 0.0;
    double      m_nextFrame = 0.0;
    int         m_appliedVsync = -1;
    bool        m_fixedDt = false;
    bool        m_headsetPaced = false;
    std::function<void()> m_afterUi;
};
