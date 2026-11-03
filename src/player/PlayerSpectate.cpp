// Spectate mode: watch other players through a camera that follows them.
//
//   - While you're dead and waiting to respawn, it watches someone else by itself
//     (unless the game turned that off with StarterGui:SetCore("SpectateWhenDead", false)).
//   - The menu's Spectate button turns it on until you press Stop (you stand still meanwhile).
//   - A game's script can turn it on and off: StarterGui:SetCore("Spectate", true / false),
//     for example while you wait for the next round.
//
//   Left / Right arrows (or the < > buttons, or a controller's LB / RB)   next person
//   right-drag / right stick                                            look around
//   mouse wheel                                                         closer / further
#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/Pad.h"
#include "../core/Settings.h"
#include "../game/PlayCamera.h"
#include "../scene/Player.h"
#include "../scene/Scene.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

using namespace Site;

bool PlayerApp::spectateManual() const {
    return m_page == Page::Game && m_scene && !m_vrOn && (m_spectate || m_scene->coreGui().spectate);
}

bool PlayerApp::spectating() const {
    if (m_page != Page::Game || !m_scene || m_vrOn || m_photo) return false;
    if (spectateTargets().empty()) return false;   // nobody else to watch
    if (spectateManual()) return true;
    const Player* p = m_scene->player();
    return p && p->isDead() && m_scene->coreGui().spectateWhenDead;
}

std::vector<std::pair<uint64_t, std::string>> PlayerApp::spectateTargets() const {
    std::vector<std::pair<uint64_t, std::string>> out;
    if (!m_scene) return out;
    for (const RemoteCharacter& rc : const_cast<Scene*>(m_scene.get())->remotes())
        if (rc.alive && const_cast<Scene*>(m_scene.get())->findById(rc.rootId)) out.push_back({rc.rootId, rc.name});
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return out;
}

void PlayerApp::spectateNext(int step) {
    const auto list = spectateTargets();
    if (list.empty()) { m_specTarget = 0; return; }
    int at = 0;
    for (size_t i = 0; i < list.size(); ++i) if (list[i].first == m_specTarget) at = (int)i;
    at = ((at + step) % (int)list.size() + (int)list.size()) % (int)list.size();
    m_specTarget = list[(size_t)at].first;
}

void PlayerApp::spectateCamera(float dt, bool hovered) {
    const bool on = spectating();
    if (!on) { m_specWasOn = false; return; }
    if (!m_specWasOn) {   // just started: look the way the playing camera looked
        m_specCam = m_camera;
        m_specCam.clearScripted();
        m_specCam.useCFrame = false;
        m_specCam.distance = std::clamp(m_camera.distance, 6.0f, 20.0f);
        m_specWasOn = true;
    }
    // Still there? (they left, or died) Then the next one.
    const auto list = spectateTargets();
    if (std::none_of(list.begin(), list.end(), [&](const auto& t) { return t.first == m_specTarget; })) spectateNext(0);
    if (std::none_of(list.begin(), list.end(), [&](const auto& t) { return t.first == m_specTarget; }) && !list.empty())
        m_specTarget = list.front().first;

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !m_chatOpen) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) spectateNext(-1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) spectateNext(1);
    }
    const GraphicsSettings& gs = GraphicsSettings::get();
    const float sens = gs.mouseSensitivity, invert = gs.invertCamera ? -1.0f : 1.0f;
    if (hovered && (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                    (ImGui::IsMouseDown(ImGuiMouseButton_Left) && gs.touchEnabled())))
        m_specCam.look(io.MouseDelta.x * sens, io.MouseDelta.y * sens * invert);
    if (Pad::connected()) {
        const glm::vec2 rs = Pad::rightStick();
        if (rs.x != 0.0f || rs.y != 0.0f) m_specCam.look(rs.x * 500.0f * dt * sens, -rs.y * 300.0f * dt * sens * invert);
    }
    if (hovered) m_specCam.distance = std::clamp(m_specCam.distance - io.MouseWheel * 2.0f, 4.0f, 40.0f);

    // Follow their head, smoothly (their character moves in network steps).
    if (SceneNode* rig = m_scene->findById(m_specTarget)) {
        SceneNode* head = rig->findChild("Head");
        const glm::vec3 at = head ? glm::vec3(head->worldMatrix()[3]) : glm::vec3(rig->worldMatrix()[3]) + glm::vec3(0.0f, 1.5f, 0.0f);
        const float k = 1.0f - std::exp(-12.0f * dt);
        m_specCam.pivot = glm::length(at - m_specCam.pivot) > 30.0f ? at : glm::mix(m_specCam.pivot, at, k);
    }
}

void PlayerApp::drawSpectate(ImDrawList* dl, ImVec2 pos, ImVec2 max) {
    if (!spectating()) return;
    const auto list = spectateTargets();
    std::string who = "someone";
    for (const auto& t : list) if (t.first == m_specTarget) who = t.second;
    const Player* p = m_scene->player();
    const bool dead = p && p->isDead() && !spectateManual();
    const float w = 360.0f, h = 64.0f;
    const ImVec2 a(std::floor((pos.x + max.x - w) * 0.5f), pos.y + 54.0f), b(a.x + w, a.y + h);
    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 165), 8.0f);
    const std::string title = "Spectating " + who;
    const ImVec2 ts = ImGui::CalcTextSize(title.c_str());
    dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, a.y + 8.0f), IM_COL32(255, 255, 255, 255), title.c_str());
    char sub[96];
    if (dead) std::snprintf(sub, sizeof sub, "Respawning in %.0f...", std::ceil(std::max(0.0f, p->respawnIn())));
    else std::snprintf(sub, sizeof sub, "%d of %d players", 1 + (int)(std::find_if(list.begin(), list.end(), [&](const auto& t) { return t.first == m_specTarget; }) - list.begin()), (int)list.size());
    const ImVec2 ss = ImGui::CalcTextSize(sub);
    dl->AddText(ImVec2((a.x + b.x - ss.x) * 0.5f, a.y + 34.0f), IM_COL32(200, 200, 210, 255), sub);
    // < and > (and Stop, when you asked for it from the menu).
    ImGui::SetCursorScreenPos(ImVec2(a.x + 8.0f, a.y + 12.0f));
    if (bigButton("<##specprev", ImVec4(0.2f, 0.21f, 0.24f, 1), ImVec2(40, 40))) spectateNext(-1);
    ImGui::SetCursorScreenPos(ImVec2(b.x - 48.0f, a.y + 12.0f));
    if (bigButton(">##specnext", ImVec4(0.2f, 0.21f, 0.24f, 1), ImVec2(40, 40))) spectateNext(1);
    if (m_spectate && !m_scene->coreGui().spectate) {
        ImGui::SetCursorScreenPos(ImVec2((a.x + b.x - 120.0f) * 0.5f, b.y + 6.0f));
        if (bigButton("Stop spectating", ImVec4(0.75f, 0.25f, 0.25f, 1), ImVec2(120, 30))) m_spectate = false;
    }
}
