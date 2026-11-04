// Take-a-break reminder: after Settings > Break Reminder minutes of playing (30 minutes
// to 2 hours, off by default), a friendly card at the top of the screen says how long
// you've played and suggests a rest. It doesn't pause anything (other players keep
// going): "Keep playing" hides it and starts counting again, "Leave game" leaves.
//
// The time adds up over every game you play while the app is open, so hopping from
// game to game still counts. Test helper: "--page break" shows the card straight away.
#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/Settings.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>

using namespace Site;

namespace {
// "1 hour", "1 hour and 30 minutes", "45 minutes".
std::string playedText(float seconds) {
    const int mins = std::max(1, (int)(seconds / 60.0f));
    const int h = mins / 60, m = mins % 60;
    auto unit = [](int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); };
    if (h == 0) return unit(m, "minute", "minutes");
    if (m == 0) return unit(h, "hour", "hours");
    return unit(h, "hour", "hours") + " and " + unit(m, "minute", "minutes");
}
} // namespace

void PlayerApp::drawBreakReminder(float dt) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    m_breakPlayed += dt;
    if (m_frame == 60 && m_opts.page == "break") { m_breakPlayed = 60.0f * 60.0f; m_breakShowing = true; }
    if (gs.breakMinutes <= 0 && !m_breakShowing) return;
    if (!m_breakShowing && m_breakPlayed >= gs.breakMinutes * 60.0f) m_breakShowing = true;
    if (!m_breakShowing || m_photo) return;   // (photo mode hides everything; it shows after)

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(460.0f, vp->WorkSize.x - 24.0f);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 70.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.13f, 0.15f, 0.96f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 14));
    ImGui::Begin("##breakreminder", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                                             ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::SetWindowFontScale(1.25f);
    const std::string title = "You've been playing for " + playedText(m_breakPlayed) + ".";
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1), "%s", title.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextUnformatted("Nice! Maybe take a short break: stretch, drink some water and rest your eyes. The game will be here when you get back.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const float bw = (ImGui::GetContentRegionAvail().x - 10.0f) * 0.5f;
    const bool keep = bigButton("Keep playing", kGreen, ImVec2(bw, 36));
    ImGui::SameLine(0, 10);
    const bool leave = bigButton("Leave game", ImVec4(0.3f, 0.3f, 0.35f, 1), ImVec2(bw, 36));
    ImGui::TextDisabled("Change or turn this off in Settings > Break Reminder.");
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (keep || leave) { m_breakShowing = false; m_breakPlayed = 0.0f; }
    if (leave) { m_paused = false; leaveGame(); }
}
