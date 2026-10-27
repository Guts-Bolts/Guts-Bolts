#pragma once
#include <glm/glm.hpp>
#include <cmath>

// Scene-wide lighting, sky, atmosphere and camera look — the engine's
// equivalent of Roblox's Lighting service (or an Unreal post-process volume).
// Owned by Scene, saved with the game, edited in the Lighting panel.
struct Environment {
    float     clockTime      = 14.0f;   // time of day (hours), see applyTimeOfDay()

    // Sun (single directional light) -----------------------------------------
    float     sunAzimuth     = 150.0f;  // degrees around the horizon
    float     sunElevation   = 55.0f;   // degrees above the horizon
    glm::vec3 sunColor       = {1.00f, 0.95f, 0.86f};
    float     sunIntensity   = 1.6f;
    float     sunSize        = 1.0f;    // size of the sun disc in the sky

    // Shadows -----------------------------------------------------------------
    bool      shadows        = true;    // sun casts shadows
    float     shadowSoftness = 1.0f;    // 0 = razor sharp, 3 = very soft
    float     shadowStrength = 0.85f;   // 0 = invisible, 1 = fully dark
    float     shadowDistance = 45.0f;   // how far from the camera shadows reach

    // Ambient / sky fill light ------------------------------------------------
    glm::vec3 ambientColor      = {0.52f, 0.62f, 0.80f};   // light from the sky
    glm::vec3 groundAmbient     = {0.30f, 0.27f, 0.22f};   // light bouncing off the ground
    float     ambientIntensity  = 0.42f;
    float     reflections       = 1.0f;                    // sky reflections on shiny parts

    // Procedural sky ----------------------------------------------------------
    bool      showSky    = true;
    glm::vec3 skyZenith  = {0.13f, 0.32f, 0.66f};
    glm::vec3 skyHorizon = {0.66f, 0.77f, 0.90f};
    glm::vec3 skyGround  = {0.18f, 0.17f, 0.16f};
    float     skyBrightness = 1.0f;
    bool      clouds     = true;
    float     cloudCover = 0.45f;       // 0 = clear, 1 = overcast
    float     cloudSpeed = 1.0f;
    glm::vec3 cloudColor = {1.0f, 1.0f, 1.0f};
    bool      stars      = true;        // visible when the sky is dark

    // Atmosphere (exponential distance fog) -----------------------------------
    bool      fogEnabled   = true;
    glm::vec3 fogColor     = {0.66f, 0.77f, 0.90f};
    float     fogDensity   = 0.010f;
    float     fogSunGlow   = 0.6f;      // fog glows when looking toward the sun

    // Weather (Lighting.Weather / WeatherIntensity) -----------------------------
    // Rain and snow fall around the camera (each computer draws its own), with
    // grey skies and thicker fog; rain makes things wet with puddles, snow settles
    // on tops. See withWeather() and Weather.h.
    int       weather       = 0;        // kWeatherNames: 0 Clear, 1 Rain, 2 Snow
    float     weatherAmount = 0.6f;     // 0 a few drops .. 1 a storm

    // Camera / post-processing -------------------------------------------------
    float     exposure       = 1.0f;
    float     bloomIntensity = 0.5f;
    float     bloomThreshold = 1.0f;
    float     aoIntensity    = 1.0f;    // ambient occlusion (contact darkening)
    float     contrast       = 1.05f;
    float     saturation     = 1.1f;
    float     vignette       = 0.25f;
    glm::vec3 tint           = {1.0f, 1.0f, 1.0f};

    // Unit vector pointing from a surface toward the sun.
    glm::vec3 sunDirection() const {
        float el = glm::radians(sunElevation);
        float az = glm::radians(sunAzimuth);
        return glm::normalize(glm::vec3(
            std::cos(el) * std::cos(az),
            std::sin(el),
            std::cos(el) * std::sin(az)));
    }
};

enum class Weather { Clear = 0, Rain = 1, Snow = 2 };
inline constexpr int kWeatherCount = 3;
inline const char* const kWeatherNames[kWeatherCount] = {"Clear", "Rain", "Snow"};

