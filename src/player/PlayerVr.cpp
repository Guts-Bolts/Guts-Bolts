#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/AppWindow.h"
#include "../core/Vr.h"
#include "../game/GameSession.h"
#include "../renderer/SceneRenderer.h"
#include "../scene/Player.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

// Playing in a VR headset. You're inside your character's head: turning your
// head turns the camera (and the character), the left stick walks the way you
// look, the right stick turns you in steps (smooth turning makes people feel
// sick), A jumps. The computer screen keeps showing what your left eye sees,
// with the usual buttons and menus on top.

namespace {
// Roblox-sized worlds: a stud is about 28 cm, so a metre is about 3.6 studs.
// Your eyes (and how far apart they are) are scaled to match.
constexpr float kStudsPerMetre = 1.0f / 0.28f;
constexpr float kSnapTurn = 30.0f;   // degrees per push of the right stick

// The way you face, as an angle around the up axis (0 = looking along -Z).
float headingOf(const glm::vec3& f) { return glm::degrees(std::atan2(-f.x, -f.z)); }
} // namespace

void PlayerApp::startVr() {
    if (m_vrOn) return;
    std::string error;
#ifdef GB_MOBILE
    GLFWwindow* window = nullptr;   // (phones: no VR in this build yet, start() says so)
#else
    GLFWwindow* window = m_window->handle();
#endif
    if (!Vr::start(window, error)) { m_vrStatus = error; return; }
    m_vrOn = true;
    m_vrAligned = false;
    m_vrTurned = false;
    m_vrYaw = 0.0f;
    m_vrStatus.clear();
    m_window->setHeadsetPaced(true);
}

void PlayerApp::stopVr() {
    if (!m_vrOn) return;
    Vr::stop();
    m_vrOn = false;
    m_window->setHeadsetPaced(false);
    for (Framebuffer& e : m_vrEyes) e.resize(0, 0);
}

void PlayerApp::vrControls(bool acceptInput) {
    const Vr::Controls& c = Vr::controls();
    if (c.menu) { m_paused = !m_paused; m_menuTab = 0; m_menuConfirm = 0; }
    if (!acceptInput) { m_session->setTouchInput(glm::vec2(0.0f), false); return; }
    m_session->setTouchInput(c.move, c.jump);   // (walks relative to m_camera.yaw: where you look)
    // Snap turning: one step each time the stick is pushed over.
    if (std::abs(c.turn.x) > 0.7f && !m_vrTurned) {
        m_vrYaw += c.turn.x > 0.0f ? -kSnapTurn : kSnapTurn;
        m_vrTurned = true;
    } else if (std::abs(c.turn.x) < 0.4f) {
        m_vrTurned = false;
    }
}

void PlayerApp::vrFollow(Player& player) {
    // Your head, turned by the snap turns, in the world.
    glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::radians(m_vrYaw), glm::vec3(0, 1, 0));
    glm::vec3 f = glm::vec3(turn * Vr::head() * glm::vec4(0, 0, -1, 0));
    if (!m_vrAligned && Vr::visible()) {   // start out looking the way the character (and camera) faced
        glm::vec3 cam = m_camera.forward();
        if (cam.x * cam.x + cam.z * cam.z > 1e-6f && f.x * f.x + f.z * f.z > 1e-6f) {
            m_vrYaw += headingOf(cam) - headingOf(f);
            turn = glm::rotate(glm::mat4(1.0f), glm::radians(m_vrYaw), glm::vec3(0, 1, 0));
            f = glm::vec3(turn * Vr::head() * glm::vec4(0, 0, -1, 0));
        }
        m_vrAligned = true;
    }
    // The play camera sits in the head looking where you look, so everything that
    // uses it (walking, clicking, sounds, name tags) follows your head. First person
    // also hides your own character (except what you hold).
    m_camera.useCFrame = false;
    m_camera.useProjection = false;
    m_camera.clip = -1.0f;
    m_camera.distance = 0.0f;
    m_camera.pivot = player.focusPoint();
    if (f.x * f.x + f.z * f.z > 1e-6f) m_camera.yaw = glm::degrees(std::atan2(-f.z, -f.x));
    m_camera.pitch = std::clamp(glm::degrees(std::asin(std::clamp(-f.y, -1.0f, 1.0f))), -89.0f, 89.0f);
    if (!player.isDead() && f.x * f.x + f.z * f.z > 1e-6f) player.faceYaw(glm::degrees(std::atan2(f.x, f.z)));
}

void PlayerApp::vrRender(const Vr::View eyes[2]) {
    const glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::radians(m_vrYaw), glm::vec3(0, 1, 0));
    const glm::vec3 headAt(Vr::head()[3]);
    for (int i = 0; i < 2; ++i) {
        // The eye sits in the character's head, a little to the side (how far your
        // own eyes are from the middle of your head, in studs).
        const glm::vec3 offset = (glm::vec3(eyes[i].pose[3]) - headAt) * kStudsPerMetre;
        glm::mat4 world = turn * eyes[i].pose;
        world[3] = glm::vec4(m_camera.pivot + glm::vec3(turn * glm::vec4(offset, 0.0f)), 1.0f);
        Camera eye = m_camera;
        eye.useCFrame = true;
        eye.cframe = world;
        eye.useProjection = true;
        eye.projectionOverride = eyes[i].projection;
        eye.resize(eyes[i].width, eyes[i].height);
        m_vrEyes[i].resize(eyes[i].width, eyes[i].height);
        m_renderer->render(*m_scene, eye, m_vrEyes[i], false);
        Vr::submitEye(i, m_vrEyes[i].colorTexture());
    }
}

void PlayerApp::vrMirror(ImVec2 size) {
    const Framebuffer& left = m_vrEyes[0];
    if (left.width() <= 0 || left.height() <= 0) {   // (nothing yet: the headset isn't on anyone's head)
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), IM_COL32(12, 13, 16, 255));
        const char* msg = "Put on your VR headset";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(at.x + (size.x - ts.x) * 0.5f, at.y + (size.y - ts.y) * 0.5f), IM_COL32(220, 220, 230, 255), msg);
        ImGui::Dummy(size);
        return;
    }
    // Fill the screen without stretching: cut off the sides (or top and bottom).
    const float eyeAspect = (float)left.width() / (float)left.height(), aspect = size.x / size.y;
    float u = 1.0f, v = 1.0f;
    if (aspect > eyeAspect) v = eyeAspect / aspect;
    else u = aspect / eyeAspect;
    ImGui::Image((ImTextureID)(intptr_t)left.colorTexture(), size,
                 ImVec2(0.5f - u * 0.5f, 0.5f + v * 0.5f), ImVec2(0.5f + u * 0.5f, 0.5f - v * 0.5f));
}
