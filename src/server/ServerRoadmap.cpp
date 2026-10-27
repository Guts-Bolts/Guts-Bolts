// The public roadmap (worker/roadmap.js has the same rules): what staff plan to add, what
// they're working on and what's done, plus ideas from developers that everyone can vote on.
// Staff answer an idea by putting it on the roadmap or saying no (with a note). roadmap.json.
//   roadmap.get         {}                                   -> {items, ideas, answered, openIdeas}   (anyone)
//   roadmap.suggest     {title, text}                        (signed up; 3 open ideas each)
//   roadmap.vote        {id, on}                             (signed up; open ideas only)
//   admin.roadmapSave   {id?, title, text, status, version}  (staff; no id = a new item)
//   admin.roadmapRemove {id}
//   admin.ideaAnswer    {id, answer: planned|declined|open, note}   (planned also adds it to the roadmap)
//   admin.ideaRemove    {id}
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kRoadTitle = 80, kRoadText = 600, kRoadVersion = 20, kRoadNote = 300;
constexpr size_t kMostRoadItems = 200, kMostIdeas = 1000, kOpenIdeasEach = 3;
constexpr size_t kShownOpenIdeas = 100, kShownAnswered = 30;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
long long   nv(const json& j, const char* k) { return j.contains(k) && j[k].is_number() ? j[k].get<long long>() : 0; }
size_t      votesOf(const json& x) { return x.contains("votes") && x["votes"].is_array() ? x["votes"].size() : 0; }
bool        votedBy(const json& x, const std::string& id) {
    if (!x.contains("votes") || !x["votes"].is_array()) return false;
    for (const json& v : x["votes"]) if (v.is_string() && v.get<std::string>() == id) return true;
    return false;
}
json* findIn(json& list, const std::string& id) {
    for (json& x : list) if (sv(x, "id") == id) return &x;
    return nullptr;
}
std::string roadFull() { return "The roadmap is full (" + std::to_string(kMostRoadItems) + " items). Remove some old ones first."; }
} // namespace

json GbServer::roadmapView(const User& me) const {
    const std::string meId = me.userId != 0 ? me.id : std::string();
    const json& items = m_roadmap["items"];
    const json& ideas = m_roadmap["ideas"];
    auto idea = [&](const json& x) {
        const std::string by = sv(x, "by");
        auto u = m_users.find(by);
        json who = nullptr;
        if (u != m_users.end()) who = {{"id", u->second.id}, {"userId", u->second.userId}, {"name", u->second.name}};
        return json{{"id", sv(x, "id")}, {"title", sv(x, "title")}, {"text", sv(x, "text")}, {"by", who}, {"at", nv(x, "at")},
                    {"votes", votesOf(x)}, {"voted", !meId.empty() && votedBy(x, meId)}, {"status", sv(x, "status")},
                    {"note", sv(x, "note")}, {"mine", !meId.empty() && by == meId}};
    };
    std::vector<const json*> open, answered;
    for (const json& x : ideas) (sv(x, "status") == "open" ? open : answered).push_back(&x);
    const size_t openCount = open.size();
    std::stable_sort(open.begin(), open.end(), [](const json* a, const json* b) {
        if (votesOf(*a) != votesOf(*b)) return votesOf(*a) > votesOf(*b);
        return nv(*a, "at") > nv(*b, "at");
    });
    std::stable_sort(answered.begin(), answered.end(), [](const json* a, const json* b) { return nv(*a, "answeredAt") > nv(*b, "answeredAt"); });
    json r = okay(), outItems = json::array(), outOpen = json::array(), outAnswered = json::array();
    for (const json& x : items)
        outItems.push_back({{"id", sv(x, "id")}, {"title", sv(x, "title")}, {"text", sv(x, "text")}, {"status", sv(x, "status")},
                            {"version", sv(x, "version")}, {"at", nv(x, "at")}});
    for (size_t i = 0; i < open.size() && i < kShownOpenIdeas; ++i) outOpen.push_back(idea(*open[i]));
    for (size_t i = 0; i < answered.size() && i < kShownAnswered; ++i) outAnswered.push_back(idea(*answered[i]));
    r["items"] = outItems; r["ideas"] = outOpen; r["answered"] = outAnswered; r["openIdeas"] = openCount;
    return r;
}

