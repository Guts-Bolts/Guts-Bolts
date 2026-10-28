// The C++ server's public roadmap (ServerRoadmap.cpp; worker/test/roadmap.test.mjs checks the
// website's): staff edit it, people suggest and vote, staff answer ideas, and it's all saved.
// Requests are signed like the apps sign them, with throwaway accounts in a temporary folder.
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
// One request from this program's current account.
json ask(GbServer& s, const std::string& op, const json& args = json::object()) {
    const long long t = Online::unixNow();
    const std::string nonce = Account::randomHex(8);
    json req = {{"op", op}, {"account", Account::id()}, {"time", t}, {"nonce", nonce}, {"args", args},
                {"sig", Account::sign(Online::requestText(op, Account::id(), t, nonce, args))}};
    return s.handle(req);
}
std::string error(const json& r) { return r.value("error", std::string()); }
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("gb-roadmap-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    GbServer::Options opts;
    opts.data = dir / "data";
    opts.official = Account::id();   // this account is staff ("Guts", #1)
    std::string err;
    // A free port: try a few random ones (another test or program may have the first).
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
        json r = ask(s, "admin.roadmapSave", {{"title", "Weather"}, {"text", "Rain and snow"}, {"status", "working"}, {"version", "0.7.8"}});
        check(r.value("ok", false) && r["items"].size() == 1 && r["items"][0]["status"] == "working", "staff add a roadmap item");
        const std::string item = r["items"][0].value("id", std::string());
        r = ask(s, "admin.roadmapSave", {{"id", item}, {"title", "Weather"}, {"status", "done"}});
        check(r.value("ok", false) && r["items"][0]["status"] == "done", "...and change it");
        check(has(error(ask(s, "admin.roadmapSave", {{"title", "X"}, {"status", "someday"}})), "Planned"), "a made-up status is refused");

        r = ask(s, "roadmap.suggest", {{"title", "Mounts"}, {"text", "Horses to ride"}});
        check(r.value("ok", false) && r["ideas"].size() == 1 && r["ideas"][0]["votes"] == 1 && r["ideas"][0]["mine"] == true,
              "an idea starts with its maker's vote");
        const std::string idea = r["ideas"][0].value("id", std::string());
        check(has(error(ask(s, "roadmap.suggest", {{"title", "mounts"}})), "Vote for it"), "the same idea twice is refused");
        r = ask(s, "roadmap.vote", {{"id", idea}, {"on", false}});
        check(r["ideas"][0]["votes"] == 0 && r["ideas"][0]["voted"] == false, "taking a vote back");
        r = ask(s, "roadmap.vote", {{"id", idea}, {"on", true}});
        r = ask(s, "roadmap.vote", {{"id", idea}, {"on", true}});
        check(r["ideas"][0]["votes"] == 1, "voting twice counts once");
        ask(s, "roadmap.suggest", {{"title", "Two"}});
        ask(s, "roadmap.suggest", {{"title", "Three"}});
        check(has(error(ask(s, "roadmap.suggest", {{"title", "Four"}})), "3 ideas waiting"), "three open ideas each");

        r = ask(s, "admin.ideaAnswer", {{"id", idea}, {"answer", "planned"}, {"note", "Coming in 0.8!"}});
        check(r.value("ok", false) && r["items"].size() == 2 && r["items"][1]["title"] == "Mounts" && r["items"][1]["status"] == "planned",
              "a planned idea goes on the roadmap");
        check(r["answered"].size() == 1 && r["answered"][0]["note"] == "Coming in 0.8!" && r["ideas"].size() == 2, "...and shows as answered");
        check(has(error(ask(s, "roadmap.vote", {{"id", idea}, {"on", true}})), "already answered"), "answered ideas can't get votes");
        const std::string two = r["ideas"][0].value("id", std::string());
        r = ask(s, "admin.ideaRemove", {{"id", two}});
        check(r.value("ok", false) && r["ideas"].size() == 1, "staff delete an idea");
        r = ask(s, "admin.roadmapRemove", {{"id", item}});
        check(r.value("ok", false) && r["items"].size() == 1, "staff remove an item");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start again: " << err << "\n"; return 1; }
        GbServer& again = *server;
        json r = ask(again, "roadmap.get");
        check(r.value("ok", false) && r["items"].size() == 1 && r["items"][0]["title"] == "Mounts" && r["ideas"].size() == 1,
              "it's all still there after a restart");
        Account::newKey();   // someone who hasn't signed up
        r = ask(again, "roadmap.get");
        check(r.value("ok", false) && r["items"].size() == 1 && r["ideas"][0]["voted"] == false, "visitors can look");
        check(has(error(ask(again, "roadmap.suggest", {{"title", "Hi"}})), "Sign up"), "...but not suggest");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "server roadmap tests FAILED\n" : "server roadmap tests passed\n");
    return failures ? 1 : 0;
}
