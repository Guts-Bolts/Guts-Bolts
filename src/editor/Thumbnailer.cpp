#include "Thumbnailer.h"
#include "../renderer/Camera.h"
#include "../renderer/Framebuffer.h"
#include "../renderer/GL.h"
#include "../renderer/SceneRenderer.h"
#include "../scene/Physics.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Serializer.h"
#include <stb_image_write.h>   // (its code is in renderer/Textures.cpp)

#include <algorithm>
#include <cmath>

namespace {
// An empty world with even, soft light and a plain light background.
void makeStudio(Scene& scene) {
    std::vector<SceneNode*> old;
    for (auto& c : scene.root()->children) old.push_back(c.get());
    for (SceneNode* n : old) scene.removeNode(n);
    Environment& e = scene.environment();
    const glm::vec3 bg(0.93f, 0.93f, 0.94f);   // light, like the Toolbox's tiles
    e.skyZenith = e.skyHorizon = e.skyGround = bg;
    e.clouds = false;
    e.stars = false;
    e.sunSize = 0.0f;
    e.sunAzimuth = 120.0f;
    e.sunElevation = 50.0f;
    e.sunIntensity = 1.5f;
    e.shadows = false;
    e.fogEnabled = false;
    e.ambientColor = {0.75f, 0.78f, 0.85f};
    e.groundAmbient = {0.45f, 0.43f, 0.40f};
    e.ambientIntensity = 0.55f;
    e.reflections = 0.5f;
    e.vignette = 0.0f;
    e.bloomIntensity = 0.2f;
    e.aoIntensity = 0.6f;
}

// A world matrix as position / rotation (degrees, Z*Y*X) / scale.
Transform fromMatrix(const glm::mat4& m) {
    Transform t;
    t.position = glm::vec3(m[3]);
    for (int i = 0; i < 3; ++i) t.scale[i] = glm::length(glm::vec3(m[i]));
    glm::mat3 R(glm::vec3(m[0]) / t.scale.x, glm::vec3(m[1]) / t.scale.y, glm::vec3(m[2]) / t.scale.z);
    float sy = std::clamp(-R[0][2], -1.0f, 1.0f);
    t.rotation.y = glm::degrees(std::asin(sy));
    if (std::abs(sy) < 0.9999f) {
        t.rotation.x = glm::degrees(std::atan2(R[1][2], R[2][2]));
        t.rotation.z = glm::degrees(std::atan2(R[0][1], R[0][0]));
    } else {
        t.rotation.x = glm::degrees(std::atan2(-R[2][1], R[1][1]));
        t.rotation.z = 0.0f;
    }
    return t;
}

// Everything solid in the scene, as one box.
bool bounds(Scene& scene, glm::vec3& lo, glm::vec3& hi) {
    lo = glm::vec3(1e9f); hi = glm::vec3(-1e9f);
    scene.forEach([&](SceneNode* n) {
        if (!n->isPart() || !n->visible) return;
        AABB b = Physics::worldBounds(n);
        lo = glm::min(lo, b.min); hi = glm::max(hi, b.max);
    });
    return lo.x <= hi.x;
}
} // namespace

Thumbnailer::Thumbnailer() = default;
Thumbnailer::~Thumbnailer() = default;

void Thumbnailer::shoot(Scene& scene, Framebuffer& fb, int size) {
    if (!m_renderer) m_renderer = std::make_unique<SceneRenderer>();
    glm::vec3 lo, hi;
    if (!bounds(scene, lo, hi)) { lo = glm::vec3(-1.0f); hi = glm::vec3(1.0f); }
    // Flat things (a plane, a decal-sized part) still get a sensible frame.
    const float radius = std::max(0.6f, glm::length(hi - lo) * 0.5f);
    Camera cam;
    cam.resize(size, size);
    cam.pivot = (lo + hi) * 0.5f;
    cam.yaw = 35.0f;
    cam.pitch = 25.0f;
    cam.distance = radius / std::sin(glm::radians(cam.fov * 0.5f)) * 1.02f;
    fb.resize(size, size);
    m_renderer->render(scene, cam, fb, false);
}

std::string Thumbnailer::toPng(Framebuffer& fb) { return fb.toPng(); }

std::string Thumbnailer::png(const std::function<void(Scene&)>& build, int size) {
    Scene scene;
    makeStudio(scene);
    if (build) build(scene);
    Framebuffer fb;
    shoot(scene, fb, size);
    return toPng(fb);
}

std::string Thumbnailer::png(const std::vector<const SceneNode*>& nodes, int size) {
    return png([&](Scene& scene) {
        for (const SceneNode* n : nodes)
            if (auto copy = Serializer::nodeFromString(Serializer::nodeToString(*n), true)) {
                // Keep where it is in the world (a part inside a Model follows its parent).
                if (n->parent) copy->transform = fromMatrix(n->worldMatrix());
                scene.insert(std::move(copy));
            }
    }, size);
}

unsigned Thumbnailer::texture(const std::string& key, const std::function<void(Scene&)>& build, int size) {
    auto it = m_textures.find(key);
    if (it != m_textures.end()) return it->second->colorTexture();
    if (m_madeThisFrame >= 2) return 0;   // the rest next frame, so Studio stays smooth
    ++m_madeThisFrame;
    Scene scene;
    makeStudio(scene);
    if (build) build(scene);
    auto fb = std::make_unique<Framebuffer>();
    shoot(scene, *fb, size);
    unsigned tex = fb->colorTexture();
    m_textures[key] = std::move(fb);
    return tex;
}

unsigned Thumbnailer::live(const std::string& slot, const std::string& key, const std::function<void(Scene&)>& build, int size) {
    Live& l = m_live[slot];
    if (l.fb && l.key == key) return l.fb->colorTexture();
    if (m_madeThisFrame >= 2) return l.fb ? l.fb->colorTexture() : 0;   // (the old one until next frame)
    ++m_madeThisFrame;
    Scene scene;
    makeStudio(scene);
    if (build) build(scene);
    if (!l.fb) l.fb = std::make_unique<Framebuffer>();
    shoot(scene, *l.fb, size);
    l.key = key;
    return l.fb->colorTexture();
}