// How the sky, sun and fog look with the weather on top (grey clouds, less sun,
// thicker fog). The saved Lighting stays as the game maker set it.
inline Environment withWeather(const Environment& e) {
    if (e.weather <= 0 || e.weather >= kWeatherCount) return e;
    Environment w = e;
    const float a = glm::clamp(e.weatherAmount, 0.0f, 1.0f);
    const bool snow = e.weather == (int)Weather::Snow;
    const glm::vec3 grey = snow ? glm::vec3(0.78f, 0.80f, 0.84f) : glm::vec3(0.50f, 0.53f, 0.58f);
    const float k = 0.25f + 0.5f * a;   // how much of the sky turns grey
    // Keep night dark: grey only as bright as the sky already was.
    const float bright = glm::clamp(glm::length(e.skyHorizon) / glm::length(grey), 0.0f, 1.0f);
    w.skyZenith  = glm::mix(e.skyZenith,  grey * 0.85f * bright, k);
    w.skyHorizon = glm::mix(e.skyHorizon, grey * bright, k);
    w.clouds     = true;
    w.cloudCover = glm::max(e.cloudCover, 0.6f + 0.4f * a);
    w.cloudColor = glm::mix(e.cloudColor, grey * bright, 0.4f + 0.4f * a);
    w.sunIntensity = e.sunIntensity * (1.0f - (snow ? 0.35f : 0.55f) * a);
    w.shadowStrength = e.shadowStrength * (1.0f - 0.5f * a);
    w.fogEnabled = true;
    w.fogDensity = (e.fogEnabled ? e.fogDensity : 0.0f) + (snow ? 0.022f : 0.012f) * a;
    w.fogColor   = glm::mix(e.fogColor, grey * bright, 0.5f * a + 0.2f);
    w.fogSunGlow = e.fogSunGlow * (1.0f - 0.7f * a);
    w.saturation = e.saturation * (1.0f - (snow ? 0.1f : 0.2f) * a);
    return w;
}

