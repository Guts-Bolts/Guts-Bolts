// The phone / tablet version of AppWindow: SDL2 + OpenGL ES 3 (see AppWindow.cpp
// for the computer version, which uses GLFW + desktop OpenGL).
#include "AppWindow.h"
#include "Pad.h"
#include "Settings.h"
#include "I18n.h"
#include "Audio.h"
#include "Voice.h"
#include "Paths.h"
#include "../player/LaunchLink.h"
#include "../editor/Theme.h"
#include "../renderer/GL.h"
#include "../renderer/MeshLibrary.h"

#include <SDL.h>
#ifdef __ANDROID__
#include <jni.h>
#endif
#ifdef GB_IOS
#include "IosPlatform.h"
#endif
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

double now() { return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }

// How much bigger the UI should be. Phones pack lots of pixels into a small
// screen, so 1 "UI pixel" becomes several real ones. GB_UI_SCALE overrides it
// (handy for trying the phone layout on a computer).
#ifdef __ANDROID__
// Android's own idea of how big things should be (DisplayMetrics.density:
// 1 = 160 dpi, 2.75 on a typical phone). This is what every Android app uses,
// so buttons end up the size people expect. SDL's DPI is the raw panel DPI,
// which some phones report wrongly. 0 = couldn't ask.
float androidDensity() {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return 0.0f;
    float density = 0.0f;
    jclass actCls = env->GetObjectClass(activity);
    jmethodID getRes = env->GetMethodID(actCls, "getResources", "()Landroid/content/res/Resources;");
    jobject res = getRes ? env->CallObjectMethod(activity, getRes) : nullptr;
    if (res && !env->ExceptionCheck()) {
        jclass resCls = env->GetObjectClass(res);
        jmethodID getDm = env->GetMethodID(resCls, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
        jobject dm = getDm ? env->CallObjectMethod(res, getDm) : nullptr;
        if (dm && !env->ExceptionCheck()) {
            jclass dmCls = env->GetObjectClass(dm);
            jfieldID f = env->GetFieldID(dmCls, "density", "F");
            if (f) density = env->GetFloatField(dm, f);
            env->DeleteLocalRef(dmCls);
            env->DeleteLocalRef(dm);
        }
        env->DeleteLocalRef(resCls);
        env->DeleteLocalRef(res);
    }
    if (env->ExceptionCheck()) { env->ExceptionClear(); density = 0.0f; }
    env->DeleteLocalRef(actCls);
    env->DeleteLocalRef(activity);
    return density;
}
#endif

float pickUiScale(SDL_Window* win) {
    if (const char* e = std::getenv("GB_UI_SCALE")) {
        float v = (float)std::atof(e);
        if (v > 0.2f) return v;
    }
#ifdef __ANDROID__
    float s = androidDensity();
    float ddpi = 0.0f;
    if (s <= 0.0f && SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(win), &ddpi, nullptr, nullptr) == 0 && ddpi > 0.0f)
        s = ddpi / 160.0f;
    if (s <= 0.0f) s = 2.0f;
    // Keep at least ~400 UI pixels on the short side, or the site can't fit
    // (a phone on its side is short), and never so few that things are tiny.
    int w, h;
    SDL_GL_GetDrawableSize(win, &w, &h);
    if (w > 0 && h > 0) s = std::min(s, std::min(w, h) / 400.0f);
    return std::clamp(s, 1.0f, 4.0f);
#else
    (void)win;
    return 1.0f;
#endif
}

#ifdef GB_IOS
// ImGui asks this every frame: keep pages, menus and the touch buttons out from
// under the notch, the rounded corners and the home bar.
SDL_Window* g_iosWindow = nullptr;
const float* g_iosUiScale = nullptr;   // AppWindow::m_uiScale (it changes when the phone turns)
ImVec4 iosWorkAreaInsets(ImGuiViewport*) {
    const IosPlatform::SafeArea a = IosPlatform::safeArea(g_iosWindow);
    const float s = g_iosUiScale && *g_iosUiScale > 0.0f ? *g_iosUiScale : 1.0f;   // points -> UI pixels
    return ImVec4(std::max(a.left, 0.0f) / s, std::max(a.top, 0.0f) / s,
                  std::max(a.right, 0.0f) / s, std::max(a.bottom, 0.0f) / s);
}
#endif

} // namespace

