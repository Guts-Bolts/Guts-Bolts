#pragma once
#include <string>

// Asks gutsandbolts.net (in the background) whether there's a newer version, and
// updates by downloading the ready-made app from there. Uses the `curl` program,
// which Windows 10+, macOS and Linux all have. No internet simply means "no update".
namespace UpdateChecker {

enum class State { Idle, Checking, UpToDate, Available, Failed };

struct Info {
    State       state = State::Idle;
    int         behindBy = 0;            // how many new changes
    bool        plainlyBehind = false;   // just older than main (not a build with its own changes)
    std::string latestMessage;           // first line of the newest change
    std::string latestDate;
    std::string compareUrl;              // page listing what's new
    // Not empty when we already tried updating to this version a little while ago and
    // it didn't work (why, in plain words). Then the app doesn't try again by itself,
    // so it can't get stuck closing and reopening over and over.
    std::string lastTryFailed;
};

void start();                 // kick off a check (does nothing if one is running)
Info info();                  // thread-safe snapshot
const char* currentVersion(); // e.g. "0.3.0 (a1b2c3d)"
bool canUpdate();             // is there a download of the new version for this computer?
bool launchUpdater(const char* relaunchApp);   // downloads it, puts it over this one, restarts
std::string downloadPage();   // the website's Download section, for updating by hand

// The pieces below are only shared for tests (tests/UpdaterTests.cpp).
enum class ScriptKind { PowerShell, Shell };
struct ScriptArgs {
    std::string url;        // the new version's download
    std::string folder;     // where this copy lives (the files get replaced here)
    std::string app;        // the program to open again afterwards
    std::string status;     // the file the script writes "ok" / "failed: why" into
    long long   pid = 0;    // this program: the script waits for it to close first
};
std::string updaterScript(ScriptKind kind, const ScriptArgs& a);
// What a status file says about trying to update to `version` (at unix time `now`):
// "" if we didn't try lately, or why it didn't work.
std::string failedReason(const std::string& statusText, const std::string& version, long long now);

} // namespace UpdateChecker
