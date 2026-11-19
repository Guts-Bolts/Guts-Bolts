// Phones and battery: battery saver's Auto (Settings > Battery Saver) and the in-game
// "running slowly" hint.
//
// Every 30 seconds it asks the device how full the battery is (core/Battery.h). On Auto,
// battery saver turns on at 20% or less while not charging: 30 FPS and the cheaper
// versions of the costliest effects (GraphicsSettings::saverCap). A short note at the top
// says so. Test helpers: GB_FAKE_BATTERY=15 pretends the battery is at 15% and not charging;
// "--page perfhint" shows the slow hint straight away.
#include "PlayerApp.h"
#include "../core/Battery.h"
#include "../core/Settings.h"

#include <imgui.h>
#include <algorithm>
#include <cstdlib>

void PlayerApp::perfTick(float dt) {
    GraphicsSettings& gs = GraphicsSettings::get();
    const bool testing = !m_opts.screenshot.empty() || testMode();
    const char* fake = std::getenv("GB_FAKE_BATTERY");
    m_batteryClock -= dt;
    if (m_batteryClock <= 0.0f && (!testing || fake)) {
        m_batteryClock = 30.0f;
        int pct = -1;
        bool charging = false;
        if (fake) pct = std::atoi(fake);
        else Battery::read(pct, charging);
        if (gs.setBattery(pct, charging)) m_perfHint.saverChanged(gs.saverActive(), gs.batteryPercent());
    }
    if (m_page == Page::Game && m_scene && !testing && !m_paused) m_perfHint.update(dt, gs);
    else m_perfHint.update(0.0f, gs);   // (the note still counts down)
}

void PlayerApp::drawPerfHint() {
    if (m_frame == 60 && m_opts.page == "perfhint") m_perfHint.saverChanged(true, 15);
    if (!m_perfHint.showing() || m_photo) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(440.0f, vp->WorkSize.x - 24.0f);
    // Under the break reminder's spot, so the two never cover each other.
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 18.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.13f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
    ImGui::Begin("##perfhint", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PushTextWrapPos(w - 40.0f);
    ImGui::TextColored(m_perfHint.slowHint() ? ImVec4(1.0f, 0.82f, 0.4f, 1) : ImVec4(0.55f, 0.9f, 0.6f, 1), "%s", m_perfHint.text().c_str());
    ImGui::PopTextWrapPos();
    ImGui::SameLine(w - 34.0f);
    if (ImGui::SmallButton("X##perfhint")) m_perfHint.dismiss();
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
