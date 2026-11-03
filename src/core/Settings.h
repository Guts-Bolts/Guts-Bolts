#pragma once
#include <string>

// Per-computer settings (not saved with games): performance and graphics
// quality. Stored in settings.json next to the program.
struct GraphicsSettings {
    enum Quality { Low, Medium, High, Ultra, Custom };

    // Display
    bool  vsync       = true;   // match the monitor's refresh rate (no tearing)
    int   fpsCap      = 0;      // 0 = unlimited (only used when VSync is off)
    bool  showFps     = true;

    // Content
    bool  allowGore   = true;   // false hides blood / gore in every game
    bool  seasonTheme = true;   // the Player dresses up for the season (Gutstober in October)
    bool  checkUpdates = true;  // look for a newer version on GitHub at startup
    char  language[16] = "";

    // Controls
    enum TouchMode { TouchAuto, TouchOn, TouchOff };
    int   touchControls = TouchAuto;
    float touchSize     = 1.0f;
    bool  shiftLockSwitch = true;
    float mouseSensitivity = 1.0f;
    bool  invertCamera  = false;
    bool  studioBlenderKeys = false;   // Studio: S scales, A picks all, Shift+D / Shift+A... (WASD flies only while right-dragging)

    // Accessibility
    enum ColorBlind { SeeNormal, Protanopia, Deuteranopia, Tritanopia };
    int   colorBlind    = SeeNormal;   // shift colours so reds / greens / blues are easier to tell apart
    float textSize      = 1.0f;        // everything written on the screen, bigger (1 = normal, up to 2)
    bool  captions      = false;       // show what you hear as words at the bottom of the screen

    // Clip recorder: keep the last 30 seconds of play so F3 can save them (about 40 MB of memory)
#if defined(GB_MOBILE) || defined(__EMSCRIPTEN__)
    bool  clipRecorder  = false;
#else
    bool  clipRecorder  = true;
#endif
    static const char* const* colorBlindNames();   // 4 names, for a menu

    // Sound and screen
    float volume        = 1.0f;
    bool  fullscreen    = false;

    // Voice chat
    bool  voiceChat     = false;
    bool  voiceOpenMic  = false;
    float voiceVolume   = 1.0f;
    bool  translateChat = true;
    bool  touchEnabled() const;

    // Graphics
    int   quality       = High;
    int   shadowRes     = 2048;
    int   shadowQuality = 2;
    bool  ssao          = true;
    bool  bloom         = true;
    bool  fxaa          = true;
    bool  postFx        = true;
    float renderScale   = 1.0f;
    int   maxLights     = 32;
    int   waterQuality  = 2;
    int   renderDistance = 8;
    static constexpr int kMaxRenderDistance = 10;
    float renderDistanceStuds() const;

    enum GraphicsApi { ApiAuto, ApiNewest, ApiMiddle, ApiSafe };
    int   graphicsApi   = ApiAuto;
    static const char* const* apiNames();
    static std::string& activeApi();

    // Water budgets are deliberately conservative at lower quality levels.
    // Particle count is a simulation cost as well as a rendering cost.
    float waterScale() const  { return waterQuality <= 0 ? 0.5f : waterQuality == 1 ? 0.75f : 1.0f; }
    int   waterBlurPasses() const { return waterQuality <= 0 ? 1 : waterQuality == 1 ? 2 : waterQuality == 2 ? 3 : 4; }
    bool  waterReflections() const { return waterQuality >= 1; }
    int   waterMaxDrops() const { return waterQuality <= 0 ? 8000 : waterQuality == 1 ? 20000 : waterQuality == 2 ? 60000 : 150000; }

    void applyPreset(int q);

    static GraphicsSettings& get();
    void load();
    void save() const;
};
