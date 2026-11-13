#include "UpdateChannel.h"
#include "Paths.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <vector>

namespace UpdateChannel {

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

bool sameCommit(const std::string& a, const std::string& b) {
    if (a.size() < 7 || b.size() < 7 || a == "unknown" || b == "unknown") return false;
    const size_t n = std::min(a.size(), b.size());
    return a.compare(0, n, b, 0, n) == 0;
}

bool runningNightly(const Mine& mine, const Build& stable) {
    if (!sameCommit(mine.commit, mine.installedNightly)) return false;
    return !(stable.ok && sameCommit(stable.commit, mine.commit));   // the release itself isn't a "test build"
}

Offer decide(const Mine& mine, const Build& stable, const Build& nightly) {
    // 1. A newer release: the normal update, same as always.
    if (stable.ok && compareVersions(mine.version, stable.version) < 0) return Offer::Stable;
    // 2. Nightly on: a different build, not an older version, not from an earlier day.
    if (mine.nightlyOn && nightly.ok && !nightly.file.empty() && !nightly.commit.empty() &&
        !sameCommit(mine.commit, nightly.commit) &&
        compareVersions(nightly.version, mine.version) >= 0 &&
        !(!mine.date.empty() && !nightly.date.empty() && nightly.date < mine.date))
        return Offer::Nightly;
    // 3. Nightly turned off while on a test build: offer the stable version back.
    if (!mine.nightlyOn && stable.ok && !stable.file.empty() && runningNightly(mine, stable) &&
        compareVersions(stable.version, mine.version) >= 0)
        return Offer::BackToStable;
    return Offer::None;
}

Build parse(const std::string& text, const char* platform) {
    Build b;
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("version") || !j["version"].is_string()) return b;
    b.ok = true;
    b.version = j["version"].get<std::string>();
    if (j.contains("commit") && j["commit"].is_string()) b.commit = j["commit"].get<std::string>();
    if (j.contains("date") && j["date"].is_string()) b.date = j["date"].get<std::string>();
    if (j.contains("files") && j["files"].is_object() && j["files"].contains(platform) && j["files"][platform].is_string())
        b.file = j["files"][platform].get<std::string>();
    return b;
}

namespace {
std::mutex g_fileMutex;

nlohmann::json readChoice() {
    std::ifstream in(Paths::file("update-channel.json"));
    if (!in) return nlohmann::json::object();
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    return j.is_object() ? j : nlohmann::json::object();
}

void writeChoice(const nlohmann::json& j) {
    std::ofstream out(Paths::file("update-channel.json"), std::ios::trunc);
    if (out) out << j.dump(2);
}
} // namespace

bool nightlyOn() {
    std::lock_guard<std::mutex> lock(g_fileMutex);
    nlohmann::json j = readChoice();
    return j.contains("nightly") && j["nightly"].is_boolean() && j["nightly"].get<bool>();
}

void setNightlyOn(bool on) {
    std::lock_guard<std::mutex> lock(g_fileMutex);
    nlohmann::json j = readChoice();
    j["nightly"] = on;
    writeChoice(j);
}

std::string installedNightly() {
    std::lock_guard<std::mutex> lock(g_fileMutex);
    nlohmann::json j = readChoice();
    return j.contains("installedNightly") && j["installedNightly"].is_string() ? j["installedNightly"].get<std::string>() : "";
}

void rememberNightly(const std::string& commit) {
    std::lock_guard<std::mutex> lock(g_fileMutex);
    nlohmann::json j = readChoice();
    j["installedNightly"] = commit;
    writeChoice(j);
}

} // namespace UpdateChannel
