// Achievements on profiles, game update logs and timed events (worker/extras.js has the same
// rules, so read that file's notes for what each one is for).
//   gamelog.list   {id}                 -> {entries}           (anyone)
//   gamelog.post   {id, title, text}    -> {entries}           (the game's creator, or staff)
//   gamelog.delete {id, entry}          -> {entries}
//   events.list    {}                   -> {events, now}       (anyone)
//   events.claim   {id}                 -> {events, got}       (signed up; played enough while it was on)
//   admin.eventSave   {id?, name, text, start, end, item, minutes}   (staff)
//   admin.eventRemove {id}
// Events are kept in events.json.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kLogTitle = 80, kLogText = 1000, kLogKept = 30;
constexpr int kLogPerDay = 5, kLogFeedEach = 2;
constexpr long long kLogFeedDays = 30;
constexpr size_t kEventName = 60, kEventText = 600, kMostEvents = 50;
constexpr long long kEventMaxMinutes = 600, kEventMaxDays = 60, kPlayReport = 5;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
long long   nv(const json& j, const char* k) { return j.contains(k) && j[k].is_number() ? j[k].get<long long>() : 0; }

struct Achievement { const char* id; const char* name; const char* text; const char* stat; long long need; };
const Achievement kAchievements[] = {
    {"first-play",   "First Steps",   "Play your first game",               "played",     1},
    {"explorer",     "Explorer",      "Play 10 different games",            "played",     10},
    {"globetrotter", "Globetrotter",  "Play 50 different games",            "played",     50},
    {"game-maker",   "Game Maker",    "Publish your first game",            "gamesMade",  1},
    {"designer",     "Designer",      "Make your first catalog item",       "itemsMade",  1},
    {"crowd",        "Crowd Pleaser", "Get 100 visits on your games",       "visits",     100},
    {"friendly",     "Friendly",      "Have 5 friends",                     "friends",    5},
    {"popular",      "Popular",       "Have 25 friends",                    "friends",    25},
    {"badge-hunter", "Badge Hunter",  "Earn a badge in a game",             "gameBadges", 1},
    {"dressed-up",   "Dressed Up",    "Save an outfit",                     "outfits",    1},
    {"party-goer",   "Party Goer",    "Get a free item from an event",      "events",     1},
    {"one-year",     "One Year Club", "Have your account for a whole year", "days",       365},
};
} // namespace

json GbServer::achievementsOf(const User& u) const {
    json stats = {{"played", u.played.size()}, {"gamesMade", 0}, {"itemsMade", 0}, {"visits", 0}, {"friends", u.friends.size()},
                  {"gameBadges", u.gameBadges.is_array() ? u.gameBadges.size() : 0}, {"outfits", u.outfits.is_array() ? u.outfits.size() : 0},
                  {"events", u.eventClaims.size()}, {"days", std::max(0LL, Online::unixNow() - (u.created ? u.created : Online::unixNow())) / 86400}};
    long long games = 0, items = 0, visits = 0;
    for (const auto& [id, a] : m_assets) {
        if (a.creator != u.id) continue;
        if (a.kind == "game") { ++games; visits += a.plays; }
        else if (a.kind != "model" && a.kind != "devproduct" && a.kind != "pass") ++items;
    }
    stats["gamesMade"] = games; stats["itemsMade"] = items; stats["visits"] = visits;
    json earned = json::array(), rest = json::array();
    for (const Achievement& x : kAchievements) {
        const long long have = stats.value(x.stat, 0LL);
        json one = {{"id", x.id}, {"name", x.name}, {"text", x.text}, {"have", std::min(have, x.need)}, {"need", x.need}, {"earned", have >= x.need}};
        (have >= x.need ? earned : rest).push_back(one);
    }
    for (json& x : rest) earned.push_back(x);
    return earned;
}

// My Feed: what's new in the games you favourited (the last 30 days, 2 notes a game).
json GbServer::gameLogFeed(const User& me) const {
    json out = json::array();
    const long long now = Online::unixNow();
    for (const std::string& gid : me.favorites) {
        auto it = findAsset(gid);
        if (it == m_assets.end() || it->second.kind != "game" || !it->second.updateLog.is_array()) continue;
        const Asset& a = it->second;
        for (size_t i = 0; i < a.updateLog.size() && i < (size_t)kLogFeedEach; ++i) {
            const json& e = a.updateLog[i];
            if (now - nv(e, "at") > kLogFeedDays * 86400) continue;
            const std::string title = sv(e, "title"), text = sv(e, "text");
            out.push_back({{"text", title + (text.empty() ? "" : ": " + text)}, {"title", title}, {"at", nv(e, "at")}, {"kind", "gamelog"},
                           {"game", {{"id", a.id}, {"name", a.name}}}});
        }
    }
    return out;
}

