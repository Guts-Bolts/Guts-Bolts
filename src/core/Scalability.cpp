#include "Scalability.h"
#include <algorithm>
#include <cctype>

namespace Scalability {

const char* groupName(int g) {
    static const char* const names[kGroups] = {"Shadows", "Lighting", "Reflections", "Effects",
                                               "Textures", "View distance", "Anti-aliasing", "Resolution"};
    return names[std::clamp(g, 0, kGroups - 1)];
}

const char* groupHelp(int g) {
    switch (g) {
        case Shadow:             return "How sharp and soft shadows are, and how many shadow maps are used.";
        case GlobalIllumination: return "Light bouncing off walls and floors. Low only darkens corners; higher levels trace the bounced light across the screen.";
        case Reflection:         return "Reflections in water and shiny surfaces.";
        case PostProcess:        return "Bloom, sun rays, depth of field and motion blur.";
        case Texture:            return "How sharp pictures on parts stay when you look at them at an angle.";
        case ViewDistance:       return "How far away things are still drawn.";
        case AntiAliasing:       return "Smooths jagged edges. Higher levels use several frames together (temporal), which also lets the game draw fewer pixels and still look sharp.";
        default:                 return "How many pixels are really drawn. The temporal upscaler fills in the rest, so this is the biggest speed-up.";
    }
}

const char* levelName(int l) {
    static const char* const names[kLevels] = {"Low", "Medium", "High", "Epic"};
    return names[std::clamp(l, 0, kLevels - 1)];
}

const char* levelHelp(int g, int l) {
    static const char* const help[kGroups][kLevels] = {
        {"one 1024 shadow map, hard edges", "1024 maps, near + far, soft edges", "2048 maps, soft edges that get blurrier away from what casts them", "4096 maps, the softest, most lamp shadows"},
        {"ambient occlusion only (dark corners)", "screen-traced bounce light, quarter size", "screen-traced bounce light, half size, more rays", "the most rays and steps, smoothest"},
        {"sky only", "water reflections", "water reflections, more detail", "the most detailed water"},
        {"no bloom or extra effects (colours stay the same)", "bloom", "bloom, sun rays, depth of field, motion blur", "everything, more samples"},
        {"slightly blurry far away, fastest", "2x sharp at an angle", "8x sharp at an angle", "16x sharp at an angle"},
        {"short", "medium", "far", "unlimited"},
        {"FXAA (one frame)", "temporal anti-aliasing", "temporal upscaling (TSR) with sharpening", "TSR, smoothest"},
        {"60% of the pixels", "75% of the pixels", "every pixel", "every pixel"},
    };
    return help[std::clamp(g, 0, kGroups - 1)][std::clamp(l, 0, kLevels - 1)];
}

Platform thisPlatform() {
#if defined(__EMSCRIPTEN__)
    return Web;
#elif defined(GB_MOBILE) || defined(GB_IOS) || defined(__ANDROID__)
    return Phone;
#else
    return Desktop;
#endif
}

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
void capAll(DeviceProfile& p, int g0, int g1, int g2, int g3, int g4, int g5, int g6, int g7) {
    const int c[kGroups] = {g0, g1, g2, g3, g4, g5, g6, g7};
    for (int i = 0; i < kGroups; ++i) p.cap[i] = std::min(p.cap[i], c[i]);
}
} // namespace

DeviceProfile profileFor(const std::string& gpuName, Platform platform) {
    DeviceProfile p;
    const std::string g = lower(gpuName);
    //                       Shadow GI Refl Post Tex View AA Res
    if (platform == Web) {
        // The web Player (WebGL 2): bounce light and the temporal upscaler are untested in
        // Safari, so it keeps the cheap, safe versions until they've been tried there.
        p.name = "Web Player";
        capAll(p,               1,   0,  1,   1,   2,  2,   0,  3);
    } else if (platform == Phone) {
        p.name = "Phone or tablet";
        capAll(p,               1,   0,  1,   1,   2,  2,   0,  3);
    }
    if (has(g, "intel") && !has(g, "uhd") && !has(g, "iris") && !has(g, "arc") &&
        (has(g, "hd graphics") || has(g, "gma"))) {
        // Older Intel chips built into the processor: shadows and bounce light are
        // the slow parts there, so those stay on the cheaper versions.
        p.name = "Older Intel graphics";
        capAll(p,               1,   1,  2,   2,   2,  2,   3,  3);
    } else if (has(g, "mali-4") || has(g, "mali-t") || has(g, "adreno (tm) 3") || has(g, "adreno (tm) 4") ||
               has(g, "powervr sgx") || has(g, "powervr rogue g6")) {
        p.name = "Older phone graphics";
        capAll(p,               0,   0,  0,   0,   1,  1,   0,  1);
    }
    return p;
}

int pickLevel(float highMs, float budgetMs) {
    if (highMs <= 0.0f || budgetMs <= 0.0f) return 2;
    // Leave a little room for the game itself (scripts, physics, sounds).
    const float room = budgetMs * 0.8f;
    if (highMs <= room) return 2;            // High fits
    if (highMs * 0.5f <= room) return 1;     // Medium (about half the cost)
    if (highMs * 0.25f <= room) return 0;    // Low (about a quarter)
    return -1;                               // Very Low
}

float halton(int index, int base) {
    float f = 1.0f, r = 0.0f;
    int i = index + 1;   // (0 would give 0 for every base)
    while (i > 0) {
        f /= (float)base;
        r += f * (float)(i % base);
        i /= base;
    }
    return r;
}

} // namespace Scalability
