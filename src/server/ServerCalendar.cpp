// The events calendar: game jams, art contests, live shows, meetups and launches. worker/calendar.js
// has the same rules (and the notes on why):
//   - anyone signed up can add an event; staff ones are "official"; a group's needs "manage" in it
//   - it starts between an hour ago and a year from now, and lasts 15 minutes to 31 days
//   - title and text go through the word filter; a link can only go somewhere on the site ("#/...")
//   - "Going": everyone going is told when the time changes or it's called off
//   - only the maker (or staff) changes or deletes it; events over for a year are tidied away
//   - kPerDay new events a day (Verified: kPerDayVerified), at most kMostUpcoming coming up each
//   calendar.list {from, to, kind, when}  calendar.get {id}  calendar.make {title, text, kind, start, end, link, group}
//   calendar.edit {id, ...}  calendar.delete {id}  calendar.going {id, on}
// Kept in calendar.json ({events: id -> event, made: account -> {day, n}}).
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <regex>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
const std::pair<const char*, const char*> kKinds[] = {{"jam", "Game jam"}, {"art", "Art contest"}, {"live", "Live show"},
                                                      {"meetup", "Meetup"}, {"launch", "Launch"}, {"other", "Other"}};
constexpr size_t kTitle = 80, kText = 3000, kMostInList = 300, kMostUpcoming = 20, kMostGoing = 5000;
constexpr long long kShortest = 15 * 60, kLongest = 31 * 86400, kAhead = 366 * 86400, kLate = 3600, kKeepOver = 365 * 86400;
constexpr long long kPerDay = 5, kPerDayVerified = 20;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
long long iv(const json& j, const char* k) { return j.contains(k) && j[k].is_number() ? (long long)j[k].get<double>() : 0; }
const char* kindName(const std::string& k) { for (const auto& [id, n] : kKinds) if (k == id) return n; return nullptr; }

// A link somewhere on the site as "#/..." ("" for none), or false when it goes somewhere else (calendarLink).
bool calendarLink(std::string l, std::string& out) {
    out.clear();
    while (!l.empty() && l.front() == ' ') l.erase(l.begin());
    while (!l.empty() && l.back() == ' ') l.pop_back();
    if (l.empty()) return true;
    l = std::regex_replace(l, std::regex("^https://(www\\.)?gutsandbolts\\.net", std::regex::icase), "");
    l = std::regex_replace(l, std::regex("^/app/?"), "");
    if (!std::regex_match(l, std::regex("^#/[A-Za-z0-9\\-_/?=&.%]{1,200}$")) || l.find("//") != std::string::npos) return false;
    out = l;
    return true;
}
std::string timesProblem(long long start, long long end, long long t) {
    if (!start || !end) return "Pick when it starts and ends.";
    if (start < t - kLate) return "That start time has already gone by.";
    if (start > t + kAhead) return "Events can be at most a year away.";
    if (end - start < kShortest) return "An event lasts at least 15 minutes.";
    if (end - start > kLongest) return "An event can last at most 31 days.";
    return "";
}
bool going(const json& e, const std::string& id) {
    if (!e.contains("going") || !e["going"].is_array()) return false;
    for (const json& x : e["going"]) if (x == id) return true;
    return false;
}
} // namespace

