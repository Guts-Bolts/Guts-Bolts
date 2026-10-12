#include "PlayCamera.h"
#include "../renderer/Camera.h"
#include "../scene/Player.h"
#include "../scene/Scene.h"
#include "../scene/Physics.h"
#include "../scene/SceneNode.h"
#include "../core/Settings.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace PlayCamera {

namespace {
constexpr float kSnapIn  = 1.2f;   // closer than this = first person
constexpr float kSnapOut = 1.6f;   // scrolling out of first person lands here
constexpr float kFadeStart = 2.4f; // the character starts fading this close
constexpr float kFadeEnd   = 0.7f; // and is gone by here

// The zoom range the player's options allow. A range that ends closer than first
// person snaps (or LockFirstPerson) means first person only.
struct Range { float lo, hi; bool firstPersonOnly; };
Range rangeOf(const CameraOptions& o) {
    const float lo = std::clamp(o.minZoom, 0.0f, CameraOptions::kLimit);
    const float hi = std::clamp(o.maxZoom, lo, CameraOptions::kLimit);
    return {lo, hi, o.lockFirstPerson || hi < kSnapIn};
}

// A script set the camera's CFrame: look the way it looks (kept when the camera
// goes back to following the character, like Roblox).
void aimAlong(Camera& cam, const glm::mat4& cf) {
    glm::vec3 f = -glm::vec3(cf[2]);
    if (glm::length(f) < 1e-6f) return;
    f = glm::normalize(f);
    cam.pitch = std::clamp(glm::degrees(std::asin(std::clamp(-f.y, -1.0f, 1.0f))), -89.0f, 89.0f);
    if (f.x * f.x + f.z * f.z > 1e-8f) cam.yaw = glm::degrees(std::atan2(-f.z, -f.x));
}

// The point the camera looks at for a CameraSubject that isn't our own character:
// a character's head (or its middle), or a part's centre.
bool subjectPoint(Scene& scene, SceneNode* n, bool humanoid, glm::vec3& out) {
    if (!n) return false;
    if (humanoid) {
        if (SceneNode* h = n->findChild("Head", true)) { out = glm::vec3(h->worldMatrix()[3]); return true; }
        for (const char* body : {"HumanoidRootPart", "Torso", "UpperTorso"})
            if (SceneNode* t = n->findChild(body, true)) { out = glm::vec3(t->worldMatrix()[3]) + glm::vec3(0, 1.5f, 0); return true; }
    }
    out = glm::vec3(n->worldMatrix()[3]);
    (void)scene;
    return true;
}

// Is the camera following our own character (not some other subject a script picked)?
bool followsMe(Scene* scene, const Player& player) {
    if (!scene) return true;
    const ScriptCamera& sc = scene->camera();
    return sc.subject == 0 || (sc.subjectIsHumanoid && sc.subject == player.rootId());
}

// Tell scripts where the camera really is (workspace.CurrentCamera.CFrame etc.).
void publish(const Camera& cam, ScriptCamera& sc, bool keepCFrame) {
    if (!keepCFrame) sc.cframe = glm::inverse(cam.view());
    if (sc.type == CameraType::Custom) sc.focus = cam.pivot;
    sc.viewport = glm::vec2((float)std::max(1, cam.width()), (float)std::max(1, cam.height()));
    sc.orthoHeight = cam.orthographic ? cam.viewHeight() : 0.0f;
}
}

bool firstPerson(const Camera& cam) { return !cam.useCFrame && cam.distance < 0.01f; }
float swimLook(const Camera& cam) {
    const float y = cam.forward().y;
    return std::clamp(firstPerson(cam) ? y : y + 0.4f, -1.0f, 1.0f);
}

