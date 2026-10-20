#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/AppWindow.h"
#include "../core/Vr.h"
#include "../game/GameSession.h"
#include "../renderer/SceneRenderer.h"
#include "../scene/Player.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

// Playing in a VR headset. You're inside your character's head: turning your
// head turns the camera, the left stick walks the way you look, the right stick
// turns you in steps (smooth turning makes people feel sick), A jumps, the
// right trigger uses your tool. The character's head turns with yours and its
// arms reach for your controllers; the body follows when you walk or turn far.
// The computer screen keeps showing what your left eye sees, with the usual
// buttons and menus on top.

namespace {
// How big you are: your eyes (and how far apart they are, and where your hands
// are) are scaled so your real eye height (about 1.6 m standing) is the
// character's. Smaller characters make the world feel bigger, like it should.
constexpr float kRealEyeHeight = 1.6f;   // metres
constexpr float kSnapTurn = 30.0f;   // degrees per push of the right stick
constexpr float kNeckTwist = 50.0f;  // how far the head turns before the body comes along
// Your eyes are at the front of your head, not the middle: without this, looking
// down shows the top of your own shoulders instead of your chest and feet.
constexpr float kEyesForward = 0.13f;   // of the eye height

// From the middle of the character's head to its eyes, in the world.
glm::vec3 eyesAhead(float bodyYaw, float eyeHeight) {
    const float y = glm::radians(bodyYaw);
    return glm::vec3(std::sin(y), 0.0f, std::cos(y)) * (kEyesForward * eyeHeight);
}

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
    m_vrBodyAligned = false;
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
    if (Player* p = m_scene->player()) p->setVrBody({});
    if (m_session) m_session->scripts().setVr({});
}

void PlayerApp::vrControls(bool acceptInput) {
    const Vr::Controls& c = Vr::controls();
    if (c.menu) { m_paused = !m_paused; m_menuTab = 0; m_menuConfirm = 0; }
    if (!acceptInput) { m_session->setTouchInput(glm::vec2(0.0f), false); return; }
    m_session->setTouchInput(c.move, c.jump);   // (walks relative to m_camera.yaw: where you look)
    if (c.use) m_session->click(0);             // swing / fire the tool you're holding
    // Snap turning: one step each time the stick is pushed over.
    if (std::abs(c.turn.x) > 0.7f && !m_vrTurned) {
        m_vrYaw += c.turn.x > 0.0f ? -kSnapTurn : kSnapTurn;
        m_vrTurned = true;
    } else if (std::abs(c.turn.x) < 0.4f) {
        m_vrTurned = false;
    }
}

void PlayerApp::vrTrack() {
    Player* p = m_scene->player();
    if (!p) return;
    m_vrScale = p->eyeHeight() / kRealEyeHeight;   // studs per real metre
    // Headset space -> the world: the snap turns, and metres -> studs.
    const glm::quat turn = glm::angleAxis(glm::radians(m_vrYaw), glm::vec3(0, 1, 0));
    const glm::mat4 head = Vr::head();
    const glm::vec3 headAt(head[3]);
    Player::VrBody body;
    body.on = true;
    body.head = turn * glm::quat_cast(glm::mat3(head));
    for (int i = 0; i < 2; ++i) {
        glm::mat4 hand;
        if (!Vr::hand(i, hand)) continue;
        body.hand[i] = true;
        body.handAt[i] = eyesAhead(m_vrBodyYaw, p->eyeHeight()) + turn * ((glm::vec3(hand[3]) - headAt) * m_vrScale);
    }
    // Tests: controllers held at fixed spots (in front of / around the head).
    float t[6];
    if (!m_opts.vrTestHands.empty() &&
        std::sscanf(m_opts.vrTestHands.c_str(), "%f,%f,%f,%f,%f,%f", &t[0], &t[1], &t[2], &t[3], &t[4], &t[5]) == 6)
        for (int i = 0; i < 2; ++i) {
            body.hand[i] = true;
            body.handAt[i] = eyesAhead(m_vrBodyYaw, p->eyeHeight()) +
                             turn * glm::quat_cast(glm::mat3(head)) * (glm::vec3(t[i * 3], t[i * 3 + 1], t[i * 3 + 2]) * m_vrScale);
        }
    p->setVrBody(body);

    // VRService for the game's scripts: your head and hands in studs, from where VR
    // started (turned with the snap turns), like Roblox's GetUserCFrame.
    ScriptEngine::VrPose pose;
    pose.on = m_vrOn;
    auto toStuds = [&](const glm::mat4& m) {
        glm::mat4 r = glm::mat4_cast(turn) * m;
        r[3] = glm::vec4(turn * (glm::vec3(m[3]) * m_vrScale), 1.0f);
        return r;
    };
    pose.head = toStuds(head);
    for (int i = 0; i < 2; ++i) {
        glm::mat4 hand;
        pose.handOk[i] = Vr::hand(i, hand);
        if (pose.handOk[i]) pose.hand[i] = toStuds(hand);
    }
    m_session->scripts().setVr(pose);
}

void PlayerApp::vrShowBody(Player& player) {
    // First person hides your whole character; in VR you see your body, arms and
    // legs (like looking down at yourself), just not the head you're inside. The
    // blocky arms are a little see-through for you, so one reaching across your
    // face doesn't hide the world (everyone else sees them solid).
    SceneNode* root = player.root();
    if (!root) return;
    for (auto& c : root->children) {
        if (!c->isPart() || !(Player::isLimb(c.get()) || c->name == "Torso")) continue;
        const bool arm = c->name == "Left Arm" || c->name == "Right Arm";
        std::vector<SceneNode*> stack{c.get()};
        while (!stack.empty()) {
            SceneNode* n = stack.back();
            stack.pop_back();
            n->localTransparency = arm ? 0.35f : 0.0f;
            for (auto& k : n->children) stack.push_back(k.get());
        }
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
    // The body follows the head: straight away when you walk, else only once you've
    // turned your head further than a neck goes (so looking around doesn't spin you).
    if (f.x * f.x + f.z * f.z > 1e-6f) {
        const float headYaw = glm::degrees(std::atan2(f.x, f.z));
        float diff = std::remainder(headYaw - m_vrBodyYaw, 360.0f);
        const bool walking = glm::length(Vr::controls().move) > 0.1f;
        if (m_vrBodyAligned && !walking) diff = std::abs(diff) > kNeckTwist ? diff - std::copysign(kNeckTwist, diff) : 0.0f;
        m_vrBodyYaw += diff;
        m_vrBodyAligned = true;
    }
    if (!player.isDead()) player.faceYaw(m_vrBodyYaw);
    m_camera.pivot += eyesAhead(m_vrBodyYaw, player.eyeHeight());
}

void PlayerApp::vrRender(const Vr::View eyes[2]) {
    const glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::radians(m_vrYaw), glm::vec3(0, 1, 0));
    const glm::vec3 headAt(Vr::head()[3]);
    for (int i = 0; i < 2; ++i) {
        // The eye sits in the character's head, a little to the side (how far your
        // own eyes are from the middle of your head, in studs).
        const glm::vec3 offset = (glm::vec3(eyes[i].pose[3]) - headAt) * m_vrScale;
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
