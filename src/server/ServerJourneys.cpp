// Journeys: branching player paths through a game, and where players leave. worker/journeys.js has
// the same rules (and the notes on why):
//   - every journey starts with "Joined the game" (__join); then up to kMostStages stages, each with up
//     to kMostNodes choices (an event name, spent:/earned:<currency> or funnel:<name>:<step>)
//   - a player's path is the first choice at each stage, in order (an event only counts for the next
//     stage they haven't done yet)
//   - events come in through analytics.log (ServerExperiments.cpp checks who may send them)
//   journeys.list {game}  journeys.create {game, name, stages}  journeys.stop {game, id}  journeys.remove {game, id}
// Kept in experiments.json next to the experiments: {journeys: {game: [journey]}, jdata: {game: {id: {player: "0,2"}}}}.
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <random>
#include <sstream>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kMostJourneys = 5, kMostRunningJourneys = 3, kMostStages = 10, kMostNodes = 10;
constexpr size_t kMostPlayersInJourney = 50000;
std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }

// (the same as tidy and metricProblem in ServerExperiments.cpp / worker/experiments.js)
std::string tidy(const json& v, size_t most) {
    if (!v.is_string()) return "";
    std::string out;
    bool space = false;
    for (unsigned char c : v.get<std::string>()) {
        if (std::isspace(c)) { space = !out.empty(); continue; }
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':')) continue;
        if (space) { out += ' '; space = false; }
        out += (char)c;
    }
    if (out.size() > most) out.resize(most);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}
std::string nodeProblem(const std::string& m) {
    if (m.rfind("spent:", 0) == 0 || m.rfind("earned:", 0) == 0) {
        const size_t c = m.find(':');
        const size_t d = m.find(':', c + 1);
        return m.substr(c + 1, d == std::string::npos ? std::string::npos : d - c - 1).empty() ? "Say which currency, like spent:Coins." : "";
    }
    if (m.rfind("funnel:", 0) == 0) {
        const size_t a = m.find(':'), b = m.find(':', a + 1);
        const bool ok = b != std::string::npos && b > a + 1 && m.find(':', b + 1) == std::string::npos && b + 1 < m.size() &&
                        m.size() - b - 1 <= 3 && std::all_of(m.begin() + (long)b + 1, m.end(), [](char c) { return c >= '0' && c <= '9'; });
        return ok ? "" : "A funnel metric looks like funnel:Tutorial:3 (the funnel's name and a step number).";
    }
    if (m.rfind("__", 0) == 0) return "Metric names can't start with two underscores.";
    return "";
}
std::vector<int> pathOf(const std::string& p) {
    std::vector<int> out;
    std::stringstream ss(p);
    std::string bit;
    while (std::getline(ss, bit, ',')) if (!bit.empty()) out.push_back(std::atoi(bit.c_str()));
    return out;
}
} // namespace

json GbServer::journeyResults(const std::string& game, const json& j) const {
    json stages = json::array();
    for (const json& s : j["stages"]) {
        json nodes = json::array();
        for (const json& n : s["nodes"]) nodes.push_back({{"name", n}, {"players", 0}, {"left", 0}});
        stages.push_back({{"name", s["name"]}, {"nodes", nodes}});
    }
    std::map<std::tuple<int, int, int>, int> flows;
    int players = 0, leftAtJoin = 0;
    const std::string id = sv(j, "id");
    const json& jd = m_experiments["jdata"];
    if (jd.contains(game) && jd[game].contains(id) && jd[game][id].is_object())
        for (auto& [player, pv] : jd[game][id].items()) {
            ++players;
            const std::vector<int> p = pathOf(pv.is_string() ? pv.get<std::string>() : std::string());
            if (p.empty()) { ++leftAtJoin; continue; }
            int from = -1;
            for (size_t s = 0; s < p.size(); ++s) {
                if (s >= stages.size() || p[s] < 0 || (size_t)p[s] >= stages[s]["nodes"].size()) break;
                json& node = stages[s]["nodes"][p[s]];
                node["players"] = node["players"].get<int>() + 1;
                ++flows[{(int)s, from, p[s]}];
                from = p[s];
            }
            const size_t last = p.size() - 1;
            if (last + 1 < stages.size() && p[last] >= 0 && (size_t)p[last] < stages[last]["nodes"].size()) {
                json& node = stages[last]["nodes"][p[last]];
                node["left"] = node["left"].get<int>() + 1;
            }
        }
    json fl = json::array();
    for (const auto& [k, n] : flows) fl.push_back({{"stage", std::get<0>(k)}, {"from", std::get<1>(k)}, {"to", std::get<2>(k)}, {"players", n}});
    json out = j;
    out["players"] = players;
    out["leftAtJoin"] = leftAtJoin;
    out["stages"] = stages;
    out["flows"] = fl;
    return out;
}

int GbServer::runningJourneyCount(const std::string& game) const {
    const json& all = m_experiments["journeys"];
    if (!all.contains(game) || !all[game].is_array()) return 0;
    int n = 0;
    for (const json& j : all[game]) if (!j.value("stopped", 0LL)) ++n;
    return n;
}

