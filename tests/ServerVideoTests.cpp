// The C++ server's G&B Videos (ServerVideo.cpp; worker/test/videos.test.mjs checks the website's):
// starting an upload, the file in pieces, the staff check, followers, likes, comments, reports and watching.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-video-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
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
    // An MP4's first bytes ("ftyp" at byte 4) and some more, and words pretending to be a video.
    std::string mp4 = std::string("\0\0\0\x18" "ftypisom", 12) + std::string(3000, '\x07');
    const std::string words = "hello, I am not a video at all, just some words";
    const std::string jpg = Online::base64Encode(std::string("\xff\xd8\xff\xe0", 4) + std::string(60, '\x03'));
    auto noAccount = [](GbServer& s, const std::string& op, const json& args) { return s.handle(json{{"op", op}, {"args", args}}); };
    // Send a file the way worker/index.js does: in pieces of `piece` bytes.
    auto sendFile = [&](GbServer& s, const json& started, const std::string& data, size_t piece) {
        const std::string up = started.value("upload", std::string());
        const std::string id = started["video"].value("id", std::string()), t = up.substr(up.find("?t=") + 3);
        json r;
        for (size_t at = 0; at < data.size() || at == 0; at += piece) {
            const bool last = at + piece >= data.size();
            r = noAccount(s, "video.chunk", {{"id", id}, {"t", t}, {"at", at}, {"data", Online::base64Encode(data.substr(at, piece))}, {"last", last}});
            if (!r.value("ok", false) || last) break;
        }
        return r;
    };
    Other bob, cat;
    std::string waiting, news;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");
        check(error(askAs(s, bob, "video.start", {{"title", " "}, {"size", 10}})).find("title") != std::string::npos, "a title is needed");
        check(error(askAs(s, bob, "video.start", {{"title", "x"}, {"size", 200LL * 1024 * 1024}})).find("too big") != std::string::npos, "not too big");
        // Words pretending to be a video are turned away when they arrive.
        json r = askAs(s, bob, "video.start", {{"title", "Fake"}, {"size", words.size()}});
        check(r.value("ok", false), "bob starts an upload: " + error(r));
        r = sendFile(s, r, words, 1000);
        check(error(r).find("isn't a video") != std::string::npos, "only real videos: " + error(r));
        // A real one, in pieces.
        r = askAs(s, bob, "video.start", {{"title", "My speedrun"}, {"cat", "gaming"}, {"tags", "speedrun, Obby #speedrun"}, {"size", mp4.size()},
                                          {"secs", 75.4}, {"thumb", jpg}});
        check(r.value("ok", false) && r["video"].value("state", std::string()) == "uploading", "an upload starts: " + error(r));
        check(r["video"]["tags"] == json::array({"speedrun", "obby"}), "tags are tidied");
        const json started = r;
        const std::string up = started.value("upload", std::string());
        check(!noAccount(s, "video.chunk", {{"id", started["video"]["id"]}, {"t", std::string(32, '0')}, {"at", 0}, {"data", ""}}).value("ok", true), "a wrong token");
        r = sendFile(s, started, mp4, 1000);
        check(r.value("ok", false) && r["video"].value("state", std::string()) == "ready", "the file arrives in pieces: " + error(r));
        waiting = r["video"].value("id", std::string());
        check(r["video"].value("review", std::string()) == "pending", "bob isn't Verified: it waits for a check");
        const std::string src = r["video"].value("src", std::string());
        check(src.find("?k=") != std::string::npos, "the uploader watches it with its key");
        const std::string key = src.substr(src.find("?k=") + 3);
        check(!sendFile(s, started, mp4, 1000).value("ok", true), "the upload address only works once");
        check(!noAccount(s, "video.bytes", {{"id", waiting}, {"from", 0}, {"size", 16}}).value("ok", true), "nobody else can watch it yet");
        r = noAccount(s, "video.bytes", {{"id", waiting}, {"k", key}, {"from", 4}, {"size", 8}});
        std::string got;
        Online::base64Decode(r.value("data", std::string()), got);
        check(r.value("ok", false) && got == "ftypisom" && r.value("size", 0LL) == (long long)mp4.size() && r.value("type", std::string()) == "mp4", "byte ranges with the key");
        check(askAs(s, cat, "video.list").at("videos").empty(), "not in the feed yet");
        check(!askAs(s, cat, "video.get", {{"id", waiting}}).value("ok", true), "cat can't open it");
        check(!askAs(s, bob, "video.review", {{"id", waiting}, {"ok", true}}).value("ok", true), "only staff check videos");
        check(ask(s, "video.pending").at("videos").size() == 1, "staff see it waiting");
        check(askAs(s, cat, "follow.add", {{"user", bob.id}}).value("ok", false), "cat follows bob");
        r = ask(s, "video.review", {{"id", waiting}, {"ok", true}});
        check(r.value("ok", false), "staff let it through: " + error(r));
        check(askAs(s, cat, "video.list").at("videos").size() == 1, "now it's in the feed");
        check(askAs(s, cat, "video.get", {{"id", waiting}}).at("video").value("src", std::string()) == "/vid/" + waiting, "with a plain address");
        check(noAccount(s, "video.thumb", {{"id", waiting}}).value("type", std::string()) == "jpg", "and its picture");
        r = askAs(s, cat, "notes.list");
        bool told = false;
        for (const json& n : r.value("notes", json::array())) if (n.value("kind", std::string()) == "video" && n.value("about", std::string()) == waiting) told = true;
        check(told, "cat (a follower) was told about the new video");
        // Staff go straight in.
        r = ask(s, "video.start", {{"title", "Big news"}, {"cat", "news"}, {"size", mp4.size()}});
        r = sendFile(s, r, mp4, 5000);
        check(r.value("ok", false) && !r["video"].contains("review"), "staff go straight in: " + error(r));
        news = r["video"].value("id", std::string());
        check(askAs(s, cat, "video.like", {{"id", news}}).value("likes", 0) == 1, "cat likes it");
        check(askAs(s, cat, "video.like", {{"id", news}, {"on", true}}).value("likes", 0) == 1, "liking twice counts once");
        r = askAs(s, bob, "video.list", {{"sort", "top"}});
        check(r["videos"].size() == 2 && r["videos"][0].value("id", std::string()) == news, "most liked first");
        check(askAs(s, bob, "video.list", {{"cat", "gaming"}}).at("videos").size() == 1, "by category");
        check(askAs(s, bob, "video.list", {{"tag", "obby"}}).at("videos").size() == 1, "by tag");
        r = askAs(s, cat, "video.comment", {{"id", waiting}, {"text", "So fast!"}});
        check(r.value("ok", false) && r["video"]["commentList"].size() == 1, "cat comments: " + error(r));
        const std::string cid = r["video"]["commentList"][0].value("id", std::string());
        r = askAs(s, bob, "report.send", {{"kind", "video"}, {"id", waiting + ":" + cid}, {"reason", "harassment"}});
        check(r.value("ok", false), "video comments can be reported: " + error(r));
        check(!askAs(s, bob, "report.send", {{"kind", "video"}, {"id", waiting}, {"reason", "harassment"}}).value("ok", true), "but not your own video");
        check(askAs(s, bob, "video.uncomment", {{"id", waiting}, {"comment", cid}}).value("ok", false), "the uploader can delete comments");
        check(!askAs(s, cat, "video.edit", {{"id", waiting}, {"title", "Mine"}}).value("ok", true), "only the uploader edits");
        check(askAs(s, bob, "video.edit", {{"id", waiting}, {"title", "My best speedrun"}}).at("video").value("title", std::string()) == "My best speedrun", "the uploader edits");
    }
    {
        auto server = startServer();   // (comes back after a restart)
        GbServer& s = *server;
        check(askAs(s, cat, "video.list").at("videos").size() == 2, "videos are kept after a restart");
        check(!askAs(s, cat, "video.delete", {{"id", waiting}}).value("ok", true), "only the uploader or staff delete");
        check(askAs(s, bob, "video.delete", {{"id", waiting}}).value("ok", false), "bob deletes his");
        check(!fs::exists(dir / "data" / "files" / ("video-" + waiting)), "and its file goes too");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll video checks passed.\n");
    return failures ? 1 : 0;
}
