// The C++ server's experiments (worker/test/experiments.test.mjs checks the website's):
// making them, the game's events adding up per version, and who may send them.
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

int variantOf(const std::string& id, long long user, int count) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned char c : id + ":" + std::to_string(user)) { h ^= c; h *= 0x01000193u; }
    return (int)(h % (uint32_t)count);
}

} // namespace

int main() {
    std::srand((unsigned)Online::unixNow() ^ (unsigned)getpid());
    const fs::path dir = fs::temp_directory_path() / ("gb-experiments-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    ServerUtil::writeFile(dir / "data" / "assets.json",
        json{{"g-obby", {{"kind", "game"}, {"name", "Obby"}, {"creator", Account::id()}, {"created", Online::unixNow()}}}}.dump());
    GbServer::Options opts;
    opts.data = dir / "data";
    opts.official = Account::id();
    std::string err;
    auto startServer = [&]() -> std::unique_ptr<GbServer> {
        for (int tries = 0; tries < 20; ++tries) {
            opts.port = 20000 + std::rand() % 40000;
            auto s = std::make_unique<GbServer>(opts);
            if (s->start(err)) return s;
        }
        return nullptr;
    };
    Other bob;
    std::string id;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        json r = ask(s, "experiments.create", {{"game", "g-obby"}, {"name", "Shop"}, {"variants", {"Old"}}});
        check(error(r).find("2 to 4 versions") != std::string::npos, "one version isn't enough: " + error(r));
        r = ask(s, "experiments.create", {{"game", "g-obby"}, {"name", "Shop"}, {"variants", {"Old", "New"}}, {"metrics", {"funnel:Tutorial"}}});
        check(error(r).find("funnel:Tutorial:3") != std::string::npos, "a funnel needs a step: " + error(r));
        r = ask(s, "experiments.create", {{"game", "g-obby"}, {"name", "Shop"}, {"variants", {"Old", "New"}}, {"metrics", {"BoughtSword", "spent:Coins"}}});
        check(r.value("ok", false) && r["experiments"].size() == 1, "Guts makes an experiment: " + error(r));
        id = r["experiments"][0].value("id", std::string());
        check(!askAs(s, bob, "experiments.create", {{"game", "g-obby"}, {"name", "Mine"}, {"variants", {"A", "B"}}}).value("ok", true),
              "only the creator makes experiments");
        r = askAs(s, bob, "experiments.forGame", {{"game", "g-obby"}});
        check(r.value("ok", false) && r["experiments"].size() == 1 && r["experiments"][0]["variants"].size() == 2, "the game can ask what's running");
        r = askAs(s, bob, "analytics.log", {{"game", "g-obby"}, {"events", json::array({{{"player", 5}, {"name", "__join"}}})}});
        check(error(r).find("own servers") != std::string::npos, "bob isn't the game's server: " + error(r));
        json events = json::array();
        int bought[2] = {0, 0}, players[2] = {0, 0};
        for (long long p = 1000; p < 1100; ++p) {
            const int v = variantOf(id, p, 2);
            ++players[v];
            events.push_back({{"player", p}, {"name", "__join"}});
            events.push_back({{"player", p}, {"name", "__minutes"}, {"value", 10}});
            if (p % 3 == 0) { events.push_back({{"player", p}, {"name", "BoughtSword"}, {"value", 1}}); ++bought[v]; }
            if (events.size() > 150) { ask(s, "analytics.log", {{"game", "g-obby"}, {"events", events}}); events = json::array(); }
        }
        ask(s, "analytics.log", {{"game", "g-obby"}, {"events", events}});
        r = ask(s, "experiments.list", {{"game", "g-obby"}});
        const json& res = r["experiments"][0]["results"];
        check(res[0].value("players", 0) == players[0] && res[1].value("players", 0) == players[1], "players per version match the website's sum");
        check(res[0]["metrics"][0].value("did", 0) == bought[0] && res[1]["metrics"][0].value("did", 0) == bought[1], "events add up per version");
        check(res[0].value("minutes", 0.0) == players[0] * 10.0, "minutes add up");
        check(askAs(s, bob, "asset.info", {{"id", "g-obby"}}).dump().find("Shop") == std::string::npos, "experiments don't show on the game's info");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start again: " << err << "\n"; return 1; }
        GbServer& s = *server;
        json r = ask(s, "experiments.list", {{"game", "g-obby"}});
        check(r["experiments"].size() == 1 && r["experiments"][0]["results"][0].value("players", 0) > 0, "kept after a restart");
        check(ask(s, "experiments.stop", {{"game", "g-obby"}, {"id", id}}).value("ok", false), "stop it");
        check(askAs(s, bob, "experiments.forGame", {{"game", "g-obby"}})["experiments"].empty(), "stopped ones aren't running");
        check(ask(s, "experiments.remove", {{"game", "g-obby"}, {"id", id}})["experiments"].empty(), "remove it");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll experiment checks passed.\n");
    return failures ? 1 : 0;
}
