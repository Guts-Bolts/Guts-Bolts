// The C++ server's journeys and promotional text (worker/test/journeys.test.mjs and
// promo-text.test.mjs check the website's): ServerJourneys.cpp and ServerPromoText.cpp.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-journeys-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    ServerUtil::writeFile(dir / "data" / "assets.json",
        json{{"g-dungeon", {{"kind", "game"}, {"name", "Dungeon"}, {"creator", Account::id()}, {"created", Online::unixNow()}}}}.dump());
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
    const json stages = json::array({
        {{"name", "Pick a class"}, {"nodes", {"ChoseWarrior", "ChoseMage", "SkippedClass"}}},
        {{"name", "First fight"}, {"nodes", {"WonFight", "LostFight"}}},
        {{"name", "Shop"}, {"nodes", {"VisitedShop"}}}});
    Other bob;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");

        // --- Journeys ---
        json r = ask(s, "journeys.create", {{"game", "g-dungeon"}, {"name", "Start"}, {"stages", json::array()}});
        check(error(r).find("at least one stage") != std::string::npos, "a journey needs a stage: " + error(r));
        r = ask(s, "journeys.create", {{"game", "g-dungeon"}, {"name", "Start"}, {"stages", {{{"name", "S"}, {"nodes", {"__join"}}}}}});
        check(error(r).find("two underscores") != std::string::npos, "built-in names can't be choices: " + error(r));
        r = ask(s, "journeys.create", {{"game", "g-dungeon"}, {"name", "First ten minutes"}, {"stages", stages}});
        check(r.value("ok", false) && r["journeys"].size() == 1, "Guts makes a journey: " + error(r));
        check(!askAs(s, bob, "journeys.list", {{"game", "g-dungeon"}}).value("ok", true), "only the creator sees journeys");
        r = askAs(s, bob, "experiments.forGame", {{"game", "g-dungeon"}});
        check(r.value("journeys", 0) == 1, "the game hears a journey is counting");
        auto ev = [](long long p, const char* n) { return json{{"player", p}, {"name", n}}; };
        r = ask(s, "analytics.log", {{"game", "g-dungeon"}, {"events", json::array({
            ev(10, "__join"), ev(10, "ChoseMage"), ev(10, "WonFight"), ev(10, "VisitedShop"),
            ev(11, "__join"), ev(11, "ChoseWarrior"), ev(11, "ChoseMage"),
            ev(12, "__join"), ev(13, "ChoseMage"), ev(11, "__join")})}});
        check(r.value("ok", false) && r.value("kept", 0) == 7, "the game's events build paths (kept " + std::to_string(r.value("kept", 0)) + ")");
        r = ask(s, "journeys.list", {{"game", "g-dungeon"}});
        const json& j = r["journeys"][0];
        check(j.value("players", 0) == 3 && j.value("leftAtJoin", 0) == 1, "players and who left at the start match the website's sum");
        check(j["stages"][0]["nodes"][0].value("players", 0) == 1 && j["stages"][0]["nodes"][1].value("players", 0) == 1,
              "one warrior, one mage");
        check(j["stages"][0]["nodes"][0].value("left", 0) == 1, "the warrior left before fighting");
        bool joinedToMage = false;
        for (const json& f : j["flows"]) if (f.value("stage", -9) == 0 && f.value("from", -9) == -1 && f.value("to", -9) == 1 && f.value("players", 0) == 1) joinedToMage = true;
        check(joinedToMage, "the flow Joined -> mage is there");
        check(askAs(s, bob, "asset.info", {{"id", "g-dungeon"}}).dump().find("ChoseMage") == std::string::npos, "journeys don't show on the game's info");

        // --- Promotional text ---
        r = ask(s, "game.promo", {{"game", "g-dungeon"}, {"text", "  New map:\n the docks  "}, {"days", 3}});
        check(r.value("ok", false) && r["promo"].value("text", std::string()) == "New map: the docks", "Guts sets promotional text: " + r.dump());
        const long long left = r["promo"].value("until", 0LL) - Online::unixNow();
        check(left > 3 * 86400 - 10 && left <= 3 * 86400, "it lasts three days");
        r = askAs(s, bob, "asset.info", {{"id", "g-dungeon"}});
        check(r.dump().find("New map: the docks") != std::string::npos, "everyone sees it on the game");
        check(!askAs(s, bob, "game.promo", {{"game", "g-dungeon"}, {"text", "mine"}}).value("ok", true), "only the creator sets it");
        r = ask(s, "game.promo", {{"game", "g-dungeon"}, {"text", "dungeon"}});
        check(error(r).find("already the game's name") != std::string::npos, "it isn't just the name again");
        r = ask(s, "game.promo", {{"game", "g-dungeon"}, {"text", ""}});
        check(r.value("ok", false) && r["promo"].is_null(), "clearing takes it off");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start again: " << err << "\n"; return 1; }
        GbServer& s = *server;
        json r = ask(s, "journeys.list", {{"game", "g-dungeon"}});
        check(r.value("ok", false) && r["journeys"].size() == 1 && r["journeys"][0].value("players", 0) == 3, "journeys are kept after a restart");
        const std::string id = r["journeys"][0].value("id", std::string());
        check(ask(s, "journeys.stop", {{"game", "g-dungeon"}, {"id", id}}).value("ok", false), "stop it");
        check(askAs(s, bob, "experiments.forGame", {{"game", "g-dungeon"}}).value("journeys", -1) == 0, "stopped journeys don't count");
        check(ask(s, "journeys.remove", {{"game", "g-dungeon"}, {"id", id}}).value("ok", false), "remove it");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nSome checks failed.\n" : "\nAll journey and promotional text checks passed.\n");
    return failures ? 1 : 0;
}
