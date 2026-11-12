// game.ServerLowMemoryWarning and Stats' memory numbers (ServerMemory.h says how they work).
#include "ServerMemory.h"
#include "LuaApi.h"
#include "../core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <psapi.h>
#elif defined(__APPLE__)
#  include <mach/mach.h>
#  include <sys/sysctl.h>
#elif defined(__EMSCRIPTEN__)
#  include <emscripten/heap.h>
#else
#  include <unistd.h>
#endif

namespace ServerMemory {
namespace {
double g_pretendUsed = 0.0, g_pretendLimit = 0.0;
constexpr double kMb = 1024.0 * 1024.0;

// The Lua half: the event, its "at most once a minute" rule, and the Stats functions.
const char* kLua = R"LUA(
local server, used, limit, share, every = ...
local list, lastAt = {}, -math.huge
local warning = {}
function warning:Connect(fn)
    local c = { Connected = true }
    function c:Disconnect() self.Connected = false end
    list[#list + 1] = { c, fn }
    return c
end
warning.connect = warning.Connect
function warning:Wait()
    local done, a, b = false, nil, nil
    local c
    c = warning:Connect(function(x, y) a, b, done = x, y, true; c:Disconnect() end)
    while not done do task.wait() end
    return a, b
end
rawset(game, "ServerLowMemoryWarning", warning)
local stats = game:GetService("Stats")
rawset(stats, "GetTotalMemoryUsageMb", function() return used() end)
rawset(stats, "GetServerMemoryLimitMb", function() return limit() end)
-- The engine calls this every few seconds on the server.
return function(usedMb, limitMb, now)
    if not server or limitMb <= 0 or usedMb < limitMb * share then return end
    if now - lastAt < every then return end
    lastAt = now
    for _, e in ipairs(list) do
        if e[1].Connected then task.spawn(e[2], math.floor(usedMb), math.floor(limitMb)) end
    end
end
)LUA";

int l_used(lua_State* L) { lua_pushnumber(L, usedMb()); return 1; }
int l_limit(lua_State* L) { lua_pushnumber(L, limitMb()); return 1; }

double realUsedMb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return (double)pmc.WorkingSetSize / kMb;
    return 0.0;
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) return (double)info.resident_size / kMb;
    return 0.0;
#elif defined(__EMSCRIPTEN__)
    return (double)emscripten_get_heap_size() / kMb;
#else
    long pages = 0, resident = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        if (std::fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
        std::fclose(f);
    }
    return (double)resident * (double)sysconf(_SC_PAGESIZE) / kMb;
#endif
}

double computerMb() {
#if defined(_WIN32)
    MEMORYSTATUSEX m{}; m.dwLength = sizeof(m);
    return GlobalMemoryStatusEx(&m) ? (double)m.ullTotalPhys / kMb : 0.0;
#elif defined(__APPLE__)
    uint64_t bytes = 0; size_t len = sizeof(bytes);
    return sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0 ? (double)bytes / kMb : 0.0;
#elif defined(__EMSCRIPTEN__)
    return (double)emscripten_get_heap_max() / kMb;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGESIZE);
    return pages > 0 && size > 0 ? (double)pages * (double)size / kMb : 0.0;
#endif
}
} // namespace

double usedMb() { return g_pretendUsed > 0 ? g_pretendUsed : realUsedMb(); }

double limitMb() {
    if (g_pretendLimit > 0) return g_pretendLimit;
    if (const char* env = std::getenv("GB_SERVER_MEMORY_MB")) {
        const double v = std::atof(env);
        if (v >= 64) return v;
    }
    const double total = computerMb();
    return total > 0 ? total / 2.0 : 4096.0;
}

void pretend(double used, double limit) { g_pretendUsed = used; g_pretendLimit = limit; }

void install(lua_State* L, bool server) {
    if (luaL_loadbuffer(L, kLua, std::strlen(kLua), "=servermemory") != LUA_OK) {
        Log::error(std::string("Server memory setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    lua_pushboolean(L, server);
    lua_pushcfunction(L, l_used);
    lua_pushcfunction(L, l_limit);
    lua_pushnumber(L, kWarnShare);
    lua_pushnumber(L, kWarnEvery);
    if (lua_pcall(L, 5, 1, 0) != LUA_OK || !lua_isfunction(L, -1)) {
        Log::error(std::string("Server memory setup failed: ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?"));
        lua_pop(L, 1);
        return;
    }
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.memoryCheck");
    lua_pushnumber(L, -1e9);
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.memoryCheckedAt");
}

void step(lua_State* L, double now) {
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.memoryCheckedAt");
    const double last = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : -1e9;
    lua_pop(L, 1);
    if (now - last < kCheckEvery) return;
    lua_pushnumber(L, now);
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.memoryCheckedAt");
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.memoryCheck");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushnumber(L, usedMb());
    lua_pushnumber(L, limitMb());
    lua_pushnumber(L, now);
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
        Log::warn(std::string("ServerLowMemoryWarning: ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "error"));
        lua_pop(L, 1);
    }
}
} // namespace ServerMemory
