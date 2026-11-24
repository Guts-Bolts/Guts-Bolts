// The C++ server's G&B Live (ServerStream.cpp; worker/test/streams.test.mjs checks the website's):
// who can go live, pieces arriving and going out, old pieces letting go, chat with time outs and
// slow mode, followers hearing about it, reports, staff ending a stream, and a restart ending streams.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-stream-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
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
    // A WebM-looking piece (the EBML mark first), and something that isn't one.
    auto piece = [](char fill) { return std::string("\x1a\x45\xdf\xa3", 4) + std::string(1500, fill); };
    const std::string notWebm = std::string("ftypisom") + std::string(2000, '\x01');
    const std::string jpg = Online::base64Encode(std::string("\xff\xd8\xff\xe0", 4) + std::string(60, '\x03'));
    auto noAccount = [](GbServer& s, const std::string& op, const json& args) { return s.handle(json{{"op", op}, {"args", args}}); };
    Other ann, bob, cat;
    std::string id;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, ann, "ann").value("ok", false), "ann signs up");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");
        json r = ask(s, "admin.grant", {{"to", "ann"}, {"key", "verified"}, {"sig", Account::sign(Online::grantMessage("verified", ann.id))}});
        check(r.value("ok", false), "staff verify ann: " + error(r));
        check(error(askAs(s, bob, "stream.start", {{"title", "Hi"}})).find("Verified") != std::string::npos, "only Verified people go live");
        check(error(askAs(s, ann, "stream.start", {{"title", " "}})).find("title") != std::string::npos, "a title is needed");
        check(askAs(s, bob, "follow.add", {{"user", ann.id}}).value("ok", false), "bob follows ann");
        r = askAs(s, ann, "stream.start", {{"title", "Building an obby live"}, {"cat", "gaming"}, {"mime", "video/webm;codecs=vp8,opus"}, {"thumb", jpg}});
        check(r.value("ok", false) && r["stream"].value("state", std::string()) == "live", "ann goes live: " + error(r));
        id = r["stream"].value("id", std::string());
        const std::string push = r.value("push", std::string()), t = push.substr(push.find("?t=") + 3);
        check(push == "/liveup/" + id + "?t=" + t, "with a sending address");
        check(askAs(s, cat, "stream.list").at("live").empty(), "not listed before the first piece");
        auto send = [&](long long n, const std::string& data, const std::string& token) {
            return noAccount(s, "stream.push", {{"id", id}, {"t", token}, {"n", n}, {"data", Online::base64Encode(data)}});
        };
        check(send(0, piece('a'), t).value("ok", false), "the first piece arrives");
        check(!send(1, piece('b'), std::string(32, '0')).value("ok", true), "a wrong token");
        check(!send(0, piece('b'), t).value("ok", true), "out of order");
        check(!send(1, notWebm, t).value("ok", true), "not WebM");
        check(!send(1, piece('b').substr(0, 100), t).value("ok", true), "too small");
        check(askAs(s, cat, "stream.list").at("live").size() == 1, "now it's listed");
        r = askAs(s, cat, "stream.get", {{"id", id}});
        check(r["stream"].value("last", -1LL) == 0 && r["stream"].value("viewers", 0LL) == 1, "cat watches (one watching)");
        r = noAccount(s, "stream.piece", {{"id", id}, {"n", 0}});
        std::string got;
        Online::base64Decode(r.value("data", std::string()), got);
        check(got == piece('a'), "the piece goes out as it came in");
        check(noAccount(s, "stream.picture", {{"id", id}}).value("ok", false), "and its picture");
        for (long long n = 1; n < 20; ++n) send(n, piece((char)('a' + n)), t);
        r = askAs(s, cat, "stream.get", {{"id", id}});
        check(r["stream"].value("last", 0LL) == 19 && r["stream"].value("first", 0LL) == 5, "only the newest 15 pieces stay");
        check(!noAccount(s, "stream.piece", {{"id", id}, {"n", 0}}).value("ok", true), "old pieces are let go");
        // Chat.
        r = askAs(s, cat, "stream.chat", {{"id", id}, {"text", "first!"}});
        check(r.value("ok", false), "cat chats: " + error(r));
        askAs(s, bob, "stream.chat", {{"id", id}, {"text", "hi ann"}});
        r = askAs(s, cat, "stream.get", {{"id", id}, {"after", 1}});
        check(r["chat"].size() == 1 && r["chat"][0].value("text", std::string()) == "hi ann", "only new lines after the last one seen");
        check(!askAs(s, bob, "stream.timeout", {{"id", id}, {"user", cat.id}}).value("ok", true), "only the streamer gives time outs");
        check(askAs(s, ann, "stream.timeout", {{"id", id}, {"user", cat.id}}).value("ok", false), "ann times cat out");
        check(error(askAs(s, cat, "stream.chat", {{"id", id}, {"text", "let me back"}})).find("time out") != std::string::npos, "cat can't chat");
        check(askAs(s, bob, "stream.get", {{"id", id}}).at("chat").size() == 1, "and cat's lines went");
        check(askAs(s, ann, "stream.slow", {{"id", id}, {"secs", 15}}).at("stream").value("slow", 0LL) == 15, "slow mode on");
        check(error(askAs(s, bob, "stream.chat", {{"id", id}, {"text", "again"}})).find("Slow mode") != std::string::npos, "slow mode holds bob back");
        check(askAs(s, ann, "stream.chat", {{"id", id}, {"text", "streamer talks"}}).value("ok", false), "but not the streamer");
        // Followers were told.
        bool told = false;
        for (const json& n : askAs(s, bob, "notes.list").value("notes", json::array()))
            if (n.value("kind", std::string()) == "stream" && n.value("about", std::string()) == id) told = true;
        check(told, "bob (a follower) heard ann went live");
        // Reports.
        check(!askAs(s, ann, "report.send", {{"kind", "stream"}, {"id", id}, {"reason", "spam"}}).value("ok", true), "you can't report yourself");
        r = askAs(s, bob, "report.send", {{"kind", "stream"}, {"id", id}, {"reason", "spam"}});
        check(r.value("ok", false), "bob reports the stream: " + error(r));
        // Ending.
        check(!askAs(s, bob, "stream.end", {{"id", id}}).value("ok", true), "only the streamer or staff end it");
        r = ask(s, "stream.end", {{"id", id}});
        check(r.value("ok", false) && r["stream"].value("state", std::string()) == "ended", "staff end it");
        check(!send(20, piece('z'), t).value("ok", true), "no more pieces after it ends");
        check(!noAccount(s, "stream.piece", {{"id", id}, {"n", 19}}).value("ok", true), "and its pieces are gone");
        check(error(askAs(s, bob, "stream.chat", {{"id", id}, {"text", "bye"}})).find("ended") != std::string::npos, "chat closes");
        r = askAs(s, ann, "stream.start", {{"title", "Take two"}});
        check(r.value("ok", false), "ann goes live again");
        id = r["stream"].value("id", std::string());
    }
    {
        auto server = startServer();   // (a restart: pieces were only in memory, so the stream has ended)
        GbServer& s = *server;
        json r = askAs(s, cat, "stream.list");
        check(r.at("live").empty() && r.at("recent").size() == 2, "after a restart, streams that were on have ended");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll live stream checks passed.\n");
    return failures ? 1 : 0;
}
