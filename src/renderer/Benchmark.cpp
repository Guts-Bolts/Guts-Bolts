#include "Benchmark.h"
#include "SceneRenderer.h"
#include "Camera.h"
#include "Framebuffer.h"
#include "MeshLibrary.h"
#include "GL.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../core/Settings.h"
#include <algorithm>
#include <chrono>
#include <memory>

namespace GraphicsBenchmark {

std::string gpuName() {
    const char* g = (const char*)glGetString(GL_RENDERER);
    return g ? g : "";
}

namespace {
SceneNode* block(Scene& scene, const glm::vec3& pos, const glm::vec3& size, const glm::vec3& color, Material m) {
    auto n = std::make_unique<SceneNode>("Block");
    n->primitiveType = PrimitiveType::Cube;
    n->mesh = MeshLibrary::get(PrimitiveType::Cube);
    n->transform.position = pos;
    n->transform.scale = size;
    n->color = color;
    n->material = m;
    return scene.insert(std::move(n));
}
} // namespace

float run(SceneRenderer& renderer, int w, int h, int frames) {
    if (w <= 0 || h <= 0 || frames < 4) return 0.0f;
    // Something like a real game: a floor, 144 blocks (some shiny, some glowing), 6 lamps.
    Scene scene;
    block(scene, {0, -0.5f, 0}, {120, 1, 120}, {0.35f, 0.55f, 0.3f}, Material::Grass);
    static const Material kinds[] = {Material::Plastic, Material::Metal, Material::Wood, Material::Concrete, Material::Brick, Material::Neon};
    for (int z = 0; z < 12; ++z)
        for (int x = 0; x < 12; ++x) {
            const float hgt = 1.0f + (float)((x * 7 + z * 3) % 5);
            block(scene, {x * 5.0f - 27.5f, hgt * 0.5f, z * 5.0f - 27.5f}, {2.5f, hgt, 2.5f},
                  {0.3f + 0.05f * (float)(x % 6), 0.35f, 0.3f + 0.05f * (float)(z % 6)}, kinds[(x + z) % 6]);
        }
    for (int i = 0; i < 6; ++i) {
        auto l = std::make_unique<SceneNode>("Lamp", NodeKind::Light);
        l->transform.position = {i * 9.0f - 22.0f, 4.0f, (i % 2) * 10.0f - 5.0f};
        l->color = {1.0f, 0.8f, 0.6f};
        l->range = 16.0f;
        scene.insert(std::move(l));
    }
    Camera cam;
    cam.resize(w, h);
    cam.distance = 40.0f;
    cam.pitch = 25.0f;

    // At High (whatever the settings are now), then put back.
    GraphicsSettings& gs = GraphicsSettings::get();
    const GraphicsSettings saved = gs;
    gs.applyPreset(GraphicsSettings::High);
    const bool wasTemporal = renderer.temporal;
    renderer.temporal = true;
    Framebuffer fb;
    fb.resize(w, h);
    using clock = std::chrono::steady_clock;
    double total = 0.0, spent = 0.0;
    int counted = 0;
    for (int i = 0; i < frames; ++i) {
        // A very slow computer has its answer after a frame or two: stop there
        // instead of making it wait seconds for the game to open.
        if (counted >= 1 && spent > 1500.0) break;
        cam.yaw = 30.0f + (float)i * 3.0f;   // (turning, like playing)
        const auto t0 = clock::now();
        renderer.render(scene, cam, fb, false);
        glFinish();
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        spent += ms;
        if (i >= 3 || (i >= 1 && spent > 1500.0)) { total += ms; ++counted; }   // (the first few load shaders and fill buffers)
    }
    renderer.temporal = wasTemporal;
    gs = saved;
    return counted ? (float)(total / counted) : 0.0f;
}

} // namespace GraphicsBenchmark
