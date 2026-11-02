// The C++ server's OrderedDataStore:GetSortedAsync (data.sorted in ServerData.cpp;
// worker/test/leaderboards.test.mjs checks the website's): a store's numbers, sorted, a page
// at a time. Requests are signed like the apps sign them.
#include "core/Account.h"
#include "core/Paths.h"
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
std::string keys(const json& r) {
    std::string out;
    for (const json& e : r.value("entries", json::array())) out += e.value("key", std::string()) + "=" + e["value"].dump() + " ";
    return out;
}
} // namespace

int main(int argc, char** argv) {
    // The example games are the staff account's games; put one where the server looks for them.
    if (argc > 1) {
        std::error_code copyErr;
        fs::copy_file(argv[1], Paths::gamesFolder() / fs::path(argv[1]).filename(), fs::copy_options::skip_existing, copyErr);
    }
    std::srand((unsigned)Online::unixNow() ^ (unsigned)getpid());
    const fs::path dir = fs::temp_directory_path() / ("gb-ordered-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    GbServer::Options opts;
    opts.data = dir / "data";
    opts.official = Account::id();   // staff: allowed to use any game's saved data
    std::string err;
    std::unique_ptr<GbServer> server;
    for (int tries = 0; tries < 20 && !server; ++tries) {
        opts.port = 20000 + std::rand() % 40000;
        auto s = std::make_unique<GbServer>(opts);
        if (s->start(err)) server = std::move(s);
    }
    if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
    GbServer& s = *server;
    json games = ask(s, "list", {{"kind", "game"}});
    if (!games.value("ok", false) || games["assets"].empty()) { std::cout << "FAIL  no example game\n"; return 1; }
    const std::string game = games["assets"][0].value("id", std::string());
    auto set = [&](const std::string& key, const json& v) { ask(s, "data.set", {{"game", game}, {"store", "Coins"}, {"key", key}, {"value", v}, {"ordered", true}}); };
    set("a", 10); set("b", 30); set("c", 20); set("d", "not a number"); set("e", 5.5);
    json r = ask(s, "data.sorted", {{"game", game}, {"store", "Coins"}});
    check(r.value("ok", false) && keys(r) == "b=30 c=20 a=10 e=5.5 ", "biggest first, numbers only: " + keys(r));
    r = ask(s, "data.sorted", {{"game", game}, {"store", "Coins"}, {"ascending", true}, {"limit", 2}});
    check(keys(r) == "e=5.5 a=10 ", "smallest first, a page of 2: " + keys(r));
    r = ask(s, "data.sorted", {{"game", game}, {"store", "Coins"}, {"limit", 2}, {"offset", 2}});
    check(keys(r) == "a=10 e=5.5 ", "the next page: " + keys(r));
    r = ask(s, "data.sorted", {{"game", game}, {"store", "Coins"}, {"min", 10}, {"max", 20}});
    check(keys(r) == "c=20 a=10 ", "between 10 and 20: " + keys(r));
    check(!ask(s, "data.sorted", {{"game", game}, {"store", ""}}).value("ok", false), "a store needs a name");
    server.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "FAILED\n" : "all good\n");
    return failures ? 1 : 0;
}