void zoom(Camera& cam, float wheel, const CameraOptions& options) {
    if (wheel == 0.0f || cam.useCFrame) return;   // (a script has the camera)
    const Range r = rangeOf(options);
    if (cam.orthographic) {   // no first person with no perspective: just closer / further
        cam.distance -= wheel * std::max(0.4f, cam.distance * 0.15f);
        cam.distance = std::clamp(cam.distance, std::max(kSnapOut, r.lo), std::max(kSnapOut, r.hi));
        return;
    }
    if (r.firstPersonOnly) { cam.distance = 0.0f; return; }
    if (firstPerson(cam)) {
        if (wheel < 0.0f) cam.distance = std::clamp(kSnapOut, r.lo, r.hi);
        return;
    }
    cam.distance -= wheel * std::max(0.4f, cam.distance * 0.15f);
    cam.distance = std::min(cam.distance, r.hi);
    if (cam.distance < kSnapIn && r.lo < kSnapIn) cam.distance = 0.0f;   // all the way in: first person
    else cam.distance = std::max(cam.distance, r.lo);
}

void turn(Camera& cam, float dx, float dy) {
    if (cam.noInput) return;   // CameraType Scriptable: only scripts move it
    const GraphicsSettings& gs = GraphicsSettings::get();
    cam.orbit(dx * gs.mouseSensitivity, (gs.invertCamera ? -dy : dy) * gs.mouseSensitivity);
}

// Like Roblox's camera: if a solid part is between the character's head and the
// camera, the camera comes in to just in front of it (so you never look through a
// wall), and slides back out once the way is clear. See-through parts, parts you
// can walk through, water and people don't count.
void keepOutOfWalls(Camera& cam, Player& player, float dt, const SceneNode* subject) {
    // (Orthographic: walls between don't hide anything, the view isn't from a point.)
    if (firstPerson(cam) || cam.distance <= 0.0f || cam.orthographic) { cam.clip = -1.0f; return; }
    Scene* scene = player.scene();
    if (!scene) return;
    SceneNode* me = player.root();
    const glm::vec3 from = cam.pivot;
    const glm::vec3 back = -cam.forward();   // from the head towards the camera
    float hit = 0.0f;
    SceneNode* wall = Physics::raycastIf(*scene, from, back, &hit, [&](const SceneNode* n) {
        if (n == me || n == subject) return false;          // ourselves (and everything we hold) / what we watch
        if (scene->isCharacterRoot(n->id)) return false;    // other players
        if (!n->isPart()) return true;                      // models: look inside them
        return n->canCollide && n->transparency < 0.25f && !Player::isWater(n);
    });
    constexpr float kMargin = 0.35f;   // stay a little in front of the wall (the near plane)
    float want = wall && hit < cam.distance + kMargin ? std::max(0.2f, hit - kMargin) : cam.distance;
    float now = cam.shownDistance();
    if (want < now) now = want;                                        // in: at once
    else now += (want - now) * std::min(1.0f, dt * 6.0f);             // out: smoothly
    cam.clip = now >= cam.distance - 0.01f ? -1.0f : now;
}

