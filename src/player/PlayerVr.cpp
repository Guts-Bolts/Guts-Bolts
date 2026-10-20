#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/AppWindow.h"
#include "../core/Vr.h"
#include "../game/GameSession.h"
#include "../renderer/GL.h"
#include "../renderer/SceneRenderer.h"
#include "../scene/Player.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

// Playing in a VR headset. You're inside your character's head: turning your
// head turns the camera, the left stick walks the way you look, the right stick
// turns you in steps (smooth turning makes people feel sick), A jumps, the
// right trigger uses your tool. The character's head turns with yours and its
// arms reach for your controllers; the body follows when you walk or turn far.
// The computer screen keeps showing what your left eye sees, with the usual
// buttons and menus on top. In the headset those buttons and menus float on a
// see-through screen in front of you: point at it with your right hand and
// pull the trigger to click.

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
// The floating screen with the menus: how far in front of you, how much lower
// than your eyes, and how wide (metres). It glides back in front of you when
// you look further away than kPanelLookAway (degrees), and snaps there when a
// menu opens.
constexpr float kPanelDistance = 1.3f, kPanelDrop = 0.12f, kPanelWidth = 1.4f;
constexpr float kPanelLookAway = 40.0f;
constexpr int kPanelPixels = 1600;   // its width in pixels (the height keeps the window's shape)

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
    m_vrPanelPlaced = false;
    m_vrPointing = m_vrPointerDown = m_vrOverUi = false;
    m_vrStatus.clear();
    m_window->setHeadsetPaced(true);
}

void PlayerApp::stopVr() {
    if (!m_vrOn) return;
    Vr::stop();
    m_vrOn = false;
    m_window->setHeadsetPaced(false);
    for (Framebuffer& e : m_vrEyes) e.resize(0, 0);
    m_vrPanel.resize(0, 0);
    if (m_vrPointerDown) ImGui::GetIO().AddMouseButtonEvent(0, false);
    m_vrPointing = m_vrPointerDown = false;
    if (Player* p = m_scene->player()) p->setVrBody({});
    if (m_session) m_session->scripts().setVr({});
}

void PlayerApp::vrControls(bool acceptInput) {
    const Vr::Controls& c = Vr::controls();
    if (c.menu) { m_paused = !m_paused; m_menuTab = 0; m_menuConfirm = 0; }
    vrPointer();
    if (!acceptInput) { m_session->setTouchInput(glm::vec2(0.0f), false); return; }
    m_session->setTouchInput(c.move, c.jump);   // (walks relative to m_camera.yaw: where you look)
    if (c.use && !(m_vrPointing && m_vrOverUi)) m_session->click(0);   // swing / fire the tool you're holding
    // Snap turning: one step each time the stick is pushed over.
    if (std::abs(c.turn.x) > 0.7f && !m_vrTurned) {
        m_vrYaw += c.turn.x > 0.0f ? -kSnapTurn : kSnapTurn;
        m_vrTurned = true;
    } else if (std::abs(c.turn.x) < 0.4f) {
        m_vrTurned = false;
    }
}

