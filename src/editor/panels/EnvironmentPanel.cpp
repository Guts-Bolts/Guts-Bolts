#include "EnvironmentPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/Environment.h"

#include <imgui.h>
#include <algorithm>

namespace {
void help(const char* text) {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}
} // namespace

EnvironmentPanel::EnvironmentPanel(Scene* scene) : m_scene(scene) {}

void EnvironmentPanel::render() {
    ImGui::Begin("Lighting");

    Environment& e = m_scene->environment();

    ImGui::TextDisabled("Presets");
    const int weather = e.weather;            // (a preset changes the look, not the weather)
    const float weatherAmount = e.weatherAmount;
    if (ImGui::Button("Day"))      e = EnvironmentPresets::day();
    ImGui::SameLine();
    if (ImGui::Button("Sunset"))   e = EnvironmentPresets::sunset();
    ImGui::SameLine();
    if (ImGui::Button("Night"))    e = EnvironmentPresets::night();
    ImGui::SameLine();
    if (ImGui::Button("Overcast")) e = EnvironmentPresets::overcast();
    ImGui::SameLine();
    if (ImGui::Button("Horror"))   e = EnvironmentPresets::horror();
    e.weather = weather;
    e.weatherAmount = weatherAmount;

    ImGui::Spacing();
    if (ImGui::SliderFloat("Time of Day", &e.clockTime, 0.0f, 24.0f, "%.1f h"))
        EnvironmentPresets::applyTimeOfDay(e, e.clockTime);
    help("Moves the sun and changes the sky colours.\nScripts can change it too: Lighting.ClockTime = 18");
    ImGui::Separator();

    const ImGuiTreeNodeFlags open = ImGuiTreeNodeFlags_DefaultOpen;

    if (ImGui::CollapsingHeader("Sun", open)) {
        ImGui::SliderFloat("Direction", &e.sunAzimuth,   -180.0f, 360.0f, "%.0f\xc2\xb0");
        ImGui::SliderFloat("Height",    &e.sunElevation, -90.0f, 90.0f, "%.0f\xc2\xb0");
        ImGui::ColorEdit3 ("Color##sun",     &e.sunColor.x);
        ImGui::SliderFloat("Brightness##sun", &e.sunIntensity, 0.0f, 6.0f);
        ImGui::SliderFloat("Sun Size",  &e.sunSize, 0.2f, 5.0f);
    }

    if (ImGui::CollapsingHeader("Shadows", open)) {
        ImGui::Checkbox   ("Sun Casts Shadows", &e.shadows);
        ImGui::SliderFloat("Softness",  &e.shadowSoftness, 0.0f, 4.0f);
        help("0 = razor sharp. Higher = softer, especially far from the object casting it.");
        ImGui::SliderFloat("Darkness",  &e.shadowStrength, 0.0f, 1.0f);
        ImGui::SliderFloat("Distance",  &e.shadowDistance, 10.0f, 200.0f, "%.0f");
        help("How far from the camera shadows are drawn. Smaller = sharper shadows.");
    }

    if (ImGui::CollapsingHeader("Ambient Light", open)) {
        ImGui::ColorEdit3 ("Sky Light",    &e.ambientColor.x);
        help("Light coming from the sky onto upward-facing surfaces");
        ImGui::ColorEdit3 ("Ground Bounce", &e.groundAmbient.x);
        help("Light bouncing off the ground onto downward-facing surfaces");
        ImGui::SliderFloat("Brightness##amb", &e.ambientIntensity, 0.0f, 2.0f);
        ImGui::SliderFloat("Reflections", &e.reflections, 0.0f, 2.0f);
        help("How much shiny materials (Metal, Glass, Ice) reflect the sky");
    }

    if (ImGui::CollapsingHeader("Sky", open)) {
        ImGui::Checkbox  ("Show Sky", &e.showSky);
        ImGui::ColorEdit3("Top",      &e.skyZenith.x);
        ImGui::ColorEdit3("Horizon",  &e.skyHorizon.x);
        ImGui::ColorEdit3("Below",    &e.skyGround.x);
        ImGui::SliderFloat("Sky Brightness", &e.skyBrightness, 0.0f, 3.0f);
        ImGui::Checkbox  ("Stars at night", &e.stars);
    }

    if (ImGui::CollapsingHeader("Clouds", open)) {
        ImGui::Checkbox   ("Clouds",  &e.clouds);
        ImGui::SliderFloat("Cover",   &e.cloudCover, 0.0f, 1.0f);
        ImGui::SliderFloat("Speed",   &e.cloudSpeed, 0.0f, 10.0f);
        ImGui::ColorEdit3 ("Cloud Color", &e.cloudColor.x);
    }

    if (ImGui::CollapsingHeader("Fog", open)) {
        ImGui::Checkbox   ("Fog",       &e.fogEnabled);
        ImGui::ColorEdit3 ("Fog Color", &e.fogColor.x);
        ImGui::SliderFloat("Density",   &e.fogDensity, 0.0f, 0.1f, "%.4f");
        ImGui::SliderFloat("Sun Glow",  &e.fogSunGlow, 0.0f, 3.0f);
        help("Fog lights up when you look towards the sun");
    }

    if (ImGui::CollapsingHeader("Weather", open)) {
        int w = std::clamp(e.weather, 0, kWeatherCount - 1);
        const char* shown[kWeatherCount] = {"Clear sky", "Rain", "Snow"};   // (Lighting.Weather: Clear, Rain, Snow)
        if (ImGui::Combo("Weather##kind", &w, shown, kWeatherCount)) e.weather = w;
        help("Rain makes everything wet with puddles, and snow settles on tops. Scripts can change it with Lighting.Weather");
        ImGui::SliderFloat("Intensity##weather", &e.weatherAmount, 0.0f, 1.0f);
        help("From a few drops (0) to a storm (1)");
    }

    if (ImGui::CollapsingHeader("Camera & Effects", open)) {
        ImGui::SliderFloat("Exposure",   &e.exposure, 0.1f, 4.0f);
        help("Overall brightness of the picture");
        ImGui::SliderFloat("Bloom",      &e.bloomIntensity, 0.0f, 3.0f);
        help("Glow around bright things like Neon parts and the sun");
        ImGui::SliderFloat("Bloom Threshold", &e.bloomThreshold, 0.1f, 5.0f);
        ImGui::SliderFloat("Ambient Occlusion", &e.aoIntensity, 0.0f, 2.0f);
        help("Darkens corners and places where objects touch");
        ImGui::SliderFloat("Contrast",   &e.contrast, 0.5f, 2.0f);
        ImGui::SliderFloat("Saturation", &e.saturation, 0.0f, 2.0f);
        ImGui::SliderFloat("Vignette",   &e.vignette, 0.0f, 1.0f);
        help("Darkens the edges of the screen");
        ImGui::ColorEdit3 ("Tint",       &e.tint.x);
    }

    if (ImGui::CollapsingHeader("World")) {
        ImGui::SliderFloat("Gravity", &m_scene->world().gravity, 0.0f, 100.0f);
        ImGui::SliderFloat("Fall Limit", &m_scene->world().fallenPartsHeight, -500.0f, 0.0f, "%.0f");
        help("Players die and parts disappear below this height");
    }

    ImGui::End();
}
