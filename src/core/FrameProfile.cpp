#include "FrameProfile.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace FrameProfile {
namespace {

constexpr int kFrames = 120;        // about two seconds at 60 frames a second
constexpr int kForget = kFrames * 5; // a part not seen for this many frames leaves the list

struct Track {
    std::vector<float> ms = std::vector<float>(kFrames, 0.0f);
    double now = 0.0;               // this frame so far
    long seen = 0;                  // the last frame it ran in
};
struct State {
    std::map<std::string, Track> sections, scripts;
    std::vector<float> frames;
    long frame = 0;
    int filled = 0;                 // frames in the history so far (up to kFrames)
    std::map<std::string, std::pair<double, long>> totals;   // GB_FRAME_PROFILE: total ms, times
};
State& state() {
    static State s;
    return s;
}

std::vector<Row> rows(const std::map<std::string, Track>& tracks) {
    const State& s = state();
    std::vector<Row> out;
    const int n = std::max(1, s.filled);
    const int last = (int)((s.frame + kFrames - 1) % kFrames);
    for (const auto& [name, t] : tracks) {
        Row r;
        r.name = name;
        r.now = s.frame > 0 ? t.ms[(size_t)last] : 0.0f;
        double sum = 0.0;
        for (float v : t.ms) { sum += v; r.max = std::max(r.max, v); }
        r.avg = (float)(sum / n);
        out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](const Row& a, const Row& b) { return a.avg > b.avg; });
    return out;
}

} // namespace

bool exact() {
    static const bool on = std::getenv("GB_FRAME_PROFILE") != nullptr;
    return on;
}

bool& live() {
    static bool on = false;
    return on;
}

void add(const char* name, double ms) {
    State& s = state();
    if (exact()) {
        auto& t = s.totals[name];
        t.first += ms;
        ++t.second;
    }
    if (live()) {
        Track& t = s.sections[name];
        t.now += ms;
        t.seen = s.frame;
    }
}

void addScript(const std::string& name, double ms) {
    if (!live()) return;
    State& s = state();
    Track& t = s.scripts[name];
    t.now += ms;
    t.seen = s.frame;
}

void endFrame(double frameMs) {
    if (!live()) return;
    State& s = state();
    const size_t at = (size_t)(s.frame % kFrames);
    for (auto* tracks : {&s.sections, &s.scripts}) {
        for (auto it = tracks->begin(); it != tracks->end();) {
            Track& t = it->second;
            t.ms[at] = (float)t.now;
            t.now = 0.0;
            if (s.frame - t.seen > kForget) it = tracks->erase(it);
            else ++it;
        }
    }
    s.frames.push_back((float)frameMs);
    if ((int)s.frames.size() > kFrames) s.frames.erase(s.frames.begin());
    ++s.frame;
    s.filled = std::min(kFrames, s.filled + 1);
}

std::vector<Row> sections() { return rows(state().sections); }
std::vector<Row> scripts() { return rows(state().scripts); }
const std::vector<float>& frames() { return state().frames; }

void clear() {
    State& s = state();
    s.sections.clear();
    s.scripts.clear();
    s.frames.clear();
    s.filled = 0;
}

void report() {
    const State& s = state();
    if (!exact() || s.totals.empty()) return;
    std::printf("FRAME PROFILE (average ms each time):\n");
    for (const auto& [name, e] : s.totals)
        std::printf("  %-22s %8.2f ms  (x%ld)\n", name.c_str(), e.second ? e.first / e.second : 0.0, e.second);
    std::fflush(stdout);
}

void (*&finishGpu())() {
    static void (*fn)() = nullptr;
    return fn;
}

} // namespace FrameProfile
