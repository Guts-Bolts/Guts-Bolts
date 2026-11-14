// Groundwork for the photoreal upgrade:
//  * frame memory (src/core/FrameArena.h): lists that last one frame come out of one
//    reused block, so drawing a frame doesn't ask the system for memory;
//  * what each device can do (src/core/DeviceFeatures.h): one place decides which
//    features a Mac, an iPhone, the web Player or an old graphics card has to skip.
//
//   GroundworkTests
#include "core/DeviceFeatures.h"
#include "core/FrameArena.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
struct Light { float pos[4]; float col[4]; const void* node; };
} // namespace

int main() {
    using namespace DeviceFeatures;

    // --- Frame memory ---
    {
        FrameArena arena(1024);
        void* a = arena.allocate(10, 1);
        void* b = arena.allocate(64, 64);
        check(a && b && a != b, "two pieces are two different places");
        check((reinterpret_cast<uintptr_t>(b) & 63) == 0, "a piece is lined up as asked (64)");
        check(arena.used() >= 74 && arena.used() <= 1024, "it counts what was handed out");
        arena.reset();
        check(arena.used() == 0, "a new frame starts empty");
        check(arena.allocate(10, 1) == a, "...and hands out the same memory again");
        check(arena.timesGrown() == 0, "nothing was asked from the system");
    }
    {
        // A frame that doesn't fit still works, and the block grows once so the next does fit.
        FrameArena arena(256);
        FrameList<Light> lights(arena, 2);
        for (int i = 0; i < 500; ++i) { Light l{}; l.pos[0] = (float)i; lights.push_back(l); }
        bool right = lights.size() == 500;
        for (int i = 0; i < 500 && right; ++i) right = lights[(size_t)i].pos[0] == (float)i;
        check(right, "a list that outgrows the block keeps everything (500 lights)");
        check(arena.overflowed(), "...by borrowing from the system that one frame");
        arena.reset();
        check(arena.timesGrown() == 1 && !arena.overflowed(), "the next frame the block is bigger");
        const size_t room = arena.capacity();
        for (int frame = 0; frame < 20; ++frame) {
            arena.reset();
            FrameList<Light> again(arena, 2);
            for (int i = 0; i < 500; ++i) again.push_back(Light{});
        }
        check(arena.timesGrown() == 1 && arena.capacity() == room && !arena.overflowed(),
              "20 more frames the same size: never grows or borrows again");
    }
    {
        FrameArena arena;
        FrameList<int> list(arena);
        check(list.empty(), "a new list is empty");
        for (int i = 0; i < 40; ++i) list.push_back(i);
        list.resize(10);
        int sum = 0;
        for (int v : list) sum += v;
        check(list.size() == 10 && sum == 45, "resize keeps the first ones; for-each works");
        list.resize(12);
        check(list[10] == 0 && list[11] == 0, "new places from resize are zero");
    }

    // --- What each device can do ---
    const GpuCaps pc      = GpuCaps::from(46, false, "GL_ARB_multi_draw_indirect", 16);
    const GpuCaps mac     = GpuCaps::from(41, false, "", 0);
    const GpuCaps iphone  = GpuCaps::from(30, true, "GL_EXT_color_buffer_half_float GL_KHR_texture_compression_astc_ldr", 0);
    const GpuCaps web     = GpuCaps::from(30, true, "GL_EXT_color_buffer_float", 0);
    const GpuCaps android = GpuCaps::from(32, true, "GL_EXT_color_buffer_float GL_KHR_texture_compression_astc_ldr", 24);
    const GpuCaps weakPhone = GpuCaps::from(31, true, "", 4);
    const GpuCaps safe    = GpuCaps::from(46, false, "", 16, true);

    check(pc.compute && pc.indirectDraw && pc.multiDrawIndirect, "a Windows / Linux PC (OpenGL 4.6) has compute shaders");
    check(pc.bc7 && pc.bc5, "...and reads BC7 / BC5 textures");
    check(!mac.compute && mac.indirectDraw && !mac.multiDrawIndirect, "a Mac (OpenGL 4.1) has no compute shaders");
    check(!mac.bc7 && mac.bc5, "a Mac reads BC5 but not BC7");
    check(!iphone.compute && iphone.astc && iphone.etc2 && !iphone.bc7, "an iPhone: no compute, ASTC / ETC2 textures");
    check(!web.compute && !web.indirectDraw, "the web Player (WebGL 2) has no compute shaders");
    check(android.compute && android.astc, "a newer Android phone (ES 3.2) has compute shaders");
    check(!weakPhone.compute, "an ES 3.1 phone with only 4 storage buffers doesn't count");
    check(!safe.compute && safe.safeMode, "Safe mode turns compute shaders off");

    for (int f = 0; f < kFeatures; ++f) {
        check(can(pc, (Feature)f), std::string("the PC can run: ") + info((Feature)f).name);
        check(!can(mac, (Feature)f) && !can(iphone, (Feature)f) && !can(web, (Feature)f),
              std::string("Mac, iPhone and web can't run: ") + info((Feature)f).name);
        check(info((Feature)f).name[0] && info((Feature)f).instead[0], "...and it says what they get instead");
    }

    // The notice only lists what is in the game today.
    Feature miss[kFeatures];
    check(missing(pc, miss) == 0, "a PC that does everything gets no notice");
    int shipped = 0;
    for (int f = 0; f < kFeatures; ++f) shipped += info((Feature)f).shipped ? 1 : 0;
    check(missing(mac, miss) == shipped && shipped >= 1, "a Mac's notice lists every shipped feature it can't run");
    check(miss[0] == GpuLiquid, "...starting with the liquid on the graphics card");
    check(missing(web, miss) == shipped && missing(iphone, miss) == shipped, "the same for the web Player and an iPhone");
    // (Its only one: the graphics card picking what to draw needs desktop OpenGL.)
    check(missing(android, miss) == 1 && miss[0] == GpuCulling, "a newer Android phone's notice lists only the graphics card picking what to draw");
    check(std::string(reason(android)).find("too old") == std::string::npos, "and doesn't call the phone too old");
    check(std::string(kNotice).find("can't run some") != std::string::npos, "the notice says it in plain words");
    check(std::string(reason(safe)).find("Safe mode") != std::string::npos, "in Safe mode it says how to turn them back on");
    check(reason(mac)[0] != 0, "there is always a reason to show");

    std::cout << (failures ? "FAILED\n" : "All groundwork tests passed\n");
    return failures ? 1 : 0;
}
