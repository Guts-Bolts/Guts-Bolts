#include "Battery.h"

#if defined(GB_MOBILE)
#include <SDL.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <filesystem>
#include <fstream>
#include <string>
#endif

namespace Battery {

bool read(int& percent, bool& charging) {
    percent = -1;
    charging = false;
#if defined(GB_MOBILE)
    int secs = 0, pct = -1;
    const SDL_PowerState st = SDL_GetPowerInfo(&secs, &pct);
    if (st == SDL_POWERSTATE_UNKNOWN || st == SDL_POWERSTATE_NO_BATTERY || pct < 0) return false;
    percent = pct;
    charging = st == SDL_POWERSTATE_CHARGING || st == SDL_POWERSTATE_CHARGED;
    return true;
#elif defined(_WIN32)
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps)) return false;
    if (ps.BatteryFlag == 128 || ps.BatteryFlag == 255 || ps.BatteryLifePercent == 255) return false;   // no battery / unknown
    percent = ps.BatteryLifePercent;
    charging = ps.ACLineStatus == 1;
    return true;
#elif defined(__linux__) && !defined(__ANDROID__)
    // The first battery the kernel lists (BAT0, BAT1...), not a mouse's or a phone's.
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/sys/class/power_supply", ec)) {
        std::string type;
        std::ifstream(e.path() / "type") >> type;
        if (type != "Battery") continue;
        std::string scope;
        std::ifstream(e.path() / "scope") >> scope;
        if (scope == "Device") continue;   // (a wireless mouse or game pad)
        int pct = -1;
        std::string status;
        std::ifstream(e.path() / "capacity") >> pct;
        std::ifstream(e.path() / "status") >> status;
        if (pct < 0) continue;
        percent = pct;
        charging = status == "Charging" || status == "Full";
        return true;
    }
    return false;
#else
    return false;
#endif
}

} // namespace Battery
