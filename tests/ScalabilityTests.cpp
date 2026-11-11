// Scalability (core/Scalability.h): quality groups Low..Epic, presets, device caps and
// the startup benchmark's pick.
//
//   ScalabilityTests
#include "core/Settings.h"
#include "core/Scalability.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
} // namespace

int main() {
    using namespace Scalability;
    GraphicsSettings s;

    // Presets are one level for every group.
    s.applyPreset(GraphicsSettings::Low);
    bool allLow = true;
    for (int g = 0; g < kGroups; ++g) allLow = allLow && s.groupLevel(g) == 0;
    check(allLow, "Low: every group at Low");
    check(s.giQuality == 0 && s.aaQuality == 0, "Low: ambient occlusion only and FXAA (the cheap fallbacks)");
    check(s.postFx, "Low keeps tone mapping and colour grading (same look on every level)");
    s.applyPreset(GraphicsSettings::High);
    check(s.giQuality == 2 && s.aaQuality == 2 && s.renderScale == 1.0f, "High: bounce light, TSR, every pixel");
    s.applyPreset(GraphicsSettings::Ultra);
    check(s.shadowRes == 4096 && s.giQuality == 3 && s.aaQuality == 3, "Epic: biggest shadows, most bounce light, TSR Epic");
    check(std::string(GraphicsSettings::qualityNames()[GraphicsSettings::menuIndexOf(GraphicsSettings::Ultra)]) == "Epic",
          "the top preset is called Epic in the menu");
    s.applyPreset(GraphicsSettings::Cinematic);
    check(s.renderScale > 1.0f && s.giQuality == 4 && s.aaQuality == 4 && s.quality == GraphicsSettings::Cinematic,
          "Cinematic: more pixels than the screen, the most rays, the longest memory");
    check(std::string(GraphicsSettings::qualityNames()[GraphicsSettings::menuIndexOf(GraphicsSettings::Cinematic)]) == "Cinematic",
          "Cinematic is in the menu");
    s.applyPreset(GraphicsSettings::Medium);
    check(s.renderScale < 1.0f && s.aaQuality >= 1, "Medium: fewer pixels, filled in by the temporal upscaler");
    s.applyPreset(GraphicsSettings::VeryLow);
    check(!s.shadows && s.giQuality == -1 && s.aaQuality == -1 && s.postFx, "Very Low: nothing extra, but still the same colours");

    // One group on its own: Custom, the others stay.
    s.applyPreset(GraphicsSettings::High);
    s.setGroup(Shadow, 0);
    check(s.quality == GraphicsSettings::Custom && s.shadowRes == 1024 && s.giQuality == 2, "changing one group makes it Custom and leaves the rest");

    // Device caps: a group can't go over what the device allows.
    GraphicsSettings phone;
    phone.setDevice("Mali-T880", Desktop);
    phone.applyPreset(GraphicsSettings::Ultra);
    check(phone.groupLevel(Shadow) == 0 && phone.aaQuality == 0 && phone.giQuality == 0, "old phone graphics: capped to the cheap versions even on Epic");
    GraphicsSettings web;
    web.setDevice("WebKit WebGL", Web);
    web.applyPreset(GraphicsSettings::High);
    check(web.aaQuality == 0 && web.giQuality == 0 && web.device().name == "Web Player", "web Player: FXAA and ambient occlusion until tested in Safari");
    GraphicsSettings pc;
    pc.setDevice("NVIDIA GeForce RTX 3060", Desktop);
    pc.applyPreset(GraphicsSettings::Ultra);
    check(pc.groupLevel(GlobalIllumination) == 3, "a normal graphics card: no caps");
    pc.applyPreset(GraphicsSettings::Cinematic);
    check(pc.groupLevel(ScreenPercentage) == kCinematic, "a normal graphics card can use Cinematic");
    web.applyPreset(GraphicsSettings::Cinematic);
    check(web.renderScale <= 1.0f && web.giQuality == 0, "the web Player never draws Cinematic (capped)");
    pc.myCaps[Shadow] = 1;
    pc.applyPreset(GraphicsSettings::Ultra);
    check(pc.groupLevel(Shadow) == 1, "your own cap for this computer counts too");
    GraphicsSettings uhd;
    uhd.setDevice("Mesa Intel(R) UHD Graphics 620 (KBL GT2)", Desktop);
    check(uhd.groupCap(Shadow) == kCinematic, "newer Intel UHD isn't treated as an old Intel chip");

    // The benchmark's pick: each level costs about half the one above.
    const float budget = 1000.0f / 60.0f;
    check(isSoftwareGpu("llvmpipe (LLVM 20.1.2, 256 bits)") && isSoftwareGpu("Google SwiftShader") && !isSoftwareGpu("NVIDIA GeForce RTX 4070"), "software graphics spotted");
    check(pickLevel(8.0f, budget) == 2, "8 ms at High: High");
    check(pickLevel(20.0f, budget) == 1, "20 ms at High: Medium");
    check(pickLevel(45.0f, budget) == 0, "45 ms at High: Low");
    check(pickLevel(200.0f, budget) == -1, "200 ms at High: Very Low");
    GraphicsSettings a;
    a.applyPreset(GraphicsSettings::Auto);
    a.setBenchmark("Slow chip", 45.0f, budget);
    check(a.quality == GraphicsSettings::Auto && a.autoLevel() == 4, "Auto starts on Low after a slow benchmark");
    check(!a.needsBenchmark("Slow chip") && a.needsBenchmark("New chip"), "the benchmark runs again only for a different chip");

    // The jitter: well spread between -0.5 and 0.5, never the same twice in a row.
    float sx = 0.0f;
    bool spread = true;
    for (int i = 0; i < 16; ++i) {
        const float x = halton(i, 2) - 0.5f, y = halton(i, 3) - 0.5f;
        spread = spread && std::fabs(x) < 0.5f && std::fabs(y) < 0.5f;
        if (i > 0) spread = spread && (x != halton(i - 1, 2) - 0.5f);
        sx += x;
    }
    check(spread && std::fabs(sx / 16.0f) < 0.1f, "the upscaler's jitter is spread evenly inside a pixel");

    std::cout << (failures ? "FAILED: " : "all passed") << (failures ? std::to_string(failures) : "") << "\n";
    return failures ? 1 : 0;
}