json GbServer::gameLogOp(const std::string& name, User& me, const json& args) {
    auto it = findAsset(sv(args, "id"));
    if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
    Asset& a = it->second;
    if (!a.updateLog.is_array()) a.updateLog = json::array();
    auto view = [&]() {
        json entries = json::array();
        for (const json& e : a.updateLog) entries.push_back({{"id", sv(e, "id")}, {"title", sv(e, "title")}, {"text", sv(e, "text")}, {"at", nv(e, "at")}});
        json r = okay(); r["entries"] = entries; return r;
    };
    if (name == "gamelog.list") return view();
    if (a.creator != me.id && !isStaff(me)) return fail("Only the game's creator can post its updates.");
    if (name == "gamelog.post") {
        const std::string title = say(sv(args, "title"), kLogTitle), text = say(sv(args, "text"), kLogText, true);
        if (title.empty()) return fail("Give the update a short title.");
        const long long now = Online::unixNow();
        const std::string day = Online::utcDay(now);
        if (a.updateLogDay != day) { a.updateLogDay = day; a.updateLogToday = 0; }
        if (a.updateLogToday >= kLogPerDay)
            return fail("That's " + std::to_string(kLogPerDay) + " updates for this game today. Post again tomorrow.");
        ++a.updateLogToday;
        a.updateLog.insert(a.updateLog.begin(), json{{"id", Account::randomHex(6)}, {"title", title}, {"text", text}, {"at", now}});
        while (a.updateLog.size() > kLogKept) a.updateLog.erase(a.updateLog.end() - 1);
    } else if (name == "gamelog.delete") {
        const std::string entry = sv(args, "entry");
        const size_t before = a.updateLog.size();
        for (size_t i = 0; i < a.updateLog.size(); ++i)
            if (sv(a.updateLog[i], "id") == entry) { a.updateLog.erase(a.updateLog.begin() + (long)i); break; }
        if (a.updateLog.size() == before) return fail("That update isn't there any more.");
    } else {
        return fail("Unknown request: " + name);
    }
    saveAssets();
    return view();
}

json GbServer::eventsView(const User& me) const {
    const long long now = Online::unixNow();
    std::vector<const json*> shown;
    for (const json& e : m_events) if (nv(e, "end") > now - 7 * 86400) shown.push_back(&e);
    std::stable_sort(shown.begin(), shown.end(), [](const json* a, const json* b) { return nv(*a, "start") < nv(*b, "start"); });
    json list = json::array();
    for (const json* p : shown) {
        const json& e = *p;
        const std::string id = sv(e, "id");
        json item = nullptr;
        if (auto it = findAsset(sv(e, "item")); it != m_assets.end() && Online::isCatalogItem(it->second.kind))
            item = {{"id", it->second.id}, {"name", it->second.name}, {"kind", it->second.kind}};
        long long played = 0;
        bool claimed = false;
        if (me.userId > 0) {
            played = std::min(me.eventPlay.is_object() ? me.eventPlay.value(id, 0LL) : 0LL, nv(e, "minutes"));
            claimed = std::find(me.eventClaims.begin(), me.eventClaims.end(), id) != me.eventClaims.end();
        }
        list.push_back({{"id", id}, {"name", sv(e, "name")}, {"text", sv(e, "text")}, {"start", nv(e, "start")}, {"end", nv(e, "end")},
                        {"minutes", nv(e, "minutes")}, {"item", item},
                        {"state", now < nv(e, "start") ? "soon" : now < nv(e, "end") ? "live" : "over"},
                        {"played", played}, {"claimed", claimed}});
    }
    json r = okay(); r["events"] = list; r["now"] = now; return r;
}

// The Player said "still playing" (bolts.play): every live event gets 5 more minutes.
void GbServer::eventsPlayed(User& me) {
    if (me.userId == 0) return;
    const long long now = Online::unixNow();
    if (!me.eventPlay.is_object()) me.eventPlay = json::object();
    for (const json& e : m_events)
        if (nv(e, "start") <= now && now < nv(e, "end")) {
            const std::string id = sv(e, "id");
            me.eventPlay[id] = std::min(me.eventPlay.value(id, 0LL) + kPlayReport, kEventMaxMinutes);
        }
    for (auto it = me.eventPlay.begin(); it != me.eventPlay.end();) {   // forget events that are gone
        const std::string key = it.key();
        const bool there = std::any_of(m_events.begin(), m_events.end(), [&](const json& e) { return sv(e, "id") == key; });
        it = there ? std::next(it) : me.eventPlay.erase(it);
    }
}

