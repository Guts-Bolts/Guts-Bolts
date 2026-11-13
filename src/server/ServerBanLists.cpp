// Ban lists: creators keep lists of people who may not play their games, and use one list in
// as many of their games as they like. worker/banlists.js has the same rules (and the notes on
// why: lists are never public, every ban has a reason and who added it, changes are logged, and
// banned players can appeal). The lists are kept in banlists.json.
//   banlist.mine / create / get / rename / delete / add / remove / share / answer / check / appeal
//   game.banLists {game, lists?}   game.ban {game, who, reason, days}
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kListName = 40, kReason = 200, kAppealText = 500, kAnswerNote = 200, kAppealMin = 10;
constexpr size_t kMostListsEach = 10, kMostEntries = 2000, kMostEditors = 10, kListsPerGame = 5, kLogKept = 300, kAppealsKept = 200;
constexpr long long kMostDays = 3650;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
long long   nv(const json& j, const char* k) { return j.contains(k) && j[k].is_number() ? j[k].get<long long>() : 0; }
bool live(const json& e, long long t) { const long long u = nv(e, "until"); return u == 0 || u > t; }
bool has(const json& arr, const std::string& v) {
    for (const json& x : arr) if (x.is_string() && x.get<std::string>() == v) return true;
    return false;
}
json fresh(const std::string& id, const std::string& name, const std::string& owner, long long t) {
    return {{"id", id}, {"name", name}, {"owner", owner}, {"editors", json::array()}, {"entries", json::array()},
            {"appeals", json::array()}, {"log", json::array()}, {"created", t}};
}
void logIt(json& l, const std::string& by, const std::string& did, const std::string& who = "", const std::string& note = "") {
    l["log"].push_back({{"at", Online::unixNow()}, {"by", by}, {"did", did}, {"who", who}, {"note", note}});
}
} // namespace

