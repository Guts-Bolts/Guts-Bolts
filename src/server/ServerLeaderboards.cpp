// Leaderboards: the site-wide top lists (worker/leaderboards.js has the same rules, and the notes
// on why). Each list is for "week" (the last 7 days) or "all" (all time):
//   - top creators: visits on their public games + faves on their public art
//   - most played games: visits on public games (this week: the per-day counts in Asset::days)
//   - most faved art: faves on public pieces (this week: pieces posted in the last 7 days)
// Nothing waiting for a staff check, nothing private, and nobody banned. Kept for kCacheSecs.
//   leaders.get {period}  -> {period, creators, games, art, at}   (guests too)
// uploadCounts(): art, art faves, videos and pastes someone posted (the profile's achievements).
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kTopSize = 10;
constexpr long long kCacheSecs = 300, kWeekDays = 7;
std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
std::string reviewOf(const json& meta) { return meta.is_object() ? meta.value("review", std::string()) : std::string(); }
} // namespace

json GbServer::leadersBuild(bool week, long long t) const {
    std::vector<std::string> days;
    for (long long i = 0; i < kWeekDays; ++i) days.push_back(Online::utcDay(t - i * 86400));
    auto fine = [&](const std::string& id) -> const User* {
        auto u = m_users.find(id);
        return u != m_users.end() && u->second.userId > 0 && !u->second.banned ? &u->second : nullptr;
    };
    auto person = [&](const User& u) {
        return json{{"id", u.id}, {"userId", u.userId}, {"name", u.name}, {"verified", isVerified(u)}, {"avatar", u.avatar}};
    };
    struct Points { long long visits = 0, faves = 0; };
    std::map<std::string, Points> points;
    std::vector<std::pair<const Asset*, long long>> games;
    for (const auto& [id, a] : m_assets) {
        if (a.kind != "game" || !reviewOf(a.meta).empty() || a.meta.value("access", std::string("public")) != "public" || !fine(a.creator)) continue;
        long long plays = 0;
        if (!week) plays = a.plays;
        else for (const std::string& d : days) if (a.days.contains(d) && a.days[d].is_object()) plays += a.days[d].value("plays", 0LL);
        if (!plays) continue;
        games.push_back({&a, plays});
        points[a.creator].visits += plays;
    }
    std::vector<std::pair<const json*, long long>> art;
    for (const auto& [id, p] : m_art["pieces"].items()) {
        if (!sv(p, "review").empty() || !fine(sv(p, "by"))) continue;
        if (week && p.value("at", 0LL) <= t - kWeekDays * 86400) continue;
        const long long faves = p.contains("faves") && p["faves"].is_array() ? (long long)p["faves"].size() : 0;
        if (!faves) continue;
        art.push_back({&p, faves});
        points[sv(p, "by")].faves += faves;
    }
    std::sort(games.begin(), games.end(), [](const auto& x, const auto& y) {
        return x.second != y.second ? x.second > y.second : x.first->created < y.first->created; });
    std::sort(art.begin(), art.end(), [](const auto& x, const auto& y) {
        return x.second != y.second ? x.second > y.second : x.first->value("at", 0LL) < y.first->value("at", 0LL); });
    std::vector<std::pair<const User*, Points>> creators;
    for (const auto& [id, p] : points) creators.push_back({fine(id), p});
    std::sort(creators.begin(), creators.end(), [](const auto& x, const auto& y) {
        const long long a = x.second.visits + x.second.faves, b = y.second.visits + y.second.faves;
        return a != b ? a > b : x.first->userId < y.first->userId; });
    json out = {{"ok", true}, {"period", week ? "week" : "all"}, {"at", t}, {"creators", json::array()}, {"games", json::array()}, {"art", json::array()}};
    for (size_t i = 0; i < creators.size() && i < kTopSize; ++i) {
        json c = person(*creators[i].first);
        c["visits"] = creators[i].second.visits; c["faves"] = creators[i].second.faves;
        c["points"] = creators[i].second.visits + creators[i].second.faves;
        out["creators"].push_back(c);
    }
    for (size_t i = 0; i < games.size() && i < kTopSize; ++i) {
        json g = publicAsset(*games[i].first);
        g["visits"] = games[i].second;
        g["by"] = person(*fine(games[i].first->creator));
        out["games"].push_back(g);
    }
    for (size_t i = 0; i < art.size() && i < kTopSize; ++i) {
        json p = artJson(*art[i].first, nullptr, false);
        p["faves"] = art[i].second;
        out["art"].push_back(p);
    }
    return out;
}

json GbServer::leadersOp(const std::string& name, const json& args) {
    if (name != "leaders.get") return fail("Unknown request.");
    const bool week = args.value("period", std::string()) != "all";
    const std::string key = week ? "week" : "all";
    const long long t = Online::unixNow();
    if (!m_leadersCache.contains(key) || t - m_leadersCache[key].value("at", 0LL) >= kCacheSecs) m_leadersCache[key] = leadersBuild(week, t);
    return m_leadersCache[key];
}

json GbServer::uploadCounts(const std::string& id) const {
    long long art = 0, faves = 0, videos = 0, pastes = 0;
    for (const auto& [k, p] : m_art["pieces"].items())
        if (sv(p, "by") == id) { ++art; faves += p.contains("faves") && p["faves"].is_array() ? (long long)p["faves"].size() : 0; }
    for (const auto& [k, v] : m_videos["videos"].items()) if (sv(v, "by") == id && sv(v, "state") == "ready") ++videos;
    for (const auto& [k, p] : m_pastes["pastes"].items()) if (sv(p, "by") == id) ++pastes;
    return {{"art", art}, {"artFaves", faves}, {"videos", videos}, {"pastes", pastes}};
}
