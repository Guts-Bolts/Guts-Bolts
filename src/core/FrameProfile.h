#pragma once
#include <chrono>
#include <string>
#include <vector>
#include "DevProfiler.h"

// A stopwatch for finding what makes a game slow.
//
// Live: the Live Profiler (Studio's View menu, or Ctrl+F6 in the Player) turns it on and
// shows, frame by frame, how many milliseconds each part of the game takes (physics,
// scripts, water, drawing...) and which scripts take the longest.
//
// Exact: start the Player with the environment variable GB_FRAME_PROFILE=1 and, when it
// closes, it prints how long each part took on average. Only then does the renderer wait
// for the graphics card at the end of each timed bit (glFinish), so drawing times are
// fair but the game runs a little slower. Off (and nearly free) otherwise.
//
//   { FrameProfile::Scope s("water"); ...work... }
namespace FrameProfile {

bool exact();                       // GB_FRAME_PROFILE is set
bool& live();                       // the Live Profiler is open
inline bool on() { return live() || exact(); }

void add(const char* name, double ms);                  // a timed bit of this frame
void addScript(const std::string& name, double ms);     // time a script's code ran this frame
void endFrame(double frameMs);                          // once a frame, with the whole frame's time

// What the Live Profiler shows: each part's time, over the last couple of seconds.
struct Row {
    std::string name;
    float now = 0.0f;   // the last frame (ms)
    float avg = 0.0f;   // average a frame (ms)
    float max = 0.0f;   // the slowest frame (ms)
};
std::vector<Row> sections();        // slowest first
std::vector<Row> scripts();         // slowest first
const std::vector<float>& frames(); // whole frames (ms), oldest first
void clear();

void report();                      // GB_FRAME_PROFILE: print the averages

// The graphics card's "wait until you're done" (set by the renderer).
void (*&finishGpu())();

struct Scope {
    const char* name;
    bool gpu;
    bool running = false;
    std::chrono::steady_clock::time_point t0;
#ifdef GB_TRACY
    DevProfiler::Zone dev;          // the same bit, in Tracy (GB_TRACY builds only)
    explicit Scope(const char* n, bool waitForGpu = false) : name(n), gpu(waitForGpu), dev(n, __FILE__, __LINE__, "FrameProfile") {
#else
    explicit Scope(const char* n, bool waitForGpu = false) : name(n), gpu(waitForGpu) {
#endif
        if (!on()) return;
        running = true;
        if (gpu && exact() && finishGpu()) finishGpu()();
        t0 = std::chrono::steady_clock::now();
    }
    ~Scope() {
        if (!running) return;
        if (gpu && exact() && finishGpu()) finishGpu()();
        add(name, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
};

} // namespace FrameProfile
