#include "Settings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include "Paths.h"

namespace {
std::string settingsFile() { return Paths::file("settings.json").string(); }
}

GraphicsSettings& GraphicsSettings::get() {
    static GraphicsSettings s;
    return s;
}

float GraphicsSettings::renderDistanceStuds() const {
    static const float kStuds[kMaxRenderDistance] = {60, 90, 130, 180, 250, 340, 450, 600, 800, 1e9f};
    return kStuds[std::clamp(renderDistance, 1, kMaxRenderDistance) - 1];
}

void GraphicsSettings::applyPreset(int q) {
    quality = q;
    switch (q) {
        case Low:
            shadowRes = 1024; shadowQuality = 0; ssao = false; bloom = false;
            fxaa = false; postFx = false; renderScale = 0.70f; maxLights = 6; waterQuality = 0; renderDistance = 3;
            break;
        case Medium:
            shadowRes = 1024; shadowQuality = 0; ssao = false; bloom = true;
            fxaa = true; postFx = true; renderScale = 0.85f; maxLights = 12; waterQuality = 1; renderDistance = 5;
            break;
        case High:
            shadowRes = 2048; shadowQuality = 2; ssao = true; bloom = true;
            fxaa = true; postFx = true; renderScale = 1.0f; maxLights = 32; waterQuality = 2; renderDistance = 8;
            break;
        case Ultra:
            shadowRes = 4096; shadowQuality = 2; ssao = true; bloom = true;
            fxaa = true; postFx = true; renderScale = 1.0f; maxLights = 32; waterQuality = 3; renderDistance = kMaxRenderDistance;
            break;
        default: break;
    }
}

void GraphicsSettings::load() {
    std::ifstream f(settingsFile());
#ifdef GB_MOBILE
    if (!f) {
        // First run on a phone: start with the performance preset to avoid
        // overwhelming mobile GPUs and memory with high-resolution effects.
        applyPreset(Low);
        showFps = false;
        checkUpdates = false;
        return;
    }
#endif
    if (!f) return;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (!j.is_object()) return;
    auto rd = [&](const char* k, auto& v) {
        if (j.contains(k)) try { v = j[k].get<std::decay_t<decltype(v)>>(); } catch (...) {}
    };
    rd("vsync", vsync); rd("fpsCap", fpsCap); rd("showFps", showFps);
    rd("quality", quality); rd("shadowRes", shadowRes); rd("shadowQuality", shadowQuality);
    rd("ssao", ssao); rd("bloom", bloom); rd("fxaa", fxaa); rd("postFx", postFx);
    rd("renderScale", renderScale); rd("maxLights", maxLights); rd("allowGore", allowGore); rd("checkUpdates", checkUpdates); rd("seasonTheme", seasonTheme);
    if (j.contains("language") && j["language"].is_string())
        std::snprintf(language, sizeof language, "%s", j["language"].get<std::string>().c_str());
    rd("touchControls", touchControls); rd("touchSize", touchSize);
    rd("shiftLockSwitch", shiftLockSwitch); rd("mouseSensitivity", mouseSensitivity); rd("invertCamera", invertCamera);
    rd("volume", volume); rd("fullscreen", fullscreen);
    rd("voiceChat", voiceChat); rd("voiceOpenMic", voiceOpenMic); rd("voiceVolume", voiceVolume);
    rd("translateChat", translateChat);
    rd("colorBlind", colorBlind); rd("textSize", textSize); rd("captions", captions); rd("clipRecorder", clipRecorder);
    colorBlind = std::clamp(colorBlind, 0, 3);
    textSize = std::clamp(textSize, 1.0f, 2.0f);
    rd("waterQuality", waterQuality); rd("graphicsApi", graphicsApi); rd("renderDistance", renderDistance);
    renderDistance = std::clamp(renderDistance, 1, kMaxRenderDistance);
    waterQuality = std::clamp(waterQuality, 0, 3);
    renderScale = std::clamp(renderScale, 0.5f, 2.0f);
    maxLights = std::clamp(maxLights, 0, 32);
}

const char* const* GraphicsSettings::colorBlindNames() {
    static const char* const names[] = {"Off", "Red-blind (protanopia)", "Green-blind (deuteranopia)", "Blue-blind (tritanopia)"};
    return names;
}

const char* const* GraphicsSettings::apiNames() {
#ifdef GB_GLES
    static const char* const names[] = {"Auto (best for this device)", "OpenGL ES 3.2", "OpenGL ES 3.1",
                                        "OpenGL ES 3.0 (safe mode)"};
#else
    static const char* const names[] = {"Auto (best for this computer)", "OpenGL 4.6 (newest)", "OpenGL 4.3",
                                        "OpenGL 4.1 (safe mode)"};
#endif
    return names;
}

std::string& GraphicsSettings::activeApi() {
    static std::string s;
    return s;
}

bool GraphicsSettings::touchEnabled() const {
    if (touchControls == TouchOn) return true;
    if (touchControls == TouchOff) return false;
#if defined(__ANDROID__) || defined(GB_MOBILE)
    return true;
#else
    return false;
#endif
}

void GraphicsSettings::save() const {
    nlohmann::json j = {
        {"vsync", vsync}, {"fpsCap", fpsCap}, {"showFps", showFps},
        {"quality", quality}, {"shadowRes", shadowRes}, {"shadowQuality", shadowQuality},
        {"ssao", ssao}, {"bloom", bloom}, {"fxaa", fxaa}, {"postFx", postFx},
        {"renderScale", renderScale}, {"maxLights", maxLights}, {"allowGore", allowGore}, {"checkUpdates", checkUpdates}, {"seasonTheme", seasonTheme}, {"language", std::string(language)},
        {"touchControls", touchControls}, {"touchSize", touchSize},
        {"shiftLockSwitch", shiftLockSwitch}, {"mouseSensitivity", mouseSensitivity}, {"invertCamera", invertCamera},
        {"volume", volume}, {"fullscreen", fullscreen},
        {"voiceChat", voiceChat}, {"voiceOpenMic", voiceOpenMic}, {"voiceVolume", voiceVolume}, {"translateChat", translateChat},
        {"colorBlind", colorBlind}, {"textSize", textSize}, {"captions", captions}, {"clipRecorder", clipRecorder},
        {"waterQuality", waterQuality}, {"graphicsApi", graphicsApi}, {"renderDistance", renderDistance},
    };
    std::ofstream f(settingsFile());
    if (f) f << j.dump(2);
}