json GbServer::roadmapOp(const std::string& name, User& me, const json& args) {
    if (name == "roadmap.get") return roadmapView(me);
    if (me.userId == 0) return fail("Sign up to suggest ideas and vote.");
    json& ideas = m_roadmap["ideas"];
    if (name == "roadmap.suggest") {
        const std::string title = say(sv(args, "title"), kRoadTitle), text = say(sv(args, "text"), kRoadText, true);
        if (title.empty()) return fail("Give your idea a short title.");
        size_t mine = 0;
        for (const json& x : ideas) if (sv(x, "by") == me.id && sv(x, "status") == "open") ++mine;
        if (mine >= kOpenIdeasEach)
            return fail("You already have " + std::to_string(kOpenIdeasEach) + " ideas waiting. Wait until staff answer one before adding another.");
        for (const json& x : ideas)
            if (sv(x, "status") == "open" && lower(sv(x, "title")) == lower(title)) return fail("Someone already suggested that. Vote for it instead!");
        if (ideas.size() >= kMostIdeas) {
            // Full: the oldest answered idea goes, or else the open one with the fewest votes.
            size_t drop = ideas.size();
            for (size_t i = 0; i < ideas.size() && drop == ideas.size(); ++i) if (sv(ideas[i], "status") != "open") drop = i;
            if (drop == ideas.size()) {
                drop = 0;
                for (size_t i = 1; i < ideas.size(); ++i) if (votesOf(ideas[i]) < votesOf(ideas[drop])) drop = i;
            }
            ideas.erase(ideas.begin() + (long)drop);
        }
        ideas.push_back({{"id", "i" + Account::randomHex(6)}, {"title", title}, {"text", text}, {"by", me.id}, {"at", Online::unixNow()},
                         {"votes", json::array({me.id})}, {"status", "open"}, {"note", ""}});
    } else if (name == "roadmap.vote") {
        json* x = findIn(ideas, sv(args, "id"));
        if (!x) return fail("That idea isn't there any more.");
        if (sv(*x, "status") != "open") return fail("Staff already answered that idea, so it can't get votes now.");
        const bool on = !(args.contains("on") && args["on"].is_boolean() && !args["on"].get<bool>());
        json& votes = (*x)["votes"];
        if (!votes.is_array()) votes = json::array();
        auto had = std::find(votes.begin(), votes.end(), json(me.id));
        if (on && had == votes.end()) votes.push_back(me.id);
        if (!on && had != votes.end()) votes.erase(had);
    } else {
        return fail("Unknown request: " + name);
    }
    saveRoadmap();
    return roadmapView(me);
}

json GbServer::roadmapAdmin(const std::string& name, const User& me, const json& args) {
    const long long now = Online::unixNow();
    json& items = m_roadmap["items"];
    json& ideas = m_roadmap["ideas"];
    const std::string id = sv(args, "id");
    if (name == "admin.roadmapSave") {
        const std::string title = Online::cleanText(sv(args, "title"), kRoadTitle), text = Online::cleanText(sv(args, "text"), kRoadText, true);
        const std::string status = sv(args, "status"), version = Online::cleanText(sv(args, "version"), kRoadVersion);
        if (title.empty()) return fail("Give it a title.");
        if (status != "planned" && status != "working" && status != "done") return fail("Pick Planned, Working on it or Done.");
        if (!id.empty()) {
            json* x = findIn(items, id);
            if (!x) return fail("That roadmap item isn't there any more.");
            (*x)["title"] = title; (*x)["text"] = text; (*x)["status"] = status; (*x)["version"] = version;
        } else {
            if (items.size() >= kMostRoadItems) return fail(roadFull());
            items.push_back({{"id", "r" + Account::randomHex(6)}, {"title", title}, {"text", text}, {"status", status}, {"version", version}, {"at", now}});
        }
        staffDid(me, "roadmap", std::string(id.empty() ? "Added" : "Changed") + " the roadmap item \"" + title + "\"");
    } else if (name == "admin.roadmapRemove") {
        json* x = findIn(items, id);
        if (!x) return fail("That roadmap item isn't there any more.");
        staffDid(me, "roadmap", "Removed the roadmap item \"" + sv(*x, "title") + "\"");
        items.erase(items.begin() + (x - &items[0]));
    } else if (name == "admin.ideaAnswer") {
        json* x = findIn(ideas, id);
        if (!x) return fail("That idea isn't there any more.");
        const std::string answer = sv(args, "answer");
        if (answer != "planned" && answer != "declined" && answer != "open") return fail("Pick an answer.");
        const std::string title = sv(*x, "title"), by = sv(*x, "by");
        if (answer == "planned" && sv(*x, "status") != "planned") {
            if (items.size() >= kMostRoadItems) return fail(roadFull());
            items.push_back({{"id", "r" + Account::randomHex(6)}, {"title", title}, {"text", sv(*x, "text")}, {"status", "planned"},
                             {"version", ""}, {"at", now}, {"idea", id}});
        }
        const std::string note = answer == "open" ? std::string() : Online::cleanText(sv(args, "note"), kRoadNote, true);
        (*x)["status"] = answer;
        (*x)["note"] = note;
        if (answer == "open") x->erase("answeredAt"); else (*x)["answeredAt"] = now;
        staffDid(me, "roadmap", answer == "planned" ? "Put the idea \"" + title + "\" on the roadmap"
                                : answer == "declined" ? "Said no to the idea \"" + title + "\"" : "Reopened the idea \"" + title + "\"", by);
        if (answer != "open") {
            auto u = m_users.find(by);
            if (u != m_users.end())
                notify(&u->second, "roadmap", answer == "planned" ? "Staff put your idea \"" + title + "\" on the roadmap!"
                                              : "Staff answered your idea \"" + title + "\"" + (note.empty() ? "." : ": " + note), id);
        }
    } else {   // admin.ideaRemove
        json* x = findIn(ideas, id);
        if (!x) return fail("That idea isn't there any more.");
        staffDid(me, "roadmap", "Deleted the idea \"" + sv(*x, "title") + "\"", sv(*x, "by"));
        ideas.erase(ideas.begin() + (x - &ideas[0]));
    }
    saveRoadmap();
    return roadmapView(me);
}

void GbServer::saveRoadmap() { writeFile(m_opts.data / "roadmap.json", m_roadmap.dump(1)); }

void GbServer::loadRoadmap() {
    std::string text;
    if (readFile(m_opts.data / "roadmap.json", text)) {
        json all = json::parse(text, nullptr, false);
        if (all.is_object()) m_roadmap = all;
    }
    for (const char* k : {"items", "ideas"})
        if (!m_roadmap.contains(k) || !m_roadmap[k].is_array()) m_roadmap[k] = json::array();
}
