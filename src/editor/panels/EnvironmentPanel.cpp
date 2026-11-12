#include "EnvironmentPanel.h"
#include "../LearnHighlight.h"
#include "../../scene/Scene.h"
#include "../../scene/Environment.h"

#include "../PropGrid.h"
#include <imgui.h>
#include <optional>
#include <algorithm>

namespace {
void help(const char* text) {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}
} // namespace

EnvironmentPanel::EnvironmentPanel(Scene* scene) : m_scene(scene) {}

void EnvironmentPanel::render() {
    ImGui::Begin("Lighting");
    std::optional<PG::Style> grid(std::in_place);   // name on the left, value on the right (PropGrid.h)

    Environment& e = m_scene->environment();

    PG::TextDisabled("Presets");
    const int weather = e.weather;            // (a preset changes the look, not the weather)
    const float weatherAmount = e.weatherAmount;
    if (PG::Button("Day"))      e = EnvironmentPresets::day();
    ImGui::SameLine();
    if (PG::Button("Sunset"))   e = EnvironmentPresets::sunset();
    ImGui::SameLine();
    if (PG::Button("Night"))    e = EnvironmentPresets::night();
    ImGui::SameLine();
    if (PG::Button("Overcast")) e = EnvironmentPresets::overcast();
    ImGui::SameLine();
    if (PG::Button("Horror"))   e = EnvironmentPresets::horror();
    e.weather = weather;
    e.weatherAmount = weatherAmount;

    PG::Spacing();
    if (PG::SliderFloat("Time of Day", &e.clockTime, 0.0f, 24.0f, "%.1f h"))
        EnvironmentPresets::applyTimeOfDay(e, e.clockTime);
    StudioLearnHighlight::mark("environment.Time of Day");
    help("Moves the sun and changes the sky colours.\nScripts can change it too: Lighting.ClockTime = 18");
    PG::Separator();

    const ImGuiTreeNodeFlags open = ImGuiTreeNodeFlags_DefaultOpen;

    if (PG::CollapsingHeader("Sun", open)) {
        PG::SliderFloat("Direction", &e.sunAzimuth,   -180.0f, 360.0f, "%.0f\xc2\xb0");
        PG::SliderFloat("Height",    &e.sunElevation, -90.0f, 90.0f, "%.0f\xc2\xb0");
        ImGui::ColorEdit3 ("Color##sun",     &e.sunColor.x);
        PG::SliderFloat("Brightness##sun", &e.sunIntensity, 0.0f, 6.0f);
        PG::SliderFloat("Sun Size",  &e.sunSize, 0.2f, 5.0f);
    }

    if (PG::CollapsingHeader("Shadows", open)) {
        ImGui::Checkbox   ("Sun Casts Shadows", &e.shadows);
        PG::SliderFloat("Softness",  &e.shadowSoftness, 0.0f, 4.0f);
        help("0 = razor sharp. Higher = softer, especially far from the object casting it.");
        PG::SliderFloat("Darkness",  &e.shadowStrength, 0.0f, 1.0f);
        PG::SliderFloat("Distance",  &e.shadowDistance, 10.0f, 200.0f, "%.0f");
        help("How far from the camera shadows are drawn. Smaller = sharper shadows.");
    }

    if (PG::CollapsingHeader("Ambient Light", open)) {
        ImGui::ColorEdit3 ("Sky Light",    &e.ambientColor.x);
        help("Light coming from the sky onto upward-facing surfaces");
        ImGui::ColorEdit3 ("Ground Bounce", &e.groundAmbient.x);
        help("Light bouncing off the ground onto downward-facing surfaces");
        PG::SliderFloat("Brightness##amb", &e.ambientIntensity, 0.0f, 2.0f);
        PG::SliderFloat("Reflections", &e.reflections, 0.0f, 2.0f);
        help("How much shiny materials (Metal, Glass, Ice) reflect the sky");
        PG::SliderFloat("Bouncy Light", &e.bounceLight, 0.0f, 2.0f);
        PG::SliderFloat("Light Rays", &e.lightRays, 0.0f, 1.0f);
        PG::Checkbox("Eyes Adjust", &e.autoExposure);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto exposure: walk into a dark room and it slowly brightens; step out and it dims.");
        PG::SliderFloat("Motion Blur", &e.motionBlur, 0.0f, 1.0f);
        help("Sun and lamp light bounces off coloured things onto what's next to them (0 = off)");
    }

    if (PG::CollapsingHeader("Sky", open)) {
        ImGui::Checkbox  ("Show Sky", &e.showSky);
        PG::ColorEdit3("Top",      &e.skyZenith.x);
        PG::ColorEdit3("Horizon",  &e.skyHorizon.x);
        PG::ColorEdit3("Below",    &e.skyGround.x);
        PG::SliderFloat("Sky Brightness", &e.skyBrightness, 0.0f, 3.0f);
        ImGui::Checkbox  ("Stars at night", &e.stars);
    }

    if (PG::CollapsingHeader("Clouds", open)) {
        ImGui::Checkbox   ("Clouds##on", &e.clouds);   // "##on": not the same ID as the header
        PG::SliderFloat("Cover",   &e.cloudCover, 0.0f, 1.0f);
        PG::SliderFloat("Speed",   &e.cloudSpeed, 0.0f, 10.0f);
        ImGui::ColorEdit3 ("Cloud Color", &e.cloudColor.x);
    }

    if (PG::CollapsingHeader("Fog", open)) {
        ImGui::Checkbox   ("Fog##on",   &e.fogEnabled);  // "##on": not the same ID as the header
        ImGui::ColorEdit3 ("Fog Color", &e.fogColor.x);
        PG::SliderFloat("Density",   &e.fogDensity, 0.0f, 0.1f, "%.4f");
        PG::SliderFloat("Sun Glow",  &e.fogSunGlow, 0.0f, 3.0f);
        help("Fog lights up when you look towards the sun");
        PG::SliderFloat("Height Falloff", &e.fogFalloff, 0.0f, 0.2f, "%.3f");
        help("Height fog: above 0 the fog lies low (thick near the ground, thin up high), like mist in a valley. 0 = the same everywhere");
        ImGui::DragFloat  ("Fog Height", &e.fogHeight, 0.5f, -500.0f, 2000.0f, "%.1f");
        help("The height (studs) where the fog is as thick as Density; it thins out above it");
        PG::SliderFloat("Wisps", &e.fogWisps, 0.0f, 1.0f, "%.2f");
        help("Wispy mist that drifts with the wind (World > Wind). 0 = smooth fog. Shows on High graphics and up, where the fog also takes light: sun shafts through gaps, lamps glowing in the mist");
    }

    if (PG::CollapsingHeader("Weather", open)) {
        int w = std::clamp(e.weather, 0, kWeatherCount - 1);
        const char* shown[kWeatherCount] = {"Clear sky", "Rain", "Snow", "Thunderstorm"};   // (Lighting.Weather: Clear, Rain, Snow, Storm)
        if (PG::Combo("Weather##kind", &w, shown, kWeatherCount)) e.weather = w;
        help("Rain makes everything wet with puddles, snow settles on tops, and a thunderstorm is heavy rain under a dark sky with lightning and thunder. Scripts can change it with Lighting.Weather");
        PG::SliderFloat("Intensity##weather", &e.weatherAmount, 0.0f, 1.0f);
        help("From a few drops (0) to a storm (1)");
    }

    if (PG::CollapsingHeader("Camera & Effects", open)) {
        PG::SliderFloat("Exposure",   &e.exposure, 0.1f, 4.0f);
        help("Overall brightness of the picture");
        PG::SliderFloat("Bloom",      &e.bloomIntensity, 0.0f, 3.0f);
        help("Glow around bright things like Neon parts and the sun");
        PG::SliderFloat("Bloom Threshold", &e.bloomThreshold, 0.1f, 5.0f);
        PG::SliderFloat("Ambient Occlusion", &e.aoIntensity, 0.0f, 2.0f);
        help("Darkens corners and places where objects touch");
        PG::SliderFloat("Contrast",   &e.contrast, 0.5f, 2.0f);
        PG::SliderFloat("Saturation", &e.saturation, 0.0f, 2.0f);
        PG::SliderFloat("Vignette",   &e.vignette, 0.0f, 1.0f);
        help("Darkens the edges of the screen");
        ImGui::ColorEdit3 ("Tint",       &e.tint.x);
    }

    if (PG::CollapsingHeader("World")) {
        PG::SliderFloat("Gravity", &m_scene->world().gravity, 0.0f, 300.0f);
        PG::SliderFloat("Fall Limit", &m_scene->world().fallenPartsHeight, -500.0f, 0.0f, "%.0f");
        help("Players die and parts disappear below this height");
        ImGui::DragFloat3 ("Wind", &m_scene->world().globalWind.x, 0.2f, -500.0f, 500.0f, "%.1f");
        help("GlobalWind, studs a second: flags and capes flap in it, and light loose parts get blown along");
        PG::SliderFloat("Air Density", &m_scene->world().airDensity, 0.0f, 0.05f, "%.4f");
        help("How thick the air is (0.0012 = real air). Light, wide parts fall slowly and blow about; 0 = no air at all");
    }

    grid.reset();
    ImGui::End();
}
