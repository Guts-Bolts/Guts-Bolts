// The C++ server's levels, daily streaks, profile walls and showcases (ServerLevels.cpp;
// worker/test/levels.test.mjs checks the website's).
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
    const fs::path dir = fs::temp_directory_path() / ("gb-levels-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
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
    Other bob, cat, dan;
    std::string bobNum, post;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");
        check(signUp(s, dan, "dan").value("ok", false), "dan signs up");
        json me = askAs(s, bob, "levels.me");
        check(me.value("ok", false) && me["levels"].value("level", 0) == 1 && me["levels"].value("xp", -1) == 0, "everyone starts at level 1");
        bobNum = std::to_string(askAs(s, bob, "hello")["me"].value("userId", 0LL));

        // The daily Bolts and the streak.
        json r = askAs(s, bob, "bolts.daily");
        check(r.value("ok", false) && r.value("got", 0) == 25 && r.value("streak", 0) == 1, "day one: 25 Bolts");
        check(r["me"]["levels"].value("xp", 0) == 20, "and 20 XP");
        check(r["me"]["levels"].value("nextDaily", 0) == 30, "tomorrow is worth 30");
        check(!askAs(s, bob, "bolts.daily").value("ok", true), "once a day");

        // The wall: friends only to start with.
        check(error(askAs(s, cat, "wall.post", {{"id", bobNum}, {"text", "hi bob"}})).find("friends") != std::string::npos, "not friends: can't post");
        check(askAs(s, bob, "wall.post", {{"id", bobNum}, {"text", "my own wall"}}).value("ok", false), "the owner can always post");
        check(askAs(s, bob, "wall.who", {{"who", "everyone"}}).value("ok", false), "bob opens his wall to everyone");
        check(error(askAs(s, bob, "wall.who", {{"who", "lol"}})).find("friends, everyone or nobody") != std::string::npos, "only the three choices");
        r = askAs(s, cat, "wall.post", {{"id", bobNum}, {"text", std::string(1000, 'x')}});
        check(r.value("ok", false) && r["post"].value("text", std::string()).size() == 300, "cat posts (cut to 300 letters)");
        post = r["post"].value("id", std::string());
        check(error(askAs(s, cat, "wall.post", {{"id", bobNum}, {"text", "   "}})).find("Write something") != std::string::npos, "empty posts are refused");
        r = askAs(s, dan, "wall.list", {{"id", bobNum}});
        check(r.value("ok", false) && r["posts"].size() == 2 && r["posts"][0]["by"].value("name", std::string()) == "cat", "the wall, newest first");
        check(!r["posts"][0].value("canDelete", true), "dan can't delete cat's post");
        check(error(askAs(s, dan, "wall.delete", {{"id", bobNum}, {"post", post}})).find("your own") != std::string::npos, "so dan's delete is refused");
        check(askAs(s, bob, "wall.delete", {{"id", bobNum}, {"post", post}}).value("ok", false), "the owner deletes it");
        check(error(askAs(s, bob, "wall.delete", {{"id", bobNum}, {"post", post}})).find("gone") != std::string::npos, "and it's gone");
        askAs(s, bob, "wall.who", {{"who", "nobody"}});
        check(error(askAs(s, cat, "wall.post", {{"id", bobNum}, {"text", "hello?"}})).find("closed") != std::string::npos, "a closed wall");

        // XP: wall posts count a few times a day, then stop.
        const long long before = askAs(s, cat, "levels.me")["levels"].value("xp", 0LL);
        for (int i = 0; i < 15; ++i) askAs(s, cat, "wall.post", {{"id", std::to_string(askAs(s, cat, "hello")["me"].value("userId", 0LL))}, {"text", "note " + std::to_string(i)}});
        const long long after = askAs(s, cat, "levels.me")["levels"].value("xp", 0LL);
        check(after - before == 45, "wall posts: 5 XP, 10 times a day, one used already (got " + std::to_string(after - before) + ")");

        // The profile shows the level and the showcase.
        r = askAs(s, dan, "profile", {{"id", bobNum}});
        check(r.value("ok", false) && r.contains("levels") && r["levels"].value("level", 0) == 1, "the profile shows the level");
        check(r["levels"]["showcase"].is_array() && r["levels"]["showcase"].empty() && r["levels"].value("wallWho", std::string()) == "nobody", "and the showcase and wall setting");
        check(error(askAs(s, bob, "showcase.set", {{"ids", {"nope"}}})).find("made or own") != std::string::npos, "only your own things");
        check(error(askAs(s, bob, "showcase.set", {{"ids", {"1", "2", "3", "4", "5", "6", "7"}}})).find("up to 6") != std::string::npos, "at most 6");
        check(askAs(s, bob, "showcase.set", {{"ids", json::array()}}).value("ok", false), "an empty showcase is fine");
    }
    {   // Kept after a restart.
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't restart: " << err << "\n"; return 1; }
        GbServer& s = *server;
        json r = askAs(s, bob, "levels.me");
        check(r["levels"].value("xp", 0) == 20 + 5 && r["levels"].value("streak", 0) == 1, "XP and the streak are kept");
        check(askAs(s, cat, "wall.list", {{"id", bobNum}})["posts"].size() == 1, "and the wall");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll levels checks passed.\n");
    return failures ? 1 : 0;
}
