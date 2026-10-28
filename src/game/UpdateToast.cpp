#include "UpdateToast.h"
#include "../core/Settings.h"
#include "../core/UpdateChecker.h"

#include <imgui.h>
#include <algorithm>
#include <cstdlib>
#include <string>
#if defined(GB_IOS)
#include <SDL.h>
#endif

namespace UpdateToast {

namespace {
bool  g_started = false;
bool  g_dismissed = false;
double g_launch = 0.0;      // ImGui time of the first frame
int   g_forced = -1;        // -1 = not decided yet, 0 = just the card, 1 = the update screen
double g_forcedAt = 0.0;
constexpr double kLaunchWindow = 45.0;   // a result this soon after opening counts as "on launch"
constexpr double kCountdown = 3.0;
float g_slide = 0.0f;       // 0 = hidden, 1 = fully in
std::string g_error;
std::string g_appName;

void openUrl(const std::string& url) {
#if defined(GB_IOS)
    // SDL delegates URL opening to the platform; iOS forbids shell execution.
    SDL_OpenURL(url.c_str());
#elif defined(_WIN32)
    std::system(("start \"\" \"" + url + "\"").c_str());
#elif defined(__APPLE__)
    std::system(("open \"" + url + "\"").c_str());
#else
    std::system(("xdg-open \"" + url + "\" >/dev/null 2>&1 &").c_str());
#endif
}
} // namespace

// The update screen: covers the whole app, counts down, runs the updater.
bool drawForced(const char* appName, const UpdateChecker::Info& info) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.10f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.93f, 0.95f, 1));
    ImGui::Begin("##updatescreen", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
    const float w = std::min(460.0f, vp->Size.x - 40.0f);
    ImGui::SetCursorPos(ImVec2((vp->Size.x - w) * 0.5f, vp->Size.y * 0.5f - 110.0f));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.25f, 1), "Updating Guts&Bolts");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    ImGui::Text("A new version is out (you have %s).", UpdateChecker::currentVersion());
    if (!info.latestMessage.empty()) ImGui::TextDisabled("Latest: %s", info.latestMessage.c_str());
    ImGui::TextDisabled("Everyone stays on the newest version, so games work the same for everybody.");
    ImGui::Spacing();
    bool quit = false;
    if (g_error.empty()) {
        double left = kCountdown - (ImGui::GetTime() - g_forcedAt);
        ImGui::ProgressBar((float)std::clamp(1.0 - left / kCountdown, 0.0, 1.0), ImVec2(w, 0),
                           left > 0 ? ("Starting the updater in " + std::to_string((int)left + 1) + "...").c_str() : "Starting...");
        if (left <= 0) {
            if (UpdateChecker::launchUpdater(g_appName.c_str())) quit = true;
            else g_error = "Couldn't start the updater.";
        }
    } else {
        ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_error.c_str());
        ImGui::TextDisabled("You can download the new version yourself and unzip it over this folder.");
        ImGui::Spacing();
        if (ImGui::Button("Download it myself", ImVec2(160, 30))) openUrl(UpdateChecker::downloadPage());
        ImGui::SameLine();
        if (ImGui::Button("Try again", ImVec2(100, 30))) { g_error.clear(); g_forcedAt = ImGui::GetTime(); }
        ImGui::SameLine();
        if (ImGui::Button("Use this version for now", ImVec2(190, 30))) { g_forced = 0; g_dismissed = true; }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    (void)appName;
    return quit;
}

bool draw(const char* appName, bool canForce) {
    if (!g_started) {
        g_started = true;
        g_launch = ImGui::GetTime();
        g_appName = appName;
        UpdateChecker::start();   // always: updates aren't optional any more
    }
    UpdateChecker::Info info = UpdateChecker::info();
    if (g_forced < 0 && info.state != UpdateChecker::State::Idle && info.state != UpdateChecker::State::Checking) {
        // Decide once, when the launch check comes back.
        const bool onLaunch = ImGui::GetTime() - g_launch < kLaunchWindow;
        g_forced = info.state == UpdateChecker::State::Available && info.plainlyBehind && onLaunch && UpdateChecker::canUpdate() ? 1 : 0;
        g_forcedAt = ImGui::GetTime();
        // The last try at this version didn't work: say why and let the person choose,
        // instead of closing and reopening over and over.
        if (g_forced == 1 && !info.lastTryFailed.empty())
            g_error = "The update didn't work last time: " + info.lastTryFailed;
    }
    if (g_forced == 1) {
        if (canForce) return drawForced(appName, info);
        g_forcedAt = ImGui::GetTime();   // waiting (unsaved work / in a game): restart the countdown when it's safe
    }
    bool show = info.state == UpdateChecker::State::Available && !g_dismissed;
    float dt = ImGui::GetIO().DeltaTime;
    g_slide = std::clamp(g_slide + (show ? dt : -dt) * 4.0f, 0.0f, 1.0f);
    if (g_slide <= 0.0f) return false;

    // Ease-out slide from the right edge.
    float e = 1.0f - (1.0f - g_slide) * (1.0f - g_slide);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = 360.0f;
    // (Pinned by its bottom edge, so a longer message grows it upwards instead of off the screen.)
    ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - (w + 20.0f) * e, vp->WorkPos.y + vp->WorkSize.y - 40.0f);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::SetNextWindowBgAlpha(0.97f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.13f, 0.17f, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.55f, 0.2f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.93f, 0.95f, 1));
    ImGui::Begin("##updatetoast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);

    bool quit = false;
    ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.25f, 1), "Update available");
    ImGui::SameLine(w - 44);
    if (ImGui::SmallButton("x")) g_dismissed = true;
    ImGui::PushTextWrapPos(w - 16);
    ImGui::Text("A new version is out (you have %s).", UpdateChecker::currentVersion());
    ImGui::PopTextWrapPos();
    if (!info.latestMessage.empty()) {
        ImGui::PushTextWrapPos(w - 16);
        ImGui::TextDisabled("Latest: %s", info.latestMessage.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!g_error.empty()) {
        ImGui::PushTextWrapPos(w - 16);
        ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_error.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton("Download it myself")) openUrl(UpdateChecker::downloadPage());
    }
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.5f, 0.15f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.25f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::BeginDisabled(!UpdateChecker::canUpdate());
    if (ImGui::Button("Update now", ImVec2(120, 30))) {
        if (UpdateChecker::launchUpdater(appName)) quit = true;
        else g_error = "Couldn't start the updater.";
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    if (!UpdateChecker::canUpdate() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("The source folder isn't next to this build - download the new version from GitHub.");
    ImGui::SameLine();
    if (ImGui::Button("What's new", ImVec2(100, 30))) openUrl(info.compareUrl);
    ImGui::SameLine();
    if (ImGui::Button("Later", ImVec2(80, 30))) g_dismissed = true;

    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return quit;
}

} // namespace UpdateToast
