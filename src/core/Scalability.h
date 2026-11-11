#pragma once
#include <string>

// Scalability: the graphics split into groups (shadows, lighting, reflections...),
// each with its own level from 0 (Low) to 3 (Epic). A quality preset is just one
// level for every group; Custom lets you pick each group yourself.
//
// The rule every group follows: each level is a cheaper version of the one above
// that still looks right, and the look of the game (colours, tone, fog) is the
// same on every level. Low has simple ambient shading instead of bounced light,
// FXAA instead of the temporal upscaler, one shadow map instead of several...
//
// On top of the levels sits the device profile: some computers and phones can't
// do the top levels (or do them badly), so the profile caps a group for that
// device ("at most Medium shadows on this graphics chip"), whatever the menu says.
namespace Scalability {

enum Group {
    Shadow,            // shadow map size, how many shadow maps (cascades), how soft
    GlobalIllumination,// light bouncing off things: ambient occlusion only .. screen-traced bounce light
    Reflection,        // sky only .. water reflections and shiny reflections
    PostProcess,       // bloom, sun rays, depth of field, motion blur
    Texture,           // texture sharpness at an angle (anisotropic filtering) and mip bias
    ViewDistance,      // how far you can see
    AntiAliasing,      // FXAA .. temporal anti-aliasing .. temporal upscaling (TSR)
    ScreenPercentage,  // how many pixels are really drawn before upscaling
    kGroups
};
// 0 Low, 1 Medium, 2 High, 3 Epic (the best that runs smoothly in a game), 4 Cinematic:
// not for playing. It takes the shortcuts out (draws 1.5x the pixels and shrinks them,
// many more rays, keeps far more of the past frames) for screenshots, photo mode and trailers.
constexpr int kLevels = 5;
constexpr int kCinematic = 4;

const char* groupName(int group);   // "Shadows", "Lighting"...
const char* groupHelp(int group);   // one plain sentence
const char* levelName(int level);   // "Low", "Medium", "High", "Epic", "Cinematic"
// What one group does at one level, in a few plain words (for the settings menu).
const char* levelHelp(int group, int level);

// A device profile: which graphics chip (or platform) it matches, and the highest
// level each group may use on it (4 = no limit).
struct DeviceProfile {
    std::string name = "Any computer";   // shown in the settings menu
    int cap[kGroups] = {4, 4, 4, 4, 4, 4, 4, 4};
    // Settings > Graphics: the extra caps a person (or a tester) set for this computer
    // only; merged in by GraphicsSettings.
};

// Which kinds of device the Player can run on (this changes the default profile).
enum Platform { Desktop, Phone, Web };
Platform thisPlatform();

// The built-in profile for a graphics chip, from its name (OpenGL's GL_RENDERER, like
// "Intel(R) UHD Graphics 620" or "Apple A15 GPU") and the platform. Software drivers
// and old integrated chips get tighter caps; phones and the web Player get the cheap
// versions of the costliest things.
DeviceProfile profileFor(const std::string& gpuName, Platform platform);

// The quick benchmark at first start: how many milliseconds a few test frames took at
// High, and the frame-time budget (16.7 ms for 60 FPS). Each level costs roughly half
// of the one above it, so this picks the highest that fits: 2 High, 1 Medium, 0 Low,
// -1 Very Low (the old-computer preset under Low).
int pickLevel(float highMs, float budgetMs);

// The halton sequence (base 2 and 3), for the temporal upscaler's sub-pixel jitter:
// point `index` in 0..1, well spread out however many you take.
float halton(int index, int base);

} // namespace Scalability
