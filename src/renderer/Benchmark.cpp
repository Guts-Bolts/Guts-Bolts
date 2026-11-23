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
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

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

namespace {
// The test world: something like a real game. A floor, 144 blocks (some shiny, some
// glowing), 6 lamps.
void buildWorld(Scene& scene) {
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
}

// Milliseconds a frame with the settings `setup` makes (then the settings are put back).
// quick: a very slow computer has its answer after a frame or two (the startup benchmark).
float measure(SceneRenderer& renderer, Scene& scene, int w, int h, int frames, bool quick,
              const std::function<void(GraphicsSettings&)>& setup) {
    Camera cam;
    cam.resize(w, h);
    cam.distance = 40.0f;
    cam.pitch = 25.0f;
    GraphicsSettings& gs = GraphicsSettings::get();
    const GraphicsSettings saved = gs;
    setup(gs);
    const bool wasTemporal = renderer.temporal;
    renderer.temporal = true;
    Framebuffer fb;
    fb.resize(w, h);
    using clock = std::chrono::steady_clock;
    double total = 0.0, spent = 0.0;
    int counted = 0;
    for (int i = 0; i < frames; ++i) {
        if (quick && counted >= 1 && spent > 1500.0) break;
        cam.yaw = 30.0f + (float)i * 3.0f;   // (turning, like playing)
        const auto t0 = clock::now();
        renderer.render(scene, cam, fb, false);
        glFinish();
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        spent += ms;
        if (i >= 3 || (quick && i >= 1 && spent > 1500.0)) { total += ms; ++counted; }   // (the first few load shaders and fill buffers)
    }
    renderer.temporal = wasTemporal;
    gs = saved;
    return counted ? (float)(total / counted) : 0.0f;
}

// How much memory this program is using right now, in MB (0 where it can't tell). With
// a software graphics driver this includes the "graphics card" memory too.
float memoryMb() {
#if defined(__linux__)
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("VmRSS:", 0) == 0) return (float)std::atof(line.c_str() + 6) / 1024.0f;
#endif
    return 0.0f;
}
} // namespace

float run(SceneRenderer& renderer, int w, int h, int frames) {
    if (w <= 0 || h <= 0 || frames < 4) return 0.0f;
    Scene scene;
    buildWorld(scene);
    // At High (whatever the settings are now), then put back.
    return measure(renderer, scene, w, h, frames, true, [](GraphicsSettings& gs) { gs.applyPreset(GraphicsSettings::High); });
}

bool writeReport(SceneRenderer& renderer, const std::string& path) {
    int w = 1280, h = 720, frames = 12;
    if (const char* sz = std::getenv("GB_PERF_SIZE")) std::sscanf(sz, "%dx%d", &w, &h);
    if (const char* fr = std::getenv("GB_PERF_FRAMES")) frames = std::max(4, std::atoi(fr));
    Scene scene;
    buildWorld(scene);
    using GS = GraphicsSettings;
    using namespace Scalability;
    struct Line { const char* name; int fpsLimit; std::function<void(GS&)> setup; };
    auto preset = [](int q) { return [q](GS& gs) { gs.setDevice("", Desktop); gs.applyPreset(q); }; };
    auto phone = [](int q, bool saver) {
        return [q, saver](GS& gs) { gs.setDevice("", Phone); gs.setBatterySaver(saver ? GS::SaverOn : GS::SaverOff); gs.applyPreset(q); };
    };
    // At High on a computer, with one group turned down a step (or shadows off).
    auto without = [](int group, int level) {
        return [group, level](GS& gs) { gs.setDevice("", Desktop); gs.applyPreset(GS::High); gs.setGroup(group, level); };
    };
    const std::vector<Line> lines = {
        {"Very Low", 60, preset(GS::VeryLow)},
        {"Low", 60, preset(GS::Low)},
        {"Medium", 60, preset(GS::Medium)},
        {"High", 60, preset(GS::High)},
        {"Epic", 60, preset(GS::Ultra)},
        {"Phone: High (phone caps)", 60, phone(GS::High, false)},
        {"Phone: Low (old first-run default)", 60, phone(GS::Low, false)},
        {"Phone: High + battery saver", 30, phone(GS::High, true)},
        {"Phone: Low + battery saver", 30, phone(GS::Low, true)},
        {"High, shadows off", 60, [](GS& gs) { gs.setDevice("", Desktop); gs.applyPreset(GS::High); gs.shadows = false; }},
        {"High, shadows Low", 60, without(Shadow, 0)},
        {"High, lighting Low (no bounce light)", 60, without(GlobalIllumination, 0)},
        {"High, reflections Medium (no mirror reflections)", 60, without(Reflection, 1)},
        {"High, effects Medium (no light-catching fog, 3D clouds, sun rays)", 60, without(PostProcess, 1)},
        {"High, anti-aliasing Low (FXAA instead of TSR)", 60, without(AntiAliasing, 0)},
        {"High, resolution Medium (75% of the pixels)", 60, without(ScreenPercentage, 1)},
    };
    // A first pass to load every shader, so the first line isn't charged for it.
    for (const Line& l : lines) measure(renderer, scene, w, h, 4, false, l.setup);

    std::ofstream out(path);
    if (!out) return false;
    char buf[400];
    std::snprintf(buf, sizeof buf, "Guts&Bolts performance report\nGraphics chip: %s\nSize: %dx%d, %d frames a line\n\n",
                  gpuName().c_str(), w, h, frames);
    out << buf;
    std::snprintf(buf, sizeof buf, "%-66s %9s %8s %10s %9s\n", "Settings", "ms/frame", "max FPS", "busy at cap", "memory MB");
    out << buf;
    float highMs = 0.0f;
    for (const Line& l : lines) {
        const float ms = measure(renderer, scene, w, h, frames, false, l.setup);
        if (std::string(l.name) == "High") highMs = ms;
        // "busy at cap": how much of each second the graphics chip spends drawing at that
        // line's frame limit (60, or battery saver's 30). Less busy = less battery and heat.
        const float busy = std::min(100.0f, ms * (float)l.fpsLimit / 10.0f);
        std::snprintf(buf, sizeof buf, "%-66s %9.2f %8.0f %7.0f%% @%d %9.0f\n", l.name, ms, ms > 0 ? 1000.0f / ms : 0.0f,
                      busy, l.fpsLimit, memoryMb());
        out << buf;
        std::printf("%s", buf);
    }
    std::snprintf(buf, sizeof buf, "\n(High took %.2f ms a frame.)\n", highMs);
    out << buf;
    return true;
}

} // namespace GraphicsBenchmark
