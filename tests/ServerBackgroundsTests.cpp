// The C++ server's profile backgrounds (worker/test/profile-backgrounds.test.mjs checks the website's):
// any decal that passed its staff check can go behind your avatar on your profile.
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

int main() {
    std::srand((unsigned)Online::unixNow() ^ (unsigned)getpid());
    const fs::path dir = fs::temp_directory_path() / ("gb-bg-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    const long long t = Online::unixNow();
    ServerUtil::writeFile(dir / "data" / "assets.json", json{
        {"d-sky",  {{"kind", "decal"}, {"name", "Sunset Sky"}, {"creator", Account::id()}, {"created", t}, {"meta", json::object()}}},
        {"d-wait", {{"kind", "decal"}, {"name", "Not Checked"}, {"creator", Account::id()}, {"created", t}, {"meta", {{"review", "pending"}}}}},
        {"g-game", {{"kind", "game"}, {"name", "A Game"}, {"creator", Account::id()}, {"created", t}, {"meta", json::object()}}}}.dump());
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
        check(!askAs(s, bob, "profile.background", {{"decal", "g-game"}}).value("ok", true), "a game isn't a background");
        check(error(askAs(s, bob, "profile.background", {{"decal", "d-wait"}})).find("staff check") != std::string::npos,
              "a decal waiting for its staff check isn't either");
        r = askAs(s, bob, "profile.background", {{"decal", "gb:d-sky"}});
        check(r.value("ok", false) && r["background"].value("name", std::string()) == "Sunset Sky", "bob picks the sky: " + error(r));
        r = ask(s, "profile", {{"id", bob.id}});
        check(r.contains("background") && r["background"].value("id", std::string()) == "d-sky", "his profile shows it to others");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start again: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "profile", {{"id", bob.id}})["background"].value("id", std::string()) == "d-sky", "it's kept after a restart");
        check(askAs(s, bob, "profile.background", {{"decal", ""}}).value("ok", false), "bob takes it off");
        check(ask(s, "profile", {{"id", bob.id}})["background"].is_null(), "...and it's gone");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll profile background checks passed.\n");
    return failures ? 1 : 0;
}
