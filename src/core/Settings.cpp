#include "Settings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include "Paths.h"
#include "KeyBinds.h"

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
    if (q == Auto) {   // starts at High and finds its own level while you play
        m_autoClock = 0.0f; m_autoFrames = 0; m_autoGood = 0; m_autoHold = 0.0f; m_autoBad = -1;
        applyAutoLevel(0);
        return;
    }
    quality = q;
    shadows = q != VeryLow;
    switch (q) {
        case VeryLow:
            shadowRes = 1024; shadowQuality = 0; ssao = false; bloom = false;
            fxaa = false; postFx = false; renderScale = 0.5f; maxLights = 2; waterQuality = 0; renderDistance = 2;
            break;
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
    rd("quality", quality); rd("shadows", shadows); rd("shadowRes", shadowRes); rd("shadowQuality", shadowQuality);
    rd("ssao", ssao); rd("bloom", bloom); rd("fxaa", fxaa); rd("postFx", postFx);
    rd("renderScale", renderScale); rd("maxLights", maxLights); rd("allowGore", allowGore); rd("checkUpdates", checkUpdates); rd("seasonTheme", seasonTheme);
    if (j.contains("language") && j["language"].is_string())
        std::snprintf(language, sizeof language, "%s", j["language"].get<std::string>().c_str());
    rd("touchControls", touchControls); rd("touchSize", touchSize);
    rd("shiftLockSwitch", shiftLockSwitch); rd("mouseSensitivity", mouseSensitivity); rd("invertCamera", invertCamera);
    rd("volume", volume); rd("fullscreen", fullscreen);
    rd("voiceChat", voiceChat); rd("voiceOpenMic", voiceOpenMic); rd("voiceVolume", voiceVolume);
    rd("translateChat", translateChat);
    rd("studioBlenderKeys", studioBlenderKeys);
    if (j.contains("keys")) KeyBinds::fromJson(j["keys"], keys);
    rd("breakMinutes", breakMinutes);
    breakMinutes = std::clamp(breakMinutes, 0, 24 * 60);
    rd("colorBlind", colorBlind); rd("textSize", textSize); rd("captions", captions); rd("clipRecorder", clipRecorder);
    colorBlind = std::clamp(colorBlind, 0, 3);
    textSize = std::clamp(textSize, 1.0f, 2.0f);
    rd("waterQuality", waterQuality); rd("graphicsApi", graphicsApi); rd("renderDistance", renderDistance);
    renderDistance = std::clamp(renderDistance, 1, kMaxRenderDistance);
    waterQuality = std::clamp(waterQuality, 0, 3);
    renderScale = std::clamp(renderScale, 0.5f, 2.0f);
    maxLights = std::clamp(maxLights, 0, 32);
    if (quality < Low || quality > VeryLow) quality = High;
    if (quality == Auto) applyPreset(Auto);   // starts again from the top: this computer may be faster today
}

const char* const* GraphicsSettings::breakChoiceNames() {
    static const char* const names[kBreakChoices] = {"Off", "After 30 minutes", "After 45 minutes", "After 1 hour", "After 1.5 hours", "After 2 hours"};
    return names;
}
int GraphicsSettings::breakChoiceMinutes(int choice) {
    static const int minutes[kBreakChoices] = {0, 30, 45, 60, 90, 120};
    return minutes[std::clamp(choice, 0, kBreakChoices - 1)];
}
int GraphicsSettings::breakChoiceOf(int minutes) {
    for (int i = 0; i < kBreakChoices; ++i) if (breakChoiceMinutes(i) == minutes) return i;
    return minutes <= 0 ? 0 : 3;   // (a hand-edited number shows as "1 hour"; it still uses its own)
}

const char* const* GraphicsSettings::qualityNames() {
    static const char* const names[kQualityChoices] = {"Auto", "Very Low", "Low", "Medium", "High", "Ultra", "Custom"};
    return names;
}

int GraphicsSettings::qualityAt(int menuIndex) {
    static const int order[kQualityChoices] = {Auto, VeryLow, Low, Medium, High, Ultra, Custom};
    return order[std::clamp(menuIndex, 0, kQualityChoices - 1)];
}

int GraphicsSettings::menuIndexOf(int q) {
    for (int i = 0; i < kQualityChoices; ++i) if (qualityAt(i) == q) return i;
    return menuIndexOf(High);
}

