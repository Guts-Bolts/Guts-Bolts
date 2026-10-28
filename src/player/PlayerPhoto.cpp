// Photo mode: F2 (or the menu's Photo Mode button) hides everything on the screen and
// lets the camera fly on its own, so you can take a nice picture of the game.
//
//   WASD / left stick       fly            Q / E               down / up
//   Shift                   faster         mouse drag / right stick   look around
//   wheel / LB RB           zoom in / out  Enter / P / A        take the picture
//   Esc / F2 / B            back to playing
//
// The game keeps running (other players move, water flows); your own character just
// stands still. Pictures are the 3D view only (no menus or chat), saved as .png in the
// Photos folder next to the app (in a browser, they download instead).

#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/Pad.h"
#include "../core/Paths.h"
#include "../core/Settings.h"
#include "../game/PlayCamera.h"
#include "../scene/Player.h"
#include "../scene/Scene.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>

using namespace Site;

namespace {

// "Photo 2026-10-28 14-20-50.png" (a number on the end if that's taken).
std::filesystem::path photoName(const std::filesystem::path& folder) {
    char stamp[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "Photo %Y-%m-%d %H-%M-%S", std::localtime(&now));
    std::filesystem::path p = folder / (std::string(stamp) + ".png");
    for (int i = 2; std::filesystem::exists(p) && i < 100; ++i)
        p = folder / (std::string(stamp) + " (" + std::to_string(i) + ").png");
    return p;
}

} // namespace

bool PlayerApp::canTakePhotos() const {
    return m_page == Page::Game && m_scene && !m_vrOn;
}

void PlayerApp::startPhoto() {
    if (!canTakePhotos()) return;
    // Start where the playing camera is, looking the same way (also a script's camera).
    m_photoCam = m_camera;
    const glm::vec3 eye = m_camera.position(), fwd = m_camera.forward();
    m_photoCam.clearScripted();
    m_photoCam.clip = -1.0f;
    m_photoCam.distance = 0.0f;   // (the eye is the pivot: turning turns the head)
    m_photoCam.yaw = glm::degrees(std::atan2(-fwd.z, -fwd.x));
    m_photoCam.pitch = glm::degrees(std::asin(std::clamp(-fwd.y, -1.0f, 1.0f)));
    m_photoCam.pivot = eye;
    m_photo = true;
    m_paused = false;
    m_chatOpen = false;
}

void PlayerApp::photoCamera(float dt, bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    Camera& cam = m_photoCam;
    const GraphicsSettings& gs = GraphicsSettings::get();
    const float sens = gs.mouseSensitivity, invert = gs.invertCamera ? -1.0f : 1.0f;
    // Look around: drag with any mouse button (or a finger) on the picture, or the right stick.
    if (hovered && !m_photoOnButton &&
        (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Middle)))
        cam.look(io.MouseDelta.x * sens, io.MouseDelta.y * sens * invert);
    if (Pad::connected()) {
        const glm::vec2 rs = Pad::rightStick();
        if (rs.x != 0.0f || rs.y != 0.0f) cam.look(rs.x * 500.0f * dt * sens, -rs.y * 300.0f * dt * sens * invert);
    }
    // Zoom: the wheel (or the shoulder buttons) narrows the lens.
    float zoom = hovered ? io.MouseWheel : 0.0f;
    if (ImGui::IsKeyDown(ImGuiKey_GamepadR1)) zoom += 4.0f * dt;
    if (ImGui::IsKeyDown(ImGuiKey_GamepadL1)) zoom -= 4.0f * dt;
    cam.fov = std::clamp(cam.fov - zoom * 5.0f, 15.0f, 100.0f);
    // Fly.
    float f = m_photoMove.x, r = m_photoMove.y, u = m_photoMove.z;   // (the on-screen arrows)
    if (!io.WantTextInput) {
        if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) f += 1;
        if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) f -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) r += 1;
        if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) r -= 1;
        if (ImGui::IsKeyDown(ImGuiKey_E) || ImGui::IsKeyDown(ImGuiKey_Space)) u += 1;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) u -= 1;
    }
    if (Pad::connected()) {
        const glm::vec2 ls = Pad::leftStick();
        f += ls.y;
        r += ls.x;
        u += Pad::rightTrigger() - Pad::leftTrigger();
    }
    // (Camera::fly needs a pivot in front of the eye; ours is the eye itself.)
    const float speed = (io.KeyShift ? 50.0f : 16.0f) * dt;
    const glm::vec3 fwd = cam.forward(), side = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    cam.pivot += (fwd * f + side * r + glm::vec3(0, 1, 0) * u) * speed;
    // Don't fly off forever: stay within reach of the world.
    cam.pivot = glm::clamp(cam.pivot, glm::vec3(-20000.0f), glm::vec3(20000.0f));
}

void PlayerApp::takePhoto() {
    const std::string png = m_view.toPng();
    if (png.empty()) { m_photoMsg = "Couldn't take the picture"; m_photoMsgUntil = ImGui::GetTime() + 3.0; return; }
#ifdef __EMSCRIPTEN__
    // In a browser: hand the picture to the page as a download.
    EM_ASM({
        const bytes = HEAPU8.slice($0, $0 + $1);
        const a = document.createElement('a');
        a.href = URL.createObjectURL(new Blob([bytes], { type: 'image/png' }));
        a.download = UTF8ToString($2);
        document.body.appendChild(a);
        a.click();
        setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
    }, png.data(), (int)png.size(), photoName("").filename().string().c_str());
    m_photoMsg = "Picture saved to your downloads";
#else
    std::error_code ec;
    const std::filesystem::path folder = Paths::appFolder() / "Photos";
    std::filesystem::create_directories(folder, ec);
    const std::filesystem::path file = photoName(folder);
    std::ofstream out(file, std::ios::binary);
    out.write(png.data(), (std::streamsize)png.size());
    if (out.good()) m_photoMsg = "Picture saved: " + file.string();
    else m_photoMsg = "Couldn't save the picture in " + folder.string();
#endif
    m_photoMsgUntil = ImGui::GetTime() + 4.0;
    m_photoFlash = 1.0f;
}

