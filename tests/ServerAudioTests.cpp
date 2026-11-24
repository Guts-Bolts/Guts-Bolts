// The C++ server's Audio portal (ServerAudio.cpp; worker/test/audio-portal.test.mjs checks the website's):
// lists, categories, plays, faves, comments, reports and the sound file.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-audio-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
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
    const std::string sound = Online::base64Encode("ID3 pretend this is an mp3 file with some sound in it");
    Other bob, cat;
    std::string boss, zap, waiting;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");
        // Staff upload two public sounds (like the portal's upload page: "upload", then "audio.tag").
        json r = ask(s, "upload", {{"kind", "audio"}, {"name", "Boss Battle"}, {"description", "A loud one"}, {"data", sound},
                                   {"meta", {{"ext", "mp3"}}}, {"access", "public"}});
        check(r.value("ok", false), "staff upload a sound: " + error(r));
        boss = r["asset"].value("id", std::string());
        r = ask(s, "audio.tag", {{"id", boss}, {"cat", "music"}, {"genre", "chiptune"}, {"tags", "boss, 8bit #boss"}});
        check(r.value("ok", false) && r["track"]["tags"] == json::array({"boss", "8bit"}), "tags are tidied: " + error(r));
        check(r["track"].value("genre", std::string()) == "chiptune" && r["track"].value("num", 0LL) > 0, "a genre, and a number games use");
        r = ask(s, "upload", {{"kind", "audio"}, {"name", "Laser zap"}, {"data", sound}, {"meta", {{"ext", "wav"}}}, {"access", "public"}});
        zap = r["asset"].value("id", std::string());
        r = askAs(s, cat, "audio.list");
        check(r["tracks"].size() == 2, "both are in the portal (an untagged one too)");
        check(askAs(s, cat, "audio.get", {{"id", zap}}).at("track").value("cat", std::string()) == "other", "untagged ones are \"other\"");
        check(!askAs(s, cat, "audio.tag", {{"id", zap}, {"cat", "sfx"}}).value("ok", true), "only the maker tags it");
        check(ask(s, "audio.tag", {{"id", zap}, {"cat", "sfx"}, {"genre", "rock"}}).at("track").value("genre", std::string()).empty(), "no genre for sound effects");
        check(askAs(s, cat, "audio.list", {{"cat", "sfx"}}).at("tracks").size() == 1, "by category");
        check(askAs(s, cat, "audio.list", {{"genre", "chiptune"}}).at("tracks").size() == 1, "by genre");
        check(askAs(s, cat, "audio.list", {{"tag", "boss"}}).at("tracks").size() == 1, "by tag");
        check(askAs(s, cat, "audio.list", {{"q", "laser"}}).at("tracks").size() == 1, "by search");
        // Plays: one per person per sound a day; the maker's own don't count.
        check(askAs(s, cat, "audio.listen", {{"id", zap}}).value("plays", 0LL) == 1, "cat plays it");
        check(askAs(s, cat, "audio.listen", {{"id", zap}}).value("plays", 0LL) == 1, "again the same day doesn't count");
        check(askAs(s, bob, "audio.listen", {{"id", zap}}).value("plays", 0LL) == 2, "bob plays it");
        check(ask(s, "audio.listen", {{"id", zap}}).value("plays", 0LL) == 2, "the maker's own don't count");
        check(askAs(s, cat, "audio.list", {{"sort", "plays"}}).at("tracks")[0].value("id", std::string()) == zap, "most played first");
        check(askAs(s, cat, "audio.fave", {{"id", boss}}).value("faves", 0) == 1, "cat faves it");
        check(askAs(s, cat, "audio.fave", {{"id", boss}, {"on", true}}).value("faves", 0) == 1, "faving twice counts once");
        check(askAs(s, bob, "audio.list", {{"sort", "top"}}).at("tracks")[0].value("id", std::string()) == boss, "most faved first");
        // Comments, reports.
        r = askAs(s, cat, "audio.comment", {{"id", boss}, {"text", "Great loop!"}});
        check(r.value("ok", false) && r["track"]["commentList"].size() == 1, "cat comments: " + error(r));
        const std::string cid = r["track"]["commentList"][0].value("id", std::string());
        r = askAs(s, bob, "report.send", {{"kind", "audio"}, {"id", boss + ":" + cid}, {"reason", "spam"}});
        check(r.value("ok", false), "comments can be reported: " + error(r));
        check(askAs(s, bob, "report.send", {{"kind", "audio"}, {"id", boss}, {"reason", "spam"}}).value("ok", false), "and the sound");
        check(!askAs(s, bob, "audio.uncomment", {{"id", boss}, {"comment", cid}}).value("ok", true), "only the writer, maker or staff delete comments");
        // bob isn't Verified: his sound waits for a check, and private ones stay out of the lists.
        r = askAs(s, bob, "upload", {{"kind", "audio"}, {"name", "Mine"}, {"data", sound}, {"meta", {{"ext", "ogg"}}}, {"access", "public"}});
        check(r.value("ok", false), "bob uploads: " + error(r));
        waiting = r["asset"].value("id", std::string());
        check(askAs(s, cat, "audio.list").at("tracks").size() == 2, "his isn't in the portal yet");
        check(!askAs(s, cat, "audio.get", {{"id", waiting}}).value("ok", true), "cat can't open it");
        check(askAs(s, bob, "audio.list", {{"by", "bob"}}).at("tracks").size() == 1, "bob sees it in My audio");
        check(!s.handle(json{{"op", "audio.file"}, {"args", {{"id", waiting}}}}).value("ok", true), "no file for it before the check");
        r = s.handle(json{{"op", "audio.file"}, {"args", {{"id", boss}}}});
        check(r.value("ok", false) && r.value("ext", std::string()) == "mp3" && r.value("data", std::string()) == sound, "the file, without an account (for /sound)");
        const json guest = s.handle(json{{"op", "audio.list"}, {"args", json::object()}});
        check(!guest.value("ok", true) || guest.contains("tracks"), "(only signed requests list)");
    }
    {
        auto server = startServer();   // (comes back after a restart)
        GbServer& s = *server;
        json r = askAs(s, cat, "audio.get", {{"id", boss}});
        check(r.value("ok", false) && r["track"].value("faves", 0) == 1 && r["track"]["commentList"].size() == 1, "faves and comments are kept after a restart");
        check(askAs(s, cat, "audio.get", {{"id", zap}}).at("track").value("plays", 0LL) == 2, "and plays");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll audio checks passed.\n");
    return failures ? 1 : 0;
}
