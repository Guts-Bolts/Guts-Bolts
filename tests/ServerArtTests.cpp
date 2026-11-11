// The C++ server's Art portal (ServerArt.cpp; worker/test/art-portal.test.mjs checks the website's):
// posting, the staff check, faves, comments, reports and the picture.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-art-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
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
    // A tiny real PNG (1x1), and text pretending to be a picture.
    const std::string png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
    const std::string text = Online::base64Encode("hello, I am not a picture at all");
    Other bob, cat;
    std::string waiting;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");
        json r = askAs(s, bob, "art.post", {{"title", "Robot"}, {"data", text}});
        check(error(r).find(".png, .jpg, .gif or .webp") != std::string::npos, "only real pictures: " + error(r));
        r = askAs(s, bob, "art.post", {{"title", "Robot at sunset"}, {"cat", "drawing"}, {"tags", "robot, Sunset #robot"}, {"data", png}});
        check(r.value("ok", false), "bob posts: " + error(r));
        waiting = r["piece"].value("id", std::string());
        check(r["piece"].value("review", std::string()) == "pending", "bob isn't Verified: it waits for a check");
        check(r["piece"]["tags"] == json::array({"robot", "sunset"}), "tags are tidied");
        check(askAs(s, cat, "art.list").at("pieces").empty(), "not in the gallery yet");
        check(!askAs(s, cat, "art.get", {{"id", waiting}}).value("ok", true), "cat can't open it");
        check(askAs(s, bob, "art.get", {{"id", waiting}}).value("ok", false), "bob can");
        check(!askAs(s, bob, "art.review", {{"id", waiting}, {"ok", true}}).value("ok", true), "only staff check art");
        check(ask(s, "art.pending").at("pieces").size() == 1, "staff see it waiting");
        r = ask(s, "art.review", {{"id", waiting}, {"ok", true}});
        check(r.value("ok", false), "staff let it through: " + error(r));
        check(askAs(s, cat, "art.list").at("pieces").size() == 1, "now it's in the gallery");
        // Staff post straight to the gallery.
        r = ask(s, "art.post", {{"title", "Castle"}, {"cat", "pixel"}, {"tags", "castle"}, {"data", png}});
        check(r.value("ok", false) && !r["piece"].contains("review"), "staff go straight in");
        const std::string castle = r["piece"].value("id", std::string());
        check(askAs(s, cat, "art.fave", {{"id", castle}}).value("faves", 0) == 1, "cat faves the castle");
        check(askAs(s, cat, "art.fave", {{"id", castle}, {"on", true}}).value("faves", 0) == 1, "faving twice counts once");
        r = askAs(s, bob, "art.list", {{"sort", "top"}});
        check(r["pieces"].size() == 2 && r["pieces"][0].value("id", std::string()) == castle, "most faved first");
        check(askAs(s, bob, "art.list", {{"cat", "pixel"}}).at("pieces").size() == 1, "by category");
        check(askAs(s, bob, "art.list", {{"tag", "robot"}}).at("pieces").size() == 1, "by tag");
        r = askAs(s, cat, "art.comment", {{"id", waiting}, {"text", "Love the colours"}});
        check(r.value("ok", false) && r["piece"]["commentList"].size() == 1, "cat comments: " + error(r));
        const std::string cid = r["piece"]["commentList"][0].value("id", std::string());
        r = askAs(s, bob, "report.send", {{"kind", "art"}, {"id", waiting + ":" + cid}, {"reason", "harassment"}});
        check(r.value("ok", false), "art comments can be reported: " + error(r));
        check(!askAs(s, bob, "report.send", {{"kind", "art"}, {"id", waiting}, {"reason", "harassment"}}).value("ok", true), "but not your own art");
        check(askAs(s, bob, "art.uncomment", {{"id", waiting}, {"comment", cid}}).value("ok", false), "the artist can delete comments");
        check(!askAs(s, cat, "art.edit", {{"id", waiting}, {"title", "Mine"}}).value("ok", true), "only the artist edits");
        check(askAs(s, bob, "art.edit", {{"id", waiting}, {"title", "Robot at dawn"}}).at("piece").value("title", std::string()) == "Robot at dawn", "the artist edits");
        r = s.handle(json{{"op", "art.picture"}, {"args", {{"id", castle}}}});
        check(r.value("ok", false) && r.value("type", std::string()) == "png" && r.value("data", std::string()) == png, "the picture, without an account (for /artpic)");
    }
    {
        auto server = startServer();   // (comes back after a restart)
        GbServer& s = *server;
        check(askAs(s, cat, "art.list").at("pieces").size() == 2, "art is kept after a restart");
        check(!askAs(s, cat, "art.delete", {{"id", waiting}}).value("ok", true), "only the artist or staff delete");
        check(askAs(s, bob, "art.delete", {{"id", waiting}}).value("ok", false), "bob deletes his");
        check(!fs::exists(dir / "data" / "files" / ("art-" + waiting)), "and its picture goes too");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll art checks passed.\n");
    return failures ? 1 : 0;
}
