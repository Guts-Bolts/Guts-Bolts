// The Nightly update channel (core/UpdateChannel): what the Player offers, and that
// no mix of releases, nightlies and switches can make it update in a loop.
//
//   UpdateChannelTests
#include "core/UpdateChannel.h"

#include <iostream>
#include <string>

using namespace UpdateChannel;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

const std::string A = "aaaaaaa1111111111111111111111111111111111";   // older main commit
const std::string B = "bbbbbbb2222222222222222222222222222222222";   // newer main commit
const std::string R = "cccccccc333333333333333333333333333333333";   // the release commit

Build release(const std::string& version, const std::string& commit = "") {
    Build b; b.ok = true; b.version = version; b.commit = commit; b.file = "GutsAndBolts-Windows.zip"; return b;
}
Build nightlyBuild(const std::string& version, const std::string& commit, const std::string& date) {
    Build b; b.ok = true; b.version = version; b.commit = commit; b.date = date; b.file = "GutsAndBolts-Windows.zip"; return b;
}
Mine me(const std::string& version, const std::string& commit, const std::string& date, bool on, const std::string& installed = "") {
    Mine m; m.version = version; m.commit = commit; m.date = date; m.nightlyOn = on; m.installedNightly = installed; return m;
}
} // namespace

int main() {
    const Build none;

    // Stable (the default): exactly like before.
    check(decide(me("0.8.0", R, "2026-11-10", false), release("0.8.1"), none) == Offer::Stable, "stable: a newer release is offered");
    check(decide(me("0.8.0", R, "2026-11-10", false), release("0.8.0"), none) == Offer::None, "stable: up to date");
    check(decide(me("0.8.0", R, "2026-11-10", false), release("0.8.0"), nightlyBuild("0.8.0", B, "2026-11-13")) == Offer::None,
          "stable: a nightly is never offered with Nightly off");

    // Nightly on.
    check(decide(me("0.8.0", R, "2026-11-10", true), release("0.8.0", R), nightlyBuild("0.8.0", B, "2026-11-13")) == Offer::Nightly,
          "nightly: a newer main build is offered");
    check(decide(me("0.8.0", B, "2026-11-13", true, B), release("0.8.0", R), nightlyBuild("0.8.0", B, "2026-11-13")) == Offer::None,
          "nightly: after updating to it, it isn't offered again (no loop)");
    check(decide(me("0.8.0", B.substr(0, 7), "2026-11-13", true, B), release("0.8.0"), nightlyBuild("0.8.0", B, "2026-11-13")) == Offer::None,
          "nightly: a short commit id counts as the same build");
    check(decide(me("0.8.1", B, "2026-11-13", true), release("0.8.0"), nightlyBuild("0.8.0", A, "2026-11-13")) == Offer::None,
          "nightly: never offers an older version");
    check(decide(me("0.8.0", B, "2026-11-13", true), release("0.8.0"), nightlyBuild("0.8.0", A, "2026-11-12")) == Offer::None,
          "nightly: never offers a build from an earlier day");
    check(decide(me("0.8.0", R, "2026-11-10", true), release("0.8.1"), nightlyBuild("0.8.0", B, "2026-11-13")) == Offer::Stable,
          "nightly: a newer release still wins");
    Build noFile = nightlyBuild("0.8.0", B, "2026-11-13"); noFile.file.clear();
    check(decide(me("0.8.0", R, "2026-11-10", true), release("0.8.0"), noFile) == Offer::None,
          "nightly: nothing for this computer, nothing offered");
    check(decide(me("0.8.0", R, "2026-11-10", true), none, nightlyBuild("0.8.0", B, "2026-11-13")) == Offer::Nightly,
          "nightly: still works when the release list can't be read");

    // Back to Stable.
    check(runningNightly(me("0.8.0", B, "2026-11-13", false, B), release("0.8.0", R)), "a nightly we updated to is a test build");
    check(!runningNightly(me("0.8.0", R, "2026-11-10", false, R), release("0.8.0", R)), "the release's own commit is not a test build");
    check(!runningNightly(me("0.8.0", R, "2026-11-10", false, B), release("0.8.0", R)), "a different copy than the nightly we noted is not one");
    check(decide(me("0.8.0", B, "2026-11-13", false, B), release("0.8.0", R), none) == Offer::BackToStable,
          "turning Nightly off on a test build offers Stable back");
    check(decide(me("0.8.0", R, "2026-11-10", false, B), release("0.8.0", R), none) == Offer::None,
          "after going back to Stable it isn't offered again (no loop)");
    check(decide(me("0.8.1", B, "2026-11-13", false, B), release("0.8.0", R), none) == Offer::None,
          "never 'back' to an older stable version");
    check(decide(me("0.8.0", "unknown", "", false, "unknown"), release("0.8.0"), none) == Offer::None,
          "a build with no commit id is never treated as a test build");

    // Reading the manifests.
    Build p = parse(R"({"version":"0.8.0","commit":")" + B + R"(","date":"2026-11-13","files":{"windows":"W.zip","linux":"L.tar.gz"}})", "linux");
    check(p.ok && p.version == "0.8.0" && p.commit == B && p.date == "2026-11-13" && p.file == "L.tar.gz", "parse: nightly.json");
    check(parse(R"({"version":"0.8.0","files":{"windows":"W.zip"}})", "mac").file.empty(), "parse: no file for this computer");
    check(!parse("<html>offline</html>", "linux").ok, "parse: junk is ignored");
    check(compareVersions("0.6.10", "0.6.9") == 1 && compareVersions("0.8.0", "0.8") == 0, "versions compare number by number");

    std::cout << (failures ? "FAILED: " : "all passed: ") << failures << " problem(s)\n";
    return failures ? 1 : 0;
}
