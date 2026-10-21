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

    // Controls
    enum TouchMode { TouchAuto, TouchOn, TouchOff };
    int   touchControls = TouchAuto;   // on-screen joystick + buttons (Auto = on phones and tablets)
    float touchSize     = 1.0f;        // how big the on-screen controls are
    bool  shiftLockSwitch = true;      // Shift toggles Shift Lock (camera over the shoulder, body faces where you look)
    float mouseSensitivity = 1.0f;     // how fast the camera turns
    bool  invertCamera  = false;       // moving the mouse up looks down

    // Sound and screen
    float volume        = 1.0f;        // everything's loudness, 0..1
    bool  fullscreen    = false;

    // Voice chat (multiplayer games that allow it). Off until you turn it on.
    bool  voiceChat     = false;
    bool  voiceOpenMic  = false;       // false = hold V (or the Talk button) to talk
    float voiceVolume   = 1.0f;        // other people's voices, 0..2
    // Chat said in another language shows in yours (core/Language: the computer's language).
    bool  translateChat = true;
    bool  touchEnabled() const;

    // Graphics
    int   quality       = High;
    int   shadowRes     = 2048; // 1024 / 2048 / 4096
    int   shadowQuality = 2;    // 0 hard, 1 soft, 2 contact-hardening (PCSS)
    bool  ssao          = true; // ambient occlusion
    bool  bloom         = true;
    bool  fxaa          = true; // anti-aliasing
    bool  postFx        = true; // tone mapping, colour grading, vignette
    float renderScale   = 1.0f; // 0.5 = faster, 1.0 = sharp
    int   maxLights     = 32;   // point / spot lights drawn at once
    int   waterQuality  = 2;    // real liquid: 0 low, 1 medium, 2 high, 3 ultra (see waterScale() etc.)
    // How far away things are drawn: 1 (close, fastest) .. 10 (everything). Things
    // further away fade into the sky and aren't drawn, and water, liquid, lights and
    // effects out there stop working until you come closer (less lag).
    int   renderDistance = 8;
    static constexpr int kMaxRenderDistance = 10;
    float renderDistanceStuds() const;   // (a huge number at 10)

    // Which graphics API to start with (takes effect next time the app opens).
    // Computers: Auto = the newest OpenGL the driver has; the others ask for that
    // version (falling back if it can't). OpenGL 4.1 is "safe mode": no compute
    // shaders, so the liquid runs on the processor. Phones: OpenGL ES 3.2 / 3.1 / 3.0.
    enum GraphicsApi { ApiAuto, ApiNewest, ApiMiddle, ApiSafe };
    int   graphicsApi   = ApiAuto;
    static const char* const* apiNames();   // for the settings menu (4 names)
    // What's really running (filled in when the window opens), e.g. "OpenGL 4.6 - NVIDIA GeForce ...".
    static std::string& activeApi();

    // Real liquid at this water quality: how sharp it's drawn (a fraction of the
    // screen), how many smoothing passes, and the most drops at once.
    float waterScale() const  { return waterQuality <= 0 ? 0.5f : waterQuality == 1 ? 0.75f : 1.0f; }
    int   waterBlurPasses() const { return waterQuality <= 0 ? 2 : waterQuality == 1 ? 3 : 4; }
    bool  waterReflections() const { return waterQuality >= 1; }   // mirror the scene (Medium and up)
    int   waterMaxDrops() const { return waterQuality <= 0 ? 25000 : waterQuality == 1 ? 60000 : waterQuality == 2 ? 100000 : 1 << 20; }

    void applyPreset(int q);

    static GraphicsSettings& get();
    void load();
    void save() const;
};