json GbServer::eventsOp(const std::string& name, User& me, const json& args) {
    if (name == "events.list") return eventsView(me);
    if (name != "events.claim") return fail("Unknown request: " + name);
    if (me.userId == 0) return fail("Sign up to get event items.");
    const long long now = Online::unixNow();
    const std::string id = sv(args, "id");
    const json* e = nullptr;
    for (const json& x : m_events) if (sv(x, "id") == id) e = &x;
    if (!e) return fail("That event isn't there any more.");
    if (now < nv(*e, "start")) return fail("That event hasn't started yet.");
    if (now >= nv(*e, "end") + 86400) return fail("That event is over.");
    if (std::find(me.eventClaims.begin(), me.eventClaims.end(), id) != me.eventClaims.end()) return fail("You already got this event's item.");
    const long long have = me.eventPlay.is_object() ? me.eventPlay.value(id, 0LL) : 0LL;
    if (have < nv(*e, "minutes")) return fail("Play " + std::to_string(nv(*e, "minutes") - have) + " more minutes during the event to earn it.");
    me.eventClaims.push_back(id);
    if (me.eventClaims.size() > 200) me.eventClaims.erase(me.eventClaims.begin());
    json got = nullptr;
    if (auto it = findAsset(sv(*e, "item")); it != m_assets.end() && Online::isCatalogItem(it->second.kind) && !it->second.limited &&
                                              !me.owned.count(it->second.id)) {
        Asset& a = it->second;
        me.owned.insert(a.id);
        giveBundle(me, a);
        ++a.sales;
        saveAssets();
        got = {{"id", a.id}, {"name", a.name}};
        notify(&me, "gift", "You got " + a.name + " from the event \"" + sv(*e, "name") + "\"!", a.id);
    }
    saveUsers();
    json r = eventsView(me);
    r["got"] = got;
    return r;
}

json GbServer::eventsAdmin(const std::string& name, const User& me, const json& args) {
    const long long now = Online::unixNow();
    const std::string id = sv(args, "id");
    if (name == "admin.eventSave") {
        const std::string title = Online::cleanText(sv(args, "name"), kEventName), text = Online::cleanText(sv(args, "text"), kEventText, true);
        const std::string item = sv(args, "item");
        const long long start = nv(args, "start"), end = nv(args, "end");
        const long long minutes = args.contains("minutes") && args["minutes"].is_number() ? (long long)std::llround(args["minutes"].get<double>()) : 0;
        if (!item.empty()) {
            auto it = findAsset(item);
            if (it == m_assets.end() || !Online::isCatalogItem(it->second.kind)) return fail("The event's item must be a catalog item (or nothing).");
        }
        if (title.empty()) return fail("Give the event a name.");
        if (start <= 0 || end <= start) return fail("The event has to end after it starts.");
        if (end - start > kEventMaxDays * 86400) return fail("An event can last " + std::to_string(kEventMaxDays) + " days at most.");
        if (minutes < 0 || minutes > kEventMaxMinutes) return fail("Minutes of playing must be between 0 and " + std::to_string(kEventMaxMinutes) + ".");
        json* old = nullptr;
        for (json& x : m_events) if (!id.empty() && sv(x, "id") == id) old = &x;
        if (!id.empty() && !old) return fail("That event isn't there any more.");
        if (!old && m_events.size() >= kMostEvents) {
            long long oldestEnd = 0; long idx = -1;
            for (size_t i = 0; i < m_events.size(); ++i)
                if (nv(m_events[i], "end") < now && (idx < 0 || nv(m_events[i], "end") < oldestEnd)) { idx = (long)i; oldestEnd = nv(m_events[i], "end"); }
            if (idx < 0) return fail("There are " + std::to_string(kMostEvents) + " events already. Remove one first.");
            m_events.erase(m_events.begin() + idx);
        }
        json fields = {{"name", title}, {"text", text}, {"start", start}, {"end", end}, {"item", item}, {"minutes", minutes}};
        if (old) old->update(fields);
        else { fields["id"] = "e" + Account::randomHex(6); fields["at"] = now; m_events.push_back(fields); }
        staffDid(me, "event", std::string(old ? "Changed" : "Added") + " the event \"" + title + "\"");
    } else {
        bool found = false;
        for (size_t i = 0; i < m_events.size(); ++i)
            if (sv(m_events[i], "id") == id) {
                staffDid(me, "event", "Removed the event \"" + sv(m_events[i], "name") + "\"");
                m_events.erase(m_events.begin() + (long)i);
                found = true;
                break;
            }
        if (!found) return fail("That event isn't there any more.");
    }
    saveEvents();
    return eventsView(me);
}

void GbServer::saveEvents() { writeFile(m_opts.data / "events.json", m_events.dump(1)); }

void GbServer::loadEvents() {
    std::string text;
    if (readFile(m_opts.data / "events.json", text)) {
        json all = json::parse(text, nullptr, false);
        if (all.is_array()) m_events = all;
    }
}
