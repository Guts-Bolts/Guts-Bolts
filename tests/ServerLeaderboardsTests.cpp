// The C++ server's leaderboards and upload achievements (ServerLeaderboards.cpp;
// worker/test/site-leaderboards.test.mjs checks the website's): this week vs all time, only
// public games, art faves, and the new achievements on a profile.
#include "core/Account.h"
#include "online/Protocol.h"
#include "server/Server.h"
#include "server/ServerUtil.h"

#include <monocypher.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
std::string hexOf(const uint8_t* b, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[b[i] >> 4]; s += d[b[i] & 15]; }
    return s;
}
struct Other {
    uint8_t secret[64], pub[32];
    std::string id;
    Other() {
        uint8_t seed[32];
        for (int i = 0; i < 32; ++i) seed[i] = (uint8_t)(std::rand() & 255);
        crypto_eddsa_key_pair(secret, pub, seed);
        id = hexOf(pub, 32);
    }
    std::string sign(const std::string& m) {
        uint8_t sig[64];
        crypto_eddsa_sign(sig, secret, (const uint8_t*)m.data(), m.size());
        return hexOf(sig, 64);
    }
};
json ask(GbServer& s, const std::string& op, const json& args = json::object()) {
    const long long t = Online::unixNow();
    const std::string nonce = Account::randomHex(8);
    json req = {{"op", op}, {"account", Account::id()}, {"time", t}, {"nonce", nonce}, {"args", args},
                {"sig", Account::sign(Online::requestText(op, Account::id(), t, nonce, args))}};
    return s.handle(req);
}
json askAs(GbServer& s, Other& who, const std::string& op, const json& args = json::object()) {
    const long long t = Online::unixNow();
    const std::string nonce = Account::randomHex(8);
    json req = {{"op", op}, {"account", who.id}, {"time", t}, {"nonce", nonce}, {"args", args},
                {"sig", who.sign(Online::requestText(op, who.id, t, nonce, args))}};
    return s.handle(req);
}
std::string error(const json& r) { return r.value("error", std::string()); }
json signUp(GbServer& s, Other& who, const std::string& name) {
    return askAs(s, who, "account.signup", {{"username", name}, {"salt", Account::randomHex(16)}, {"auth", Account::randomHex(32)},
                                            {"key", Account::backupKey(Account::randomHex(32))}});
}
} // namespace

int main() {
    std::srand((unsigned)Online::unixNow() ^ (unsigned)getpid());
    const fs::path dir = fs::temp_directory_path() / ("gb-leaders-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    const long long t = Online::unixNow();
    const std::string today = Online::utcDay(t), yesterday = Online::utcDay(t - 86400), longAgo = Online::utcDay(t - 20 * 86400);
    // Games by Guts (this test's own account): an old hit, a new one, a private one and one waiting for a check.
    auto game = [&](const std::string& name, long long plays, json days, json meta) {
        return json{{"kind", "game"}, {"name", name}, {"creator", Account::id()}, {"created", t}, {"plays", plays}, {"days", days}, {"meta", meta}};
    };
    ServerUtil::writeFile(dir / "data" / "assets.json", json{
        {"g-old", game("Old Hit", 1502, {{longAgo, {{"plays", 1500}}}, {yesterday, {{"plays", 2}}}}, json::object())},
        {"g-new", game("New Thing", 40, {{today, {{"plays", 40}}}}, json::object())},
        {"g-secret", game("Secret", 900, {{today, {{"plays", 900}}}}, {{"access", "private"}})},
        {"g-wait", game("Waiting", 900, {{today, {{"plays", 900}}}}, {{"review", "pending"}})},
    }.dump());
    GbServer::Options opts;
    opts.data = dir / "data";
    opts.official = Account::id();
    std::string err;
    std::unique_ptr<GbServer> server;
    for (int tries = 0; tries < 20 && !server; ++tries) {
        opts.port = 20000 + std::rand() % 40000;
        auto s = std::make_unique<GbServer>(opts);
        if (s->start(err)) server = std::move(s);
    }
    if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
    GbServer& s = *server;
    const std::string png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
    Other bob;
    check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
    check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
    json r = ask(s, "profile", {{"id", "1"}});
    auto earned = [&](const json& p, const std::string& id) {
        for (const json& x : p.value("achievements", json::array())) if (x.value("id", std::string()) == id) return x.value("earned", false);
        return false;
    };
    check(r.value("ok", false) && r["achievements"].size() >= 20, "profiles list the new achievements too");
    check(earned(r, "first-upload") && earned(r, "hit-maker") && !earned(r, "superstar") && !earned(r, "artist"), "first upload and 1,000 visits, no art yet");
    r = ask(s, "art.post", {{"title", "Castle"}, {"cat", "pixel"}, {"data", png}});
    check(r.value("ok", false), "Guts posts art: " + error(r));
    check(askAs(s, bob, "art.fave", {{"id", r["piece"].value("id", std::string())}}).value("faves", 0) == 1, "bob faves it");
    check(earned(ask(s, "profile", {{"id", "1"}}), "artist"), "now Guts is an Artist");

    r = askAs(s, bob, "leaders.get", {{"period", "week"}});
    check(r.value("ok", false) && r.value("period", std::string()) == "week", "anyone can see this week's lists: " + error(r));
    check(r["games"].size() == 2 && r["games"][0].value("name", std::string()) == "New Thing" && r["games"][0].value("visits", 0LL) == 40
          && r["games"][1].value("visits", 0LL) == 2, "this week: only the last 7 days, no private or waiting games");
    check(r["art"].size() == 1 && r["art"][0].value("faves", 0LL) == 1, "most faved art");
    check(r["creators"].size() == 1 && r["creators"][0].value("points", 0LL) == 43 && r["creators"][0].value("faves", 0LL) == 1,
          "top creators: visits + faves");
    r = askAs(s, bob, "leaders.get", {{"period", "all"}});
    check(r["games"].size() == 2 && r["games"][0].value("name", std::string()) == "Old Hit" && r["games"][0].value("visits", 0LL) == 1502, "all time counts everything");
    std::cout << (failures ? "\nSOME CHECKS FAILED\n" : "\nall good\n");
    return failures ? 1 : 0;
}
