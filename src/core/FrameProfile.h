#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

// A stopwatch for finding what makes a game slow. Start the Player with the
// environment variable GB_FRAME_PROFILE=1 and, when it closes, it prints how
// many milliseconds each part of a frame took on average (physics, water,
// drawing the parts, the liquid...). Off (and free) otherwise.
//
//   { FrameProfile::Scope s("water"); ...work... }
//
// The graphics card works in the background, so to time drawing fairly the
// renderer waits for it (glFinish) at the end of each timed bit, but only
// while profiling.
namespace FrameProfile {

inline bool on() {
    static const bool enabled = std::getenv("GB_FRAME_PROFILE") != nullptr;
    return enabled;
}

struct Entry { double total = 0.0; long count = 0; };
inline std::map<std::string, Entry>& entries() {
    static std::map<std::string, Entry> e;
    return e;
}

inline void add(const char* name, double ms) {
    Entry& e = entries()[name];
    e.total += ms;
    ++e.count;
}

inline void report() {
    if (!on() || entries().empty()) return;
    std::printf("FRAME PROFILE (average ms each time):\n");
    for (const auto& [name, e] : entries())
        std::printf("  %-22s %8.2f ms  (x%ld)\n", name.c_str(), e.count ? e.total / e.count : 0.0, e.count);
    std::fflush(stdout);
}

// The graphics card's "wait until you're done" (set by the renderer).
inline void (*&finishGpu())() {
    static void (*fn)() = nullptr;
    return fn;
}

struct Scope {
    const char* name;
    bool gpu;
    std::chrono::steady_clock::time_point t0;
    explicit Scope(const char* n, bool waitForGpu = false) : name(n), gpu(waitForGpu) {
        if (!on()) return;
        if (gpu && finishGpu()) finishGpu()();
        t0 = std::chrono::steady_clock::now();
    }
    ~Scope() {
        if (!on()) return;
        if (gpu && finishGpu()) finishGpu()();
        add(name, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
};

} // namespace FrameProfile
