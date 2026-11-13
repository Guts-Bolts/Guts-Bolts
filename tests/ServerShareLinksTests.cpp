// The C++ server's share links (worker/test/share-links.test.mjs checks the website's):
// only owners make them, clicks and different people, plays and buys counted once.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-share-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    ServerUtil::writeFile(dir / "data" / "assets.json",
        json{{"g-obby", {{"kind", "game"}, {"name", "Obby"}, {"creator", Account::id()}, {"created", Online::unixNow()}}},
             {"h-cap", {{"kind", "hat"}, {"name", "Cool Cap"}, {"creator", Account::id()}, {"created", Online::unixNow()}, {"price", 10}}}}.dump());
    ServerUtil::writeFile(dir / "data" / "files" / "g-obby", "{\"nodes\":[]}");
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
    std::string code, capCode;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");
        check(!askAs(s, bob, "share.create", {{"kind", "game"}, {"id", "g-obby"}}).value("ok", true), "only the owner makes links");
        json r = ask(s, "share.create", {{"kind", "game"}, {"id", "g-obby"}, {"label", "Discord"}});
        check(r.value("ok", false) && r["link"].value("path", std::string()).rfind("/s/", 0) == 0, "Guts makes a link: " + error(r));
        code = r["link"].value("code", std::string());
        r = askAs(s, bob, "share.open", {{"code", code}, {"visitor", "abc"}});
        check(r.value("path", std::string()) == "#/game/g-obby", "opening it says where to go: " + error(r));
        askAs(s, bob, "share.open", {{"code", code}, {"visitor", "abc"}});
        check(askAs(s, bob, "share.done", {{"code", code}})["counted"].empty(), "nothing done yet");
        check(askAs(s, bob, "get", {{"id", "g-obby"}}).value("ok", false), "bob plays");
        r = askAs(s, bob, "share.done", {{"code", code}});
        check(r["counted"].size() == 1 && r["counted"][0] == "play", "the play counts: " + r.dump());
        check(askAs(s, bob, "share.done", {{"code", code}})["counted"].empty(), "...once");
        askAs(s, cat, "get", {{"id", "g-obby"}});
        askAs(s, cat, "share.open", {{"code", code}, {"visitor", "q"}});
        check(askAs(s, cat, "share.done", {{"code", code}})["counted"].empty(), "played before the click: not counted");
        r = ask(s, "share.list", {{"kind", "game"}, {"id", "g-obby"}});
        const json& t = r["links"][0]["total"];
        check(t.value("clicks", 0) == 3 && t.value("people", 0) == 2 && t.value("play", 0) == 1, "totals: " + t.dump());
        check(!askAs(s, bob, "share.list", {{"kind", "game"}, {"id", "g-obby"}}).value("ok", true), "only the owner sees the numbers");
        capCode = ask(s, "share.create", {{"kind", "item"}, {"id", "h-cap"}})["link"].value("code", std::string());
        askAs(s, bob, "share.open", {{"code", capCode}, {"visitor", "b"}});
        check(askAs(s, bob, "buy", {{"id", "h-cap"}}).value("ok", false), "bob buys the cap");
        r = askAs(s, bob, "share.done", {{"code", capCode}});
        check(r["counted"].size() == 1 && r["counted"][0] == "buy", "the buy counts");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start again: " << err << "\n"; return 1; }
        GbServer& s = *server;
        json r = ask(s, "share.list", {{"kind", "item"}, {"id", "h-cap"}});
        check(r["links"].size() == 1 && r["links"][0]["total"].value("spent", 0) == 10, "kept after a restart, with what was spent");
        check(!askAs(s, bob, "share.remove", {{"code", code}}).value("ok", true), "bob can't remove Guts's link");
        check(ask(s, "share.remove", {{"code", code}}).value("ok", false), "Guts removes it");
        check(!askAs(s, bob, "share.open", {{"code", code}}).value("ok", true), "a removed link goes nowhere");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll share link checks passed.\n");
    return failures ? 1 : 0;
}
