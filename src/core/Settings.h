#pragma once
#include <string>
#include "Scalability.h"

// Per-computer settings (not saved with games): performance and graphics
// quality. Stored in settings.json next to the program.
struct GraphicsSettings {
    // (The numbers are saved in settings.json, so new ones go on the end.)
    enum Quality { Low, Medium, High, Ultra, Custom, Auto, VeryLow, Cinematic };   // (Ultra shows as "Epic")
    // The Quality menu, in the order it shows: Auto, Very Low, Low, Medium, High, Epic, Cinematic, Custom.
    static constexpr int kQualityChoices = 8;
    static const char* const* qualityNames();      // kQualityChoices names, in menu order
    static int qualityAt(int menuIndex);           // menu position -> Quality
    static int menuIndexOf(int quality);           // Quality -> menu position
    static const char* qualityHelp(int quality);   // one plain sentence about it

    // Display
    bool  vsync       = true;   // match the monitor's refresh rate (no tearing)
    int   fpsCap      = 0;      // 0 = unlimited (only used when VSync is off)
    bool  showFps     = true;

    // Content
    bool  allowGore   = true;   // false hides blood / gore in every game
    bool  seasonTheme = true;   // the Player dresses up for the season (Gutstober in October)
    bool  checkUpdates = true;  // look for a newer version on GitHub at startup
    int   breakMinutes = 0;     // take-a-break reminder after this many minutes of playing (0 = off)
    static constexpr int kBreakChoices = 6;        // Off, 30 min, 45 min, 1 hour, 1.5 hours, 2 hours
    static const char* const* breakChoiceNames();
    static int breakChoiceMinutes(int choice);
    static int breakChoiceOf(int minutes);          // the menu position for a saved number
    char  language[16] = "";

    // Controls
    enum TouchMode { TouchAuto, TouchOn, TouchOff };
    int   touchControls = TouchAuto;
    float touchSize     = 1.0f;
    bool  shiftLockSwitch = true;
    float mouseSensitivity = 1.0f;
    bool  invertCamera  = false;
    bool  studioSimpleLayout = true;   // Studio: the everyday buttons and panels only (VIEW > Advanced shows them all)
    bool  studioBlenderKeys = false;   // Studio: S scales, A picks all, Shift+D / Shift+A... (WASD flies only while right-dragging)
    // Your own keys (KeyBinds.h): one per KeyBinds::Action, 0 = the usual key.
    static constexpr int kKeyActions = 9;
    int   keys[kKeyActions] = {};

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
    int   quality       = Auto;   // (Auto starts on High's settings, below)
    bool  shadows       = true;   // false: no shadows at all (the biggest single saving on old computers)
    int   shadowRes     = 2048;
    int   shadowQuality = 2;
    // Light bouncing off things (Scalability::GlobalIllumination): -1 none, 0 ambient
    // occlusion only (dark corners), 1..3 screen-traced bounce light (more rays each level).
    int   giQuality     = 2;   // (4 = Cinematic: full size, the most rays)
    bool  bloom         = true;
    // Smoothing jagged edges: -1 off, 0 FXAA, 1 temporal anti-aliasing (TAA), 2 temporal
    // upscaling with sharpening (TSR), 3 TSR that keeps more of the past frames.
    int   aaQuality     = 2;   // (4 = Cinematic: keeps the most frames)
    int   postQuality   = 2;   // effects: 0 none (bloom off), 1 bloom, 2 + sun rays, depth of field, motion blur, 3 + more samples
    int   textureQuality = 2;  // 0 blurrier far away .. 3 16x anisotropic filtering
    bool  postFx        = true;   // tone mapping and colour grading (the game's look: on in every preset)
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
    // Shiny parts (metal, glass, ice, wet floors...) mirror the world around them, not just
    // the sky: screen-space reflections, from High up (0 = sky only). Each level walks the
    // reflected ray further: High 20 steps, Epic 32, Cinematic 48.
    int   shinyReflections() const { int l = groupLevel(Scalability::Reflection); return l >= 2 ? l - 1 : 0; }
    // Volumetric fog: fog that takes light (sun shafts, glowing lamps) and drifts in wisps,
    // from High up (0 = the simple fog). High 20 steps a ray, Epic 32, Cinematic 56 at full size.
    int   volumetricFog() const { int l = groupLevel(Scalability::PostProcess); return l >= 2 ? l - 1 : 0; }
    int   waterMaxDrops() const { return waterQuality <= 0 ? 5000 : waterQuality == 1 ? 20000 : waterQuality == 2 ? 60000 : 150000; }

    void applyPreset(int q);

    // Scalability (Scalability.h): each group's level, 0 Low .. 3 Epic. A preset sets them
    // all the same; setGroup() changes one (and makes the quality Custom). applyGroups()
    // turns the levels into the settings above, capped by this device's profile.
    int   sg[Scalability::kGroups] = {2, 2, 2, 2, 2, 2, 2, 2};
    void  setGroup(int group, int level);
    void  applyGroups();
    int   groupLevel(int group) const;   // the level really used (after the device's cap)
    int   groupCap(int group) const;     // the highest level this device allows
    // This device's profile, from the graphics chip's name (called once the window is
    // open). Re-applies the current preset so its caps take effect.
    void  setDevice(const std::string& gpuName, Scalability::Platform platform);
    const Scalability::DeviceProfile& device() const { return m_device; }
    // Your own caps for this computer (saved), on top of the built-in profile: 4 = no limit.
    int   myCaps[Scalability::kGroups] = {4, 4, 4, 4, 4, 4, 4, 4};

    // The quick benchmark (PlayerApp runs it once per graphics chip, on Auto): which chip,
    // how long a test frame took at High, and the level it picked (Scalability::pickLevel).
    std::string benchGpu;
    float benchMs = 0.0f;
    int   benchLevel = 2;
    bool  needsBenchmark(const std::string& gpuName) const { return quality == Auto && benchGpu != gpuName; }
    void  setBenchmark(const std::string& gpuName, float highMs, float budgetMs);

    // Auto quality: while you play, it watches the frame rate. Too slow for a few
    // seconds and it steps the graphics down (sharpness, water, shadows...); smooth
    // for a while and it tries one step up again. Only while quality is Auto.
    // Call once a frame with how long the frame took; true when it changed something.
    bool autoAdjust(float dt);
    int  autoLevel() const { return m_autoLevel; }
    // Low, Very Low, or Auto down at those: skip the extras (depth of field...).
    bool lowEnd() const { return quality == Low || quality == VeryLow || (quality == Auto && m_autoLevel >= 4); }
    static constexpr int kAutoLevels = 7;          // 0 = best (High) .. 6 = lowest
    static const char* autoLevelName(int level);
    float autoFps() const { return m_autoFps; }    // the frame rate it measured last
private:
    void applyAutoLevel(int level);
    void applyLevels(int level);                    // every group at `level` (no quality change)
    Scalability::DeviceProfile m_device;
    int   m_autoStart = 0;                          // where Auto starts (from the benchmark)
    int   m_autoLevel = 0;
    float m_autoClock = 0.0f, m_autoFps = 0.0f;
    int   m_autoFrames = 0, m_autoGood = 0;
    float m_autoHold = 0.0f;                        // seconds before it may step up again
    int   m_autoBad = -1;                           // a level that was too slow (not retried for a while)
    float m_autoBadTime = 0.0f;
public:

    static GraphicsSettings& get();
    void load();
    void save() const;
    // Studio's phone test swaps in a phone's settings for a while: they mustn't be saved.
    static void holdSaving(bool on);
};
