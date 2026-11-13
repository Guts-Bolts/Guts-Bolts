// The C++ server's G&B Academy (ServerAcademy.cpp; worker/test/academy.test.mjs checks the website's):
// writing, drafts, publishing, lessons, progress, likes, reports and deletes.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-academy-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
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
    std::string course, l1, l2;
    const json lessons = json::array({{{"title", "Your first script"}, {"text", "Make a Script.\n```lua\nprint(\"hi\")\n\tif x then end\n```"}},
                                      {{"title", "Variables"}, {"text", "local x = 5"}}});
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");
        check(error(askAs(s, bob, "academy.save", {{"title", " "}})).find("title") != std::string::npos, "a course needs a title");
        check(error(askAs(s, bob, "academy.save", {{"title", "Lua"}, {"lessons", {{{"title", "x"}, {"text", ""}}}}})).find("needs some text") != std::string::npos, "lessons need text");
        json r = askAs(s, bob, "academy.save", {{"title", "Lua for beginners"}, {"summary", "Learn to script"}, {"topic", "scripting"}, {"level", "beginner"}, {"lessons", lessons}});
        check(r.value("ok", false), "bob writes a course: " + error(r));
        course = r["course"].value("id", std::string());
        l1 = r["course"]["lessonList"][0].value("id", std::string());
        l2 = r["course"]["lessonList"][1].value("id", std::string());
        check(!r["course"].value("published", true), "it starts as a draft");
        check(askAs(s, cat, "academy.list").at("courses").empty(), "drafts aren't listed");
        check(!askAs(s, cat, "academy.get", {{"id", course}}).value("ok", true), "and others can't open them");
        check(askAs(s, bob, "academy.publish", {{"id", course}, {"on", true}}).value("ok", false), "bob publishes");
        r = askAs(s, cat, "academy.list", {{"topic", "scripting"}});
        check(r["courses"].size() == 1 && r["courses"][0].value("lessons", 0) == 2, "now it's listed, by topic too");
        r = askAs(s, cat, "academy.lesson", {{"id", course}, {"lesson", l1}});
        check(r.value("ok", false) && r["lesson"].value("next", std::string()) == l2, "a lesson knows the next one");
        check(r["lesson"].value("text", std::string()).find("    if x then end") != std::string::npos, "tabs become spaces, so code keeps its shape");
        // bob changes the course: lesson ids stay, so ticks stay.
        json changed = lessons; changed[0]["id"] = l1; changed[1]["id"] = l2; changed[1]["title"] = "Variables and numbers";
        check(askAs(s, cat, "academy.done", {{"id", course}, {"lesson", l1}}).value("done", 0) == 1, "cat ticks lesson 1");
        r = askAs(s, bob, "academy.save", {{"id", course}, {"title", "Lua for beginners"}, {"topic", "scripting"}, {"lessons", changed}});
        check(r.value("ok", false) && r["course"]["lessonList"][1].value("id", std::string()) == l2, "editing keeps lesson ids");
        check(!askAs(s, cat, "academy.save", {{"id", course}, {"title", "Mine now"}}).value("ok", true), "only the writer edits");
        r = askAs(s, cat, "academy.done", {{"id", course}, {"lesson", l2}});
        check(r.value("finished", false), "cat finishes the course");
        r = askAs(s, cat, "academy.get", {{"id", course}});
        check(r["course"].value("done", 0) == 2 && r["course"].value("learners", 0) == 1, "progress and learners count");
        check(askAs(s, cat, "academy.like", {{"id", course}}).value("likes", 0) == 1, "cat likes it");
        check(!askAs(s, bob, "academy.like", {{"id", course}}).value("ok", true), "but bob can't like his own");
        r = askAs(s, cat, "academy.mine");
        check(r["learning"].size() == 1 && r["written"].empty(), "cat's own list shows what she's learning");
        check(!askAs(s, bob, "report.send", {{"kind", "course"}, {"id", course}, {"reason", "spam"}}).value("ok", true), "you can't report your own course");
        check(askAs(s, cat, "report.send", {{"kind", "course"}, {"id", course}, {"reason", "spam"}}).value("ok", false), "courses can be reported");
    }
    {
        auto server = startServer();   // (comes back after a restart)
        GbServer& s = *server;
        json r = askAs(s, cat, "academy.get", {{"id", course}});
        check(r.value("ok", false) && r["course"].value("done", 0) == 2 && r["course"].value("likes", 0) == 1, "courses and progress are kept after a restart");
        check(!askAs(s, cat, "academy.delete", {{"id", course}}).value("ok", true), "only the writer or staff delete");
        check(ask(s, "academy.delete", {{"id", course}}).value("ok", false), "staff can");
        check(askAs(s, cat, "academy.list").at("courses").empty(), "and it's gone");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll academy checks passed.\n");
    return failures ? 1 : 0;
}
