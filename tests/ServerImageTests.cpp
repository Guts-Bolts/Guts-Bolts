// The C++ server's G&B Image (ServerImage.cpp; worker/test/image.test.mjs checks the website's):
// uploads, the staff check, public and unlisted links, renaming, deleting, reports and the picture.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-image-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
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
    // A tiny real PNG (1x1).
    const std::string png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
    Other bob, cat;
    std::string waiting, mine;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");
        check(error(askAs(s, bob, "image.upload", {{"data", Online::base64Encode("not a picture at all, no")}})).find(".png") != std::string::npos, "only real pictures");
        json r = askAs(s, bob, "image.upload", {{"title", ""}, {"data", png}, {"visibility", "secret"}, {"w", 1}, {"h", 99999999}});
        check(r.value("ok", false), "bob uploads a picture: " + error(r));
        waiting = r["image"].value("id", std::string());
        check(waiting.size() == 15 && waiting.rfind("im-", 0) == 0, "its id is long and random");
        check(r["image"].value("title", std::string()) == "Untitled" && r["image"].value("visibility", std::string()) == "public", "no title is Untitled, odd choices are public");
        check(r["image"].value("h", 0) == 20000 && r["image"].value("type", std::string()) == "png", "sizes are kept sensible; the type comes from the bytes");
        check(r["image"].value("review", std::string()) == "pending", "bob isn't Verified, so it waits for a staff check");
        check(askAs(s, cat, "image.list").at("images").empty(), "not in the list yet");
        check(!askAs(s, cat, "image.get", {{"id", waiting}}).value("ok", true), "others can't open it");
        check(!s.handle(json{{"op", "image.picture"}, {"args", {{"id", waiting}}}}).value("ok", true), "and its link shows nothing yet");
        check(askAs(s, bob, "image.get", {{"id", waiting}}).value("ok", false), "bob can");
        check(!askAs(s, bob, "image.review", {{"id", waiting}, {"ok", true}}).value("ok", true), "only staff check");
        check(ask(s, "image.pending").at("images").size() == 1, "staff see it waiting");
        check(ask(s, "image.review", {{"id", waiting}, {"ok", true}}).value("ok", false), "staff let it through");
        check(askAs(s, cat, "image.list").at("images").size() == 1, "now everyone sees it");
        r = s.handle(json{{"op", "image.picture"}, {"args", {{"id", waiting + ".png"}}}});
        check(r.value("ok", false) && r.value("data", std::string()) == png, "the picture, without an account (for /imgpic), .png links too");
        r = ask(s, "image.upload", {{"title", "Logo"}, {"data", png}, {"visibility", "unlisted"}});
        mine = r["image"].value("id", std::string());
        check(!r["image"].contains("review"), "staff go straight up");
        check(askAs(s, cat, "image.list").at("images").size() == 1, "unlisted ones stay out of the list");
        r = askAs(s, cat, "image.get", {{"id", mine}});
        check(r.value("ok", false) && r["image"].value("views", 0) == 1 && !r["image"].value("canDelete", true), "but open with their link (a view counted)");
        check(!askAs(s, cat, "image.edit", {{"id", mine}, {"title", "mine"}}).value("ok", true), "only the uploader renames");
        check(ask(s, "image.edit", {{"id", mine}, {"title", "Big logo"}, {"visibility", "public"}})["image"].value("title", std::string()) == "Big logo", "renamed");
        check(askAs(s, cat, "image.list", {{"by", "bob"}}).at("images").size() == 1, "by person");
        check(askAs(s, cat, "report.send", {{"kind", "image"}, {"id", mine}, {"reason", "spam"}}).value("ok", false), "pictures can be reported");
        check(!askAs(s, bob, "report.send", {{"kind", "image"}, {"id", waiting}, {"reason", "spam"}}).value("ok", true), "not your own");
        check(!askAs(s, cat, "image.delete", {{"id", waiting}}).value("ok", true), "only the uploader or staff delete");
    }
    {
        auto server = startServer();
        GbServer& s = *server;
        check(askAs(s, cat, "image.list").at("images").size() == 2, "pictures are kept after a restart");
        check(askAs(s, bob, "image.mine").at("images").size() == 1, "bob's own list");
        check(askAs(s, bob, "image.delete", {{"id", waiting}}).value("ok", false), "bob deletes his");
        check(!fs::exists(dir / "data" / "files" / ("image-" + waiting)), "and its file is gone");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll image checks passed.\n");
    return failures ? 1 : 0;
}
