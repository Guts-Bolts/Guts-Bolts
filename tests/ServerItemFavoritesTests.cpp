// The C++ server's favourite catalog items (item.favorite in ServerItems.cpp; worker/test/
// item-favorites.test.mjs checks the website's): a heart on an item, the "My Favorites" list,
// the "Most Favorited" sort, and it's all saved. Requests are signed like the apps sign them.
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
bool lists(const json& r, const std::string& id) {
    for (const json& a : r.value("assets", json::array())) if (a.value("id", std::string()) == id) return true;
    return false;
}
const char* kHat = "item-verified-hat";   // a built-in catalog item every server has
} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("gb-itemfavs-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    GbServer::Options opts;
    opts.data = dir / "data";
    opts.official = Account::id();   // this account is "Guts", #1 (signed up from the start)
    std::string err;
    auto startServer = [&]() -> std::unique_ptr<GbServer> {
        for (int tries = 0; tries < 20; ++tries) {
            opts.port = 20000 + std::rand() % 40000;
            auto s = std::make_unique<GbServer>(opts);
            if (s->start(err)) return s;
        }
        return nullptr;
    };
    std::srand((unsigned)Online::unixNow() ^ (unsigned)getpid());
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        json r = ask(s, "item.favorite", {{"id", kHat}, {"on", true}});
        check(r.value("ok", false) && r["asset"].value("favorites", 0LL) == 1 && r["asset"].value("myFavorite", false), "favourite an item");
        r = ask(s, "item.favorite", {{"id", kHat}, {"on", true}});
        check(r.value("ok", false) && r["asset"].value("favorites", 0LL) == 1, "...twice still counts once");
        r = ask(s, "list", {{"kind", "clothing"}, {"favorites", true}});
        check(r.value("ok", false) && r["assets"].size() == 1 && lists(r, kHat), "My Favorites lists it (and nothing else)");
        r = ask(s, "list", {{"kind", "clothing"}, {"sort", "favorited"}});
        check(r.value("ok", false) && !r["assets"].empty() && r["assets"][0].value("id", std::string()) == kHat, "Most Favorited puts it first");
        check(!ask(s, "item.favorite", {{"id", "nope"}}).value("ok", false), "...an item that isn't there can't be favourited");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start again: " << err << "\n"; return 1; }
        GbServer& again = *server;
        json r = ask(again, "asset.info", {{"id", kHat}});
        check(r.value("ok", false) && r["asset"].value("favorites", 0LL) == 1 && r["asset"].value("myFavorite", false), "still there after a restart");
        r = ask(again, "item.favorite", {{"id", kHat}, {"on", false}});
        check(r.value("ok", false) && r["asset"].value("favorites", 0LL) == 0 && !r["asset"].value("myFavorite", true), "take it back");
        check(ask(again, "list", {{"kind", "clothing"}, {"favorites", true}})["assets"].empty(), "...and My Favorites is empty");
        Account::newKey();   // someone who hasn't signed up
        check(!ask(again, "item.favorite", {{"id", kHat}}).value("ok", false), "visitors can't favourite");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "server item favourite tests FAILED\n" : "server item favourite tests passed\n");
    return failures ? 1 : 0;
}
