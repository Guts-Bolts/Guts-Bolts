// The C++ server's ban lists (ServerBanLists.cpp; worker/test/ban-lists.test.mjs checks the
// website's): Guts makes a list, puts it on a game, bans someone with a reason, and that person
// can't find a server of the game any more. Lists are kept after a restart.
// Requests are signed like the apps sign them; "bob" signs with a second key made here.
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
// A second account: its own key pair, signing the same way Account::sign does.
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
} // namespace

int main() {
    std::srand((unsigned)Online::unixNow() ^ (unsigned)getpid());
    const fs::path dir = fs::temp_directory_path() / ("gb-banlists-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    // One game, made by Guts (this test's own account).
    ServerUtil::writeFile(dir / "data" / "assets.json",
        json{{"g-obby", {{"kind", "game"}, {"name", "Obby"}, {"creator", Account::id()}, {"created", Online::unixNow()}, {"meta", json::object()}}}}.dump());
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
    std::string listId;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(askAs(s, bob, "hello", {{"name", "bob"}}).value("ok", false), "bob says hello");
        check(askAs(s, bob, "servers.play", {{"game", "g-obby"}}).value("ok", false), "bob can play before he's banned");

        json r = ask(s, "banlist.create", {{"name", "Cheaters"}});
        check(r.value("ok", false) && r["list"]["role"] == "owner", "make a ban list");
        listId = r["list"].value("id", std::string());
        r = ask(s, "game.banLists", {{"game", "g-obby"}, {"lists", json::array({listId})}});
        check(r.value("ok", false) && r["lists"].size() == 1, "put it on the game");
        check(error(ask(s, "banlist.add", {{"id", listId}, {"who", bob.id}})).find("needs a reason") != std::string::npos, "a ban needs a reason");
        check(error(ask(s, "banlist.add", {{"id", listId}, {"who", Account::id()}, {"reason", "x"}})).find("yourself") != std::string::npos,
              "you can't ban yourself");
        r = ask(s, "banlist.add", {{"id", listId}, {"who", bob.id}, {"reason", "Speed hacking"}, {"days", 7}});
        check(r.value("ok", false) && r["list"]["entries"].size() == 1 && r["list"]["entries"][0]["by"]["id"] == Account::id(),
              "ban bob, and the list keeps who did it");
        r = askAs(s, bob, "servers.play", {{"game", "g-obby"}});
        check(!r.value("ok", false) && error(r).find("Reason: Speed hacking") != std::string::npos, "bob can't play, and is told why");
        check(askAs(s, bob, "servers.list", {{"game", "g-obby"}}).value("ok", false), "...but can still see the game's servers");
        check(error(askAs(s, bob, "banlist.mine")).find("Sign up") != std::string::npos, "guests can't make ban lists");
        r = ask(s, "banlist.get", {{"id", listId}});
        check(r.value("ok", false) && r["list"]["log"].size() >= 3 && r["list"]["log"][0]["did"] == "add", "the log says what happened, newest first");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start again: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(!askAs(s, bob, "servers.play", {{"game", "g-obby"}}).value("ok", false), "the ban is kept after a restart");
        json r = ask(s, "banlist.remove", {{"id", listId}, {"who", bob.id}});
        check(r.value("ok", false) && r["list"]["entries"].empty(), "take the ban off");
        check(askAs(s, bob, "servers.play", {{"game", "g-obby"}}).value("ok", false), "bob can play again");
        r = ask(s, "game.ban", {{"game", "g-obby"}, {"who", bob.id}, {"reason", "Spam"}});
        check(r.value("ok", false) && r["list"]["id"] == listId, "game.ban uses the game's list");
        check(ask(s, "banlist.delete", {{"id", listId}}).value("ok", false), "delete the list");
        check(askAs(s, bob, "servers.play", {{"game", "g-obby"}}).value("ok", false), "...and its bans stop with it");
        r = ask(s, "game.banLists", {{"game", "g-obby"}});
        check(r.value("ok", false) && r["lists"].empty(), "the game doesn't use it any more");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "SOME CHECKS FAILED\n" : "all good\n");
    return failures ? 1 : 0;
}
