// The C++ server's GIFs, reactions and forum votes (ServerForum.cpp, ServerSocial.cpp,
// core/TextFilter.cpp; worker/test/chat-forum-reactions.test.mjs checks the website's).
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
    const fs::path dir = fs::temp_directory_path() / ("gb-react-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    fs::create_directories(dir / "data");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    GbServer::Options opts;
    opts.data = dir / "data";
    opts.official = Account::id();
    std::string err;
    std::unique_ptr<GbServer> server;
    for (int tries = 0; tries < 20 && !server; ++tries) {
        opts.port = 20000 + std::rand() % 40000;
        auto s = std::make_unique<GbServer>(opts);
        if (s->start(err)) server = std::move(s);
    }
    if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
    GbServer& s = *server;
    const std::string gif = "[media:https://static.klipy.com/ii/d7aec6f6f171607374b2065c836f92f4/c6/64/QLYkF4M7.gif]";
    const std::string fire = "\xF0\x9F\x94\xA5", laugh = "\xF0\x9F\x98\x82";

    check(TextFilter::filter("look " + gif) == "look " + gif, "a KLIPY GIF gets through the text filter");
    check(TextFilter::filter("[media:https://evil.example/a.gif]") == TextFilter::kContentDeleted, "other picture links don't");
    check(TextFilter::filter(gif + " go to evil.com") == TextFilter::kContentDeleted, "a GIF doesn't let a link through next to it");

    check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
    Other bob, cat, dan;
    json bobMe = signUp(s, bob, "bob"), catMe = signUp(s, cat, "cat");
    check(bobMe.value("ok", false) && catMe.value("ok", false), "bob and cat sign up");
    check(signUp(s, dan, "dan").value("ok", false), "dan signs up");

    json r = askAs(s, bob, "forum.post", {{"board", "help"}, {"title", "my build"}, {"text", "Here it is\n" + gif}});
    check(r.value("ok", false), "bob posts with a GIF: " + error(r));
    const std::string th = r["thread"].value("id", std::string());
    const std::string post = r["posts"][0].value("id", std::string());
    check(r["posts"][0].value("text", std::string()) == "Here it is\n" + gif, "the GIF is kept");
    r = askAs(s, cat, "forum.list", {{"board", "help"}});
    check(r["threads"][0].value("preview", std::string()) == "Here it is", "the card has a preview");
    check(r["threads"][0].value("image", std::string()).find("static.klipy.com") != std::string::npos, "and its first GIF");

    r = askAs(s, cat, "forum.react", {{"thread", th}, {"post", post}, {"emoji", fire}});
    check(r.value("ok", false) && r["reactions"].size() == 1 && r["reactions"][0].value("count", 0) == 1 && r["reactions"][0].value("mine", false), "cat reacts: " + error(r));
    r = askAs(s, bob, "forum.react", {{"thread", th}, {"post", post}, {"emoji", fire}});
    check(r["reactions"][0].value("count", 0) == 2, "bob too");
    r = askAs(s, cat, "forum.react", {{"thread", th}, {"post", post}, {"emoji", fire}});
    check(r["reactions"][0].value("count", 0) == 1 && !r["reactions"][0].value("mine", true), "pressing it again takes it back");
    check(!askAs(s, cat, "forum.react", {{"thread", th}, {"post", post}, {"emoji", "lol"}}).value("ok", true), "only the listed emoji");
    r = askAs(s, dan, "forum.thread", {{"thread", th}});
    check(r["posts"][0]["reactions"].size() == 1 && r["canReact"].get<bool>(), "the thread shows the reactions");

    check(askAs(s, cat, "forum.vote", {{"thread", th}, {"vote", 1}}).at("thread").value("score", 0) == 1, "cat votes up");
    check(askAs(s, dan, "forum.vote", {{"thread", th}, {"vote", -1}}).at("thread").value("score", 9) == 0, "dan votes down");
    r = askAs(s, dan, "forum.vote", {{"thread", th}, {"vote", 0}});
    check(r["thread"].value("score", 0) == 1 && r["thread"].value("myVote", 9) == 0, "dan takes it back");
    check(!askAs(s, dan, "forum.vote", {{"thread", th}, {"vote", 5}}).value("ok", true), "only up, down or nothing");
    r = askAs(s, cat, "forum.list", {{"board", "all"}, {"sort", "top"}});
    check(r.value("ok", false) && r["threads"].size() == 1 && !r.value("canPost", true) && r.value("sort", std::string()) == "top", "the all-boards feed");
    askAs(s, cat, "forum.thread", {{"thread", th}, {"peek", true}});
    check(askAs(s, cat, "forum.list", {{"board", "help"}}).at("threads")[0].value("views", 9) == 1, "a peek (link card) isn't a view");

    // A chat: bob and cat are friends, bob sends a GIF, cat reacts.
    const long long catId = catMe["me"].value("userId", 0LL);
    askAs(s, bob, "friends.add", {{"user", catId}});
    r = askAs(s, cat, "friends.accept", {{"user", bobMe["me"].value("userId", 0LL)}});
    check(r.value("status", std::string()) == "friends", "bob and cat are friends: " + error(r));
    r = askAs(s, bob, "party.create", {{"members", json::array({cat.id})}});
    check(r.value("ok", false), "bob starts a chat: " + error(r));
    const std::string chat = r["chat"].value("id", std::string());
    check(askAs(s, cat, "party.respond", {{"id", chat}, {"accept", true}}).value("ok", false), "cat joins");
    r = askAs(s, bob, "party.send", {{"id", chat}, {"body", "gg\n" + gif}});
    check(r.value("ok", false), "bob sends a GIF: " + error(r));
    const json msg = r["chat"]["messages"].back();
    check(msg.value("body", std::string()) == "gg\n" + gif, "the GIF is kept in the chat");
    r = askAs(s, cat, "party.react", {{"id", chat}, {"message", msg.value("id", std::string())}, {"emoji", laugh}});
    check(r.value("ok", false), "cat reacts in the chat: " + error(r));
    r = askAs(s, bob, "party.read", {{"id", chat}});
    const json seen = r["chat"]["messages"].back();
    check(seen.contains("reacts") && seen["reacts"].contains(laugh) && seen["reacts"][laugh].size() == 1, "bob sees cat's reaction");
    askAs(s, cat, "party.react", {{"id", chat}, {"message", msg.value("id", std::string())}, {"emoji", laugh}});
    check(!askAs(s, bob, "party.read", {{"id", chat}}).at("chat")["messages"].back().contains("reacts"), "and it goes when cat takes it back");
    check(!askAs(s, cat, "party.react", {{"id", chat}, {"message", "nope"}, {"emoji", laugh}}).value("ok", true), "a missing message is refused");

    server.reset();
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("All passed")) << "\n";
    return failures ? 1 : 0;
}