AppWindow::AppWindow(const char* title, int width, int height, const char* layoutFile) {
    // The site works either way up; games switch to landscape (lockLandscape).
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");        // Back = Esc (menu / go back)
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");              // taps also click buttons
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
        throw std::runtime_error(std::string("Couldn't start SDL: ") + SDL_GetError());

    Paths::installBundledFiles();          // phones: unpack the built-in games (first run / new version)
    GraphicsSettings::get().load();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);   // (asked for properly below)
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
    width = height = 0;
#elif defined(GB_IOS)
    // The whole screen, with the status bar (clock, battery) hidden.
    flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
    if (const char* e = std::getenv("GB_WINDOW_SIZE")) std::sscanf(e, "%dx%d", &width, &height);
#endif
    m_sdl = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
    if (!m_sdl) throw std::runtime_error(std::string("Couldn't open a window: ") + SDL_GetError());
    // Settings > Graphics API: ask for that OpenGL ES version, falling back to older
    // ones if the phone can't. 3.0 is "safe mode" (no compute shaders: the liquid
    // runs on the processor).
    {
        const int api = GraphicsSettings::get().graphicsApi;
        const int tries[] = {32, 31, 30};
        const int first = api == GraphicsSettings::ApiSafe ? 2 : api == GraphicsSettings::ApiMiddle ? 1 : 0;
        for (int i = first; i < 3 && !m_gl; ++i) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, tries[i] / 10);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, tries[i] % 10);
            m_gl = SDL_GL_CreateContext(m_sdl);
        }
    }
    if (!m_gl) throw std::runtime_error(std::string("This device needs OpenGL ES 3: ") + SDL_GetError());
    SDL_GL_MakeCurrent(m_sdl, (SDL_GLContext)m_gl);
    SDL_GL_SetSwapInterval(1);
    {
        const char* ver = (const char*)glGetString(GL_VERSION);
        const char* gpu = (const char*)glGetString(GL_RENDERER);
        GraphicsSettings::activeApi() = std::string(ver ? ver : "OpenGL ES ?") + " - " + (gpu ? gpu : "?");
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#ifdef __ANDROID__
    m_touchScreen = true;
#else
    m_touchScreen = SDL_GetNumTouchDevices() > 0 || std::getenv("GB_TOUCH_SCREEN") != nullptr;
#endif
    m_uiScale = pickUiScale(m_sdl);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // controllers: menus, and the pad's buttons for games (Pad.h)
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    static std::string ini;
    ini = (Paths::appFolder() / layoutFile).string();
    io.IniFilename = ini.c_str();

    I18n::init();   // the saved language (or the computer's), before any text
    EditorTheme::loadFonts();
    EditorTheme::apply();
    if (m_touchScreen) {
        // Fingers are fatter than mouse pointers: bigger scrollbars, easier to hit.
        ImGuiStyle& st = ImGui::GetStyle();
        st.ScrollbarSize = std::max(st.ScrollbarSize, 18.0f);
        st.TouchExtraPadding = ImVec2(4, 4);
    }

    ImGui_ImplSDL2_InitForOpenGL(m_sdl, m_gl);
    ImGui_ImplOpenGL3_Init("#version 300 es");
#ifdef GB_IOS
    g_iosWindow = m_sdl;
    g_iosUiScale = &m_uiScale;
    ImGui::GetPlatformIO().Platform_GetWindowWorkAreaInsets = iosWorkAreaInsets;
#endif

    m_lastTime = m_nextFrame = now();
    Audio::init();
}

AppWindow::~AppWindow() {
#ifdef GB_IOS
    ImGui::GetPlatformIO().Platform_GetWindowWorkAreaInsets = nullptr;
    g_iosWindow = nullptr;
    g_iosUiScale = nullptr;
#endif
    Audio::shutdown();
    MeshLibrary::clear();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (m_gl) SDL_GL_DeleteContext((SDL_GLContext)m_gl);
    if (m_sdl) SDL_DestroyWindow(m_sdl);
    SDL_Quit();
}

bool AppWindow::shouldClose() const { return m_quit; }
void AppWindow::close() { m_quit = true; }
void AppWindow::setTitle(const std::string& t) { SDL_SetWindowTitle(m_sdl, t.c_str()); }

void AppWindow::lockLandscape(bool on) {
#ifdef GB_QUEST
    (void)on;   // (headsets don't turn)
    return;
#endif
    if (m_landscape == (int)on) return;
    m_landscape = on;
#ifdef __ANDROID__
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetStaticMethodID(cls, "setGameOrientation", "(Z)V");
    if (m) env->CallStaticVoidMethod(cls, m, (jboolean)on);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
#elif defined(GB_IOS)
    IosPlatform::setLandscape(m_sdl, on);
#endif
}

