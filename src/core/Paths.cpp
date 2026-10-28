#include "Paths.h"
#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <fstream>
#if defined(GB_MOBILE)
#include <SDL.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <mach-o/dyld.h>
#include <climits>
#endif
#endif
#ifdef __ANDROID__
#include <SDL.h>
#include <fstream>
#include <sstream>
#endif

namespace Paths {

std::filesystem::path appFolder() {
    static std::filesystem::path cached = [] {
        std::error_code ec;
#if defined(__ANDROID__)
        // The app's private storage (the APK itself is read-only).
        if (const char* p = SDL_AndroidGetInternalStoragePath()) return std::filesystem::path(p);
#elif defined(GB_IOS)
        // Keep mutable data outside the signed, read-only .app bundle.
        if (const char* p = SDL_GetPrefPath("GutsAndBolts", "Player")) {
            std::filesystem::path out(p);
            SDL_free((void*)p);
            std::filesystem::create_directories(out, ec);
            return out;
        }
#elif defined(__EMSCRIPTEN__)
        // In a browser: the games and pictures that come with it are put in /gb when the page loads.
        std::filesystem::create_directories("/gb", ec);
        return std::filesystem::path("/gb");
#elif defined(_WIN32)
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n > 0) return std::filesystem::path(std::wstring(buf, n)).parent_path();
#elif defined(__APPLE__)
        char buf[PATH_MAX];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) == 0)
            return std::filesystem::canonical(buf, ec).parent_path();
#else
        auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec) return exe.parent_path();
#endif
        return std::filesystem::current_path(ec);
    }();
    return cached;
}

std::filesystem::path file(const char* name) {
#ifdef __EMSCRIPTEN__
    // In a browser: settings and your avatar go in /save, which web/index.html keeps in
    // the browser's storage (IndexedDB), so they're still there next time.
    std::error_code ec;
    std::filesystem::create_directories("/save", ec);
    return std::filesystem::path("/save") / name;
#else
    return appFolder() / name;
#endif
}

std::filesystem::path downloadsFolder() {
    std::error_code ec;
    auto p = appFolder() / "downloads";
    std::filesystem::create_directories(p, ec);
    return p;
}

std::filesystem::path downloaded(const std::string& assetId) {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(downloadsFolder(), ec))
        if (e.is_regular_file() && e.path().stem().string() == assetId) return e.path();
    return {};
}

#ifdef __ANDROID__
namespace {
bool readAsset(const std::string& name, std::string& out) {
    SDL_RWops* rw = SDL_RWFromFile(name.c_str(), "rb");   // SDL reads files inside the APK
    if (!rw) return false;
    Sint64 size = SDL_RWsize(rw);
    out.resize(size > 0 ? (size_t)size : 0);
    size_t got = size > 0 ? SDL_RWread(rw, out.data(), 1, (size_t)size) : 0;
    SDL_RWclose(rw);
    out.resize(got);
    return true;
}
} // namespace
#endif

void installBundledFiles() {
#ifdef __ANDROID__
    // gb_manifest.txt (made when the APK is built): a version line, then one file per line.
    std::string manifest;
    if (!readAsset("gb_manifest.txt", manifest)) return;
    std::istringstream lines(manifest);
    std::string version;
    std::getline(lines, version);
    std::filesystem::path stamp = appFolder() / "bundle_version.txt";
    std::string have;
    { std::ifstream f(stamp); std::getline(f, have); }
    if (have == version) return;
    std::string name;
    std::error_code ec;
    while (std::getline(lines, name)) {
        if (name.empty() || name.find("..") != std::string::npos) continue;
        std::string data;
        if (!readAsset(name, data)) continue;
        std::filesystem::path dest = appFolder() / name;
        std::filesystem::create_directories(dest.parent_path(), ec);
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        out.write(data.data(), (std::streamsize)data.size());
    }
    std::ofstream(stamp) << version << "\n";
#elif defined(GB_IOS)
    // The signed app bundle is read-only; copy packaged content into writable storage.
    const char* base = SDL_GetBasePath();
    if (!base) return;
    const std::filesystem::path resources = std::filesystem::path(base) / "Resources";
    const std::filesystem::path stamp = appFolder() / "bundle_version.txt";
    std::string installed;
    { std::ifstream in(stamp); std::getline(in, installed); }
#ifdef GB_APP_VERSION
    const std::string version = GB_APP_VERSION;
#else
    const std::string version = "unknown";
#endif
    if (installed != version) {
        std::error_code ec;
        bool complete = true;
        // Runtime code uses appFolder() for both built-in content and writable
        // user data. The signed .app is read-only, so install each packaged
        // resource tree into the app's writable sandbox before the Player loads.
        for (const char* name : {"games", "catalog", "assets"}) {
            const auto src = resources / name;
            const auto dst = appFolder() / name;
            ec.clear();
            if (!std::filesystem::exists(src, ec) || ec) {
                complete = false;
                continue;
            }
            std::filesystem::create_directories(dst, ec);
            if (!ec) {
                std::filesystem::copy(src, dst, std::filesystem::copy_options::recursive |
                    std::filesystem::copy_options::overwrite_existing, ec);
            }
            if (ec) complete = false;
        }
        // Do not mark a partial installation as current. The next launch will
        // retry any copy that failed rather than silently keeping broken assets.
        if (complete) std::ofstream(stamp, std::ios::trunc) << version << "\n";
    }
    SDL_free((void*)base);
#endif
}

std::filesystem::path gamesFolder() {
    std::filesystem::path p = appFolder() / "games";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

std::string relativeToGames(const std::filesystem::path& file) {
    std::error_code ec;
    auto games = std::filesystem::weakly_canonical(gamesFolder(), ec);
    auto full  = std::filesystem::weakly_canonical(file, ec);
    auto rel   = full.lexically_relative(games);
    if (!rel.empty() && *rel.begin() != "..") return rel.generic_string();
    return file.string();
}

std::vector<std::filesystem::path> listGames() {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(gamesFolder(), ec))
        if (e.is_regular_file() && (e.path().extension() == kExtension ||
                                    e.path().extension() == ".rbxl" || e.path().extension() == ".rbxlx"))
            out.push_back(e.path());   // Roblox places can be played straight away
    std::sort(out.begin(), out.end());
    return out;
}

std::filesystem::path sibling(const char* programName) {
#ifdef _WIN32
    return appFolder() / (std::string(programName) + ".exe");
#else
    return appFolder() / programName;
#endif
}

bool launch(const std::filesystem::path& program, const std::string& argument) {
    std::error_code ec;
    if (!std::filesystem::exists(program, ec)) return false;
#ifdef _WIN32
    std::wstring args = L"\"" + program.wstring() + L"\"";
    if (!argument.empty()) args += L" \"" + std::filesystem::path(argument).wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring dir = program.parent_path().wstring();
    if (!CreateProcessW(nullptr, args.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#elif defined(GB_IOS)
    // iOS apps cannot spawn sibling executables or invoke a shell.
    (void)argument;
    return false;
#else
    std::string cmd = "\"" + program.string() + "\"";
    if (!argument.empty()) cmd += " \"" + argument + "\"";
    cmd += " >/dev/null 2>&1 &";
    return std::system(cmd.c_str()) == 0;
#endif
}

} // namespace Paths