// One event for each running journey of `game` (from analytics.log). How many kept it.
int GbServer::journeyEvent(const std::string& game, long long player, const std::string& what) {
    json& all = m_experiments["journeys"];
    if (!all.contains(game) || !all[game].is_array()) return 0;
    int kept = 0;
    const std::string pk = std::to_string(player);
    for (const json& j : all[game]) {
        if (j.value("stopped", 0LL)) continue;
        json& data = m_experiments["jdata"][game][sv(j, "id")];
        if (!data.is_object()) data = json::object();
        if (what == "__join") {
            if (data.contains(pk) || data.size() >= kMostPlayersInJourney) continue;
            data[pk] = "";
            ++kept;
            continue;
        }
        if (!data.contains(pk) || !data[pk].is_string()) continue;
        const std::string path = data[pk].get<std::string>();
        const size_t done = pathOf(path).size();
        if (done >= j["stages"].size()) continue;
        const json& nodes = j["stages"][done]["nodes"];
        int at = -1;
        for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i] == what) { at = (int)i; break; }
        if (at < 0) continue;
        data[pk] = path.empty() ? std::to_string(at) : path + "," + std::to_string(at);
        ++kept;
    }
    return kept;
}

json GbServer::journeysOp(const std::string& name, User& me, const json& args) {
    auto it = findAsset(Online::cleanText(sv(args, "game"), 80));
    if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
    const Asset& a = it->second;
    json& games = m_experiments["journeys"];
    if (!games.is_object()) games = json::object();
    if (!games.contains(a.id) || !games[a.id].is_array()) games[a.id] = json::array();
    json& list = games[a.id];
    const bool mine = a.creator == me.id;
    if (!mine && !(isStaff(me) && name == "journeys.list")) return fail("Only the game's creator can see its journeys.");
    auto answer = [&]() {
        json out = json::array();
        for (const json& j : list) out.push_back(journeyResults(a.id, j));
        json r = okay();
        r["journeys"] = out; r["most"] = kMostJourneys; r["mostRunning"] = kMostRunningJourneys;
        r["mostStages"] = kMostStages; r["mostNodes"] = kMostNodes;
        return r;
    };
    if (name == "journeys.list") return answer();

    if (name == "journeys.create") {
        if (me.userId == 0) return fail("Sign up to track journeys.");
        const std::string title = tidy(args.value("name", json()), 40);
        if (title.empty()) return fail("Give the journey a name, like \"First ten minutes\".");
        size_t running = 0;
        for (const json& j : list)
            if (!j.value("stopped", 0LL)) {
                ++running;
                if (lower(sv(j, "name")) == lower(title)) return fail("There's already a running journey called " + title + ".");
            }
        if (running >= kMostRunningJourneys) return fail("A game can track " + std::to_string(kMostRunningJourneys) + " journeys at once. Stop one first.");
        if (list.size() >= kMostJourneys) return fail("A game can keep " + std::to_string(kMostJourneys) + " journeys. Remove an old one first.");
        json stages = json::array();
        if (args.contains("stages") && args["stages"].is_array())
            for (const json& s : args["stages"]) {
                if (!s.is_object()) continue;
                const std::string sname = tidy(s.value("name", json()), 40);
                std::vector<std::string> nodes;
                if (s.contains("nodes") && s["nodes"].is_array())
                    for (const json& n : s["nodes"]) {
                        const std::string t = tidy(n, 60);
                        if (!t.empty() && std::find(nodes.begin(), nodes.end(), t) == nodes.end()) nodes.push_back(t);
                    }
                if (sname.empty() && nodes.empty()) continue;
                if (sname.empty()) return fail("Give each stage a name, like \"Pick a class\".");
                if (nodes.empty()) return fail("Stage \"" + sname + "\" needs at least one event (a choice players can make there).");
                if (nodes.size() > kMostNodes) return fail("A stage can have up to " + std::to_string(kMostNodes) + " choices.");
                for (const auto& n : nodes) { const std::string bad = nodeProblem(n); if (!bad.empty()) return fail(bad); }
                stages.push_back({{"name", sname}, {"nodes", nodes}});
            }
        if (stages.empty()) return fail("Add at least one stage after \"Joined the game\".");
        if (stages.size() > kMostStages) return fail("A journey can have up to " + std::to_string(kMostStages) + " stages.");
        std::random_device rd;
        char id[13];
        std::snprintf(id, sizeof id, "%06x%06x", rd() & 0xffffff, rd() & 0xffffff);
        list.push_back({{"id", id}, {"name", title}, {"stages", stages}, {"started", Online::unixNow()}, {"stopped", 0}});
        saveExperiments();
        return answer();
    }
    json* j = nullptr;
    for (json& x : list) if (sv(x, "id") == sv(args, "id")) j = &x;
    if (!j) return fail("That journey isn't there.");
    if (name == "journeys.stop") {
        if (j->value("stopped", 0LL)) return fail("It's already stopped.");
        (*j)["stopped"] = Online::unixNow();
        saveExperiments();
        return answer();
    }
    if (name == "journeys.remove") {
        const std::string id = sv(*j, "id");
        list.erase(std::remove_if(list.begin(), list.end(), [&](const json& x) { return sv(x, "id") == id; }), list.end());
        if (m_experiments["jdata"].contains(a.id)) m_experiments["jdata"][a.id].erase(id);
        saveExperiments();
        return answer();
    }
    return fail("Unknown request.");
}