void AppWindow::injectTouch(long long id, float x, float y, bool down) {
    SDL_Event e{};
    bool known = std::any_of(m_touches.begin(), m_touches.end(), [&](const TouchPoint& t) { return t.id == id; });
    e.type = !down ? SDL_FINGERUP : known ? SDL_FINGERMOTION : SDL_FINGERDOWN;
    e.tfinger.touchId = 1;
    e.tfinger.fingerId = (SDL_FingerID)id;
    e.tfinger.x = x;
    e.tfinger.y = y;
    e.tfinger.pressure = 1.0f;
    e.tfinger.windowID = SDL_GetWindowID(m_sdl);
    SDL_PushEvent(&e);

    // A real touch screen also moves the "mouse" with the first finger (SDL
    // does that for real touches, but not for pushed ones), so copy it here.
    static long long mouseFinger = -1;
    if (mouseFinger != -1 && mouseFinger != id) return;
    int w, h;
    SDL_GetWindowSize(m_sdl, &w, &h);
    SDL_Event m{};
    int mx = (int)(x * w), my = (int)(y * h);
    if (!known && down) {
        mouseFinger = id;
        m.type = SDL_MOUSEMOTION;
        m.motion.which = SDL_TOUCH_MOUSEID; m.motion.windowID = e.tfinger.windowID; m.motion.x = mx; m.motion.y = my;
        SDL_PushEvent(&m);
        m = SDL_Event{};
        m.type = SDL_MOUSEBUTTONDOWN;
    } else if (!down) {
        mouseFinger = -1;
        m.type = SDL_MOUSEBUTTONUP;
    } else {
        m.type = SDL_MOUSEMOTION;
        m.motion.which = SDL_TOUCH_MOUSEID; m.motion.windowID = e.tfinger.windowID; m.motion.x = mx; m.motion.y = my;
        SDL_PushEvent(&m);
        return;
    }
    m.button.which = SDL_TOUCH_MOUSEID;
    m.button.windowID = e.tfinger.windowID;
    m.button.button = SDL_BUTTON_LEFT;
    m.button.state = m.type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    m.button.clicks = 1;
    m.button.x = mx; m.button.y = my;
    SDL_PushEvent(&m);
}

// Phones have no mouse to lock (dragging a finger looks around instead).
void  AppWindow::lockMouse(float, float) {}
float AppWindow::mouseLookX() { return 0.0f; }
float AppWindow::mouseLookY() { return 0.0f; }
bool  AppWindow::mouseLocked() { return false; }
std::vector<std::string> AppWindow::takeDroppedFiles() { return {}; }   // nothing to drag files from on a phone

