// The graphics card picks what to draw (step 7d of the photoreal upgrade,
// src/renderer/GpuCulling.h). The shader itself needs a graphics card, so this checks
// the parts around it: the lists' layout, grouping by mesh, that its maths picks the
// same parts and detail levels as the old way, that only what changed is sent, and
// which devices get it.
//
//   GpuCullingTests
#include "core/DeviceFeatures.h"
#include "renderer/Frustum.h"
#include "renderer/GpuCulling.h"
#include "renderer/MeshLibrary.h"
#include "renderer/MeshLod.h"

#include <glm/gtc/matrix_transform.hpp>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <vector>

// Every `new` in this program goes through here and is counted.
static std::atomic<long> g_news{0};
void* operator new(size_t n) {
    ++g_news;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

GpuCulling::Instance part(glm::vec3 at, glm::vec3 size, float seed = 0.0f) {
    GpuCulling::Instance i;
    const glm::mat4 m = glm::scale(glm::translate(glm::mat4(1.0f), at), size);
    i.draw = {m, glm::vec4(1, 0, 0, 1), glm::vec4(1, 0, 0, seed)};
    i.sphere = glm::vec4(at, 0.5f * glm::length(size));
    return i;
}

// A tiny random number maker (the same numbers every run).
uint32_t s_seed = 12345;
float rnd(float lo, float hi) {
    s_seed = s_seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)(s_seed >> 8) / 16777216.0f;
}
} // namespace

