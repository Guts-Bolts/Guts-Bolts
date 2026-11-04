// Wishlist alerts on the C++ server (wishlistAlert in ServerItems.cpp; worker/test/
// wishlist-alerts.test.mjs checks the website's): when an item someone favourited gets
// cheaper or comes back on sale, they get a note on the bell. Requests are signed like
// the apps sign them.
#include "core/Account.h"
#include "online/Protocol.h"
#include "server/Server.h"

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
json ask(GbServer& s, const std::string& op, const json& args = json::object()) {
    const long long t = Online::unixNow();
    const std::string nonce = Account::randomHex(8);
    json req = {{"op", op}, {"account", Account::id()}, {"time", t}, {"nonce", nonce}, {"args", args},
                {"sig", Account::sign(Online::requestText(op, Account::id(), t, nonce, args))}};
    return s.handle(req);
}
int wishlistNotes(GbServer& s) {
    int n = 0;
    for (const json& x : ask(s, "notes.list").value("notes", json::array()))
        if (x.value("kind", std::string()) == "wishlist") ++n;
    return n;
}
std::string lastNote(GbServer& s) {
    const json notes = ask(s, "notes.list").value("notes", json::array());
    return notes.empty() ? "" : notes[0].value("text", std::string());
}
const char* kHat = "item-verified-hat";   // a built-in catalog item every server has (made by Guts)
} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("gb-wishlist-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    GbServer::Options opts;
    opts.data = dir / "data";
    opts.official = Account::id();   // this account is "Guts", #1 (staff)
    std::string err;
    std::unique_ptr<GbServer> server;
    std::srand((unsigned)Online::unixNow() ^ (unsigned)getpid());
    for (int tries = 0; tries < 20 && !server; ++tries) {
        opts.port = 20000 + std::rand() % 40000;
        auto s = std::make_unique<GbServer>(opts);
        if (s->start(err)) server = std::move(s);
    }
    if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
    GbServer& s = *server;

    // Keep Guts's key so we can switch back to staff later.
    const std::string gutsLock = Account::randomHex(32), gutsKey = Account::backupKey(gutsLock);
    check(ask(s, "item.edit", {{"id", kHat}, {"price", 100}}).value("ok", false), "staff put a price on the hat");

    // Bob signs up and favourites it.
    Account::newKey();
    const std::string bobLock = Account::randomHex(32), bobKey = Account::backupKey(bobLock);
    json r = ask(s, "account.signup", {{"username", "bob"}, {"salt", Account::randomHex(16)},
                                       {"auth", Account::randomHex(32)}, {"key", bobKey}});
    check(r.value("ok", false), "bob signs up");
    check(ask(s, "item.favorite", {{"id", kHat}, {"on", true}}).value("ok", false), "bob favourites the hat");
    check(wishlistNotes(s) == 0, "...no wishlist notes yet");

    Account::restoreKey(gutsLock, gutsKey);
    check(ask(s, "item.edit", {{"id", kHat}, {"price", 150}}).value("ok", false), "staff make it dearer");
    Account::restoreKey(bobLock, bobKey);
    check(wishlistNotes(s) == 0, "a higher price rings nobody");

    Account::restoreKey(gutsLock, gutsKey);
    ask(s, "item.edit", {{"id", kHat}, {"price", 60}});
    Account::restoreKey(bobLock, bobKey);
    check(wishlistNotes(s) == 1 && lastNote(s).find("dropped from 150 to 60") != std::string::npos, "a lower price rings bob");

    Account::restoreKey(gutsLock, gutsKey);
    ask(s, "item.edit", {{"id", kHat}, {"staffOffsale", true}});
    ask(s, "item.edit", {{"id", kHat}, {"price", 10}});   // (cheaper, but still off sale: no note)
    Account::restoreKey(bobLock, bobKey);
    check(wishlistNotes(s) == 1, "nothing while it's off sale");

    Account::restoreKey(gutsLock, gutsKey);
    ask(s, "item.edit", {{"id", kHat}, {"staffOffsale", false}});
    check(wishlistNotes(s) == 0, "the person who changed it isn't rung");
    Account::restoreKey(bobLock, bobKey);
    check(wishlistNotes(s) == 2 && lastNote(s).find("back on sale (10 Bolts)") != std::string::npos, "back on sale rings bob");

    ask(s, "item.favorite", {{"id", kHat}, {"on", false}});
    Account::restoreKey(gutsLock, gutsKey);
    ask(s, "item.edit", {{"id", kHat}, {"price", 0}});
    Account::restoreKey(bobLock, bobKey);
    check(wishlistNotes(s) == 2, "after un-favouriting, no more notes");

    server.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "server wishlist alert tests FAILED\n" : "server wishlist alert tests passed\n");
    return failures ? 1 : 0;
}
