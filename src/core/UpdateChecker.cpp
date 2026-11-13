#include "UpdateChecker.h"
#include "UpdateChannel.h"
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
#include <chrono>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define popen  _popen
#define pclose _pclose
#else
#include <unistd.h>
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

std::string g_downloadUrl;   // the newest version of this app for this computer
std::string g_latestVersion; // what the status file calls this try ("0.8.0", "nightly-abc1234", "stable-0.8.0")
Kind        g_kind = Kind::Stable;
std::string g_targetCommit;  // the nightly we'd update to
bool        g_runningNightly = false;
std::string lastTryFailed(const std::string& version);

std::string fetch(const std::string& path) {
#ifdef _WIN32
    const char* quiet = " 2>NUL";
#else
    const char* quiet = " 2>/dev/null";
#endif
    return runCapture("curl -fsSL -m 15 \"" + siteUrl() + path + "\"" + quiet);
}

void check() {
    UpdateChannel::Mine mine;
    mine.version = GB_VERSION;
    if (const char* fake = std::getenv("GB_PRETEND_VERSION")) mine.version = fake;   // for testing the update screen
    mine.commit = GB_COMMIT;
    if (const char* fake = std::getenv("GB_PRETEND_COMMIT")) mine.commit = fake;
    mine.date = GB_COMMIT_DATE;
    mine.nightlyOn = UpdateChannel::nightlyOn();
    mine.installedNightly = UpdateChannel::installedNightly();

    const std::string latestText = fetch("/download/latest.json");
    const UpdateChannel::Build stable = UpdateChannel::parse(latestText, platformKey());
    UpdateChannel::Build nightly;
    if (mine.nightlyOn) nightly = UpdateChannel::parse(fetch("/download/nightly.json"), platformKey());

    Info result;
    const UpdateChannel::Offer offer = UpdateChannel::decide(mine, stable, nightly);
    const bool runningNightly = UpdateChannel::runningNightly(mine, stable);
    std::string url, key, target;
    Kind kind = Kind::Stable;
    if (!stable.ok && !nightly.ok) {
        result.state = State::Failed;
    } else if (offer == UpdateChannel::Offer::Stable) {
        // Exactly the old behaviour: a newer release, which the app may install on launch.
        nlohmann::json j = nlohmann::json::parse(latestText, nullptr, false);
        result.state = State::Available;
        result.plainlyBehind = true;
        result.behindBy = 1;
        result.latestMessage = j.is_object() ? j.value("name", std::string("Guts&Bolts ") + stable.version) : "Guts&Bolts " + stable.version;
        result.offeredVersion = stable.version;
        key = stable.version;
        url = stable.file.empty() ? "" : siteUrl() + "/download/" + stable.file;
    } else if (offer == UpdateChannel::Offer::Nightly) {
        // A test build: only ever offered (plainlyBehind stays false, so it's never forced).
        kind = Kind::Nightly;
        result.state = State::Available;
        result.buildId = nightly.commit.substr(0, 7);
        result.buildDate = nightly.date;
        result.offeredVersion = nightly.version;
        result.latestMessage = "Test build " + result.buildId + (nightly.date.empty() ? "" : " (" + nightly.date + ")");
        key = "nightly-" + result.buildId;
        target = nightly.commit;
        url = siteUrl() + "/download/nightly/" + nightly.commit + "/" + nightly.file;
    } else if (offer == UpdateChannel::Offer::BackToStable) {
        kind = Kind::BackToStable;
        result.state = State::Available;
        result.offeredVersion = stable.version;
        result.latestMessage = "Guts&Bolts " + stable.version + " (stable)";
        key = "stable-" + stable.version;
        url = siteUrl() + "/download/" + stable.file;
    } else {
        result.state = State::UpToDate;
    }
    result.kind = kind;
    result.compareUrl = siteUrl() + "/app/#/updates";
    if (kind == Kind::Nightly && GB_COMMIT != std::string("unknown"))
        result.compareUrl = std::string("https://github.com/") + GB_REPO + "/compare/" + std::string(GB_COMMIT).substr(0, 12) + "..." + target.substr(0, 12);
    if (result.state == State::Available) result.lastTryFailed = lastTryFailed(key);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_info = result;
        g_downloadUrl = result.state == State::Available ? url : "";
        g_latestVersion = key;
        g_kind = kind;
        g_targetCommit = target;
        g_runningNightly = runningNightly;
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

std::string downloadPage() { return siteUrl() + "/#download"; }

const char* buildDate() { return GB_COMMIT_DATE; }

bool nightlyEnabled() { return UpdateChannel::nightlyOn(); }

void setNightly(bool on) {
    UpdateChannel::setNightlyOn(on);
    start();   // look again with the new choice
}

bool runningNightly() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_runningNightly;
}

