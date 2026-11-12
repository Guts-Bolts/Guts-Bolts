// The C++ server's G&B Paste (ServerPaste.cpp; worker/test/paste.test.mjs checks the website's):
// making, listing, unlisted links, running out, deleting, reports and the raw text.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-paste-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
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
    std::string code, secret, soon;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");
        check(error(askAs(s, bob, "paste.make", {{"text", "  \n\n "}})).find("Paste something") != std::string::npos, "an empty paste is refused");
        check(error(askAs(s, bob, "paste.make", {{"text", std::string(101 * 1024, 'x')}})).find("too long") != std::string::npos, "over 100 KB is refused");
        check(error(askAs(s, bob, "paste.make", {{"text", "hi"}, {"expires", 5}})).find("runs out") != std::string::npos, "only the listed run-out times");
        json r = askAs(s, bob, "paste.make", {{"title", "My script"}, {"text", "local x = 1\r\nprint(x)\t-- hi\n\n\n"}, {"lang", "lua"}});
        check(r.value("ok", false), "bob makes a paste: " + error(r));
        code = r["paste"].value("id", std::string());
        check(code.size() == 12, "its id is long and random");
        check(r["paste"].value("text", std::string()) == "local x = 1\nprint(x)\t-- hi", "the text is kept as typed (new lines tidied)");
        check(r["paste"].value("lines", 0) == 2 && r["paste"].value("lang", std::string()) == "lua", "lines and language");
        r = askAs(s, bob, "paste.make", {{"text", "password=hunter2"}, {"visibility", "unlisted"}, {"lang", "klingon"}});
        secret = r["paste"].value("id", std::string());
        check(r["paste"].value("lang", std::string()) == "text" && r["paste"].value("title", std::string()) == "Untitled", "unknown language is text, no title is Untitled");
        r = askAs(s, bob, "paste.make", {{"title", "Crash log"}, {"text", "boom"}, {"lang", "log"}, {"expires", 1}});
        soon = r["paste"].value("id", std::string());
        check(r["paste"].value("until", 0LL) > Online::unixNow(), "a paste can run out");
        r = askAs(s, cat, "paste.list");
        check(r["pastes"].size() == 2, "unlisted pastes aren't in the list");
        check(!r["pastes"][0].contains("text"), "the list leaves out the text");
        check(askAs(s, cat, "paste.list", {{"lang", "lua"}}).at("pastes").size() == 1, "by language");
        check(askAs(s, cat, "paste.list", {{"by", "bob"}}).at("pastes").size() == 2, "by person");
        r = askAs(s, cat, "paste.get", {{"id", secret}});
        check(r.value("ok", false) && r["paste"].value("views", 0) == 1, "an unlisted paste opens with its link (and counts a view)");
        check(!r["paste"].value("canDelete", true), "cat can't delete bob's paste");
        check(askAs(s, bob, "paste.mine").at("pastes").size() == 3, "bob sees all his, unlisted too");
        check(!askAs(s, cat, "paste.mine").value("pastes", json::array()).size(), "cat has none");
        check(!askAs(s, bob, "report.send", {{"kind", "paste"}, {"id", code}, {"reason", "spam"}}).value("ok", true), "you can't report your own paste");
        r = askAs(s, cat, "report.send", {{"kind", "paste"}, {"id", secret}, {"reason", "scam"}});
        check(r.value("ok", false), "pastes can be reported: " + error(r));
        r = s.handle(json{{"op", "paste.raw"}, {"args", {{"id", code}}}});
        check(r.value("ok", false) && r.value("text", std::string()) == "local x = 1\nprint(x)\t-- hi", "the raw text, without an account (for /pasteraw)");
        check(!askAs(s, cat, "paste.delete", {{"id", code}}).value("ok", true), "only the maker or staff delete");
        check(ask(s, "paste.delete", {{"id", secret}}).value("ok", false), "staff can delete");
        check(!askAs(s, cat, "paste.get", {{"id", secret}}).value("ok", true), "a deleted paste is gone");
    }
    {   // Make the crash log run out, then start again.
        std::string text;
        ServerUtil::readFile(dir / "data" / "pastes.json", text);
        json j = json::parse(text);
        j["pastes"][soon]["until"] = Online::unixNow() - 5;
        ServerUtil::writeFile(dir / "data" / "pastes.json", j.dump());
    }
    {
        auto server = startServer();
        GbServer& s = *server;
        check(askAs(s, cat, "paste.list").at("pastes").size() == 1, "pastes are kept after a restart, run-out ones aren't");
        check(!askAs(s, cat, "paste.get", {{"id", soon}}).value("ok", true), "a run-out paste can't be opened");
        check(askAs(s, bob, "paste.delete", {{"id", code}}).value("ok", false), "bob deletes his");
        check(askAs(s, cat, "paste.list").at("pastes").empty(), "and the list is empty");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll paste checks passed.\n");
    return failures ? 1 : 0;
}