// Ready-made atmospheres, like Roblox's lighting presets.
namespace EnvironmentPresets {

inline Environment day() { return Environment{}; }

inline Environment sunset() {
    Environment e;
    e.sunAzimuth = 18.0f;  e.sunElevation = 7.0f;
    e.sunColor = {1.00f, 0.56f, 0.30f}; e.sunIntensity = 1.9f; e.sunSize = 1.6f;
    e.ambientColor = {0.50f, 0.38f, 0.40f}; e.groundAmbient = {0.25f, 0.16f, 0.12f};
    e.ambientIntensity = 0.45f;
    e.skyZenith  = {0.16f, 0.18f, 0.40f};
    e.skyHorizon = {0.97f, 0.55f, 0.30f};
    e.skyGround  = {0.16f, 0.11f, 0.11f};
    e.cloudColor = {1.0f, 0.75f, 0.6f};
    e.fogColor = {0.95f, 0.58f, 0.36f}; e.fogDensity = 0.018f; e.fogSunGlow = 1.0f;
    e.bloomIntensity = 0.7f; e.saturation = 1.2f; e.tint = {1.0f, 0.95f, 0.9f};
    return e;
}

inline Environment night() {
    Environment e;
    e.sunAzimuth = 205.0f; e.sunElevation = 35.0f;
    e.sunColor = {0.55f, 0.65f, 0.95f}; e.sunIntensity = 0.45f; e.sunSize = 0.7f;
    e.ambientColor = {0.16f, 0.21f, 0.38f}; e.groundAmbient = {0.05f, 0.05f, 0.08f};
    e.ambientIntensity = 0.35f; e.reflections = 0.6f;
    e.skyZenith  = {0.01f, 0.015f, 0.05f};
    e.skyHorizon = {0.05f, 0.08f, 0.17f};
    e.skyGround  = {0.02f, 0.02f, 0.04f};
    e.cloudCover = 0.3f; e.cloudColor = {0.35f, 0.4f, 0.55f};
    e.fogColor = {0.05f, 0.08f, 0.16f}; e.fogDensity = 0.02f; e.fogSunGlow = 0.3f;
    e.exposure = 1.25f; e.bloomIntensity = 0.8f; e.saturation = 0.9f;
    e.tint = {0.9f, 0.95f, 1.1f}; e.vignette = 0.4f;
    return e;
}

inline Environment overcast() {
    Environment e;
    e.sunElevation = 70.0f;
    e.sunColor = {0.90f, 0.90f, 0.92f}; e.sunIntensity = 0.7f;
    e.shadowSoftness = 3.0f; e.shadowStrength = 0.5f;
    e.ambientColor = {0.66f, 0.68f, 0.72f}; e.groundAmbient = {0.36f, 0.36f, 0.36f};
    e.ambientIntensity = 0.8f;
    e.skyZenith  = {0.55f, 0.58f, 0.62f};
    e.skyHorizon = {0.73f, 0.75f, 0.77f};
    e.skyGround  = {0.30f, 0.30f, 0.31f};
    e.cloudCover = 0.9f; e.cloudColor = {0.8f, 0.82f, 0.85f};
    e.fogColor = {0.73f, 0.75f, 0.77f}; e.fogDensity = 0.028f; e.fogSunGlow = 0.1f;
    e.saturation = 0.85f; e.contrast = 1.0f;
    return e;
}

// Dark and moody — handy for horror / gore games.
inline Environment horror() {
    Environment e;
    e.sunAzimuth = 120.0f; e.sunElevation = 20.0f;
    e.sunColor = {0.8f, 0.3f, 0.25f}; e.sunIntensity = 0.5f; e.sunSize = 2.0f;
    e.ambientColor = {0.25f, 0.12f, 0.12f}; e.groundAmbient = {0.06f, 0.03f, 0.03f};
    e.ambientIntensity = 0.3f; e.shadowStrength = 1.0f;
    e.skyZenith  = {0.04f, 0.01f, 0.01f};
    e.skyHorizon = {0.25f, 0.06f, 0.05f};
    e.skyGround  = {0.03f, 0.01f, 0.01f};
    e.cloudCover = 0.7f; e.cloudColor = {0.35f, 0.12f, 0.1f};
    e.fogColor = {0.18f, 0.04f, 0.04f}; e.fogDensity = 0.045f; e.fogSunGlow = 0.8f;
    e.exposure = 1.3f; e.bloomIntensity = 0.9f; e.contrast = 1.2f; e.saturation = 0.8f;
    e.vignette = 0.6f; e.tint = {1.1f, 0.9f, 0.85f};
    return e;
}

inline float smoothstep01(float e0, float e1, float x) {
    float t = glm::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Move the sun for a time of day (0-24 hours) and blend the day / sunset /
// night looks to match. Used by the Lighting panel and Lighting.ClockTime.
inline void applyTimeOfDay(Environment& env, float hours) {
    float t = std::fmod(std::fmod(hours, 24.0f) + 24.0f, 24.0f);
    float angle = (t - 6.0f) / 12.0f * 3.14159265f;    // 6am sunrise, noon overhead
    float height = std::sin(angle);

    glm::vec3 dir = glm::normalize(glm::vec3(std::cos(angle), height, 0.35f));
    bool night = height < -0.05f;
    if (night) dir = -dir;                              // show the moon instead

    env.sunElevation = glm::degrees(std::asin(glm::clamp(dir.y, -1.0f, 1.0f)));
    env.sunAzimuth   = glm::degrees(std::atan2(dir.z, dir.x));

    Environment day = EnvironmentPresets::day(), dusk = EnvironmentPresets::sunset(),
                nite = EnvironmentPresets::night();
    float dayK  = smoothstep01(-0.15f, 0.35f, height);
    float duskK = glm::clamp(1.0f - std::abs(height) / 0.35f, 0.0f, 1.0f) * 0.8f;
    auto blend = [&](glm::vec3 Environment::*f) {
        return glm::mix(glm::mix(nite.*f, day.*f, dayK), dusk.*f, duskK);
    };
    env.skyZenith    = blend(&Environment::skyZenith);
    env.skyHorizon   = blend(&Environment::skyHorizon);
    env.skyGround    = blend(&Environment::skyGround);
    env.fogColor     = blend(&Environment::fogColor);
    env.ambientColor = blend(&Environment::ambientColor);
    env.sunColor     = night ? nite.sunColor : glm::mix(dusk.sunColor, day.sunColor, glm::clamp(height * 3.0f, 0.0f, 1.0f));
    env.sunIntensity = night ? nite.sunIntensity : glm::mix(0.4f, day.sunIntensity, dayK);
    env.groundAmbient = blend(&Environment::groundAmbient);
    env.cloudColor    = blend(&Environment::cloudColor);
    env.exposure      = glm::mix(nite.exposure, day.exposure, dayK);
    env.clockTime     = t;
}

} // namespace EnvironmentPresets