void GbServer::saveBanLists() {
    for (auto& [id, l] : m_banLists.items()) {
        json& log = l["log"];
        while (log.size() > kLogKept) log.erase(log.begin());
        json& aps = l["appeals"];
        size_t extra = aps.size() > kAppealsKept ? aps.size() - kAppealsKept : 0;
        for (size_t i = 0; i < aps.size() && extra > 0;) {
            if (sv(aps[i], "status") != "open") { aps.erase(aps.begin() + (long)i); --extra; }
            else ++i;
        }
    }
    writeFile(m_opts.data / "banlists.json", m_banLists.dump(1));
}
void GbServer::loadBanLists() {
    std::string text;
    if (readFile(m_opts.data / "banlists.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object()) m_banLists = j;
    }
}

std::string GbServer::banListRole(const json& l, const User& me) const {
    if (sv(l, "owner") == me.id) return "owner";
    if (has(l["editors"], me.id)) return "editor";
    if (isStaff(me)) return "staff";
    return "";
}

const json* GbServer::banFor(const Asset& a, const User& me, const json** entry) const {
    if (a.banLists.empty() || a.creator == me.id || isStaff(me)) return nullptr;
    const long long t = Online::unixNow();
    for (const std::string& id : a.banLists) {
        if (!m_banLists.contains(id)) continue;
        const json& l = m_banLists[id];
        for (const json& e : l["entries"])
            if (sv(e, "who") == me.id && live(e, t)) { if (entry) *entry = &e; return &l; }
    }
    return nullptr;
}

std::string GbServer::banMessageFor(const std::string& gameId, const User& me) const {
    auto it = findAsset(gameId);
    if (it == m_assets.end()) return "";
    const json* e = nullptr;
    if (!banFor(it->second, me, &e)) return "";
    const long long until = nv(*e, "until");
    return "You're banned from " + (it->second.name.empty() ? std::string("this game") : it->second.name) +
           (until ? " until " + Online::utcDay(until) : std::string()) + ". Reason: " + sv(*e, "reason") +
           " (You can appeal on the game's page.)";
}

json GbServer::banListSummary(const json& l, const User& me) const {
    const long long t = Online::unixNow();
    json games = json::array();
    const std::string id = sv(l, "id");
    for (const auto& [aid, a] : m_assets)
        if (a.kind == "game" && std::find(a.banLists.begin(), a.banLists.end(), id) != a.banLists.end()) games.push_back({{"id", aid}, {"name", a.name}});
    long long bans = 0, open = 0;
    for (const json& e : l["entries"]) if (live(e, t)) ++bans;
    for (const json& x : l["appeals"]) if (sv(x, "status") == "open") ++open;
    return {{"id", id}, {"name", sv(l, "name")}, {"owner", banListPerson(sv(l, "owner"))}, {"role", banListRole(l, me)},
            {"created", nv(l, "created")}, {"bans", bans}, {"openAppeals", open}, {"games", games}};
}

json GbServer::banListPerson(const std::string& id) const {
    auto it = m_users.find(id);
    if (it == m_users.end()) return {{"id", id}, {"name", "?"}, {"username", ""}, {"userId", 0}};
    return {{"id", id}, {"name", it->second.name}, {"username", it->second.username}, {"userId", it->second.userId}};
}

json GbServer::banListFull(const json& l, const User& me) const {
    const long long t = Online::unixNow();
    json r = banListSummary(l, me);
    json editors = json::array(), entries = json::array(), appeals = json::array(), log = json::array();
    for (const json& x : l["editors"]) editors.push_back(banListPerson(x.get<std::string>()));
    for (const json& e : l["entries"])
        if (live(e, t)) entries.push_back({{"who", banListPerson(sv(e, "who"))}, {"reason", sv(e, "reason")}, {"by", banListPerson(sv(e, "by"))},
                                           {"at", nv(e, "at")}, {"until", nv(e, "until")}});
    for (auto it = l["appeals"].rbegin(); it != l["appeals"].rend(); ++it) {
        const json& x = *it;
        appeals.push_back({{"id", sv(x, "id")}, {"who", banListPerson(sv(x, "who"))}, {"game", sv(x, "game")}, {"text", sv(x, "text")},
                           {"at", nv(x, "at")}, {"status", sv(x, "status")}, {"note", sv(x, "note")},
                           {"answeredBy", sv(x, "answeredBy").empty() ? json() : banListPerson(sv(x, "answeredBy"))}, {"answeredAt", nv(x, "answeredAt")}});
    }
    const json& lg = l["log"];
    for (size_t i = lg.size(), n = 0; i-- > 0 && n < 100; ++n) {
        const json& x = lg[i];
        log.push_back({{"at", nv(x, "at")}, {"by", banListPerson(sv(x, "by"))}, {"did", sv(x, "did")},
                       {"who", sv(x, "who").empty() ? json() : banListPerson(sv(x, "who"))}, {"note", sv(x, "note")}});
    }
    r["editors"] = editors; r["entries"] = entries; r["appeals"] = appeals; r["log"] = log;
    return r;
}

// A new list for `me` ("" and an error if they have too many).
std::string GbServer::banListMake(const User& me, const std::string& name, std::string& error) {
    size_t owned = 0;
    for (const auto& [id, l] : m_banLists.items()) if (sv(l, "owner") == me.id) ++owned;
    if (owned >= kMostListsEach) { error = "You can have " + std::to_string(kMostListsEach) + " ban lists at most. Delete one first."; return ""; }
    const std::string id = "bl" + Account::randomHex(6);
    m_banLists[id] = fresh(id, name, me.id, Online::unixNow());
    logIt(m_banLists[id], me.id, "create", "", name);
    return id;
}

// Add (or change) a ban on list l. "" = done, otherwise why not.
std::string GbServer::banListAdd(json& l, const User& me, const json& args) {
    User* who = findPerson(args.contains("who") && args["who"].is_number() ? std::to_string(args["who"].get<long long>()) : sv(args, "who"));
    if (!who) return "There's nobody with that name or number.";
    if (who->id == me.id) return "You can't ban yourself.";
    if (who->id == sv(l, "owner") || has(l["editors"], who->id)) return "That person looks after this list, so they can't be on it.";
    if (isStaff(*who)) return "Guts&Bolts staff can't be banned from games.";
    const std::string reason = say(sv(args, "reason"), kReason);
    if (reason.empty()) return "Say why. Every ban needs a reason, so the person (and your team) know what happened.";
    const long long days = args.contains("days") && args["days"].is_number() ? (long long)args["days"].get<double>() : 0;
    if (days < 0 || days > kMostDays) return "A ban lasts 1 to " + std::to_string(kMostDays) + " days, or 0 for good.";
    const long long t = Online::unixNow();
    json& entries = l["entries"];
    bool changed = false;
    for (size_t i = 0; i < entries.size(); ++i)
        if (sv(entries[i], "who") == who->id) { entries.erase(entries.begin() + (long)i); changed = true; break; }
    if (!changed) {
        json keep = json::array();
        for (const json& e : entries) if (live(e, t)) keep.push_back(e);
        entries = keep;
        if (entries.size() >= kMostEntries) return "This list is full (" + std::to_string(kMostEntries) + " people). Start another one.";
    }
    entries.push_back({{"who", who->id}, {"reason", reason}, {"by", me.id}, {"at", t}, {"until", days ? t + days * 86400 : 0}});
    logIt(l, me.id, changed ? "change" : "add", who->id, reason + (days ? " (" + std::to_string(days) + " days)" : std::string(" (for good)")));
    return "";
}

json GbServer::banListOp(const std::string& name, User& me, const json& args) {
    if (me.userId == 0) return fail("Sign up to use ban lists.");
    const long long t = Online::unixNow();
    auto listOut = [&](const json& l) { json r = okay(); r["list"] = banListFull(l, me); return r; };

    if (name == "banlist.check" || name == "banlist.appeal") {
        auto it = findAsset(sv(args, "game"));
        if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
        const json* e = nullptr;
        const json* lc = banFor(it->second, me, &e);
        if (name == "banlist.check") {
            json r = okay();
            r["banned"] = lc != nullptr;
            if (!lc) return r;
            const json* mine = nullptr;
            for (const json& x : (*lc)["appeals"]) if (sv(x, "who") == me.id && nv(x, "banAt") == nv(*e, "at")) mine = &x;
            r["ban"] = {{"reason", sv(*e, "reason")}, {"at", nv(*e, "at")}, {"until", nv(*e, "until")}, {"list", sv(*lc, "name")},
                        {"owner", banListPerson(sv(*lc, "owner"))},
                        {"appeal", mine ? json{{"status", sv(*mine, "status")}, {"note", sv(*mine, "note")}, {"at", nv(*mine, "at")}} : json()},
                        {"canAppeal", !mine || sv(*mine, "status") == "denied"}};
            return r;
        }
        if (!lc) return fail("You aren't banned from this game.");
        const std::string text = say(sv(args, "text"), kAppealText, true);
        if (text.size() < kAppealMin) return fail("Write a little more: say why you should be let back in.");
        json& l = m_banLists[sv(*lc, "id")];
        const long long banAt = nv(*e, "at");
        for (const json& x : l["appeals"])
            if (sv(x, "who") == me.id && nv(x, "banAt") == banAt && sv(x, "status") == "open") return fail("You already appealed this ban. Wait for an answer.");
        l["appeals"].push_back({{"id", "ap" + Account::randomHex(5)}, {"who", me.id}, {"game", it->first}, {"text", text}, {"at", t},
                                {"status", "open"}, {"banAt", banAt}});
        logIt(l, me.id, "appeal", "", it->second.name);
        saveBanLists();
        std::vector<std::string> team = {sv(l, "owner")};
        for (const json& x : l["editors"]) team.push_back(x.get<std::string>());
        for (const std::string& id : team)
            notify(findUser(id), "banlist", me.name + " appealed their ban from " + it->second.name + " (list \"" + sv(l, "name") + "\").", sv(l, "id"));
        json r = okay(); r["ban"] = {{"status", "open"}, {"at", t}}; return r;
    }

    if (name == "banlist.mine") {
        json lists = json::array();
        for (const auto& [id, l] : m_banLists.items()) if (sv(l, "owner") == me.id || has(l["editors"], me.id)) lists.push_back(banListSummary(l, me));
        std::stable_sort(lists.begin(), lists.end(), [](const json& x, const json& y) {
            const bool xo = x["role"] == "owner", yo = y["role"] == "owner";
            return xo != yo ? xo : x["created"].get<long long>() > y["created"].get<long long>();
        });
        json r = okay(); r["lists"] = lists; return r;
    }
    if (name == "banlist.create") {
        const std::string listName = say(sv(args, "name"), kListName);
        if (listName.empty()) return fail("Give the list a name.");
        std::string error;
        const std::string id = banListMake(me, listName, error);
        if (id.empty()) return fail(error);
        saveBanLists();
        return listOut(m_banLists[id]);
    }

    if (name == "game.banLists" || name == "game.ban") {
        auto it = findAsset(sv(args, "game"));
        if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
        Asset& a = it->second;
        if (a.creator != me.id && !isStaff(me)) return fail("Only the game's creator can change who's banned from it.");
        auto listsOf = [&]() {
            json out = json::array();
            for (const std::string& id : a.banLists) if (m_banLists.contains(id)) out.push_back(banListSummary(m_banLists[id], me));
            json r = okay(); r["lists"] = out; return r;
        };
        if (name == "game.banLists") {
            if (!args.contains("lists") || !args["lists"].is_array()) return listsOf();
            std::vector<std::string> want;
            for (const json& x : args["lists"])
                if (x.is_string() && std::find(want.begin(), want.end(), x.get<std::string>()) == want.end()) want.push_back(x.get<std::string>());
            if (want.size() > kListsPerGame) return fail("A game can use " + std::to_string(kListsPerGame) + " ban lists at most.");
            for (const std::string& id : want) {
                if (!m_banLists.contains(id)) return fail("You can only use ban lists you own or were added to.");
                const json& l = m_banLists[id];
                const std::string role = banListRole(l, me);
                if (role != "owner" && role != "editor" && !(sv(l, "owner") == a.creator || has(l["editors"], a.creator)))
                    return fail("You can only use ban lists you own or were added to.");
            }
            const std::vector<std::string> before = a.banLists;
            a.banLists = want;
            for (const std::string& id : want)
                if (std::find(before.begin(), before.end(), id) == before.end()) logIt(m_banLists[id], me.id, "use", "", a.name);
            for (const std::string& id : before)
                if (std::find(want.begin(), want.end(), id) == want.end() && m_banLists.contains(id)) logIt(m_banLists[id], me.id, "unuse", "", a.name);
            saveAssets();
            saveBanLists();
            return listsOf();
        }
        // game.ban: onto the game's first list you can edit; a new "<game> bans" list if there's none.
        std::string id;
        for (const std::string& x : a.banLists)
            if (m_banLists.contains(x)) {
                const std::string role = banListRole(m_banLists[x], me);
                if (!role.empty()) { id = x; break; }
            }
        if (id.empty()) {
            if (a.banLists.size() >= kListsPerGame) return fail("This game already uses " + std::to_string(kListsPerGame) + " ban lists you can't add to.");
            std::string error;
            id = banListMake(me, say((a.name.empty() ? std::string("Game") : a.name) + " bans", kListName), error);
            if (id.empty()) return fail(error);
            a.banLists.push_back(id);
            logIt(m_banLists[id], me.id, "use", "", a.name);
            saveAssets();
        }
        if (std::string bad = banListAdd(m_banLists[id], me, args); !bad.empty()) { saveBanLists(); return fail(bad); }
        saveBanLists();
        return listOut(m_banLists[id]);
    }

    const std::string id = sv(args, "id");
    if (!m_banLists.contains(id)) return fail("There's no ban list like that (or it isn't yours).");
    json& l = m_banLists[id];
    const std::string role = banListRole(l, me);
    if (role.empty()) return fail("There's no ban list like that (or it isn't yours).");
    if (name == "banlist.get") return listOut(l);
    if ((name == "banlist.rename" || name == "banlist.delete" || name == "banlist.share") && role != "owner" && role != "staff")
        return fail("Only the list's owner can do that.");

    if (name == "banlist.rename") {
        const std::string listName = say(sv(args, "name"), kListName);
        if (listName.empty()) return fail("Give the list a name.");
        logIt(l, me.id, "rename", "", listName);
        l["name"] = listName;
    } else if (name == "banlist.delete") {
        for (auto& [aid, a] : m_assets) a.banLists.erase(std::remove(a.banLists.begin(), a.banLists.end(), id), a.banLists.end());
        m_banLists.erase(id);
        saveAssets();
        saveBanLists();
        return okay();
    } else if (name == "banlist.share") {
        User* who = findPerson(args.contains("who") && args["who"].is_number() ? std::to_string(args["who"].get<long long>()) : sv(args, "who"));
        if (!who || who->userId == 0) return fail("There's nobody with that name or number.");
        json& editors = l["editors"];
        if (args.value("remove", false) == true) {
            if (!has(editors, who->id)) return fail("They aren't an editor of this list.");
            for (size_t i = 0; i < editors.size(); ++i) if (editors[i] == who->id) { editors.erase(editors.begin() + (long)i); break; }
            logIt(l, me.id, "unshare", who->id);
        } else {
            if (who->id == sv(l, "owner")) return fail("That's the list's owner.");
            if (has(editors, who->id)) return fail("They're already an editor.");
            if (editors.size() >= kMostEditors) return fail("A list can have " + std::to_string(kMostEditors) + " editors at most.");
            json keep = json::array();
            for (const json& e : l["entries"]) if (sv(e, "who") != who->id) keep.push_back(e);   // a team member can't be on the list
            l["entries"] = keep;
            editors.push_back(who->id);
            logIt(l, me.id, "share", who->id);
            notify(who, "banlist", me.name + " added you as an editor of the ban list \"" + sv(l, "name") + "\".", id);
        }
    } else if (name == "banlist.add") {
        if (std::string bad = banListAdd(l, me, args); !bad.empty()) return fail(bad);
    } else if (name == "banlist.remove") {
        User* who = findPerson(args.contains("who") && args["who"].is_number() ? std::to_string(args["who"].get<long long>()) : sv(args, "who"));
        json& entries = l["entries"];
        bool found = false;
        if (who) for (size_t i = 0; i < entries.size(); ++i)
            if (sv(entries[i], "who") == who->id) { entries.erase(entries.begin() + (long)i); found = true; break; }
        if (!found) return fail("They aren't on this list.");
        logIt(l, me.id, "remove", who->id, say(sv(args, "note"), kAnswerNote));
    } else if (name == "banlist.answer") {
        json* ap = nullptr;
        for (json& x : l["appeals"]) if (sv(x, "id") == sv(args, "appeal")) ap = &x;
        if (!ap) return fail("That appeal isn't there.");
        if (sv(*ap, "status") != "open") return fail("Someone already answered that appeal.");
        const bool accept = args.value("accept", false) == true;
        const std::string note = say(sv(args, "note"), kAnswerNote), who = sv(*ap, "who"), game = sv(*ap, "game");
        (*ap)["status"] = accept ? "accepted" : "denied";
        (*ap)["note"] = note; (*ap)["answeredBy"] = me.id; (*ap)["answeredAt"] = t;
        if (accept) {
            json keep = json::array();
            for (const json& e : l["entries"]) if (sv(e, "who") != who) keep.push_back(e);
            l["entries"] = keep;
        }
        logIt(l, me.id, accept ? "accept" : "deny", who, note);
        auto g = findAsset(game);
        const std::string gname = g != m_assets.end() ? g->second.name : std::string("that game");
        notify(findUser(who), "banappeal", (accept ? "Your ban appeal was accepted: you can play " : "Your ban appeal was turned down for ") + gname +
               (note.empty() ? std::string(".") : ". \"" + note + "\""), game);
    } else {
        return fail("Unknown request.");
    }
    saveBanLists();
    return listOut(l);
}