const char* GraphicsSettings::qualityHelp(int q) {
    switch (q) {
        case Auto:    return "Watches your frame rate while you play and turns the graphics down when the game gets laggy (and back up when it can).";
        case VeryLow: return "For old or slow computers and phones: no shadows, half sharpness, short view distance, the least water.";
        case Low:     return "Fast: simple shadows, a bit less sharp, fewer lights, short view distance, light water.";
        case Medium:  return "A balance of looks and speed.";
        case High:    return "Soft shadows, ambient occlusion, full sharpness and nice water.";
        case Ultra:   return "Everything turned up: sharpest shadows, the most water and an unlimited view. Needs a strong graphics card.";
        default:      return "Your own mix of the settings below.";
    }
}

// Auto's steps, best first. Each is a preset with a few things changed.
void GraphicsSettings::applyAutoLevel(int level) {
    m_autoLevel = std::clamp(level, 0, kAutoLevels - 1);
    switch (m_autoLevel) {
        case 0: applyPreset(High); break;
        case 1: applyPreset(High); renderScale = 0.85f; shadowQuality = 1; break;
        case 2: applyPreset(Medium); break;
        case 3: applyPreset(Medium); renderScale = 0.7f; waterQuality = 0; break;
        case 4: applyPreset(Low); break;
        case 5: applyPreset(Low); renderScale = 0.6f; shadows = false; break;
        default: applyPreset(VeryLow); break;
    }
    quality = Auto;
}

const char* GraphicsSettings::autoLevelName(int level) {
    static const char* const names[kAutoLevels] = {"High", "High (a bit less sharp)", "Medium", "Medium (less sharp, light water)",
                                                   "Low", "Low (no shadows)", "Very Low"};
    return names[std::clamp(level, 0, kAutoLevels - 1)];
}

bool GraphicsSettings::autoAdjust(float dt) {
    if (quality != Auto || dt <= 0.0f || dt > 1.0f) return false;   // (a long hitch, like loading, doesn't count)
    m_autoClock += dt;
    ++m_autoFrames;
    m_autoHold = std::max(0.0f, m_autoHold - dt);
    m_autoBadTime = std::max(0.0f, m_autoBadTime - dt);
    if (m_autoBadTime <= 0.0f) m_autoBad = -1;
    if (m_autoClock < 2.0f) return false;   // judge two seconds at a time
    m_autoFps = m_autoFrames / m_autoClock;
    m_autoClock = 0.0f;
    m_autoFrames = 0;
    // What "smooth" means here: 60 FPS, or the FPS limit if that's lower.
    const float target = !vsync && fpsCap > 0 ? std::min(60.0f, (float)fpsCap) : 60.0f;
    if (m_autoFps < target * 0.8f && m_autoLevel < kAutoLevels - 1) {   // laggy: a step down
        m_autoBad = m_autoLevel;
        m_autoBadTime = 90.0f;
        m_autoGood = 0;
        m_autoHold = 20.0f;
        applyAutoLevel(m_autoLevel + 1);
        return true;
    }
    if (m_autoFps >= target * 0.95f) ++m_autoGood; else m_autoGood = 0;
    // Smooth for 10 seconds: try one step up (unless that step was too slow lately).
    if (m_autoGood >= 5 && m_autoHold <= 0.0f && m_autoLevel > 0 && m_autoLevel - 1 != m_autoBad) {
        m_autoGood = 0;
        m_autoHold = 10.0f;
        applyAutoLevel(m_autoLevel - 1);
        return true;
    }
    return false;
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
        {"quality", quality}, {"shadows", shadows}, {"shadowRes", shadowRes}, {"shadowQuality", shadowQuality},
        {"ssao", ssao}, {"bloom", bloom}, {"fxaa", fxaa}, {"postFx", postFx},
        {"renderScale", renderScale}, {"maxLights", maxLights}, {"allowGore", allowGore}, {"checkUpdates", checkUpdates}, {"seasonTheme", seasonTheme}, {"language", std::string(language)},
        {"touchControls", touchControls}, {"touchSize", touchSize},
        {"shiftLockSwitch", shiftLockSwitch}, {"mouseSensitivity", mouseSensitivity}, {"invertCamera", invertCamera},
        {"volume", volume}, {"fullscreen", fullscreen},
        {"voiceChat", voiceChat}, {"voiceOpenMic", voiceOpenMic}, {"voiceVolume", voiceVolume}, {"translateChat", translateChat},
        {"colorBlind", colorBlind}, {"textSize", textSize}, {"captions", captions}, {"clipRecorder", clipRecorder},
        {"waterQuality", waterQuality}, {"graphicsApi", graphicsApi}, {"renderDistance", renderDistance},
        {"studioBlenderKeys", studioBlenderKeys}, {"breakMinutes", breakMinutes},
    };
    if (nlohmann::json k = KeyBinds::toJson(keys); !k.empty()) j["keys"] = k;
    std::ofstream f(settingsFile());
    if (f) f << j.dump(2);
}