void PlayerApp::vrPointer() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) return;
    const glm::mat4 head = Vr::head();
    const glm::vec3 headAt(head[3]);
    const float panelHeight = kPanelWidth * io.DisplaySize.y / io.DisplaySize.x;

    // Where the screen goes: in front of you (the way you face, level), a little
    // low, turned to face you. It stays put while you look around it.
    const bool menus = m_paused || m_showSettings || m_chatOpen;
    const bool opened = menus && !m_vrWasPaused;
    m_vrWasPaused = menus;
    glm::vec3 f = -glm::vec3(head[2]);
    f.y = 0.0f;
    if (glm::length(f) > 1e-3f) {
        f = glm::normalize(f);
        const glm::vec3 want = headAt + f * kPanelDistance - glm::vec3(0.0f, kPanelDrop, 0.0f);
        glm::vec3 at(m_vrPanelPose[3]);
        glm::vec3 toPanel = at - headAt;
        toPanel.y = 0.0f;
        const float away = glm::length(toPanel) > 1e-3f
                               ? glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(toPanel), f), -1.0f, 1.0f)))
                               : 180.0f;
        if (!m_vrPanelPlaced || opened) {
            at = want;
            m_vrPanelPlaced = true;
            m_vrPanelMoving = false;
        } else {
            if (away > kPanelLookAway) m_vrPanelMoving = true;
            if (m_vrPanelMoving) {
                at += (want - at) * std::min(1.0f, io.DeltaTime * 5.0f);
                if (away < 3.0f) m_vrPanelMoving = false;
            }
        }
        glm::vec3 back = headAt - at;   // (its front faces you)
        back.y = 0.0f;
        const float yaw = glm::length(back) > 1e-3f ? std::atan2(back.x, back.z) : 0.0f;
        m_vrPanelPose = glm::rotate(glm::translate(glm::mat4(1.0f), at), yaw, glm::vec3(0, 1, 0));
    }

    // The right hand's beam: where it meets the screen is the mouse pointer.
    const bool wasPointing = m_vrPointing;
    m_vrPointing = false;
    glm::mat4 aim;
    if (Vr::aim(1, aim)) {
        const glm::vec3 from(aim[3]), dir = glm::normalize(-glm::vec3(aim[2]));
        const glm::vec3 normal(m_vrPanelPose[2]), centre(m_vrPanelPose[3]);
        const float facing = glm::dot(dir, normal);
        if (facing < -1e-4f) {
            const float t = glm::dot(centre - from, normal) / facing;
            const glm::vec4 local = glm::inverse(m_vrPanelPose) * glm::vec4(from + dir * t, 1.0f);
            const float u = local.x / kPanelWidth + 0.5f, v = 0.5f - local.y / panelHeight;
            if (t > 0.0f && u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
                m_vrPointing = true;
                m_vrPointer = ImVec2(u * io.DisplaySize.x, v * io.DisplaySize.y);
            }
        }
    }
    if (m_vrPointing) {
        io.AddMousePosEvent(m_vrPointer.x, m_vrPointer.y);
        // The trigger is the mouse button on menus and buttons (else it uses your tool).
        const bool down = Vr::controls().useHeld && (m_vrOverUi || menus || m_vrPointerDown);
        if (down != m_vrPointerDown) io.AddMouseButtonEvent(0, down);
        m_vrPointerDown = down;
        // The pointer: a ring with a dot (brighter on something you can click).
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const ImU32 col = m_vrOverUi || menus ? IM_COL32(255, 255, 255, 240) : IM_COL32(255, 255, 255, 120);
        fg->AddCircle(m_vrPointer, 9.0f, IM_COL32(0, 0, 0, 140), 24, 4.0f);
        fg->AddCircle(m_vrPointer, 9.0f, col, 24, 2.0f);
        fg->AddCircleFilled(m_vrPointer, 2.5f, col);
    } else {
        if (m_vrPointerDown) io.AddMouseButtonEvent(0, false);
        m_vrPointerDown = false;
        if (wasPointing) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);   // (nothing stays lit up)
    }
}

void PlayerApp::vrAfterUi() {
    // The menus and buttons that were just drawn on the computer screen, again on
    // the headset's floating screen, without the picture of the world behind them.
    ImDrawData* dd = ImGui::GetDrawData();
    if (m_vrOn && m_page == Page::Game && dd && dd->Valid && dd->DisplaySize.x > 0.0f && dd->DisplaySize.y > 0.0f) {
        m_vrOverUi = m_vrOnGameUi || ImGui::IsAnyItemHovered();
        const int w = kPanelPixels, h = (int)std::lround(kPanelPixels * dd->DisplaySize.y / dd->DisplaySize.x);
        m_vrPanel.resize(w, h);
        const unsigned world = m_vrEyes[0].colorTexture();
        if (world)
            for (ImDrawList* list : dd->CmdLists)
                for (ImDrawCmd& cmd : list->CmdBuffer)
                    if (!cmd.UserCallback && cmd.GetTexID() == (ImTextureID)(intptr_t)world) cmd.ElemCount = 0;
        const ImVec2 scale = dd->FramebufferScale;
        dd->FramebufferScale = ImVec2((float)w / dd->DisplaySize.x, (float)h / dd->DisplaySize.y);
        GLint oldFbo = 0, oldViewport[4];
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
        glGetIntegerv(GL_VIEWPORT, oldViewport);
        m_vrPanel.bind();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(dd);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
        glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
        dd->FramebufferScale = scale;
        Vr::showPanel(m_vrPanel.colorTexture(), w, h, m_vrPanelPose, kPanelWidth);
    }
    Vr::endFrame();   // (does nothing when the headset isn't drawing)
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
