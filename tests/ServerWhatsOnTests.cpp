// The C++ server's events calendar (ServerCalendar.cpp), Press (ServerPress.cpp) and polls on forum
// threads and group walls (ServerUtil.h makePoll); worker/test/calendar-press-polls.test.mjs checks the website's.
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
    const fs::path dir = fs::temp_directory_path() / ("gb-whatson-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
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
    std::string eventId, postId, threadId;
    const long long now = Online::unixNow(), D = 86400, H = 3600;
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        GbServer& s = *server;
        check(ask(s, "hello", {{"name", "Guts"}}).value("ok", false), "Guts says hello");
        check(signUp(s, bob, "bob").value("ok", false), "bob signs up");
        check(signUp(s, cat, "cat").value("ok", false), "cat signs up");

        // --- polls on the forum ---
        json r = askAs(s, bob, "forum.post", {{"board", "offtopic"}, {"title", "Next map"}, {"text", ""},
                                              {"poll", {{"question", "Which one?"}, {"options", {"Docks", "Desert"}}}}});
        check(r.value("ok", false), "a forum thread with only a poll: " + error(r));
        threadId = r["thread"].value("id", std::string());
        check(r["thread"].value("hasPoll", false) && r["thread"]["poll"].value("question", std::string()) == "Which one?", "the thread shows its poll");
        check(error(askAs(s, bob, "forum.post", {{"board", "offtopic"}, {"title", "x"}, {"text", "y"}, {"poll", {{"question", "Q"}, {"options", {"a", " A "}}}}})).find("2 different") != std::string::npos,
              "a poll needs 2 different answers");
        check(error(askAs(s, bob, "forum.post", {{"board", "offtopic"}, {"title", "x"}, {"text", "y"}, {"poll", {{"question", "Q"}, {"options", {"a", "b"}}, {"hours", 5}}}})).find("how long") != std::string::npos,
              "only the listed poll lengths");
        r = askAs(s, cat, "forum.pollVote", {{"thread", threadId}, {"option", 1}});
        check(r.value("ok", false) && r["poll"]["options"][1].value("votes", 0) == 1 && r["poll"].value("mine", -2) == 1, "cat votes: " + error(r));
        check(!askAs(s, cat, "forum.pollVote", {{"thread", threadId}, {"option", 7}}).value("ok", true), "only real answers");
        r = askAs(s, bob, "forum.thread", {{"thread", threadId}});
        check(r["thread"]["poll"].value("total", 0) == 1 && r["thread"]["poll"].value("mine", -2) == -1, "bob sees the count, not who voted");
        r = askAs(s, cat, "forum.pollVote", {{"thread", threadId}, {"option", -1}});
        check(r.value("ok", false) && r["poll"].value("total", 1) == 0, "cat takes the vote back");

        // --- polls on a group wall ---
        r = askAs(s, bob, "groups.create", {{"name", "Builders"}});
        const std::string gid = r.contains("group") ? r["group"].value("id", std::string()) : "";
        check(!gid.empty(), "bob makes a group: " + error(r));
        r = askAs(s, bob, "groups.post", {{"id", gid}, {"text", ""}, {"poll", {{"question", "When?"}, {"options", {"Fri", "Sat"}}, {"hours", 72}}}});
        check(r.value("ok", false), "a wall post with a poll: " + error(r));
        json wall = askAs(s, bob, "groups.get", {{"id", gid}})["wall"];
        const long long time = wall.empty() ? 0 : wall[0].value("time", 0LL);
        check(!wall.empty() && wall[0].contains("poll"), "the wall shows the poll");
        check(!askAs(s, cat, "groups.pollVote", {{"id", gid}, {"by", bob.id}, {"time", time}, {"option", 0}}).value("ok", true), "only members vote");
        askAs(s, cat, "groups.join", {{"id", gid}});
        r = askAs(s, cat, "groups.pollVote", {{"id", gid}, {"by", bob.id}, {"time", time}, {"option", 0}});
        check(r.value("ok", false) && r["poll"].value("total", 0) == 1, "a member votes: " + error(r));

        // --- the calendar ---
        json ev = {{"title", "Winter Jam"}, {"text", "Make a game about snow."}, {"kind", "jam"}, {"start", now + D}, {"end", now + 8 * D}};
        r = askAs(s, bob, "calendar.make", ev);
        check(r.value("ok", false), "bob puts a jam on the calendar: " + error(r));
        eventId = r["event"].value("id", std::string());
        check(!r["event"].value("official", true) && r["event"].value("going", 0) == 1 && r["event"].value("kindName", std::string()) == "Game jam", "not official, the maker is going");
        json bad = ev; bad["link"] = "https://evil.example";
        check(error(askAs(s, bob, "calendar.make", bad)).find("Guts&Bolts") != std::string::npos, "links stay on the site");
        bad = ev; bad["end"] = now + 40 * D;
        check(error(askAs(s, bob, "calendar.make", bad)).find("31 days") != std::string::npos, "at most 31 days long");
        r = ask(s, "calendar.make", {{"title", "Live show"}, {"kind", "live"}, {"start", now + 2 * H}, {"end", now + 3 * H}, {"link", "https://gutsandbolts.net/app/#/live"}});
        check(r.value("ok", false) && r["event"].value("official", false) && r["event"].value("link", std::string()) == "#/live", "staff events are official; site links are tidied");
        r = askAs(s, cat, "calendar.list");
        check(r["events"].size() == 2 && r["events"][0].value("title", std::string()) == "Live show", "upcoming, soonest first");
        check(!r["events"][0].contains("text"), "the list leaves out the description");
        check(askAs(s, cat, "calendar.list", {{"kind", "jam"}})["events"].size() == 1, "by kind");
        check(!askAs(s, cat, "calendar.list", {{"from", now}, {"to", now + 90 * D}}).value("ok", true), "at most two months at a time");
        r = askAs(s, cat, "calendar.going", {{"id", eventId}});
        check(r["event"].value("going", 0) == 2 && r["event"].value("amGoing", false), "cat is going");
        check(!askAs(s, cat, "calendar.edit", {{"id", eventId}, {"title", "Mine"}, {"start", now + D}, {"end", now + 8 * D}}).value("ok", true), "only the maker or staff change it");
        r = askAs(s, bob, "calendar.edit", {{"id", eventId}, {"title", "Winter Jam"}, {"kind", "jam"}, {"start", now + 2 * D}, {"end", now + 9 * D}});
        check(r.value("ok", false), "bob moves it: " + error(r));
        r = askAs(s, cat, "report.send", {{"kind", "calendar"}, {"id", eventId}, {"reason", "spam"}});
        check(r.value("ok", false), "events can be reported: " + error(r));

        // --- Press ---
        check(error(askAs(s, bob, "press.write", {{"title", "Hi"}, {"text", "x"}})).find("staff") != std::string::npos, "only staff write Press posts");
        r = ask(s, "press.write", {{"title", "Creator spotlight: bob"}, {"kind", "spotlight"}, {"creator", "bob"},
                                   {"text", "bob makes cosy games.\n\n## His jam\nIt's about snow."}});
        check(r.value("ok", false), "staff write a spotlight: " + error(r));
        postId = r["post"].value("id", std::string());
        check(postId.rfind("creator-spotlight-bob-", 0) == 0, "the id reads well: " + postId);
        check(r["post"].value("summary", std::string()) == "bob makes cosy games.", "the first paragraph is the summary");
        check(r["post"]["blocks"].size() == 3 && r["post"]["blocks"][1].value("h", std::string()) == "His jam", "headings and paragraphs");
        json draft = ask(s, "press.write", {{"title", "Secret"}, {"text", "Soon."}, {"published", false}});
        const std::string draftId = draft["post"].value("id", std::string());
        r = askAs(s, cat, "press.list");
        check(r["posts"].size() == 1, "drafts aren't listed");
        check(!askAs(s, cat, "press.get", {{"id", draftId}}).value("ok", true), "drafts only open for staff");
        check(ask(s, "press.list", {{"drafts", true}})["posts"].size() == 1, "staff see the drafts");
        r = askAs(s, cat, "press.get", {{"id", postId}});
        check(r.value("ok", false) && r["post"].value("views", 0) == 1 && !r["post"].value("canEdit", true), "anyone reads it (a view)");
        check(ask(s, "press.delete", {{"id", draftId}}).value("ok", false), "staff delete a post");
        r = askAs(s, bob, "notes.list");
        if (r.value("ok", false)) {
            std::string kinds;
            for (const json& n : r.value("notes", json::array())) kinds += n.value("kind", std::string()) + " ";
            check(kinds.find("press") != std::string::npos, "bob hears about his spotlight");
        }
    }
    {
        auto server = startServer();
        GbServer& s = *server;
        check(askAs(s, cat, "calendar.get", {{"id", eventId}}).value("ok", false), "events are kept after a restart");
        check(askAs(s, cat, "press.get", {{"id", postId}}).value("ok", false), "Press posts are kept after a restart");
        check(askAs(s, cat, "forum.thread", {{"thread", threadId}})["thread"].contains("poll"), "forum polls are kept");
        json gs = askAs(s, cat, "groups.list");
        bool kept = false;
        for (const json& g : gs.value("groups", json::array())) {
            json w = askAs(s, cat, "groups.get", {{"id", g.value("id", std::string())}})["wall"];
            if (!w.empty() && w[0].contains("poll") && w[0]["poll"].value("total", 0) == 1) kept = true;
        }
        check(kept, "group wall polls are kept");
        check(askAs(s, bob, "calendar.delete", {{"id", eventId}}).value("ok", false), "bob calls it off");
        check(!askAs(s, cat, "calendar.get", {{"id", eventId}}).value("ok", true), "and it's gone");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll calendar, Press and poll checks passed.\n");
    return failures ? 1 : 0;
}
