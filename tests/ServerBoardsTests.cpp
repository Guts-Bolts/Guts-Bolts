// The C++ server's G&B Boards (ServerBoards.cpp; worker/test/boards.test.mjs checks the website's):
// templates, columns, cards, moving them, people on a board, who can look and reports.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-boards-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
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
    Other bob, cat;
    std::string id;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");
        check(!askAs(s, bob, "board.make", {{"title", "  "}}).value("ok", true), "a board needs a name");
        json r = askAs(s, bob, "board.make", {{"title", "My racing game"}, {"template", "game"}});
        check(r.value("ok", false), "bob makes a board: " + error(r));
        id = r["board"].value("id", std::string());
        check(r["board"]["lists"].size() == 4 && r["board"]["lists"][0].value("title", std::string()) == "Ideas", "the game template's columns");
        check(r["board"]["lists"][0]["cards"].size() == 2, "and starter cards");
        check(r["board"].value("visibility", std::string()) == "private", "private to start");
        const std::string ideas = r["board"]["lists"][0].value("id", std::string()), build = r["board"]["lists"][1].value("id", std::string());
        check(!askAs(s, cat, "board.get", {{"id", id}}).value("ok", true), "cat can't see a private board");
        r = askAs(s, bob, "board.card", {{"id", id}, {"do", "add"}, {"column", ideas}, {"title", "Add cars"}, {"to", 0}});
        check(r["board"]["lists"][0]["cards"][0].value("title", std::string()) == "Add cars", "a card goes where it was dropped");
        const std::string card = r["board"]["lists"][0]["cards"][0].value("id", std::string());
        r = askAs(s, bob, "board.card", {{"id", id}, {"do", "edit"}, {"card", card}, {"text", "Fast ones"}, {"labels", {"green", "pink", "green"}},
                                         {"due", "2026-02-30"}, {"checks", json::array({{{"text", "Red car"}}, {{"text", " "}}, {{"text", "Blue car"}, {"done", true}}})}});
        const json k = r["board"]["lists"][0]["cards"][0];
        check(k.value("title", std::string()) == "Add cars" && k.value("text", std::string()) == "Fast ones", "left-out bits stay");
        check(k["labels"] == json::array({"green"}) && k.value("due", std::string()).empty(), "labels and dates are tidied");
        check(k["checks"].size() == 2 && k["checks"][1].value("done", false), "the checklist (empty lines dropped)");
        r = askAs(s, bob, "board.card", {{"id", id}, {"do", "move"}, {"card", card}, {"column", build}});
        check(r["board"]["lists"][1]["cards"][3].value("id", std::string()) == card && r["board"]["lists"][0]["cards"].size() == 2, "moved to the end of another column");
        r = askAs(s, bob, "board.column", {{"id", id}, {"do", "add"}, {"title", "Waiting"}, {"to", 1}});
        check(r["board"]["lists"][1].value("title", std::string()) == "Waiting", "a new column");
        const std::string waiting = r["board"]["lists"][1].value("id", std::string());
        r = askAs(s, bob, "board.column", {{"id", id}, {"do", "move"}, {"column", waiting}, {"to", 4}});
        check(r["board"]["lists"][4].value("id", std::string()) == waiting, "columns move");
        check(askAs(s, bob, "board.column", {{"id", id}, {"do", "delete"}, {"column", waiting}})["board"]["lists"].size() == 4, "and go");
        r = askAs(s, bob, "board.member", {{"id", id}, {"who", "cat"}});
        check(r.value("ok", false), "bob adds cat: " + error(r));
        check(!askAs(s, bob, "board.member", {{"id", id}, {"who", "cat"}}).value("ok", true), "not twice");
        r = askAs(s, cat, "board.card", {{"id", id}, {"do", "edit"}, {"card", card}, {"people", {cat.id, "nobody", bob.id}}, {"done", true}});
        check(r.value("ok", false) && r["board"]["lists"][1]["cards"][3]["people"].size() == 2, "people on it change cards; only people on it go on cards");
        check(r["board"].value("done", 0) == 1, "done cards are counted");
        check(!askAs(s, cat, "board.set", {{"id", id}, {"title", "Cat's"}}).value("ok", true), "only the owner changes settings");
        check(!askAs(s, cat, "board.delete", {{"id", id}}).value("ok", true), "or deletes it");
        check(askAs(s, cat, "board.mine").at("boards").size() == 1, "cat sees it in their boards");
        check(askAs(s, cat, "board.member", {{"id", id}, {"who", cat.id}, {"on", false}}).value("ok", false), "cat leaves");
        r = askAs(s, bob, "board.get", {{"id", id}});
        check(r["board"]["members"].empty() && r["board"]["lists"][1]["cards"][3]["people"].size() == 1, "and comes off the cards");
        check(askAs(s, bob, "board.set", {{"id", id}, {"visibility", "public"}, {"color", "blue"}}).value("ok", false), "bob makes it public");
        r = askAs(s, cat, "board.get", {{"id", id}});
        check(r.value("ok", false) && !r["board"].value("canEdit", true), "cat can look but not change it");
        check(!askAs(s, cat, "board.card", {{"id", id}, {"do", "add"}, {"column", ideas}, {"title", "x"}}).value("ok", true), "or add cards");
        check(askAs(s, cat, "board.list").at("boards").size() == 1, "public boards are listed");
        check(askAs(s, cat, "report.send", {{"kind", "board"}, {"id", id}, {"reason", "spam"}}).value("ok", false), "boards can be reported");
    }
    {
        auto server = startServer();
        GbServer& s = *server;
        json r = askAs(s, cat, "board.get", {{"id", id}});
        check(r.value("ok", false) && r["board"].value("color", std::string()) == "blue", "boards are kept after a restart");
        check(ask(s, "board.delete", {{"id", id}}).value("ok", false), "staff can delete one");
        check(askAs(s, cat, "board.list").at("boards").empty(), "and it's gone");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll board checks passed.\n");
    return failures ? 1 : 0;
}
