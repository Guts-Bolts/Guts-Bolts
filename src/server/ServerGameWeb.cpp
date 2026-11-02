// Games talking to the web (HttpService) and the secrets they use (worker/game-web.js has the
// same rules, and is the one that really makes the requests).
//
// This server keeps a game's "Web requests" switch and its secrets, so the website's Settings
// work the same, but it has no way to make web requests itself: http.request says so. Secrets
// are write-only: their values are kept in data/gamesecrets/<game>.json and never sent back.
//
//   game.secrets      {id}                                  -> {http, secrets: [{name, host, dedicatedOnly, updated}], hosts}
//   game.secretSet    {id, name, value, host, dedicatedOnly} -> the same
//   game.secretRemove {id, name}                            -> the same
//   http.request      {game, url, method, headers, body}    -> not on this server
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

#include <cctype>
#include <ctime>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
const char* const kWebHosts[] = {"lyreto.com"};   // (worker/game-web.js WEB_HOSTS)
constexpr size_t kSecretsPerGame = 20, kSecretValue = 2048, kSecretMin = 8;

bool secretName(const std::string& s) {
    if (s.empty() || s.size() > 50) return false;
    for (char c : s) if (!std::isalnum((unsigned char)c) && c != '_') return false;
    return true;
}
bool webHost(const std::string& h) {
    for (const char* x : kWebHosts) if (h == x) return true;
    return false;
}
} // namespace

json GbServer::webOp(const std::string& name, User& me, const json& args) {
    if (name == "http.request")
        return fail("Web requests from games only work on the main Guts&Bolts server (gutsandbolts.net), not on this one.");
    auto it = findAsset(args.value("id", std::string()));
    if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
    Asset& a = it->second;
    if (a.creator != me.id) return fail("Only the game's creator can see or change its secrets.");
    for (char c : a.id)   // (it names a file)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return fail("That game doesn't exist (any more).");
    const fs::path file = m_opts.data / "gamesecrets" / (a.id + ".json");
    std::string text;
    json all = readFile(file, text) ? json::parse(text, nullptr, false) : json::object();
    if (!all.is_object()) all = json::object();
    auto list = [&]() {
        json r = okay(), out = json::array(), hosts = json::array();
        for (auto& [k, v] : all.items())
            out.push_back({{"name", k}, {"host", v.value("host", std::string())}, {"dedicatedOnly", v.value("dedicated", false)},
                           {"updated", v.value("updated", 0LL)}});
        for (const char* h : kWebHosts) hosts.push_back(h);
        r["http"] = a.meta.value("httpOn", false); r["secrets"] = out; r["hosts"] = hosts;
        return r;
    };
    auto save = [&]() {
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        writeFile(file, all.dump());
    };
    if (name == "game.secrets") return list();
    std::string key = args.value("name", std::string());
    while (!key.empty() && key.front() == ' ') key.erase(key.begin());
    while (!key.empty() && key.back() == ' ') key.pop_back();
    if (!secretName(key)) return fail("Secret names are 1-50 letters, numbers and _ (like LyretoKey).");
    if (name == "game.secretRemove") { all.erase(key); save(); return list(); }
    if (name == "game.secretSet") {
        const std::string value = args.contains("value") && args["value"].is_string() ? args["value"].get<std::string>() : "";
        if (value.empty()) return fail("Paste the secret's value.");
        if (value.size() < kSecretMin) return fail("That secret is too short (8 letters at least): real API keys are longer.");
        if (value.size() > kSecretValue) return fail("That secret is too long (2048 letters at most).");
        if (value.find_first_of(std::string("\r\n\0", 3)) != std::string::npos) return fail("A secret can't have line breaks in it.");
        std::string host = args.value("host", std::string());
        for (char& c : host) c = (char)std::tolower((unsigned char)c);
        if (!webHost(host)) return fail("Pick the site this secret is for (lyreto.com).");
        if (!all.contains(key) && all.size() >= kSecretsPerGame) return fail("A game can have 20 secrets. Remove one first.");
        all[key] = {{"value", value}, {"host", host}, {"dedicated", args.value("dedicatedOnly", false) == true}, {"updated", (long long)std::time(nullptr)}};
        save();
        return list();
    }
    return fail("Unknown request.");
}