namespace {
// Text inside '...' quotes: PowerShell writes a ' as '', sh closes and reopens the quote.
std::string quoted(const std::string& s, ScriptKind kind) {
    std::string out = "'";
    for (char c : s) {
        if (c != '\'') out += c;
        else out += kind == ScriptKind::PowerShell ? "''" : "'\\''";
    }
    return out + "'";
}

std::string utf8(const std::filesystem::path& p) {
    std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

long long currentPid() {
#ifdef _WIN32
    return (long long)GetCurrentProcessId();
#else
    return (long long)getpid();
#endif
}

long long unixNow() {
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Notes how the last update went, so a failed one isn't retried in a loop (see check()).
std::filesystem::path statusFile() {
    std::error_code ec;
    return std::filesystem::temp_directory_path(ec) / "gutsandbolts-update-status.txt";
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool writeFile(const std::filesystem::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
    return (bool)out;
}

std::string lastTryFailed(const std::string& version) {
    return failedReason(readFile(statusFile()), version, unixNow());
}
} // namespace

std::string failedReason(const std::string& statusText, const std::string& version, long long now) {
    constexpr long long kRecent = 60 * 60;   // after an hour, one more try by itself is fine
    std::istringstream in(statusText);
    std::string first;
    std::getline(in, first);
    std::istringstream head(first);
    std::string triedVersion;
    long long at = 0;
    if (!(head >> triedVersion >> at) || triedVersion != version || now - at > kRecent || now < at) return "";
    std::string line, why;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("failed: ", 0) == 0) why = line.substr(8);
        else if (line == "ok" && why.empty()) why = "the new files went in, but the old version opened again";
    }
    if (why.empty()) return "the updater stopped before it finished";
    if (why.size() > 240) why = why.substr(0, 240) + "...";
    while (!why.empty() && (why.back() == '.' || why.back() == ' ')) why.pop_back();
    return why;
}

// The little program that does the update after we quit (a running program can't
// replace itself). It waits for us to close, downloads the new version, puts it over
// this one and opens the app again. Your own games are kept: only files the update
// brings that you don't have yet are added to games/ and catalog/. Whatever happens it
// writes "ok" or "failed: why" into the status file, and the app reads that next time.
std::string updaterScript(ScriptKind kind, const ScriptArgs& a) {
    std::ostringstream o;
    if (kind == ScriptKind::PowerShell) {
        o << "$ErrorActionPreference = 'Stop'\n"
             "$ProgressPreference = 'SilentlyContinue'\n"   // the progress bar makes big downloads very slow
             "$status = " << quoted(a.status, kind) << "\n"
             "$app = " << quoted(a.app, kind) << "\n"
             "$dst = " << quoted(a.folder, kind) << "\n"
             "try {\n"
             "  [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12\n"
             "  Wait-Process -Id " << a.pid << " -Timeout 60 -ErrorAction SilentlyContinue\n"
             "  Get-ChildItem -LiteralPath $dst -Filter '*.update-old*' -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue\n"
             "  $t = Join-Path $env:TEMP 'gutsandbolts-update'\n"
             "  Remove-Item -Recurse -Force $t -ErrorAction SilentlyContinue\n"
             "  New-Item -ItemType Directory $t | Out-Null\n"
             "  try { Invoke-WebRequest -UseBasicParsing " << quoted(a.url, kind) << " -OutFile (Join-Path $t 'update.zip') }\n"
             "  catch { throw (\"couldn't download the new version (\" + $_.Exception.Message + \")\") }\n"
             "  try { Expand-Archive -Force (Join-Path $t 'update.zip') (Join-Path $t 'x') }\n"
             "  catch { throw (\"couldn't unpack the download (\" + $_.Exception.Message + \")\") }\n"
             "  $src = (Get-ChildItem (Join-Path $t 'x') -Directory | Select-Object -First 1).FullName\n"
             "  if (-not $src) { throw 'the download was empty' }\n"
             // A program that's still open (the Player, a test server) can't be overwritten,
             // but Windows lets it be renamed out of the way.
             "  function Put($from, $to) {\n"
             "    for ($i = 0; $i -lt 10; $i++) {\n"
             "      try { Copy-Item -LiteralPath $from -Destination $to -Force; return } catch { $why = $_.Exception.Message }\n"
             "      if (Test-Path -LiteralPath $to) { try { Rename-Item -LiteralPath $to -NewName ((Split-Path $to -Leaf) + '.update-old' + $i) } catch {} }\n"
             "      Start-Sleep -Milliseconds 500\n"
             "    }\n"
             "    throw (\"couldn't replace \" + (Split-Path $to -Leaf) + \" (\" + $why + \")\")\n"
             "  }\n"
             "  Get-ChildItem -LiteralPath $src -File | ForEach-Object { Put $_.FullName (Join-Path $dst $_.Name) }\n"
             "  Get-ChildItem -LiteralPath $src -Directory | ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Recurse -File | ForEach-Object {\n"
             "    $rel = $_.FullName.Substring($src.Length + 1); $to = Join-Path $dst $rel\n"
             "    if (-not (Test-Path -LiteralPath $to)) { try { New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null; Copy-Item -LiteralPath $_.FullName $to } catch {} } } }\n"
             "  Remove-Item -Recurse -Force $t -ErrorAction SilentlyContinue\n"
             "  Add-Content -LiteralPath $status 'ok'\n"
             "} catch {\n"
             "  Add-Content -LiteralPath $status ('failed: ' + $_.Exception.Message)\n"
             "}\n"
             "Start-Process -FilePath $app\n";
    } else {
        o << "#!/bin/sh\n"
             "status=" << quoted(a.status, kind) << "\n"
             "app=" << quoted(a.app, kind) << "\n"
             "dst=" << quoted(a.folder, kind) << "\n"
             "url=" << quoted(a.url, kind) << "\n"
             "done_() { echo \"$1\" >> \"$status\"; [ -n \"$t\" ] && rm -rf \"$t\"; \"$app\" >/dev/null 2>&1 & exit 0; }\n"
             "fail() { done_ \"failed: $1\"; }\n"
             "i=0; while kill -0 " << a.pid << " 2>/dev/null && [ $i -lt 60 ]; do sleep 1; i=$((i+1)); done\n"
             "t=$(mktemp -d) || fail \"couldn't make a temporary folder\"\n"
             "curl -fsSL \"$url\" -o \"$t/update\" || fail \"couldn't download the new version\"\n"
             "case \"$url\" in *.zip) (cd \"$t\" && unzip -q update -d x) ;; *) mkdir -p \"$t/x\" && tar xzf \"$t/update\" -C \"$t/x\" ;; esac || fail \"couldn't unpack the download\"\n"
             "src=$(find \"$t/x\" -mindepth 1 -maxdepth 1 -type d | head -n 1)\n"
             "[ -n \"$src\" ] || fail \"the download was empty\"\n"
             "[ -w \"$dst\" ] || fail \"this folder can't be changed: $dst\"\n"
             // Remove first, then copy: overwriting a program in place can make macOS refuse
             // to open it, and Linux won't overwrite one that's still running.
             "for f in \"$src\"/*; do\n"
             "  [ -f \"$f\" ] || continue\n"
             "  n=$(basename \"$f\")\n"
             "  rm -f \"$dst/$n\" && cp \"$f\" \"$dst/$n\" || fail \"couldn't replace $n\"\n"
             "done\n"
             "for d in \"$src\"/*/; do cp -R -n \"$d\" \"$dst\"/ 2>/dev/null || true; done\n"
             "chmod +x \"$dst\"/GutsAndBolts \"$dst\"/GutsAndBoltsPlayer \"$dst\"/GutsAndBoltsGameServer 2>/dev/null\n"
             "done_ ok\n";
    }
    return o.str();
}

bool launchUpdater(const char* relaunchApp) {
#if defined(GB_IOS)
    // iOS apps cannot replace their installed bundle or launch shell scripts.
    // The user must install updates through the platform distribution channel.
    (void)relaunchApp;
    return false;
#else
    if (!canUpdate()) return false;
    std::string url, version, target;
    Kind kind;
    { std::lock_guard<std::mutex> lock(g_mutex); url = g_downloadUrl; version = g_latestVersion; kind = g_kind; target = g_targetCommit; }
    if (std::getenv("GB_UPDATE_DRYRUN")) { std::printf("UPDATER would download %s for %s\n", url.c_str(), relaunchApp); std::fflush(stdout); return true; }   // tests
    // Remember which nightly this is, so after the update the app knows it's a test
    // build (and can offer Stable back when Nightly is turned off).
    if (kind == Kind::Nightly) UpdateChannel::rememberNightly(target);

    ScriptArgs a;
    a.url = url;
    a.folder = utf8(Paths::appFolder());
    a.app = utf8(Paths::sibling(relaunchApp));
    a.status = utf8(statusFile());
    a.pid = currentPid();
    // Note the try first: if the script never gets to say how it went, that counts as "didn't work".
    writeFile(statusFile(), version + " " + std::to_string(unixNow()) + "\n");

    std::error_code ec;
#ifdef _WIN32
    const std::filesystem::path script = std::filesystem::temp_directory_path(ec) / "gutsandbolts-update.ps1";
    const std::string text = "\xEF\xBB\xBF" + updaterScript(ScriptKind::PowerShell, a);   // UTF-8 with BOM, for PowerShell 5
    std::string cmd = "start \"\" powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script.string() + "\"";
#else
    const std::filesystem::path script = std::filesystem::temp_directory_path(ec) / "gutsandbolts-update.sh";
    const std::string text = updaterScript(ScriptKind::Shell, a);
    std::string cmd = "(sh '" + script.string() + "' >/dev/null 2>&1 &)";
#endif
    if (!writeFile(script, text)) return false;
    return std::system(cmd.c_str()) == 0;
#endif
}

} // namespace UpdateChecker
