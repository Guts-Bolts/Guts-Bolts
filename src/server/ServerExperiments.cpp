// Experiments (A/B tests) for games. worker/experiments.js has the same rules (and the notes on why):
//   - a creator makes up to kMostExperiments (kMostRunning at once): a name, 2-4 versions, up to 3 metrics
//     (an event name, spent:<currency>, earned:<currency>, funnel:<name>:<step>)
//   - each player always gets the same version: FNV-1a of "<id>:<user number>" (Experiments::variantOf)
//   - only the game's own servers send events (its creator, staff, or the host of a live server of it,
//     then only for the players in that server); players, minutes and coming back are counted by themselves
//   experiments.list {game}  experiments.create {game, name, variants, metrics}  experiments.stop {game, id}
//   experiments.remove {game, id}  experiments.forGame {game}  analytics.log {game, events}
// Kept in experiments.json: {games: {game: [experiment]}, data: {game: {exp: {player: {metric: value}}}}}.
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kMostExperiments = 10, kMostRunning = 5, kMostMetrics = 3, kMostVariants = 4, kMostEvents = 200;
constexpr size_t kMostPlayersCounted = 50000;
std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }

int variantOf(const std::string& id, long long user, int count) {
    if (count <= 0) return 0;
    uint32_t h = 0x811c9dc5u;
    for (unsigned char c : id + ":" + std::to_string(user)) { h ^= c; h *= 0x01000193u; }
    return (int)(h % (uint32_t)count);
}
// Plain letters, numbers, spaces and a few marks (worker: tidy).
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
std::string metricProblem(const std::string& m) {
    if (m.empty()) return "A metric needs a name.";
    if (m.rfind("spent:", 0) == 0 || m.rfind("earned:", 0) == 0) {
        const size_t c = m.find(':');
        const std::string cur = m.substr(c + 1, m.find(':', c + 1) == std::string::npos ? std::string::npos : m.find(':', c + 1) - c - 1);
        return cur.empty() ? "Say which currency, like spent:Coins." : "";
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
double zScore(double didA, double nA, double didB, double nB) {
    if (nA < 1 || nB < 1) return 0;
    const double p = (didA + didB) / (nA + nB);
    const double se = std::sqrt(p * (1 - p) * (1 / nA + 1 / nB));
    return se > 0 ? (didB / nB - didA / nA) / se : 0;
}
long long today() { return Online::unixNow() / 86400; }
} // namespace

void GbServer::saveExperiments() { writeFile(m_opts.data / "experiments.json", m_experiments.dump()); }
void GbServer::loadExperiments() {
    std::string text;
    if (readFile(m_opts.data / "experiments.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object()) m_experiments = j;
    }
    for (const char* k : {"games", "data"})
        if (!m_experiments.contains(k) || !m_experiments[k].is_object()) m_experiments[k] = json::object();
}

json GbServer::experimentResults(const std::string& game, const json& e) const {
    const json& variants = e["variants"];
    const json& metrics = e["metrics"];
    const int n = (int)variants.size();
    json per = json::array();
    for (const auto& v : variants) {
        json ms = json::array();
        for (const auto& m : metrics) ms.push_back({{"metric", m}, {"did", 0}, {"total", 0.0}});
        per.push_back({{"name", v}, {"players", 0}, {"minutes", 0.0}, {"cameBack", 0}, {"metrics", ms}});
    }
    const std::string id = sv(e, "id");
    const json* data = nullptr;
    if (m_experiments["data"].contains(game) && m_experiments["data"][game].contains(id)) data = &m_experiments["data"][game][id];
    if (data) {
        for (auto& [player, vals] : data->items()) {
            json& v = per[variantOf(id, std::atoll(player.c_str()), n)];
            for (auto& [metric, value] : vals.items()) {
                const double x = value.is_number() ? value.get<double>() : 0.0;
                if (metric == "__days") { v["players"] = v["players"].get<int>() + 1; if (x >= 2) v["cameBack"] = v["cameBack"].get<int>() + 1; continue; }
                if (metric == "__minutes") { v["minutes"] = v["minutes"].get<double>() + x; continue; }
                for (json& m : v["metrics"])
                    if (m["metric"] == metric) { m["total"] = m["total"].get<double>() + x; if (x > 0) m["did"] = m["did"].get<int>() + 1; }
            }
        }
    }
    const json first = per.empty() ? json::object() : per[0];
    for (size_t i = 0; i < per.size(); ++i) {
        json& v = per[i];
        v["minutes"] = std::round(v["minutes"].get<double>());
        const double fp = first.value("players", 0), vp = v.value("players", 0);
        v["cameBackSure"] = i > 0 && std::abs(zScore(first.value("cameBack", 0), fp, v.value("cameBack", 0), vp)) >= 1.96;
        for (size_t k = 0; k < v["metrics"].size(); ++k) {
            json& m = v["metrics"][k];
            m["total"] = std::round(m["total"].get<double>() * 100) / 100;
            m["sure"] = i > 0 && std::abs(zScore(first["metrics"][k].value("did", 0), fp, m.value("did", 0), vp)) >= 1.96;
        }
    }
    json out = e;
    out["results"] = per;
    return out;
}

json GbServer::experimentsOp(const std::string& name, User& me, const json& args) {
    auto it = findAsset(Online::cleanText(sv(args, "game"), 80));
    if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
    const Asset& a = it->second;
    json& games = m_experiments["games"];
    if (!games.contains(a.id) || !games[a.id].is_array()) games[a.id] = json::array();
    json& list = games[a.id];
    std::vector<json*> running;
    for (json& e : list) if (!e.value("stopped", 0LL)) running.push_back(&e);

    if (name == "experiments.forGame") {
        json out = json::array();
        for (const json* e : running) out.push_back({{"id", (*e)["id"]}, {"name", (*e)["name"]}, {"variants", (*e)["variants"]}});
        json r = okay(); r["experiments"] = out; return r;
    }

    if (name == "analytics.log") {
        const bool trusted = isStaff(me) || a.creator == me.id;
        std::set<long long> allowed;
        if (!trusted) {
            bool hosting = false;
            for (const auto& [sid, s] : m_sessions) {
                if (s.host != me.id || findAsset(s.game) != it) continue;
                hosting = true;
                for (const Client* c : s.players)
                    if (c && !c->account.empty()) { auto u = m_users.find(c->account); if (u != m_users.end()) allowed.insert(u->second.userId); }
            }
            if (!hosting) return fail("Only the game's own servers can send its analytics.");
            allowed.insert(me.userId);
        }
        json r = okay();
        r["kept"] = 0;
        if (running.empty() || !args.contains("events") || !args["events"].is_array()) return r;
        json& gameData = m_experiments["data"][a.id];
        const long long day = today();
        int kept = 0;
        size_t seen = 0;
        for (const json& ev : args["events"]) {
            if (++seen > kMostEvents) break;
            if (!ev.is_object()) continue;
            const long long player = ev.contains("player") && ev["player"].is_number() ? (long long)ev["player"].get<double>() : 0;
            std::string what = sv(ev, "name");
            if (what.size() > 60) what.resize(60);
            double value = ev.contains("value") && ev["value"].is_number() ? ev["value"].get<double>() : 1.0;
            if (!std::isfinite(value)) value = 1.0;
            if (player <= 0 || what.empty() || (!trusted && !allowed.count(player))) continue;
            const std::string pk = std::to_string(player);
            for (json* e : running) {
                json& expData = gameData[sv(*e, "id")];
                if (!expData.is_object()) expData = json::object();
                if (what == "__join") {
                    if (!expData.contains(pk)) {
                        if (expData.size() >= kMostPlayersCounted) continue;
                        expData[pk] = json::object();
                    }
                    json& p = expData[pk];
                    if (p.value("__lastday", -1LL) == day) continue;
                    p["__days"] = p.value("__days", 0.0) + 1;
                    p["__lastday"] = day;
                    ++kept;
                    continue;
                }
                if (!expData.contains(pk)) continue;   // (only players the experiment has seen join)
                json& p = expData[pk];
                if (what == "__minutes") { p["__minutes"] = p.value("__minutes", 0.0) + std::clamp(value, 0.0, 600.0); ++kept; continue; }
                const json& ms = (*e)["metrics"];
                if (std::find(ms.begin(), ms.end(), what) == ms.end()) continue;
                p[what] = p.value(what, 0.0) + std::clamp(value, -1e9, 1e9);
                ++kept;
            }
        }
        if (kept) saveExperiments();
        r["kept"] = kept;
        return r;
    }

    const bool mine = a.creator == me.id;
    if (!mine && !(isStaff(me) && name == "experiments.list")) return fail("Only the game's creator can run its experiments.");
    auto answer = [&]() {
        json out = json::array();
        for (const json& e : list) out.push_back(experimentResults(a.id, e));
        json r = okay(); r["experiments"] = out; r["most"] = kMostExperiments; r["mostRunning"] = kMostRunning; return r;
    };
    if (name == "experiments.list") return answer();

    if (name == "experiments.create") {
        if (me.userId == 0) return fail("Sign up to run experiments.");
        const std::string title = tidy(args.value("name", json()), 40);
        if (title.empty()) return fail("Give the experiment a name, like \"Shop\".");
        for (const json* e : running)
            if (lower(sv(*e, "name")) == lower(title)) return fail("There's already a running experiment called " + title + ".");
        if (running.size() >= kMostRunning) return fail("A game can run " + std::to_string(kMostRunning) + " experiments at once. Stop one first.");
        if (list.size() >= kMostExperiments) return fail("A game can keep " + std::to_string(kMostExperiments) + " experiments. Remove an old one first.");
        std::vector<std::string> variants;
        if (args.contains("variants") && args["variants"].is_array())
            for (const json& v : args["variants"]) { std::string t = tidy(v, 20); if (!t.empty()) variants.push_back(t); }
        if (variants.size() < 2 || variants.size() > kMostVariants)
            return fail("An experiment needs 2 to " + std::to_string(kMostVariants) + " versions, like \"Old\" and \"New\".");
        std::set<std::string> low;
        for (const auto& v : variants) low.insert(lower(v));
        if (low.size() != variants.size()) return fail("Give each version a different name.");
        std::vector<std::string> metrics;
        if (args.contains("metrics") && args["metrics"].is_array())
            for (const json& m : args["metrics"]) { std::string t = tidy(m, 60); if (!t.empty()) metrics.push_back(t); }
        if (metrics.size() > kMostMetrics) return fail("Pick up to " + std::to_string(kMostMetrics) + " things to count.");
        for (const auto& m : metrics) { const std::string bad = metricProblem(m); if (!bad.empty()) return fail(bad); }
        std::vector<std::string> unique;
        for (const auto& m : metrics) if (std::find(unique.begin(), unique.end(), m) == unique.end()) unique.push_back(m);
        std::random_device rd;
        char id[13];
        std::snprintf(id, sizeof id, "%06x%06x", rd() & 0xffffff, rd() & 0xffffff);
        list.push_back({{"id", id}, {"name", title}, {"variants", variants}, {"metrics", unique}, {"started", Online::unixNow()}, {"stopped", 0}});
        saveExperiments();
        return answer();
    }
    json* e = nullptr;
    for (json& x : list) if (sv(x, "id") == sv(args, "id")) e = &x;
    if (!e) return fail("That experiment isn't there.");
    if (name == "experiments.stop") {
        if (e->value("stopped", 0LL)) return fail("It's already stopped.");
        (*e)["stopped"] = Online::unixNow();
        saveExperiments();
        return answer();
    }
    if (name == "experiments.remove") {
        const std::string id = sv(*e, "id");
        list.erase(std::remove_if(list.begin(), list.end(), [&](const json& x) { return sv(x, "id") == id; }), list.end());
        if (m_experiments["data"].contains(a.id)) m_experiments["data"][a.id].erase(id);
        saveExperiments();
        return answer();
    }
    return fail("Unknown request.");
}