void GbServer::saveCalendar() { writeFile(m_opts.data / "calendar.json", m_calendar.dump(1)); }
void GbServer::loadCalendar() {
    std::string text;
    if (readFile(m_opts.data / "calendar.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object() && j.contains("events") && j["events"].is_object()) m_calendar = j;
        if (!m_calendar.contains("made") || !m_calendar["made"].is_object()) m_calendar["made"] = json::object();
    }
}

json GbServer::calendarJson(const json& e, const User* me, long long t, bool full) const {
    const std::string by = sv(e, "by");
    auto u = m_users.find(by);
    json who = u == m_users.end() ? json{{"id", by}, {"name", "?"}, {"userId", 0}, {"verified", false}}
                                  : json{{"id", by}, {"name", u->second.name}, {"userId", u->second.userId}, {"verified", isVerified(u->second)}};
    auto g = m_groups.find(sv(e, "group"));
    const long long start = iv(e, "start"), end = iv(e, "end");
    const char* kn = kindName(sv(e, "kind"));
    json out = {{"id", sv(e, "id")}, {"title", sv(e, "title")}, {"kind", sv(e, "kind")}, {"kindName", kn ? kn : "Other"},
                {"start", start}, {"end", end}, {"link", sv(e, "link")}, {"official", e.value("official", false)},
                {"going", e.contains("going") && e["going"].is_array() ? e["going"].size() : 0},
                {"amGoing", me && me->userId != 0 && going(e, me->id)},
                {"state", t < start ? "soon" : t < end ? "now" : "over"}, {"at", iv(e, "at")}, {"by", who},
                {"group", g == m_groups.end() || sv(e, "group").empty() ? json(nullptr) : json{{"id", g->second.id}, {"name", g->second.name}}}};
    if (full) {
        out["text"] = sv(e, "text");
        out["edited"] = iv(e, "edited");
        out["mine"] = me && by == me->id;
        out["canEdit"] = me && me->userId != 0 && (by == me->id || isStaff(*me));
    }
    return out;
}

std::string GbServer::calendarReportTarget(const std::string& id, const User& me, std::string& target, std::string& about, json& copy) {
    json& all = m_calendar["events"];
    if (id.empty() || !all.contains(id)) return "That event isn't on the calendar any more.";
    const json& e = all[id];
    if (sv(e, "by") == me.id) return "You can't report yourself.";
    target = id; about = sv(e, "by");
    copy = {{"subject", "Event: " + sv(e, "title")}, {"body", sv(e, "text").substr(0, 2000)}, {"at", iv(e, "at")}};
    return "";
}
std::string GbServer::calendarReportName(const std::string& target) const {
    const json& all = m_calendar["events"];
    return all.contains(target) ? sv(all[target], "title") : "(deleted)";
}

json GbServer::calendarOp(const std::string& name, User* me, const json& args) {
    const long long t = Online::unixNow();
    json& all = m_calendar["events"];
    auto hidden = [&](const json& e) {
        if (!me || me->userId == 0) return false;
        const User* u = findUser(sv(e, "by"));
        return u && blocks(*me, *u);
    };
    if (name == "calendar.list") {
        bool dropped = false;
        for (auto it = all.begin(); it != all.end();) {
            if (iv(*it, "end") + kKeepOver < t) { it = all.erase(it); dropped = true; } else ++it;
        }
        if (dropped) saveCalendar();
        const std::string kind = kindName(sv(args, "kind")) ? sv(args, "kind") : "";
        const long long from = iv(args, "from"), to = iv(args, "to");
        const bool range = from || to, past = sv(args, "when") == "past";
        if (range && (!(to > from) || to - from > 62 * 86400)) return fail("Ask for at most two months at a time.");
        std::vector<const json*> list;
        for (const auto& [id, e] : all.items()) {
            if ((!kind.empty() && sv(e, "kind") != kind) || hidden(e)) continue;
            const long long s = iv(e, "start"), en = iv(e, "end");
            if (range ? (s < to && en > from) : past ? en <= t : en > t) list.push_back(&e);
        }
        std::sort(list.begin(), list.end(), [&](const json* x, const json* y) {
            if (past) return iv(*x, "end") > iv(*y, "end");
            if (iv(*x, "start") != iv(*y, "start")) return iv(*x, "start") < iv(*y, "start");
            return x->value("official", false) && !y->value("official", false);
        });
        json out = json::array();
        for (const json* e : list) { if (out.size() >= kMostInList) break; out.push_back(calendarJson(*e, me, t, false)); }
        json kinds = json::object();
        for (const auto& [id, n] : kKinds) kinds[id] = n;
        json r = okay(); r["events"] = out; r["kinds"] = kinds; return r;
    }
    const std::string id = sv(args, "id");
    if (name == "calendar.get") {
        if (id.empty() || !all.contains(id) || hidden(all[id])) return fail("That event isn't on the calendar (any more).");
        json r = okay(); r["event"] = calendarJson(all[id], me, t, true); return r;
    }
    if (!me || me->userId == 0) return fail("Sign up to use the calendar.");
    const bool staff = isStaff(*me);
    // The fields of calendar.make / calendar.edit: "" and f filled in, or why not.
    auto fields = [&](const json* old, json& f) -> std::string {
        const std::string title = say(sv(args, "title"), kTitle);
        if (title.empty()) return "Give your event a name.";
        const std::string text = say(sv(args, "text"), kText, true);
        const std::string kind = kindName(sv(args, "kind")) ? sv(args, "kind") : "other";
        const long long start = iv(args, "start"), end = iv(args, "end");
        if (!(old && start == iv(*old, "start") && end == iv(*old, "end")))
            if (std::string bad = timesProblem(start, end, t); !bad.empty()) return bad;
        std::string link;
        if (!calendarLink(sv(args, "link"), link)) return "Links can only go somewhere on Guts&Bolts (like #/game/12 or #/live).";
        std::string group = old ? sv(*old, "group") : "";
        if (args.contains("group")) {
            group = sv(args, "group");
            if (!group.empty()) {
                auto g = m_groups.find(group);
                if (g == m_groups.end()) return "That group doesn't exist (any more).";
                if (!staff && !groupCan(g->second, *me, "manage")) return "You need \"manage\" in " + g->second.name + " to put events on its behalf.";
            }
        }
        f = {{"title", title}, {"text", text}, {"kind", kind}, {"start", start}, {"end", end}, {"link", link}, {"group", group}};
        return "";
    };
    if (name == "calendar.make") {
        json f;
        if (std::string bad = fields(nullptr, f); !bad.empty()) return fail(bad);
        json& made = m_calendar["made"][me->id];
        const long long today = t / 86400;
        if (!made.is_object() || made.value("day", 0LL) != today) made = {{"day", today}, {"n", 0}};
        const long long most = isVerified(*me) || staff ? kPerDayVerified : kPerDay;
        if (!staff && made.value("n", 0LL) >= most) return fail("You've put " + std::to_string(most) + " events on the calendar today. Come back tomorrow!");
        size_t coming = 0;
        for (const auto& [k, e] : all.items()) if (sv(e, "by") == me->id && iv(e, "end") > t) ++coming;
        if (!staff && coming >= kMostUpcoming) return fail("You already have " + std::to_string(kMostUpcoming) + " events coming up.");
        const std::string nid = "ev-" + Account::randomHex(5);
        f["id"] = nid; f["by"] = me->id; f["at"] = t; f["official"] = staff; f["going"] = json::array({me->id});
        all[nid] = f;
        made["n"] = made.value("n", 0LL) + 1;
        saveCalendar();
        json r = okay(); r["event"] = calendarJson(all[nid], me, t, true); return r;
    }
    if (id.empty() || !all.contains(id)) return fail("That event isn't on the calendar (any more).");
    json& e = all[id];
    auto tellGoing = [&](const std::string& text) {
        if (!e.contains("going") || !e["going"].is_array()) return;
        for (const json& x : e["going"])
            if (x.is_string() && x.get<std::string>() != me->id) notify(findUser(x.get<std::string>()), "calendar", text, id);
    };
    if (name == "calendar.going") {
        if (t >= iv(e, "end")) return fail("That event is over.");
        if (hidden(e)) return fail("You can't go to this event.");
        if (!e.contains("going") || !e["going"].is_array()) e["going"] = json::array();
        json& g = e["going"];
        const bool off = args.contains("on") && args["on"] == false;
        auto it = std::find(g.begin(), g.end(), json(me->id));
        if (off) { if (it != g.end()) g.erase(it); }
        else if (it == g.end()) { if (g.size() >= kMostGoing) return fail("This event is full."); g.push_back(me->id); }
        saveCalendar();
        json r = okay(); r["event"] = calendarJson(e, me, t, true); return r;
    }
    if (sv(e, "by") != me->id && !staff) return fail("Only the person who made it, or staff, can change it.");
    if (name == "calendar.edit") {
        json f;
        if (std::string bad = fields(&e, f); !bad.empty()) return fail(bad);
        const bool moved = iv(f, "start") != iv(e, "start") || iv(f, "end") != iv(e, "end");
        for (auto& [k, v] : f.items()) e[k] = v;
        e["edited"] = t;
        saveCalendar();
        if (sv(e, "by") != me->id) staffDid(*me, "calendar", "Changed the event \"" + sv(e, "title") + "\"", sv(e, "by"));
        if (moved) tellGoing("\"" + sv(e, "title").substr(0, 60) + "\" moved to a new time.");
        json r = okay(); r["event"] = calendarJson(e, me, t, true); return r;
    }
    if (name == "calendar.delete") {
        if (sv(e, "by") != me->id) staffDid(*me, "calendar", "Deleted the event \"" + sv(e, "title") + "\"", sv(e, "by"));
        if (t < iv(e, "end")) tellGoing("\"" + sv(e, "title").substr(0, 60) + "\" was called off.");
        all.erase(id);
        saveCalendar();
        return okay();
    }
    return fail("Unknown request.");
}