void follow(Camera& cam, Player& player, float dt, bool shiftLock) {
    Scene* scene = player.scene();
    if (scene) {   // the game's camera setting (Workspace.Orthographic)
        cam.orthographic = scene->world().orthographic;
        cam.orthoSize = scene->world().orthographicSize;
        if (cam.orthographic && cam.distance < kSnapOut) cam.distance = 12.0f;   // out of first person
    }

    // What the game's scripts want (workspace.CurrentCamera).
    ScriptCamera* sc = scene ? &scene->camera() : nullptr;
    if (sc) {
        cam.fov = sc->fov;
        if (sc->cframeSet) {
            aimAlong(cam, sc->cframe);
            if (sc->type == CameraType::Fixed) sc->fixedAt = glm::vec3(sc->cframe[3]);
            sc->cframeSet = false;
        }
        cam.noInput = sc->type == CameraType::Scriptable;
        if (sc->type == CameraType::Scriptable) {
            // Hands off: show exactly what the script set.
            cam.useCFrame = true;
            cam.cframe = sc->cframe;
            cam.clip = -1.0f;
            cam.pivot = cam.position() + cam.forward() * 8.0f;   // (shadows are drawn around here)
            publish(cam, *sc, true);
            return;
        }
        if (sc->type == CameraType::Fixed) {
            // It stays put; the mouse can still turn it where it stands.
            cam.useCFrame = false;
            const glm::vec3 at = sc->fixedAt;
            cam.cframe = glm::inverse(glm::lookAt(at, at + cam.forward(), glm::vec3(0, 1, 0)));
            cam.useCFrame = true;
            cam.clip = -1.0f;
            cam.pivot = at + cam.forward() * 8.0f;
            publish(cam, *sc, false);
            return;
        }
        cam.useCFrame = false;
        // The player's zoom limits and CameraMode (scripts can change them any time).
        const Range r = rangeOf(scene->cameraOptions());
        if (cam.orthographic) cam.distance = std::clamp(cam.distance, std::max(kSnapOut, r.lo), std::max(kSnapOut, r.hi));
        else if (r.firstPersonOnly) cam.distance = 0.0f;
        else if (firstPerson(cam)) { if (r.lo >= kSnapIn) cam.distance = r.lo; }   // not allowed this close
        else cam.distance = std::clamp(cam.distance, r.lo, r.hi);
    }

    // What it follows: our head, or the CameraSubject a script picked.
    const bool mine = followsMe(scene, player);
    glm::vec3 target = player.focusPoint();
    SceneNode* subject = nullptr;
    if (!mine) {
        subject = scene->findById(sc->subject);
        if (!subjectPoint(*scene, subject, sc->subjectIsHumanoid, target)) {
            sc->subject = 0;   // it's gone: back to our own character
            sc->subjectIsHumanoid = true;
            subject = nullptr;
            target = player.focusPoint();
        }
    }
    if (shiftLock && !firstPerson(cam)) {
        // Over the right shoulder (Roblox moves the camera 1.75 studs right).
        glm::vec3 f = cam.forward();
        glm::vec3 right = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)) + glm::vec3(1e-6f));
        target += right * 0.875f;
        if (mine && !player.isDead() && f.x * f.x + f.z * f.z > 1e-6f) player.faceYaw(glm::degrees(std::atan2(f.x, f.z)));
    }
    if (firstPerson(cam)) {
        cam.pivot = target;   // right in the head, no lag
        if (mine && !player.isDead()) {
            glm::vec3 f = cam.forward();
            if (f.x * f.x + f.z * f.z > 1e-6f) player.faceYaw(glm::degrees(std::atan2(f.x, f.z)));
        }
    } else {
        cam.pivot += (target - cam.pivot) * std::min(1.0f, dt * 12.0f);
    }
    keepOutOfWalls(cam, player, dt, subject);
    // Explosions shake the camera as their shockwave rolls past (Blast.h).
    if (scene) {
        const float shake = scene->blasts().shake(cam.pivot);
        if (shake > 0.001f) {
            static float t = 0.0f;
            t += dt * 37.0f;
            const glm::vec3 j(std::sin(t * 1.3f) + std::sin(t * 2.9f) * 0.5f, std::sin(t * 1.7f + 1.0f) + std::sin(t * 3.3f) * 0.5f,
                              std::sin(t * 1.1f + 2.0f));
            cam.pivot += j * shake * 0.12f;
            cam.pitch += std::sin(t * 2.3f) * shake * 0.8f;
            cam.yaw += std::sin(t * 1.9f + 0.5f) * shake * 0.8f;
        }
    }
    if (sc) publish(cam, *sc, false);
}

void fade(Scene& scene, Player& player, const Camera& cam) {
    SceneNode* root = player.root();
    if (!root) return;
    // How close the camera is to our head (closer when a wall pulls it in). When a
    // script has the camera, or it watches something else, measure it for real.
    float d;
    if (cam.useCFrame || !followsMe(&scene, player)) d = glm::length(cam.position() - player.focusPoint());
    else d = firstPerson(cam) ? 0.0f : cam.shownDistance();
    float t = std::clamp((kFadeStart - d) / (kFadeStart - kFadeEnd), 0.0f, 1.0f);
    if (player.isDead() || cam.orthographic) t = 0.0f;   // watch yourself fall apart / never in your head
    SceneNode* tool = player.equippedTool();
    std::vector<SceneNode*> stack{root};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n == tool) continue;     // you still see what you're holding
        n->localTransparency = t;
        for (auto& c : n->children) stack.push_back(c.get());
    }
}

} // namespace PlayCamera