void PlayerApp::drawPhotoMode(ImDrawList* dl, ImVec2 pos, ImVec2 max) {
    ImGuiIO& io = ImGui::GetIO();
    const bool touch = GraphicsSettings::get().touchEnabled();
    m_photoOnButton = false;
    m_photoMove = glm::vec3(0.0f);

    // The white flash when a picture is taken.
    if (m_photoFlash > 0.0f) {
        dl->AddRectFilled(pos, max, IM_COL32(255, 255, 255, (int)(180 * m_photoFlash)));
        m_photoFlash = std::max(0.0f, m_photoFlash - io.DeltaTime * 4.0f);
    }

    bool shoot = !io.WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_P, false) ||
                                       ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false));
    // The bar along the bottom: what the keys do, and the buttons.
    const float barH = touch ? 64.0f : 48.0f;
    const ImVec2 a(pos.x, max.y - barH);
    if (!m_photoHideBar) {
        dl->AddRectFilled(a, max, IM_COL32(12, 12, 16, 170));
        ImGui::SetCursorScreenPos(ImVec2(a.x + 12, a.y + (barH - (touch ? 48.0f : 34.0f)) * 0.5f));
        const ImVec2 bs(touch ? 150.0f : 160.0f, touch ? 48.0f : 34.0f);
        if (bigButton("Take Picture", kAccent, bs)) shoot = true;
        m_photoOnButton |= ImGui::IsItemHovered() || ImGui::IsItemActive();
        ImGui::SameLine(0, 8);
        if (bigButton("Back to Game", ImVec4(0.2f, 0.21f, 0.24f, 1), ImVec2(bs.x, bs.y))) m_photo = false;
        m_photoOnButton |= ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (!touch) {
            ImGui::SameLine(0, 16);
            const char* help = "WASD fly, Q/E down/up, drag to look, wheel to zoom";
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (bs.y - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.9f, 1), "%s", help);
        }
        // Phones: hold an arrow to fly (drag anywhere else to look around).
        if (touch) {
            const float s = 56.0f, gap = 6.0f;
            const ImVec2 c(max.x - s * 1.5f - gap - 16, a.y - s * 1.5f - gap - 16);   // the middle of the arrows
            struct Arrow { const char* id; const char* label; ImVec2 off; glm::vec3 move; };
            const Arrow arrows[] = {
                {"##fwd", "^", ImVec2(0, -1), {1, 0, 0}},  {"##back", "v", ImVec2(0, 1), {-1, 0, 0}},
                {"##left", "<", ImVec2(-1, 0), {0, -1, 0}}, {"##right", ">", ImVec2(1, 0), {0, 1, 0}},
            };
            for (const Arrow& ar : arrows) {
                const ImVec2 at(c.x + ar.off.x * (s + gap) - s * 0.5f, c.y + ar.off.y * (s + gap) - s * 0.5f);
                ImGui::SetCursorScreenPos(at);
                ImGui::InvisibleButton(ar.id, ImVec2(s, s));
                const bool held = ImGui::IsItemActive();
                m_photoOnButton |= held || ImGui::IsItemHovered();
                if (held) m_photoMove += ar.move;
                dl->AddRectFilled(at, ImVec2(at.x + s, at.y + s), held ? IM_COL32(80, 150, 240, 220) : IM_COL32(20, 20, 26, 170), 10.0f);
                const ImVec2 ts = ImGui::CalcTextSize(ar.label);
                dl->AddText(ImVec2(at.x + (s - ts.x) * 0.5f, at.y + (s - ts.y) * 0.5f), IM_COL32(255, 255, 255, 230), ar.label);
            }
            // Up and down, beside the arrows.
            const char* ud[] = {"Up", "Down"};
            for (int i = 0; i < 2; ++i) {
                const ImVec2 at(c.x - s * 2.5f - gap * 3, c.y + (i ? gap * 0.5f : -s - gap * 0.5f));
                ImGui::SetCursorScreenPos(at);
                ImGui::InvisibleButton(i ? "##down" : "##up", ImVec2(s, s));
                const bool held = ImGui::IsItemActive();
                m_photoOnButton |= held || ImGui::IsItemHovered();
                if (held) m_photoMove.z += i ? -1.0f : 1.0f;
                dl->AddRectFilled(at, ImVec2(at.x + s, at.y + s), held ? IM_COL32(80, 150, 240, 220) : IM_COL32(20, 20, 26, 170), 10.0f);
                const ImVec2 ts = ImGui::CalcTextSize(ud[i]);
                dl->AddText(ImVec2(at.x + (s - ts.x) * 0.5f, at.y + (s - ts.y) * 0.5f), IM_COL32(255, 255, 255, 230), ud[i]);
            }
        }
    }
    // H hides the bar too, for a completely clean screen.
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_H, false)) m_photoHideBar = !m_photoHideBar;

    if (shoot) takePhoto();
    if (ImGui::GetTime() < m_photoMsgUntil) {
        const ImVec2 ts = ImGui::CalcTextSize(m_photoMsg.c_str());
        const float w = ts.x + 32, x = pos.x + (max.x - pos.x - w) * 0.5f, y = pos.y + 16;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + ts.y + 16), IM_COL32(20, 20, 24, 220), 8.0f);
        dl->AddText(ImVec2(x + 16, y + 8), IM_COL32(255, 255, 255, 255), m_photoMsg.c_str());
    }
}
