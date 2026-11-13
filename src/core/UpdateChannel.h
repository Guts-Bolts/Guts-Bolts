#pragma once
#include <string>

// The update channel: Stable (the normal releases, everyone starts here) or the
// opt-in Nightly test builds (the newest code on main, published by CI after the
// tests pass). This file only decides WHAT to offer; UpdateChecker downloads it.
//
// Rules that keep it safe:
//   - A newer stable release always wins, exactly like before Nightly existed.
//   - A nightly is never installed by itself: the Player asks first ("may be unstable").
//   - A nightly is only offered when it's a different build that isn't older than
//     this one, so updating to it can't come round again (no update loops).
//   - Turning Nightly off on a test build offers the stable version back (again, only
//     when the person clicks).
// The choice lives in its own file (<app folder>/update-channel.json), so a test
// build never changes settings.json and going back to Stable keeps it.
namespace UpdateChannel {

struct Build {
    bool        ok = false;      // the manifest was there and made sense
    std::string version;         // "0.8.0"
    std::string commit;          // full or short commit id ("" if the manifest doesn't say)
    std::string date;            // "2026-11-13" (nightly builds)
    std::string file;            // this computer's download, "" if there isn't one
};

struct Mine {
    std::string version;            // GB_VERSION
    std::string commit;             // GB_COMMIT
    std::string date;               // GB_COMMIT_DATE
    bool        nightlyOn = false;  // the Settings tick box
    std::string installedNightly;   // the last nightly we updated to (its commit)
};

enum class Offer { None, Stable, Nightly, BackToStable };

// What to offer, if anything.
Offer decide(const Mine& mine, const Build& stable, const Build& nightly);
// Is this copy a nightly test build (and not also the stable release)?
bool runningNightly(const Mine& mine, const Build& stable);
// Reads latest.json / nightly.json text. `platform` is "windows", "mac" or "linux".
Build parse(const std::string& jsonText, const char* platform);
// Same commit? (A short id matches the full one it starts with; "" or "unknown" never match.)
bool sameCommit(const std::string& a, const std::string& b);
// "0.6.10" > "0.6.9": -1, 0 or 1.
int compareVersions(const std::string& a, const std::string& b);

// The saved choice (update-channel.json next to the app).
bool nightlyOn();
void setNightlyOn(bool on);
std::string installedNightly();
void rememberNightly(const std::string& commit);   // just before updating to a nightly

} // namespace UpdateChannel
