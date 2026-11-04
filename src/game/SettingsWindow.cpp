#include <cstdio>
#include <algorithm>
#include "SettingsWindow.h"
#include "../core/Settings.h"
#include "../core/UpdateChecker.h"
#include "../core/I18n.h"

#include <imgui.h>
#include <cstring>

namespace SettingsWindow {

namespace {
void help(const char* text) {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}
} // namespace

void languagePicker(float width) {
    const auto& langs = I18n::languages();
    const std::string saved = GraphicsSettings::get().language;
    auto nameOf = [&](const std::string& code) {
        for (const auto& l : langs) if (l.code == code) return l.name;
        return code;
    };
    // "Same as this computer" is translated; the languages show their own names.
    std::string preview = saved.empty() ? std::string(I18n::tr("Same as this computer")) + " (" + nameOf(I18n::current()) + ")"
                                        : nameOf(saved);
    if (width > 0) ImGui::SetNextItemWidth(width);
    I18n::Pause keep;
    if (ImGui::BeginCombo("##language", preview.c_str())) {
        std::string pick = "-";
        if (ImGui::Selectable(I18n::tr("Same as this computer"), saved.empty())) pick = "";
        for (const auto& l : langs)
            if (ImGui::Selectable(l.name.c_str(), saved == l.code)) pick = l.code;
        ImGui::EndCombo();
        if (pick != "-") I18n::setLanguage(pick);
    }
}

void draw(bool* open) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(std::min(420.0f, ImGui::GetIO().DisplaySize.x - 24.0f), 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    // Never taller than the screen (phones on their side): it scrolls instead.
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(screen.x - 16.0f, screen.y - 16.0f));
    if (!ImGui::Begin("Settings", open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    GraphicsSettings& s = GraphicsSettings::get();
    GraphicsSettings before = s;

    ImGui::SeparatorText("Language");
    languagePicker(-1.0f);
    help("The language for the menus and buttons in the Player and Studio. The words inside games are up to each game.");

    ImGui::SeparatorText("Frame rate");
    ImGui::Checkbox("VSync", &s.vsync);
    help("Locks the frame rate to your monitor (smooth, no tearing).\nTurn off to unlock the FPS.");
    ImGui::BeginDisabled(s.vsync);
    const char* caps[] = {"Unlimited", "30", "60", "120", "144", "165", "240"};
    const int   capValues[] = {0, 30, 60, 120, 144, 165, 240};
    int capIdx = 0;
    for (int i = 0; i < IM_ARRAYSIZE(capValues); ++i) if (capValues[i] == s.fpsCap) capIdx = i;
    if (ImGui::Combo("FPS Limit", &capIdx, caps, IM_ARRAYSIZE(caps))) s.fpsCap = capValues[capIdx];
    ImGui::EndDisabled();
    ImGui::Checkbox("Show FPS", &s.showFps);
    ImGui::TextDisabled("Current: %.0f FPS", ImGui::GetIO().Framerate);

    ImGui::SeparatorText("Content");
    ImGui::Checkbox("Show blood & gore", &s.allowGore);
    help("Turn off to hide blood, oil and gibs in every game.");
    ImGui::Checkbox("Gutstober theme (October)", &s.seasonTheme);
    help("In October the app dresses up for Halloween: orange and purple, a pumpkin and bats. Turn off for the usual blue.");
    {
        int bi = GraphicsSettings::breakChoiceOf(s.breakMinutes);
        if (ImGui::Combo("Break reminder", &bi, GraphicsSettings::breakChoiceNames(), GraphicsSettings::kBreakChoices))
            s.breakMinutes = GraphicsSettings::breakChoiceMinutes(bi);
        help("In the Player, a friendly nudge to rest your eyes and stretch after playing this long.");
    }

    ImGui::SeparatorText("Accessibility");
    ImGui::Combo("Colorblind mode", &s.colorBlind, GraphicsSettings::colorBlindNames(), 4);
    help("Shifts the game's colours so reds, greens or blues are easier to tell apart.");
    ImGui::SliderFloat("Text size", &s.textSize, 1.0f, 2.0f, "%.1fx");
    help("Makes all the writing on the screen bigger.");
    ImGui::Checkbox("Sound captions", &s.captions);
    help("Shows the sounds you hear as words while playing, with an arrow when they come from the side.");

    ImGui::SeparatorText("Clips");
    ImGui::Checkbox("Clip recorder", &s.clipRecorder);
    help("Keeps the last 30 seconds of the game you're playing, so F3 (or Save Clip in the menu) can save them as a GIF.");

    ImGui::SeparatorText("Controls");
    const char* touch[] = {"Auto (phones and tablets)", "Always on", "Off"};
    ImGui::Combo("Touch controls", &s.touchControls, touch, 3);
    help("An on-screen joystick, jump button and chat / menu buttons. Drag anywhere else to look "
         "around, pinch to zoom and tap things to click them. Turn it on to try it with a mouse.");
    ImGui::BeginDisabled(!s.touchEnabled());
    ImGui::SliderFloat("Button size", &s.touchSize, 0.7f, 1.6f, "%.1fx");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Graphics");
    int qi = GraphicsSettings::menuIndexOf(s.quality);
    if (ImGui::Combo("Quality", &qi, GraphicsSettings::qualityNames(), GraphicsSettings::kQualityChoices)) {
        const int q = GraphicsSettings::qualityAt(qi);
        if (q != GraphicsSettings::Custom) s.applyPreset(q); else s.quality = q;
    }
    help("Auto turns the graphics down by itself when a game gets laggy, and back up when it can. "
         "Very Low and Low are for slow computers; High and Ultra look best.");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", GraphicsSettings::qualityHelp(s.quality));
    if (s.quality == GraphicsSettings::Auto && s.autoFps() > 0.0f)
        ImGui::TextDisabled("Auto is on %s right now (%.0f FPS).", GraphicsSettings::autoLevelName(s.autoLevel()), s.autoFps());
    ImGui::PopTextWrapPos();

    bool custom = false;
    custom |= ImGui::Checkbox("Shadows", &s.shadows);
    help("Turning shadows off is the biggest single speed-up on slow computers.");
    ImGui::BeginDisabled(!s.shadows);
    const char* resNames[] = {"1024 (fast)", "2048", "4096 (sharpest)"};
    const int   resValues[] = {1024, 2048, 4096};
    int resIdx = s.shadowRes >= 4096 ? 2 : s.shadowRes >= 2048 ? 1 : 0;
    if (ImGui::Combo("Shadow Detail", &resIdx, resNames, 3)) { s.shadowRes = resValues[resIdx]; custom = true; }
    const char* sq[] = {"Hard", "Soft", "Realistic (contact-hardening)"};
    custom |= ImGui::Combo("Shadow Style", &s.shadowQuality, sq, 3);
    ImGui::EndDisabled();
    custom |= ImGui::Checkbox("Ambient Occlusion", &s.ssao);
    help("Darkens corners and contact points");
    custom |= ImGui::Checkbox("Bloom", &s.bloom);
    help("Glow around bright lights and Neon");
    custom |= ImGui::Checkbox("Anti-aliasing (FXAA)", &s.fxaa);
    help("Smooths jagged edges");
    custom |= ImGui::Checkbox("Tone mapping & colour grading", &s.postFx);
    custom |= ImGui::SliderFloat("Render Scale", &s.renderScale, 0.5f, 2.0f, "%.2fx");
    help("Below 1 = faster but blurrier. Above 1 = super-sampling (very sharp, slow).");
    custom |= ImGui::SliderInt("Max Lights", &s.maxLights, 0, 32);
    {
        char label[48];
        if (s.renderDistance >= GraphicsSettings::kMaxRenderDistance) std::snprintf(label, sizeof label, "%%d (max)");
        else std::snprintf(label, sizeof label, "%%d (%d studs)", (int)s.renderDistanceStuds());
        custom |= ImGui::SliderInt("Render Distance", &s.renderDistance, 1, GraphicsSettings::kMaxRenderDistance, label);
        help("How far away things are drawn. Further things fade into the sky; water, liquid, lights and effects "
             "out there rest until you come closer. Lower = faster. 10 (Ultra) = everything.");
    }
    const char* wq[] = {"Low", "Medium", "High", "Ultra"};
    custom |= ImGui::Combo("Water Quality", &s.waterQuality, wq, 4);
    help("Real liquid (FluidSource / FluidEmitter). Low: half resolution, 1 smoothing pass, 5,000 drops. Medium: 3/4 resolution, 2 passes, 20,000 drops. High: full resolution, 3 passes, 60,000 drops. Ultra: full resolution, 4 passes, 150,000 drops. Lower water quality reduces simulation and rendering work.");
    if (custom) s.quality = GraphicsSettings::Custom;

    ImGui::SeparatorText("Graphics API");
    static const int startedWith = s.graphicsApi;
    ImGui::Combo("Graphics API", &s.graphicsApi, GraphicsSettings::apiNames(), 4);
    help("Auto picks the newest version your graphics card has. Safe mode is for old or buggy drivers: "
         "everything still works, but the liquid runs on the processor (fewer drops).");
    if (!GraphicsSettings::activeApi().empty()) ImGui::TextDisabled("Using %s", GraphicsSettings::activeApi().c_str());
    if (s.graphicsApi != startedWith)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Close and reopen the app to switch.");

    ImGui::Spacing();
    if (ImGui::Button("Reset to High")) s.applyPreset(GraphicsSettings::High);

    ImGui::SeparatorText("Updates");
    ImGui::TextDisabled("Guts&Bolts checks when it starts and updates itself if a new version is out.");
    ImGui::TextDisabled("Version %s", UpdateChecker::currentVersion());
    ImGui::SameLine();
    if (ImGui::SmallButton("Check now")) UpdateChecker::start();
    switch (UpdateChecker::info().state) {
        case UpdateChecker::State::Checking:  ImGui::TextDisabled("Checking..."); break;
        case UpdateChecker::State::UpToDate:  ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.45f, 1), "You're up to date!"); break;
        case UpdateChecker::State::Available: ImGui::TextColored(ImVec4(1, 0.6f, 0.25f, 1), "An update is available."); break;
        case UpdateChecker::State::Failed:    ImGui::TextDisabled("Couldn't check (offline, or a private repository)."); break;
        default: break;
    }

    if (std::memcmp(&before, &s, sizeof(GraphicsSettings)) != 0) s.save();
    ImGui::End();
}

} // namespace SettingsWindow
