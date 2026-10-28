// The updater (core/UpdateChecker): a failed update is noticed so the app doesn't
// close and reopen over and over, and the update script really swaps the files in
// (or says why it couldn't).
//
//   UpdaterTests
#include "core/UpdateChecker.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
} // namespace

int main() {
    using UpdateChecker::failedReason;
    const long long now = 1'800'000'000;

    // --- Reading the status file ---
    check(failedReason("", "0.7.7", now).empty(), "no status file: never tried, so update normally");
    check(failedReason("0.7.7 " + std::to_string(now - 30) + "\nfailed: couldn't replace GutsAndBolts.exe (Access denied.)\n", "0.7.7", now)
              == "couldn't replace GutsAndBolts.exe (Access denied.)",
          "a failed try gives its reason");
    check(failedReason("0.7.7 " + std::to_string(now - 30) + "\n", "0.7.7", now) == "the updater stopped before it finished",
          "a try that never reported back counts as failed");
    check(!failedReason("0.7.7 " + std::to_string(now - 30) + "\r\nok\r\n", "0.7.7", now).empty(),
          "\"ok\" but still on the old version still counts as failed (no loop)");
    check(failedReason("0.7.6 " + std::to_string(now - 30) + "\nfailed: x\n", "0.7.7", now).empty(),
          "a failed try at an older version doesn't block this one");
    check(failedReason("0.7.7 " + std::to_string(now - 2 * 3600) + "\nfailed: x\n", "0.7.7", now).empty(),
          "after an hour it may try once more by itself");
    check(failedReason("garbage", "0.7.7", now).empty(), "a broken status file is ignored");

    // --- The scripts ---
    UpdateChecker::ScriptArgs a;
    a.url = "https://example.com/GutsAndBolts-Windows.zip";
    a.folder = "C:\\Users\\D'Arcy\\Guts&Bolts";
    a.app = "C:\\Users\\D'Arcy\\Guts&Bolts\\GutsAndBolts.exe";
    a.status = "C:\\Temp\\gutsandbolts-update-status.txt";
    a.pid = 4242;
    const std::string ps = UpdateChecker::updaterScript(UpdateChecker::ScriptKind::PowerShell, a);
    check(has(ps, "$ErrorActionPreference = 'Stop'"), "PowerShell: stops at the first error");
    check(has(ps, "Wait-Process -Id 4242"), "PowerShell: waits for the app to really close");
    check(has(ps, "'C:\\Users\\D''Arcy\\Guts&Bolts'"), "PowerShell: a ' in the folder name is escaped");
    check(has(ps, "'failed: '") && has(ps, "'ok'"), "PowerShell: writes how it went");
    check(has(ps, ".update-old"), "PowerShell: moves a still-open program out of the way");
    check(ps.rfind("Start-Process") > ps.rfind("catch"), "PowerShell: reopens the app after writing the result");

#ifndef _WIN32
    // Run the real shell script on a pretend install, with a pretend download.
    const fs::path root = fs::temp_directory_path() / ("gb-updater-test-" + std::to_string(::getpid()));
    fs::remove_all(root);
    const fs::path install = root / "my 'install'";
    write(install / "GutsAndBolts", "old studio");
    write(install / "games" / "Mine.gbscene", "my own game");
    write(install / "games" / "Obby.gbscene", "my edited obby");
    const fs::path pkg = root / "pkg" / "GutsAndBolts-Linux";
    write(pkg / "GutsAndBolts", "new studio");
    write(pkg / "GutsAndBoltsPlayer", "new player");
    write(pkg / "games" / "Obby.gbscene", "stock obby");
    write(pkg / "games" / "New.gbscene", "a new sample game");
    const std::string tar = (root / "update.tar.gz").string();
    check(std::system(("tar czf '" + tar + "' -C '" + (root / "pkg").string() + "' GutsAndBolts-Linux").c_str()) == 0, "made a pretend download");

    auto runScript = [&](const std::string& url) {
        UpdateChecker::ScriptArgs s;
        s.url = url;
        s.folder = install.string();
        s.app = "true";
        s.status = (root / "status.txt").string();
        s.pid = 0x7ffffff0;   // nobody: no waiting
        write(s.status, "0.7.7 1\n");
        write(root / "update.sh", UpdateChecker::updaterScript(UpdateChecker::ScriptKind::Shell, s));
        std::system(("sh '" + (root / "update.sh").string() + "' >/dev/null 2>&1").c_str());
        return read(s.status);
    };

    std::string status = runScript("file://" + tar);
    check(has(status, "\nok"), "shell: says ok when it worked");
    check(read(install / "GutsAndBolts") == "new studio", "shell: the program was replaced");
    check(read(install / "GutsAndBoltsPlayer") == "new player", "shell: new programs were added");
    check(read(install / "games" / "Mine.gbscene") == "my own game", "shell: your own games are kept");
    check(read(install / "games" / "Obby.gbscene") == "my edited obby", "shell: your edits to sample games are kept");
    check(read(install / "games" / "New.gbscene") == "a new sample game", "shell: new sample games are added");

    write(install / "GutsAndBolts", "old studio");
    status = runScript("file://" + (root / "missing.tar.gz").string());
    check(has(status, "failed: couldn't download"), "shell: a failed download is written down");
    check(read(install / "GutsAndBolts") == "old studio", "shell: nothing changed after a failed download");
    check(!failedReason(status, "0.7.7", 2).empty(), "shell: the app sees that the update failed");
    fs::remove_all(root);
#endif

    std::cout << (failures ? "FAILED: " : "all passed: ") << failures << " problem(s)\n";
    return failures ? 1 : 0;
}
