// Early testers: before a game goes public, its creator picks a few people to play it first and say
// what they think. worker/testers.js has the same rules (and the notes on why):
//   - access "testers": only the creator, staff and the people on the game's tester list can play it
//   - up to kMostTesters testers, each told they were picked
//   - a tester leaves one note (1-5 stars and a few words); only the creator and staff read them
//   - making the game public tells every tester it's out
//   game.testers {game}   game.addTester {game, who}   game.removeTester {game, who}
//   game.testerNote {game, stars, text}   game.amTester {game}
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kMostTesters = 25, kNoteText = 500;
std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
} // namespace

bool GbServer::isTester(const Asset& a, const User& me) const {
    return std::find(a.testers.begin(), a.testers.end(), me.id) != a.testers.end();
}

json GbServer::testerPerson(const std::string& id) const {
    auto u = m_users.find(id);
    if (u == m_users.end()) return {{"id", id}, {"name", "?"}, {"username", ""}, {"userId", 0}};
    return {{"id", u->second.id}, {"name", u->second.name}, {"username", u->second.username}, {"userId", u->second.userId}};
}

void GbServer::testersLaunched(const Asset& a, const User& me) {
    for (const std::string& id : a.testers) {
        auto u = m_users.find(id);
        if (u != m_users.end()) notify(&u->second, "testers", a.name + ", the game you tested for " + me.name + ", is out for everyone now.", a.id, false);
    }
    saveUsers();
}

json GbServer::testerOp(const std::string& name, User& me, const json& args) {
    if (me.userId == 0) return fail("Sign up to test games.");
    auto it = findAsset(Online::cleanText(sv(args, "game"), 80));
    if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
    Asset& a = it->second;
    const bool mine = a.creator == me.id;

    auto list = [&]() {
        json out = json::array();
        for (const std::string& id : a.testers) { json p = testerPerson(id); p["noted"] = a.testerNotes.contains(id); out.push_back(p); }
        return out;
    };

    if (name == "game.amTester") {
        json r = okay(); r["tester"] = isTester(a, me);
        r["note"] = a.testerNotes.contains(me.id) ? a.testerNotes[me.id] : json(nullptr);
        return r;
    }
    if (name == "game.testerNote") {
        if (!isTester(a, me)) return fail("Only the game's testers can leave a tester note.");
        const long long stars = args.contains("stars") && args["stars"].is_number() ? (long long)args["stars"].get<double>() : 0;
        if (stars < 1 || stars > 5) return fail("Pick 1 to 5 stars.");
        const std::string text = say(sv(args, "text"), kNoteText, true);
        if (text.empty()) return fail("Write a few words: what was fun, and what wasn't?");
        const bool had = a.testerNotes.contains(me.id);
        a.testerNotes[me.id] = {{"stars", stars}, {"text", text}, {"at", Online::unixNow()}};
        saveAssets();
        if (!had) {
            auto c = m_users.find(a.creator);
            if (c != m_users.end()) notify(&c->second, "testers", me.name + " left a tester note on " + a.name + " (" + std::to_string(stars) + " stars).", a.id);
        }
        json r = okay(); r["note"] = a.testerNotes[me.id]; return r;
    }

    if (!mine && !(isStaff(me) && name == "game.testers")) return fail("Only the game's creator can choose its testers.");
    if (name == "game.testers") {
        json notes = json::array();
        for (auto& [id, n] : a.testerNotes.items())
            notes.push_back({{"who", testerPerson(id)}, {"stars", n.value("stars", 0)}, {"text", n.value("text", std::string())}, {"at", n.value("at", 0LL)}});
        std::sort(notes.begin(), notes.end(), [](const json& x, const json& y) { return x.value("at", 0LL) > y.value("at", 0LL); });
        json r = okay(); r["testers"] = list(); r["notes"] = notes; r["most"] = kMostTesters; return r;
    }

    const std::string whoText = args.contains("who") && args["who"].is_number() ? std::to_string(args["who"].get<long long>()) : sv(args, "who");
    User* who = findPerson(whoText);
    if (!who) return fail("There's nobody with that name or number.");
    if (name == "game.addTester") {
        if (who->id == me.id) return fail("You made it, so you can always play it.");
        if (who->userId == 0) return fail("Guests can't be testers. Ask them to sign up first.");
        if (isTester(a, *who)) return fail(who->name + " is already a tester.");
        if (a.testers.size() >= kMostTesters) return fail("A game can have " + std::to_string(kMostTesters) + " testers at most. Take someone off first.");
        a.testers.push_back(who->id);
        saveAssets();
        notify(who, "testers", me.name + " picked you to test " + a.name + " before anyone else. Play it and leave a note!", a.id);
        json r = okay(); r["testers"] = list(); return r;
    }
    if (name == "game.removeTester") {
        if (!isTester(a, *who)) return fail(who->name + " isn't a tester of this game.");
        a.testers.erase(std::remove(a.testers.begin(), a.testers.end(), who->id), a.testers.end());
        a.testerNotes.erase(who->id);
        saveAssets();
        json r = okay(); r["testers"] = list(); return r;
    }
    return fail("Unknown request.");
}