int main() {
    Mesh::headless = true;

    // --- The lists' layout (the shader reads them byte for byte) ---
    check(sizeof(GpuCulling::Instance) == 144 && sizeof(GpuCulling::MeshEntry) == 32 && sizeof(GpuCulling::Command) == 20,
          "the lists are laid out the way the shader reads them");
    check(std::string(GpuCulling::kShader).find("atomicAdd") != std::string::npos,
          "the shader counts the copies it keeps");

    // --- Grouping by mesh ---
    auto cube = MeshLibrary::get(PrimitiveType::Cube);
    auto ball = MeshLibrary::get(PrimitiveType::Sphere);
    auto cyl = MeshLibrary::get(PrimitiveType::Cylinder);
    std::vector<GpuCulling::Candidate> parts;
    for (int i = 0; i < 10; ++i) {
        const Mesh* m = i % 3 == 0 ? cube.get() : i % 3 == 1 ? ball.get() : cyl.get();
        parts.push_back({m, part({i * 2.0f, 0, 0}, {1, 1, 1}, (float)i)});
    }
    GpuCulling::Plan plan;
    GpuCulling::plan(parts, plan);
    check(plan.meshes.size() == 3 && plan.meshPtrs[0] == cube.get() && plan.meshPtrs[1] == ball.get() && plan.meshPtrs[2] == cyl.get(),
          "one entry per mesh, in the order they were first seen");
    check(plan.meshes[0].capacity == 4 && plan.meshes[1].capacity == 3 && plan.meshes[2].capacity == 3,
          "each mesh knows how many parts use it");
    bool grouped = true, inOrder = true;
    for (size_t i = 0; i < plan.instances.size(); ++i) {
        const uint32_t want = i < 4 ? 0u : i < 7 ? 1u : 2u;
        grouped = grouped && plan.instances[i].mesh == want;
        if (i > 0 && plan.instances[i].mesh == plan.instances[i - 1].mesh)
            inOrder = inOrder && plan.instances[i].draw.info.w > plan.instances[i - 1].draw.info.w;
    }
    check(plan.instances.size() == 10 && grouped, "the parts are grouped by mesh");
    check(inOrder, "within a mesh, parts keep the order they came in");
    bool cmdsOk = plan.commands.size() == 3;
    uint32_t region = 0;
    for (size_t m = 0; cmdsOk && m < plan.meshes.size(); ++m) {
        const GpuCulling::MeshEntry& e = plan.meshes[m];
        const GpuCulling::Command& c = plan.commands[e.firstCommand];
        cmdsOk = e.levels == 1 && e.base == region && c.baseInstance == region && c.instanceCount == 0 &&
                 c.count == (uint32_t)plan.meshPtrs[m]->indexCount();
        region += e.capacity * e.levels;
    }
    check(cmdsOk, "each mesh's draw record starts at 0 copies, with its own room in the output");
    check(plan.outputSize == 10, "the output has room for every part");
    {
        const long before = g_news;
        for (int i = 0; i < 50; ++i) GpuCulling::plan(parts, plan);
        const bool none = g_news == before;   // (worked out before check() makes its message)
        check(none, "planning again asks for no memory");
    }
    {
        std::vector<GpuCulling::Candidate> none;
        GpuCulling::plan(none, plan);
        check(plan.instances.empty() && plan.commands.empty() && plan.outputSize == 0, "nothing to draw makes an empty plan");
    }

    // --- The shader's checks agree with the old way's ---
    {
        const glm::mat4 proj = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
        const glm::mat4 viewM = glm::lookAt(glm::vec3(3, 8, 20), glm::vec3(0, 2, 0), glm::vec3(0, 1, 0));
        const Frustum f(proj * viewM);
        GpuCulling::View v;
        for (int i = 0; i < 5; ++i) v.planes[i] = f.planes[i];
        v.frustumOn = f.on;
        v.viewPos = glm::vec3(3, 8, 20);
        v.viewDist = 120.0f;
        int agree = 0, total = 0, seenCount = 0;
        for (int k = 0; k < 4000; ++k) {
            const glm::vec3 at(rnd(-200, 200), rnd(-50, 80), rnd(-200, 200));
            const glm::vec3 size(rnd(0.2f, 12), rnd(0.2f, 12), rnd(0.2f, 12));
            const GpuCulling::Instance i = part(at, size);
            const float r = i.sphere.w;
            // The old way (SceneRenderer::drawGeometry): render distance, then the frustum.
            const bool far = glm::length(at - v.viewPos) - r > v.viewDist;
            const bool old = !far && f.sees(at, r * 1.5f + 1.0f);
            const bool gpu = GpuCulling::visible(i, v);
            agree += old == gpu;
            seenCount += gpu;
            ++total;
        }
        check(agree == total, "on screen and in range: the same parts as before (" + std::to_string(agree) + " of " + std::to_string(total) + ")");
        check(seenCount > 100 && seenCount < total - 100, "and the test really has parts on and off screen");
        GpuCulling::View blind = v;
        blind.frustumOn = false;
        blind.viewDist = 0.0f;
        check(GpuCulling::visible(part({0, -9000, 0}, {1, 1, 1}), blind), "with no camera box and no render distance, everything is drawn");
    }
    {
        // Detail levels: the same as MeshLod::pick with the same numbers.
        GpuCulling::MeshEntry e;
        e.levels = 1 + MeshLod::kLevels;
        const std::vector<float> px(MeshLod::kPixels, MeshLod::kPixels + MeshLod::kLevels);
        for (int l = 0; l < MeshLod::kLevels; ++l) e.lodPixels[l] = px[l];
        GpuCulling::View v;
        v.viewPos = glm::vec3(0);
        v.pixelsPerStud = 900.0f;
        bool same = true, usedAll[4] = {false, false, false, false};
        for (float bias : {0.5f, 1.0f, 2.0f}) {
            v.bias = bias;
            for (int k = 0; k < 2000; ++k) {
                const glm::vec3 at(rnd(-600, 600), rnd(0, 50), rnd(-600, 600));
                const GpuCulling::Instance i = part(at, glm::vec3(rnd(0.5f, 6)));
                const float want = MeshLod::pixelsAcross(i.sphere.w, glm::length(at), v.pixelsPerStud);
                const int old = MeshLod::pick(want, bias, px);
                const int gpu = GpuCulling::pickLevel(i, e, v);
                same = same && old == gpu;
                usedAll[std::min(gpu, 3)] = true;
            }
        }
        check(same, "detail levels: the same level as before, at every graphics setting");
        check(usedAll[0] && usedAll[1] && usedAll[2] && usedAll[3], "and every level gets used somewhere");
        v.pixelsPerStud = 0.0f;
        check(GpuCulling::pickLevel(part({500, 0, 0}, {1, 1, 1}), e, v) == 0, "detail levels off: always the full mesh");
        v.pixelsPerStud = 900.0f;
        v.ortho = true;
        const int a = GpuCulling::pickLevel(part({5, 0, 0}, {1, 1, 1}), e, v), b = GpuCulling::pickLevel(part({500, 0, 0}, {1, 1, 1}), e, v);
        check(a == b, "orthographic: the level doesn't change with distance");
        GpuCulling::MeshEntry plain;
        check(GpuCulling::pickLevel(part({900, 0, 0}, {1, 1, 1}), plain, v) == 0, "a mesh with no simpler copies is always full");
    }

    // --- Only what changed is sent ---
    {
        std::vector<GpuCulling::Instance> a, b;
        for (int i = 0; i < 100; ++i) a.push_back(part({i * 1.0f, 0, 0}, {1, 1, 1}));
        b = a;
        size_t first = 0, last = 0;
        GpuCulling::changed(a, b, first, last);
        check(first == last, "nothing moved: nothing is sent");
        b[40].draw.color.g = 0.5f;
        b[42].sphere.x += 1.0f;
        GpuCulling::changed(a, b, first, last);
        check(first == 40 && last == 43, "two parts changed: just the stretch between them is sent");
        b.push_back(part({0, 9, 0}, {1, 1, 1}));
        GpuCulling::changed(a, b, first, last);
        check(first == 40 && last == 101, "a part was added: from the first change to the end");
        b = a;
        b.pop_back();
        GpuCulling::changed(a, b, first, last);
        check(first == 99 && last == 99, "a part was taken off the end: nothing new to send");
        std::vector<GpuCulling::Instance> empty;
        GpuCulling::changed(empty, a, first, last);
        check(first == 0 && last == 100, "the first time, everything is sent");
    }

    // --- Which devices get it ---
    {
        using namespace DeviceFeatures;
        const GpuCaps pc = GpuCaps::from(46, false, "", 16);
        const GpuCaps oldPc = GpuCaps::from(42, false, "", 16);
        const GpuCaps mac = GpuCaps::from(41, false, "", 0);
        const GpuCaps phone = GpuCaps::from(32, true, "GL_EXT_multi_draw_indirect", 8);
        const GpuCaps web = GpuCaps::from(30, true, "", 0);
        const GpuCaps safe = GpuCaps::from(46, false, "", 16, true);
        check(can(pc, DeviceFeatures::GpuCulling), "a computer with OpenGL 4.3+ lets the graphics card pick");
        check(!can(oldPc, DeviceFeatures::GpuCulling) && !can(mac, DeviceFeatures::GpuCulling), "older cards and Macs pick on the processor");
        check(!can(phone, DeviceFeatures::GpuCulling) && !can(web, DeviceFeatures::GpuCulling), "phones and the web Player pick on the processor");
        check(!can(safe, DeviceFeatures::GpuCulling), "Safe mode picks on the processor");
        check(info(DeviceFeatures::GpuCulling).shipped, "it's in the game, so Settings mentions it to devices that can't");
        Feature miss[kFeatures];
        bool listed = false;
        for (int i = 0, n = missing(mac, miss); i < n; ++i) listed = listed || miss[i] == DeviceFeatures::GpuCulling;
        check(listed, "a Mac's notice lists it");
    }

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
