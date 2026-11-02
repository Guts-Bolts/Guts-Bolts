// The C++ server's achievements, game update logs and events (ServerExtras.cpp;
// worker/test/achievements-events-gamelogs.test.mjs checks the website's): staff make an event,
// playing counts minutes towards it, its item can be claimed once, and it's all saved.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-extras-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
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
    const long long t = Online::unixNow();
    std::string eventId;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        json r = ask(s, "profile", {{"id", "1"}});
        check(r.value("ok", false) && r["achievements"].is_array() && r["achievements"].size() >= 10, "profiles list achievements");
        check(!ask(s, "admin.eventSave", {{"name", "Backwards"}, {"start", t + 100}, {"end", t}}).value("ok", false), "an event must end after it starts");
        r = ask(s, "admin.eventSave", {{"name", "Winter Fest"}, {"text", "Play for a hat"}, {"start", t - 60}, {"end", t + 86400},
                                       {"item", kHat}, {"minutes", 5}});
        check(r.value("ok", false) && r["events"].size() == 1 && r["events"][0]["state"] == "live", "staff add an event");
        eventId = r["events"][0].value("id", std::string());
        check(!ask(s, "events.claim", {{"id", eventId}}).value("ok", false), "...its item needs playing first");
        ask(s, "bolts.play");   // the Player's "still playing" (5 minutes)
        r = ask(s, "events.list");
        check(r.value("ok", false) && r["events"][0].value("played", 0LL) == 5, "playing counts 5 minutes");
        check(!ask(s, "gamelog.post", {{"id", "nope"}, {"title", "Hi"}}).value("ok", false), "no update notes for a game that isn't there");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start again: " << err << "\n"; return 1; }
        GbServer& again = *server;
        json r = ask(again, "events.list");
        check(r.value("ok", false) && r["events"].size() == 1 && r["events"][0].value("played", 0LL) == 5, "the event and minutes are still there after a restart");
        r = ask(again, "events.claim", {{"id", eventId}});
        check(r.value("ok", false) && r["events"][0].value("claimed", false), "claim the event's item");
        check(!ask(again, "events.claim", {{"id", eventId}}).value("ok", false), "...only once");
        r = ask(again, "profile", {{"id", "1"}});
        bool partyGoer = false;
        for (const json& a : r["achievements"]) if (a.value("id", std::string()) == "party-goer") partyGoer = a.value("earned", false);
        check(partyGoer, "...and that's an achievement");
        r = ask(again, "admin.eventRemove", {{"id", eventId}});
        check(r.value("ok", false) && r["events"].empty(), "staff remove the event");
        Account::newKey();   // someone who hasn't signed up
        check(ask(again, "events.list").value("ok", false), "visitors can look at events");
        check(!ask(again, "events.claim", {{"id", eventId}}).value("ok", false), "...but not claim");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "server extras tests FAILED\n" : "server extras tests passed\n");
    return failures ? 1 : 0;
}
