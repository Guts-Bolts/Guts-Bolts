#include "AppWindow.h"
#include "Pad.h"
#include <cmath>
#include "Settings.h"
#include "Audio.h"
#include "../editor/Theme.h"
#include "../renderer/MeshLibrary.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace { std::vector<std::string> g_dropped; }   // files dropped on the window

std::vector<std::string> AppWindow::takeDroppedFiles() {
    std::vector<std::string> out;
    out.swap(g_dropped);
    return out;
}

AppWindow::AppWindow(const char* title, int width, int height, const char* layoutFile) {
    GraphicsSettings::get().load();

    if (!glfwInit())
        throw std::runtime_error("Failed to initialise GLFW");

    // OpenGL core profile. Settings > Graphics API picks which version to ask for;
    // if the driver can't do it, try the next one down. 4.1 is the newest every
    // desktop OS (including macOS) has, so it's always the last try.
    const int api = GraphicsSettings::get().graphicsApi;
    std::vector<int> versions = api == GraphicsSettings::ApiSafe   ? std::vector<int>{41}
                              : api == GraphicsSettings::ApiMiddle ? std::vector<int>{43, 41}
                                                                   : std::vector<int>{46, 45, 44, 43, 42, 41};
    for (int v : versions) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, v / 10);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, v % 10);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);   // required on macOS
        m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (m_window) break;
    }
    if (!m_window)
        throw std::runtime_error("Failed to create a window. Your graphics driver needs OpenGL 4.1 or newer.");

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

