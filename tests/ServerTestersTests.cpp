// The C++ server's early testers (worker/test/early-testers.test.mjs checks the website's):
// a testers-only game, notes only the creator reads, and going public.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-testers-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    // One testers-only game, made by Guts (this test's own account).
    ServerUtil::writeFile(dir / "data" / "assets.json",
        json{{"g-obby", {{"kind", "game"}, {"name", "Obby"}, {"creator", Account::id()}, {"created", Online::unixNow()},
                         {"meta", {{"access", "testers"}}}}}}.dump());
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
    Other bob, cat;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        json r = signUp(s, bob, "bob");
        check(r.value("ok", false), "bob signs up: " + error(r));
        r = signUp(s, cat, "cat");
        check(r.value("ok", false), "cat signs up: " + error(r));
        r = askAs(s, bob, "servers.play", {{"game", "g-obby"}});
        check(!r.value("ok", true) && error(r).find("still being tested") != std::string::npos, "bob can't play a testers-only game yet");
        check(!askAs(s, bob, "game.addTester", {{"game", "g-obby"}, {"who", "cat"}}).value("ok", true), "only the creator picks testers");
        r = ask(s, "game.addTester", {{"game", "g-obby"}, {"who", "bob"}});
        check(r.value("ok", false) && r["testers"].size() == 1, "Guts adds bob: " + error(r));
        check(askAs(s, bob, "servers.play", {{"game", "g-obby"}}).value("ok", false), "now bob can play");
        check(!askAs(s, cat, "servers.play", {{"game", "g-obby"}}).value("ok", true), "cat still can't");
        check(askAs(s, bob, "game.amTester", {{"game", "g-obby"}}).value("tester", false), "bob is a tester");
        check(!askAs(s, cat, "game.testerNote", {{"game", "g-obby"}, {"stars", 4}, {"text", "hi"}}).value("ok", true), "cat can't leave a note");
        check(!askAs(s, bob, "game.testerNote", {{"game", "g-obby"}, {"stars", 7}, {"text", "hi"}}).value("ok", true), "stars are 1 to 5");
        r = askAs(s, bob, "game.testerNote", {{"game", "g-obby"}, {"stars", 3}, {"text", "Level 2 is too hard"}});
        check(r.value("ok", false), "bob leaves a note: " + error(r));
        r = ask(s, "game.testers", {{"game", "g-obby"}});
        check(r.value("ok", false) && r["notes"].size() == 1 && r["notes"][0].value("stars", 0) == 3 && r["testers"][0].value("noted", false),
              "Guts reads the note");
        check(!askAs(s, cat, "game.testers", {{"game", "g-obby"}}).value("ok", true), "cat can't read the notes");
        r = askAs(s, cat, "asset.info", {{"id", "g-obby"}});
        check(r.dump().find("too hard") == std::string::npos, "the notes never show up for others");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start again: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(askAs(s, bob, "servers.play", {{"game", "g-obby"}}).value("ok", false), "the testers are kept after a restart");
        check(ask(s, "game.testers", {{"game", "g-obby"}})["notes"].size() == 1, "...and so are the notes");
        check(ask(s, "game.settings", {{"id", "g-obby"}, {"access", "public"}}).value("ok", false), "Guts makes it public");
        json n = askAs(s, bob, "notes.list");
        check(n.dump().find("is out for everyone now") != std::string::npos, "bob is told it's out");
        check(askAs(s, cat, "servers.play", {{"game", "g-obby"}}).value("ok", false), "now cat can play too");
        check(!ask(s, "game.settings", {{"id", "g-obby"}, {"access", "nope"}}).value("ok", true), "a made-up access is refused");
        check(ask(s, "game.settings", {{"id", "g-obby"}, {"access", "private"}}).value("ok", false), "Guts makes it private");
        check(!askAs(s, bob, "servers.play", {{"game", "g-obby"}}).value("ok", true), "a private game can't be played by others");
        check(!askAs(s, bob, "servers.list", {{"game", "g-obby"}}).value("ok", true), "...or even listed");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll early tester checks passed.\n");
    return failures ? 1 : 0;
}
