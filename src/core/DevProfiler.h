#pragma once
// Tracy, for people working on the engine itself (not for game makers: they have the
// Live Profiler). Only in builds made with -DGB_TRACY=ON; everywhere else these do
// nothing and cost nothing. See docs/engine/tracy.md.
//
// Every FrameProfile::Scope ("draw: shadows", "game: scripts"...) already shows up in
// Tracy, and so does each script's run. To time something else:
//
//   { GB_ZONE("load the map"); ...work... }
#include <cstddef>
#include <cstring>

#ifdef GB_TRACY
#include <tracy/TracyC.h>

namespace DevProfiler {

inline bool connected() { return TracyCIsConnected; }

// A timed bit with a name made while the game runs (like a script's name).
struct Zone {
    TracyCZoneCtx ctx{};
    bool on = false;
    Zone(const char* name, size_t len, const char* file, unsigned line, const char* func) {
        if (!connected()) return;
        const uint64_t at = ___tracy_alloc_srcloc_name(line, file, std::strlen(file), func, std::strlen(func), name, len, 0);
        ctx = ___tracy_emit_zone_begin_alloc(at, 1);
        on = true;
    }
    Zone(const char* name, const char* file, unsigned line, const char* func)
        : Zone(name, std::strlen(name), file, line, func) {}
    ~Zone() { if (on) ___tracy_emit_zone_end(ctx); }
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;
};

inline void frame() { TracyCFrameMark }

} // namespace DevProfiler

#define GB_ZONE_CAT2(a, b) a##b
#define GB_ZONE_CAT(a, b) GB_ZONE_CAT2(a, b)
#define GB_ZONE(name) DevProfiler::Zone GB_ZONE_CAT(gbZone, __LINE__)(name, __FILE__, __LINE__, __func__)

#else

namespace DevProfiler {
inline bool connected() { return false; }
inline void frame() {}
} // namespace DevProfiler

#define GB_ZONE(name) ((void)0)

#endif