#ifdef _WIN32
    // Use the embedded application icon (resource id 1) for the title bar and
    // taskbar, so it matches the .exe icon shown in Explorer.
    if (HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                                        MAKEINTRESOURCEW(1), IMAGE_ICON,
                                        0, 0, LR_DEFAULTSIZE | LR_SHARED)) {
        HWND hwnd = glfwGetWin32Window(m_window);
        SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
#endif

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
        throw std::runtime_error("Failed to initialise GLEW");
    glGetError();   // GLEW can leave a harmless error behind on core profiles
    {
        const char* ver = (const char*)glGetString(GL_VERSION);
        const char* gpu = (const char*)glGetString(GL_RENDERER);
        GraphicsSettings::activeApi() = std::string("OpenGL ") + (ver ? ver : "?") + " - " + (gpu ? gpu : "?");
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // controllers: menus, and the pad's buttons for games (Pad.h)
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = layoutFile;

    EditorTheme::loadFonts();
    EditorTheme::apply();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    glfwSetDropCallback(m_window, [](GLFWwindow*, int count, const char** paths) {
        for (int i = 0; i < count; ++i) if (paths[i]) g_dropped.push_back(paths[i]);
    });
    ImGui_ImplOpenGL3_Init("#version 410");

    m_lastTime = m_nextFrame = glfwGetTime();
    Audio::init();
}

AppWindow::~AppWindow() {
    Audio::shutdown();
    // GPU resources must be released while the GL context still exists.
    MeshLibrary::clear();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool AppWindow::shouldClose() const { return glfwWindowShouldClose(m_window); }
void AppWindow::close() { glfwSetWindowShouldClose(m_window, GLFW_TRUE); }
void AppWindow::setTitle(const std::string& t) { glfwSetWindowTitle(m_window, t.c_str()); }

namespace {
struct MouseLock {
    bool want = false, active = false;
    double x = 0, y = 0;            // where it's asked to stay
    double homeX = 0, homeY = 0;    // where it really is after being put back there
    float dx = 0, dy = 0;
};
MouseLock g_lock;

// Keep the hidden pointer where it was asked to be, and measure how far it moved.
void updateMouseLock(GLFWwindow* w) {
    const bool want = g_lock.want && glfwGetWindowAttrib(w, GLFW_FOCUSED);
    g_lock.want = false;   // asked for again every frame it's wanted
    g_lock.dx = g_lock.dy = 0.0f;
    if (!want) {
        if (g_lock.active) glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        g_lock.active = false;
        return;
    }
    double cx, cy;
    glfwGetCursorPos(w, &cx, &cy);
    // How far it moved since we put it back: measured from where it really ended up
    // last time, not from where we asked. (The middle of the screen is often half a
    // pixel; the pointer can only sit on whole pixels, so measuring from the asked-for
    // spot read a half-pixel move every frame and the camera slowly drifted.)
    if (g_lock.active) { g_lock.dx = (float)(cx - g_lock.homeX); g_lock.dy = (float)(cy - g_lock.homeY); }
    else glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    glfwSetCursorPos(w, std::floor(g_lock.x), std::floor(g_lock.y));
    glfwGetCursorPos(w, &g_lock.homeX, &g_lock.homeY);
    g_lock.active = true;
}
} // namespace

namespace {
// Fullscreen and volume follow the settings (the in-game menu changes them).
void applyScreenAndSound(GLFWwindow* w) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    static float volume = -1.0f;
    if (gs.volume != volume) { volume = gs.volume; Audio::setMasterVolume(volume); }
    static bool full = false;
    static int wx = 100, wy = 100, ww = 1280, wh = 720;
    if (gs.fullscreen == full) return;
    full = gs.fullscreen;
    if (full) {
        glfwGetWindowPos(w, &wx, &wy);
        glfwGetWindowSize(w, &ww, &wh);
        GLFWmonitor* m = glfwGetPrimaryMonitor();
        if (const GLFWvidmode* mode = m ? glfwGetVideoMode(m) : nullptr)
            glfwSetWindowMonitor(w, m, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(w, nullptr, wx, wy, ww, wh, 0);
    }
}
} // namespace

void  AppWindow::lockMouse(float x, float y) { g_lock.want = true; g_lock.x = x; g_lock.y = y; }
float AppWindow::mouseLookX() { return g_lock.dx; }
float AppWindow::mouseLookY() { return g_lock.dy; }
bool  AppWindow::mouseLocked() { return g_lock.active; }

float AppWindow::beginFrame(const std::function<void()>& beforeImGui) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    int wantVsync = (gs.vsync && !m_fixedDt && !m_headsetPaced) ? 1 : 0;
    if (wantVsync != m_appliedVsync) { glfwSwapInterval(wantVsync); m_appliedVsync = wantVsync; }

    double now = glfwGetTime();
    float dt = m_fixedDt ? 1.0f / 60.0f : (float)(now - m_lastTime);
    m_lastTime = now;

    glfwPollEvents();
    Audio::update();
    updateMouseLock(m_window);
    applyScreenAndSound(m_window);

    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    Pad::injectApply();   // (tests)
    if (beforeImGui) beforeImGui();
    ImGui::NewFrame();
    Pad::update();
    return dt;
}

void AppWindow::endFrame(const std::string& screenshotPath) {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (m_afterUi) m_afterUi();
    if (!screenshotPath.empty()) saveScreenshot(screenshotPath);
    glfwSwapBuffers(m_window);

    // FPS limit (only when VSync is off).
    const GraphicsSettings& gs = GraphicsSettings::get();
    if (!gs.vsync && gs.fpsCap > 0 && !m_fixedDt && !m_headsetPaced) {
        double step = 1.0 / gs.fpsCap;
        m_nextFrame += step;
        double now = glfwGetTime();
        if (m_nextFrame > now) {
            double wait = m_nextFrame - now;
            // Sleep most of the wait, then spin for accuracy.
            if (wait > 0.002)
                std::this_thread::sleep_for(std::chrono::duration<double>(wait - 0.001));
            while (glfwGetTime() < m_nextFrame) {}
        } else if (now - m_nextFrame > step * 4) {
            m_nextFrame = now;   // fell far behind; don't try to catch up
        }
    } else {
        m_nextFrame = glfwGetTime();
    }
}

void AppWindow::injectTouch(long long, float, float, bool) {}   // no touch screen here
void AppWindow::lockLandscape(bool) {}                           // computers don't rotate

void AppWindow::saveScreenshot(const std::string& path) {
    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    std::vector<unsigned char> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y) std::fwrite(&px[(size_t)y * w * 3], 1, (size_t)w * 3, f);
        std::fclose(f);
    }
}
