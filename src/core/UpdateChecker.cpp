#include "UpdateChecker.h"
#include "Paths.h"
#include "Version.h"

#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

namespace UpdateChecker {

namespace {
std::mutex        g_mutex;
Info              g_info;
std::atomic<bool> g_running{false};

std::string runCapture(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

void setState(State s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_info.state = s;
}

// Where the apps come from: gutsandbolts.net (GB_UPDATE_URL overrides it, for tests).
std::string siteUrl() {
    if (const char* u = std::getenv("GB_UPDATE_URL"); u && *u) return u;
    return "https://gutsandbolts.net";
}

const char* platformKey() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "mac";
#else
    return "linux";
#endif
}

// "0.6.10" > "0.6.9" (numbers compared one by one; "-dev" and such ignored).
int compareVersions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& v) {
        std::vector<long> out;
        long cur = 0; bool any = false;
        for (char c : v) {
            if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); any = true; }
            else if (c == '.') { out.push_back(cur); cur = 0; any = false; }
            else break;
        }
        if (any || !out.empty()) out.push_back(cur);
        return out;
    };
    std::vector<long> x = parts(a), y = parts(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
        long p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

std::string g_downloadUrl;   // the newest version of this app for this computer

void check() {
    std::string mine = GB_VERSION;
    if (const char* fake = std::getenv("GB_PRETEND_VERSION")) mine = fake;   // for testing the update screen
#ifdef _WIN32
    const char* quiet = " 2>NUL";
#else
    const char* quiet = " 2>/dev/null";
#endif
    std::string text = runCapture("curl -fsSL -m 15 \"" + siteUrl() + "/download/latest.json\"" + quiet);
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);

    Info result;
    if (!j.is_object() || !j.contains("version") || !j["version"].is_string()) {
        result.state = State::Failed;
    } else {
        const std::string latest = j["version"].get<std::string>();
        result.compareUrl = siteUrl() + "/app/#/updates";
        if (compareVersions(mine, latest) < 0) {
            result.state = State::Available;
            result.plainlyBehind = true;
            result.behindBy = 1;
            result.latestMessage = j.value("name", std::string("Guts&Bolts ") + latest);
            const std::string file = j.contains("files") && j["files"].is_object() ? j["files"].value(platformKey(), std::string()) : "";
            std::lock_guard<std::mutex> lock(g_mutex);
            g_downloadUrl = file.empty() ? "" : siteUrl() + "/download/" + file;
        } else {
            result.state = State::UpToDate;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_info = result;
    }
    g_running = false;
}
} // namespace

void start() {
#ifdef GB_MOBILE
    return;   // phones get updates from the new APK instead
#endif
    if (const char* off = std::getenv("GB_NO_UPDATE"); off && *off) return;   // tests and screenshots
    if (g_running.exchange(true)) return;
    setState(State::Checking);
    std::thread(check).detach();
}

Info info() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_info;
}

const char* currentVersion() {
    static std::string v = [] {
        std::string c = GB_COMMIT;
        return std::string(GB_VERSION) + (c == "unknown" ? "" : " (" + c.substr(0, 7) + ")");
    }();
    return v.c_str();
}

bool canUpdate() {
#ifdef GB_MOBILE
    return false;   // phones: download the new APK from the website
#else
    std::lock_guard<std::mutex> lock(g_mutex);
    return !g_downloadUrl.empty();
#endif
}

// Downloads the new version from gutsandbolts.net and puts it over this one, then
// starts the app again. A little script does it after we quit (a running program
// can't replace itself). Your own games are kept: only files the update brings
// that you don't have yet are added to games/ and catalog/.
bool launchUpdater(const char* relaunchApp) {
#if defined(GB_IOS)
    // iOS apps cannot replace their installed bundle or launch shell scripts.
    // The user must install updates through the platform distribution channel.
    (void)relaunchApp;
    return false;
#else
    if (!canUpdate()) return false;
    std::string url;
    { std::lock_guard<std::mutex> lock(g_mutex); url = g_downloadUrl; }
    if (std::getenv("GB_UPDATE_DRYRUN")) { std::printf("UPDATER would download %s for %s\n", url.c_str(), relaunchApp); std::fflush(stdout); return true; }   // tests
    const std::string app = Paths::sibling(relaunchApp).string();
    const std::string dir = Paths::appFolder().string();
#ifdef _WIN32
    const std::filesystem::path script = std::filesystem::temp_directory_path() / "gutsandbolts-update.ps1";
    FILE* f = std::fopen(script.string().c_str(), "w");
    if (!f) return false;
    std::fprintf(f,
        "Start-Sleep -Seconds 2\n"
        "$t = Join-Path $env:TEMP 'gutsandbolts-update'\n"
        "Remove-Item -Recurse -Force $t -ErrorAction SilentlyContinue\n"
        "New-Item -ItemType Directory $t | Out-Null\n"
        "Invoke-WebRequest -UseBasicParsing '%s' -OutFile (Join-Path $t 'update.zip')\n"
        "Expand-Archive -Force (Join-Path $t 'update.zip') (Join-Path $t 'x')\n"
        "$src = (Get-ChildItem (Join-Path $t 'x') -Directory | Select-Object -First 1).FullName\n"
        "$dst = '%s'\n"
        "Get-ChildItem $src -File | ForEach-Object { Copy-Item $_.FullName $dst -Force }\n"
        "Get-ChildItem $src -Directory | ForEach-Object { $d = $_; Get-ChildItem $d.FullName -Recurse -File | ForEach-Object {\n"
        "  $rel = $_.FullName.Substring($src.Length + 1); $to = Join-Path $dst $rel\n"
        "  if (-not (Test-Path $to)) { New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null; Copy-Item $_.FullName $to } } }\n"
        "Start-Process '%s'\n", url.c_str(), dir.c_str(), app.c_str());
    std::fclose(f);
    std::string cmd = "start \"\" powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script.string() + "\"";
#else
    const std::filesystem::path script = std::filesystem::temp_directory_path() / "gutsandbolts-update.sh";
    FILE* f = std::fopen(script.string().c_str(), "w");
    if (!f) return false;
    std::fprintf(f,
        "#!/bin/sh\n"
        "sleep 2\n"
        "t=$(mktemp -d) || exit 1\n"
        "curl -fsSL '%s' -o \"$t/update\" || exit 1\n"
        "case '%s' in *.zip) (cd \"$t\" && unzip -q update -d x) ;; *) mkdir -p \"$t/x\" && tar xzf \"$t/update\" -C \"$t/x\" ;; esac || exit 1\n"
        "src=$(find \"$t/x\" -mindepth 1 -maxdepth 1 -type d | head -n 1)\n"
        "dst='%s'\n"
        "find \"$src\" -mindepth 1 -maxdepth 1 -type f -exec cp -f {} \"$dst\"/ \\;\n"
        "for d in \"$src\"/*/; do cp -R -n \"$d\" \"$dst\"/ 2>/dev/null || true; done\n"
        "chmod +x \"$dst\"/GutsAndBolts \"$dst\"/GutsAndBoltsPlayer 2>/dev/null\n"
        "rm -rf \"$t\"\n"
        "\"%s\" >/dev/null 2>&1 &\n", url.c_str(), url.c_str(), dir.c_str(), app.c_str());
    std::fclose(f);
    std::string cmd = "(sh \"" + script.string() + "\" >/dev/null 2>&1 &)";
#endif
    return std::system(cmd.c_str()) == 0;
#endif
}

} // namespace UpdateChecker
