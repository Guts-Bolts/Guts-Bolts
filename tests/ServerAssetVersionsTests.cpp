// The C++ server's asset versions (worker/test/asset-versions.test.mjs checks the website's):
// updating a model keeps its old versions, one can be put back, and only its creator can.
// "bob" signs with a second key made here, like ServerBanListsTests.
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
std::string model(const std::string& name) {
    return Online::base64Encode(json{{"format", "gbmodel"}, {"nodes", json::array({{{"id", 1}, {"name", name}, {"kind", "Part"}}})}}.dump());
}
std::string error(const json& r) { return r.value("error", std::string()); }
} // namespace

int main() {
    std::srand((unsigned)Online::unixNow() ^ (unsigned)getpid());
    const fs::path dir = fs::temp_directory_path() / ("gb-assetversions-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
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
    Other bob;
    check(askAs(s, bob, "hello", {{"name", "bob"}}).value("ok", false), "bob says hello");

    json r = ask(s, "upload", {{"kind", "model"}, {"name", "Crate"}, {"data", model("Crate")}});
    check(r.value("ok", false), "upload a model: " + error(r));
    const std::string id = r["asset"].value("id", std::string());
    r = ask(s, "update", {{"id", id}, {"data", model("Crate2")}});
    check(r.value("ok", false) && r.value("version", 0LL) == 2 && r.value("newVersion", false), "an update is version 2");
    r = ask(s, "update", {{"id", id}, {"data", model("Broken")}});
    check(r.value("version", 0LL) == 3, "and the next is version 3");
    r = ask(s, "update", {{"id", id}, {"data", model("Broken")}});
    check(r.value("ok", false) && !r.value("newVersion", true), "the same file again adds no version");
    r = ask(s, "asset.versions", {{"id", id}});
    check(r.value("ok", false) && r["versions"].size() == 3 && r["versions"][0].value("live", false), "asset.versions lists 3, newest first");
    check(!askAs(s, bob, "asset.versions", {{"id", id}}).value("ok", true), "bob can't see them");
    check(!askAs(s, bob, "asset.restore", {{"id", id}, {"version", 2}}).value("ok", true), "bob can't restore one");
    check(error(ask(s, "asset.restore", {{"id", id}, {"version", 3}})).find("already the live one") != std::string::npos, "the live one can't be restored");
    r = ask(s, "asset.restore", {{"id", id}, {"version", 2}});
    check(r.value("ok", false) && r.value("version", 0LL) == 4, "put version 2 back (as version 4)");
    check(r.value("ok", false) && r["versions"][0].value("restored", 0LL) == 2, "it says it was restored from 2");
    check(!ask(s, "game.versions", {{"id", id}}).value("ok", true), "game.versions is still only for games");

    server.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll asset version checks passed.\n");
    return failures ? 1 : 0;
}
