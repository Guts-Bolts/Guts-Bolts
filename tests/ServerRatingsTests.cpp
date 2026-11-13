// The C++ server's content levels (worker/test/content-levels.test.mjs checks the website's):
// answers -> rating, its history, and players' rating limits.
#include "core/Account.h"
#include "online/Protocol.h"
#include "server/Server.h"
#include "server/ServerUtil.h"

#include <monocypher.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
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

json noneAnswers() {
    json a = json::object();
    for (const char* q : {"violence", "scary", "language", "romance", "substances", "chance"}) a[q] = "none";
    return a;
}
std::vector<std::string> gameNames(GbServer& s, Other& who) {
    std::vector<std::string> out;
    json r = askAs(s, who, "list", {{"kind", "game"}});
    // (Only this test's three games: the server can add its own sample games too.)
    for (const json& a : r.value("assets", json::array())) {
        const std::string n = a.value("name", std::string());
        if (n == "Calm Farm" || n == "Night Terror" || n == "Unrated") out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

int main() {
    std::srand((unsigned)Online::unixNow() ^ (unsigned)getpid());
    const fs::path dir = fs::temp_directory_path() / ("gb-ratings-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    const long long t = Online::unixNow();
    ServerUtil::writeFile(dir / "data" / "assets.json", json{
        {"g-farm",  {{"kind", "game"}, {"name", "Calm Farm"}, {"creator", Account::id()}, {"created", t}, {"meta", json::object()}}},
        {"g-night", {{"kind", "game"}, {"name", "Night Terror"}, {"creator", Account::id()}, {"created", t}, {"meta", json::object()}}},
        {"g-new",   {{"kind", "game"}, {"name", "Unrated"}, {"creator", Account::id()}, {"created", t}, {"meta", json::object()}}}}.dump());
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
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        json r = signUp(s, bob, "bob");
        check(r.value("ok", false), "bob signs up: " + error(r));
        check(!askAs(s, bob, "game.rate", {{"game", "g-farm"}, {"answers", noneAnswers()}}).value("ok", true), "only the creator rates a game");
        check(error(ask(s, "game.rate", {{"game", "g-farm"}, {"answers", {{"violence", "none"}}}})).find("every question") != std::string::npos,
              "every question needs an answer");
        r = ask(s, "game.rate", {{"game", "g-farm"}, {"answers", noneAnswers()}});
        check(r.value("rating", std::string()) == "mild", "no to everything: Mild");
        json gore = noneAnswers(); gore["violence"] = "gore"; gore["scary"] = "mild";
        r = ask(s, "game.rate", {{"game", "g-night"}, {"answers", gore}});
        check(r.value("rating", std::string()) == "extreme", "gore: Extreme");
        r = askAs(s, bob, "game.rating", {{"game", "g-night"}});
        check(r.value("rating", std::string()) == "extreme" && r["history"].size() == 1 && r["answers"].value("violence", std::string()) == "gore",
              "anyone can see why: answers and history");
        r = askAs(s, bob, "asset.info", {{"id", "g-night"}});
        check(r["asset"].value("rating", std::string()) == "extreme", "the rating is on the game");
        check(gameNames(s, bob).size() == 3, "no limit: every game shows");
        check(!askAs(s, bob, "rating.limit", {{"limit", "kids"}}).value("ok", true), "a made-up limit is refused");
        check(askAs(s, bob, "rating.limit", {{"limit", "strong"}}).value("limit", std::string()) == "strong", "bob picks Strong");
        auto names = gameNames(s, bob);
        check(names == std::vector<std::string>({"Calm Farm", "Unrated"}), "the Extreme game leaves bob's lists");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start again: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(askAs(s, bob, "rating.limit").value("limit", std::string()) == "strong", "the limit is kept after a restart");
        check(askAs(s, bob, "game.rating", {{"game", "g-night"}}).value("rating", std::string()) == "extreme", "...and so is the rating");
        check(gameNames(s, bob).size() == 2, "...and the list still leaves it out");
        askAs(s, bob, "rating.limit", {{"limit", ""}});
        check(gameNames(s, bob).size() == 3, "no limit again: back to every game");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll content level checks passed.\n");
    return failures ? 1 : 0;
}
