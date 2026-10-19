#pragma once
// Small helpers shared by the server's files.
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <nlohmann/json.hpp>
#include "../online/Protocol.h"
#include "../core/TextFilter.h"

namespace ServerUtil {

using json = nlohmann::json;
namespace fs = std::filesystem;

inline std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
inline bool isHex(const std::string& s, size_t minLen, size_t maxLen) {
    if (s.size() < minLen || s.size() > maxLen) return false;
    for (char c : s) if (!std::isxdigit((unsigned char)c)) return false;
    return true;
}
// What people write for others to read goes through the text filter (core/TextFilter).
inline std::string say(const std::string& s, size_t maxLen, bool allowNewlines = false) {
    return TextFilter::filter(Online::cleanText(s, maxLen, allowNewlines));
}
// Lyreto (lyreto.com) is the chat app Guts&Bolts partners with: games and groups can link their
// Lyreto server. A link cleaned up to https://lyreto.com/... ("" for no link), or false when it
// isn't a Lyreto link (the same rules as lyretoLink in worker/server.js).
inline bool lyretoLink(std::string s, std::string& out) {
    out.clear();
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(0, 1);
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    if (s.empty()) return true;
    if (s.size() > 200) return false;
    const std::string l = lower(s);
    size_t at = 0;
    if (l.rfind("https://", 0) == 0) at = 8;
    else if (l.rfind("http://", 0) == 0) at = 7;
    else if (l.find("://") != std::string::npos) return false;
    if (l.compare(at, 4, "www.") == 0) at += 4;
    if (l.compare(at, 10, "lyreto.com") != 0) return false;
    at += 10;
    if (at < s.size() && s[at] != '/' && s[at] != '?') return false;   // lyreto.com.evil.com, lyreto.com:8080
    const size_t q = s.find('?', at);
    std::string path = s.substr(at, q == std::string::npos ? std::string::npos : q - at);
    std::string query = q == std::string::npos ? "" : s.substr(q);
    while (!path.empty() && path.back() == '/') path.pop_back();
    if (path.empty()) return false;   // just lyreto.com isn't anyone's server
    const auto ok = [](char c, const char* extra) {
        return std::isalnum((unsigned char)c) || std::strchr(extra, c) != nullptr;
    };
    bool lastSlash = false;
    for (char c : path) {
        if (c == '/') { if (lastSlash) return false; lastSlash = true; continue; }
        lastSlash = false;
        if (!ok(c, "._~-")) return false;
    }
    if (!query.empty()) {
        if (query.size() < 2 || query.size() > 101) return false;
        for (size_t i = 1; i < query.size(); ++i) if (!ok(query[i], "._~=&-")) return false;
    }
    out = "https://lyreto.com" + path + query;
    return true;
}
inline json fail(const std::string& why) { return {{"ok", false}, {"error", why}}; }
inline json okay() { return {{"ok", true}}; }

// Write a file safely: to a temporary name first, then swap it in.
inline bool writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), (std::streamsize)text.size());
        if (!f) return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) {   // Windows can't rename over an existing file
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
    }
    return !ec;
}
inline bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Library models' "Best rated" sort: the Wilson score (the lowest the real like rate is likely
// to be, 95% sure), so 1 vote at 100% doesn't beat 500 votes at 95%. Unrated scores 0.
// (worker/server.js wilsonScore has the same.)
inline double wilsonScore(long long up, long long down) {
    const double n = (double)(up + down);
    if (n <= 0) return 0;
    const double z = 1.96, p = (double)up / n;
    return (p + z * z / (2 * n) - z * std::sqrt((p * (1 - p) + z * z / (4 * n)) / n)) / (1 + z * z / n);
}
// Games' rating for the "rated" sort: likes out of votes, pulled towards 50% while there are few.
inline double gameRating(long long up, long long down) { return (double)(up + 1) / (double)(up + down + 2); }
// Things people give a thumbs up or down: games and Library models.
inline bool hasVotes(const std::string& kind) { return kind == "game" || kind == "model"; }
constexpr size_t kMaxUsedModels = 2000;   // Library models someone inserted (they can rate those), newest kept
constexpr size_t kGiftNote = 100;         // the note that comes with a gift

inline void log(const std::string& what) {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    std::printf("[%s] %s\n", buf, what.c_str());
    std::fflush(stdout);
}


} // namespace ServerUtil
