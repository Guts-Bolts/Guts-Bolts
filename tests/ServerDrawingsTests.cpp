// The C++ server: pictures players draw in games (AssetService:PromptPublishImageAsync) are
// free to publish (worker/test/drawings.test.mjs checks the website's).
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
    const fs::path dir = fs::temp_directory_path() / ("gb-draw-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
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
    check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
    check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
    const std::string png = Online::base64Encode(std::string("\x89PNG\r\n\x1a\n\0\0\0\0", 12));
    const long long before = askAs(s, bob, "ping")["me"].value("bolts", 0LL);

    json r = askAs(s, bob, "upload", {{"kind", "decal"}, {"name", "My cat"}, {"data", png}, {"access", "public"},
                                      {"meta", {{"drawing", true}, {"game", "abc123"}, {"sneaky", "x"}}}});
    check(r.value("ok", false), "a drawing from a game is published: " + error(r));
    check(r.value("fee", -1LL) == 0, "...for free");
    check(askAs(s, bob, "ping")["me"].value("bolts", 0LL) == before, "...and bob's Bolts are untouched");
    const json meta = r.contains("asset") ? r["asset"].value("meta", json::object()) : json::object();
    check(meta == json{{"ext", "png"}, {"drawing", true}, {"game", "abc123"}}, "it keeps only its own notes: " + meta.dump());

    r = askAs(s, bob, "upload", {{"kind", "decal"}, {"name", "Dog"}, {"data", png}, {"meta", {{"drawing", true}, {"game", "<b>"}}}});
    check(r.value("ok", false) && !r["asset"]["meta"].contains("game"), "a strange game id is dropped");

    r = askAs(s, bob, "upload", {{"kind", "decal"}, {"name", "Sign"}, {"data", png}, {"meta", {{"drawing", "yes"}}}});
    check(r.value("ok", false) && r.value("fee", 0LL) > 0 && !r["asset"]["meta"].contains("drawing"),
          "an ordinary decal pays the usual fee: " + r.dump().substr(0, 200));

    server.reset();
    fs::remove_all(dir);
    std::cout << (failures ? "FAILED: " : "all passed: ") << failures << " problem(s)\n";
    return failures ? 1 : 0;
}