float AppWindow::beginFrame(const std::function<void()>& beforeImGui) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    int wantVsync = (gs.vsync && !m_fixedDt && !m_headsetPaced) ? 1 : 0;
    if (wantVsync != m_appliedVsync) { SDL_GL_SetSwapInterval(wantVsync); m_appliedVsync = wantVsync; }

    double t = now();
    float dt = m_fixedDt ? 1.0f / 60.0f : (float)std::min(t - m_lastTime, 0.25);
    m_lastTime = t;

    ImGuiIO& io = ImGui::GetIO();
    if (m_backPressed > 0 && --m_backPressed == 0) io.AddKeyEvent(ImGuiKey_Escape, false);

    const float s = m_uiScale;
    int winW, winH;
    SDL_GetWindowSize(m_sdl, &winW, &winH);
    SDL_Event e;
    // In the background the app must not draw (iOS closes apps that use the GPU there), so
    // it sleeps here, waiting for events, until it's back on screen.
    while (!m_quit && (m_background ? SDL_WaitEvent(&e) : SDL_PollEvent(&e))) {
        switch (e.type) {
        case SDL_QUIT:
        case SDL_APP_TERMINATING:
            m_quit = true;
            break;
        case SDL_APP_WILLENTERBACKGROUND:
            m_background = true;
            Voice::setActive(false); // release the microphone before iOS suspends the process
            Audio::suspend();         // mute without overwriting the user's volume
            m_touches.clear();        // don't leave a virtual joystick/camera drag held across suspension
            io.AddMouseButtonEvent(0, false);
            break;
        case SDL_APP_DIDENTERFOREGROUND: {
            m_background = false;
            // Rebind the existing context after an iOS interruption and refresh
            // drawable dimensions before rendering the next frame. iOS normally
            // preserves the context, but making it current explicitly avoids
            // relying on whichever context SDL left current after resume.
            if (m_gl && SDL_GL_MakeCurrent(m_sdl, (SDL_GLContext)m_gl) == 0) {
                int drawableW = 0, drawableH = 0;
                SDL_GL_GetDrawableSize(m_sdl, &drawableW, &drawableH);
                glViewport(0, 0, drawableW, drawableH);
                glEnable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                m_uiScale = pickUiScale(m_sdl);
            }
            Audio::resume();
            break;
        }
        case SDL_WINDOWEVENT:
            if (e.window.windowID == SDL_GetWindowID(m_sdl)) {
                if (e.window.event == SDL_WINDOWEVENT_CLOSE) {
                    m_quit = true;
                } else if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                           e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    // iOS can rotate between portrait (site) and landscape (games).
                    // Recompute the UI scale so controls remain usable after rotation,
                    // and reset the viewport to the drawable's current pixel dimensions.
                    m_uiScale = pickUiScale(m_sdl);
                    int drawableW = 0, drawableH = 0;
                    SDL_GL_GetDrawableSize(m_sdl, &drawableW, &drawableH);
                    glViewport(0, 0, drawableW, drawableH);
                }
            }
            break;
#ifdef GB_IOS
        case SDL_DROPFILE:
            // SDL's iOS application delegate forwards URLs opened while the app
            // is already running as drop-file events. Route them through the
            // same validated launch-link flow used at startup.
            if (e.drop.file) {
                LaunchLink::enqueue(e.drop.file);
                SDL_free(e.drop.file);
                e.drop.file = nullptr;
            }
            break;
#endif
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_AC_BACK) {           // Android back button / gesture
                io.AddKeyEvent(ImGuiKey_Escape, true);
                m_backPressed = 2;                            // release it a frame later
                continue;
            }
            break;
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION: {
            m_touchScreen = true;
            TouchPoint p{(long long)e.tfinger.fingerId, e.tfinger.x * winW / s, e.tfinger.y * winH / s};
            auto it = std::find_if(m_touches.begin(), m_touches.end(), [&](const TouchPoint& q) { return q.id == p.id; });
            if (it != m_touches.end()) *it = p;
            else if (e.type == SDL_FINGERDOWN) m_touches.push_back(p);
            break;
        }
        case SDL_FINGERUP:
            m_touches.erase(std::remove_if(m_touches.begin(), m_touches.end(),
                            [&](const TouchPoint& q) { return q.id == (long long)e.tfinger.fingerId; }),
                            m_touches.end());
            break;
        // The UI works in "UI pixels": scale the mouse (and taps, which SDL
        // also sends as mouse clicks) down to match.
        case SDL_MOUSEMOTION:
            e.motion.x = (Sint32)(e.motion.x / s);
            e.motion.y = (Sint32)(e.motion.y / s);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            e.button.x = (Sint32)(e.button.x / s);
            e.button.y = (Sint32)(e.button.y / s);
            break;
        default:
            break;
        }
        ImGui_ImplSDL2_ProcessEvent(&e);
    }
    Audio::update();

    int fbW, fbH;
    SDL_GL_GetDrawableSize(m_sdl, &fbW, &fbH);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW, fbH);
    glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    // Lay out in UI pixels, draw with every real pixel (sharp text on phones).
    io.DisplaySize = ImVec2(winW / s, winH / s);
    if (io.DisplaySize.x > 0 && io.DisplaySize.y > 0)
        io.DisplayFramebufferScale = ImVec2(fbW / io.DisplaySize.x, fbH / io.DisplaySize.y);
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
    SDL_GL_SwapWindow(m_sdl);

    const GraphicsSettings& gs = GraphicsSettings::get();
    if (!gs.vsync && gs.fpsCap > 0 && !m_fixedDt && !m_headsetPaced) {
        double step = 1.0 / gs.fpsCap;
        m_nextFrame += step;
        double t = now();
        if (m_nextFrame > t) SDL_Delay((Uint32)((m_nextFrame - t) * 1000.0));
        else if (t - m_nextFrame > step * 4) m_nextFrame = t;
    } else {
        m_nextFrame = now();
    }
}

void AppWindow::saveScreenshot(const std::string& path) {
    int w, h;
    SDL_GL_GetDrawableSize(m_sdl, &w, &h);
    std::vector<unsigned char> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());   // ES only guarantees RGBA
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y)
            for (int x = 0; x < w; ++x) std::fwrite(&px[((size_t)y * w + x) * 4], 1, 3, f);
        std::fclose(f);
    }
}
