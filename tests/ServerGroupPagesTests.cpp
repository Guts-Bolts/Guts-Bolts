// The C++ server's group pages (worker/test/group-pages.test.mjs checks the website's): group icons
// and banners from decals, announcements, members by rank, and the People page's cards.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-gp-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    const long long t = Online::unixNow();
    ServerUtil::writeFile(dir / "data" / "assets.json", json{
        {"d-logo", {{"kind", "decal"}, {"name", "Logo"}, {"num", 77}, {"creator", Account::id()}, {"created", t}, {"meta", json::object()}}},
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
    Other bob, cat;
    std::string gid;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");
        json r = ask(s, "groups.create", {{"name", "Builders"}, {"description", "We build"}});
        check(r.value("ok", false), "Guts makes a group: " + error(r));
        gid = r["group"].value("id", std::string());
        check(askAs(s, bob, "groups.join", {{"id", gid}}).value("ok", false), "bob joins");
        check(askAs(s, cat, "groups.join", {{"id", gid}}).value("ok", false), "cat joins");

        // icon and banner
        check(error(askAs(s, bob, "groups.pictures", {{"id", gid}, {"icon", "d-logo"}})).find("rank") != std::string::npos, "members can't change the pictures");
        check(error(ask(s, "groups.pictures", {{"id", gid}, {"icon", "g-game"}})).find("decal") != std::string::npos, "a game isn't a picture");
        r = ask(s, "groups.pictures", {{"id", gid}, {"icon", "77"}, {"banner", "gb:d-logo"}});
        check(r.value("ok", false) && r["group"]["icon"].value("id", std::string()) == "d-logo" && r["group"]["banner"].value("num", 0) == 77,
              "the owner sets the icon (by number) and banner (by ID): " + error(r));
        r = ask(s, "groups.pictures", {{"id", gid}, {"icon", "d-wait"}});
        check(r.value("ok", false) && r.value("waiting", false) && r["group"]["icon"].is_null(), "a decal waiting for its check is saved but hidden");

        // announcements
        check(error(askAs(s, bob, "groups.announce", {{"id", gid}, {"title", "Hi"}, {"text", "x"}})).find("rank") != std::string::npos, "members can't announce");
        check(error(ask(s, "groups.announce", {{"id", gid}, {"title", ""}, {"text", "x"}})).find("title") != std::string::npos, "it needs a title");
        r = ask(s, "groups.announce", {{"id", gid}, {"title", "Game night"}, {"text", "Friday at 8!"}});
        check(r.value("ok", false) && r["news"].size() == 1, "Guts posts an announcement: " + error(r));
        for (int i = 0; i < 35; ++i) ask(s, "groups.announce", {{"id", gid}, {"title", "News " + std::to_string(i)}, {"text", "x"}});
        r = askAs(s, bob, "groups.get", {{"id", gid}});
        check(r["news"].size() == 30 && r["news"][0].value("title", std::string()) == "News 34", "the newest 30 stay, newest first");
        const std::string nid = r["news"][0].value("id", std::string());
        check(error(askAs(s, bob, "groups.announceDelete", {{"id", gid}, {"post", nid}})).find("admin") != std::string::npos, "bob can't delete it");
        check(ask(s, "groups.announceDelete", {{"id", gid}, {"post", nid}})["news"].size() == 29, "Guts can");

        // ranks
        check(ask(s, "groups.member", {{"id", gid}, {"user", bob.id}, {"action", "rank"}, {"rank", "Admin"}}).value("ok", false), "bob becomes an Admin");
        r = askAs(s, cat, "groups.get", {{"id", gid}});
        check(r["rankCounts"].value("Owner", 0) == 1 && r["rankCounts"].value("Admin", 0) == 1 && r["rankCounts"].value("Member", 0) == 1, "rank counts");
        r = askAs(s, cat, "groups.members", {{"id", gid}, {"rank", "Admin"}});
        check(r.value("total", 0) == 1 && r["people"][0].value("username", std::string()) == "bob" && r["people"][0].value("role", std::string()) == "Admin"
              && r["people"][0].contains("level") && r["people"][0].contains("online"), "the Admins: just bob, with a card");
        r = askAs(s, cat, "groups.members", {{"id", gid}, {"rank", ""}, {"offset", 1}, {"limit", 1}});
        check(r.value("total", 0) == 3 && r["people"].size() == 1 && r["people"][0].value("username", std::string()) == "bob", "everyone, a page at a time");
        check(error(askAs(s, cat, "groups.members", {{"id", gid}, {"rank", "r-gone"}})).find("gone") != std::string::npos, "a rank that's gone");

        // people
        r = askAs(s, cat, "users.search", {{"query", "bob"}});
        check(r["users"].size() == 1 && r["users"][0].value("groups", 0) == 1 && r["users"][0].value("level", 0) >= 1, "people cards have groups and levels");
        r = askAs(s, cat, "users.search", {{"show", "verified"}});
        bool allVerified = true, hasBob = false;
        for (const auto& u : r["users"]) { if (!u.value("verified", false)) allVerified = false; if (u.value("username", std::string()) == "bob") hasBob = true; }
        check(allVerified && !hasBob, "the Verified tab only has Verified people");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start again: " << err << "\n"; return 1; }
        GbServer& s = *server;
        json r = ask(s, "groups.get", {{"id", gid}});
        check(r["group"]["banner"].value("id", std::string()) == "d-logo" && r["news"].size() == 29, "pictures and announcements are kept after a restart");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll group page checks passed.\n");
    return failures ? 1 : 0;
}
