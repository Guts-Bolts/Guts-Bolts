// Content levels: a game's creator answers a few plain questions, and the answers give it a level
// (Mild, Moderate, Strong or Extreme). Everyone on Guts&Bolts is 18+, so these are not age ratings.
// worker/ratings.js has the same rules (and the notes on why):
//   - the highest level any answer needs is the game's level; every question needs an answer
//   - every change goes in the game's rating history (when, who, from, to); staff can fix a rating
//     and the creator is told
//   - each player can pick the highest level they want to see; higher ones leave their game lists
//     (their own games always show; games nobody rated yet always show)
//   game.rating {game}   game.rate {game, answers}   rating.limit {limit?}
// The rating, answers and history live in the game's meta: rating, ratingAnswers, ratingLog.
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <iterator>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
struct Answer { const char* v; int level; };
struct Question { const char* id; std::vector<Answer> answers; };
// (Keep in step with QUESTIONS in worker/ratings.js: the website shows the words.)
const std::vector<Question> kQuestions = {
    {"violence",   {{"none", 0}, {"cartoon", 0}, {"realistic", 2}, {"gore", 3}}},
    {"scary",      {{"none", 0}, {"mild", 1}, {"intense", 2}}},
    {"language",   {{"none", 0}, {"mild", 1}, {"strong", 3}}},
    {"romance",    {{"none", 0}, {"mild", 1}, {"suggestive", 3}}},
    {"substances", {{"none", 0}, {"mentioned", 2}, {"used", 3}}},
    {"chance",     {{"none", 0}, {"shown", 2}, {"gambling", 3}}},
};
constexpr size_t kHistoryKept = 50;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
const char* const kLevels[] = {"mild", "moderate", "strong", "extreme"};
const char* const kLevelNames[] = {"Mild", "Moderate", "Strong", "Extreme"};
int levelOf(const std::string& rating) {
    for (int i = 0; i < 4; ++i) if (rating == kLevels[i]) return i;
    return 0;
}
std::string ratingName(const std::string& r) { return kLevelNames[levelOf(r)]; }
bool knownRating(const std::string& r) { return std::find(std::begin(kLevels), std::end(kLevels), r) != std::end(kLevels); }

// The rating these answers give, or "" if one is missing or made up.
std::string ratingFor(const json& answers) {
    if (!answers.is_object()) return "";
    int level = 0;
    for (const Question& q : kQuestions) {
        const std::string picked = sv(answers, q.id);
        auto a = std::find_if(q.answers.begin(), q.answers.end(), [&](const Answer& x) { return picked == x.v; });
        if (a == q.answers.end()) return "";
        level = std::max(level, a->level);
    }
    return kLevels[level];
}
} // namespace

bool GbServer::ratingAllowed(const Asset& a, const User& me) const {
    if (a.kind != "game" || me.ratingLimit.empty() || a.creator == me.id) return true;
    const std::string rating = a.meta.is_object() ? a.meta.value("rating", std::string()) : std::string();
    if (rating.empty()) return true;   // not rated yet
    return levelOf(rating) <= levelOf(me.ratingLimit);
}

json GbServer::ratingOp(const std::string& name, User& me, const json& args) {
    if (name == "rating.limit") {
        if (args.contains("limit")) {
            if (me.userId == 0) return fail("Sign up to pick a rating limit.");
            const std::string limit = sv(args, "limit");
            if (!limit.empty() && !knownRating(limit)) return fail("Pick Mild, Moderate, Strong, Extreme or \"show everything\".");
            me.ratingLimit = limit;
            saveUsers();
        }
        json r = okay(); r["limit"] = me.ratingLimit; return r;
    }
    auto it = findAsset(Online::cleanText(sv(args, "game"), 80));
    if (it == m_assets.end() || it->second.kind != "game" || !canSee(it->second, me)) return fail("That game doesn't exist (any more).");
    Asset& a = it->second;
    if (!a.meta.is_object()) a.meta = json::object();
    auto history = [&]() {
        json out = json::array();
        if (!a.meta.contains("ratingLog") || !a.meta["ratingLog"].is_array()) return out;
        for (const json& h : a.meta["ratingLog"]) {
            json e = {{"at", h.value("at", 0LL)}, {"from", h.value("from", std::string())}, {"to", h.value("to", std::string())},
                      {"staff", h.value("staff", false)}, {"by", nullptr}};
            auto u = m_users.find(h.value("by", std::string()));
            if (u != m_users.end()) e["by"] = {{"id", u->second.id}, {"name", u->second.name}, {"userId", u->second.userId}};
            out.push_back(e);
        }
        return out;
    };
    if (name == "game.rating") {
        json r = okay();
        r["rating"] = a.meta.value("rating", std::string());
        r["answers"] = a.meta.contains("ratingAnswers") ? a.meta["ratingAnswers"] : json::object();
        r["history"] = history();
        return r;
    }
    if (name == "game.rate") {
        const bool staff = isStaff(me), fixing = staff && a.creator != me.id;
        if (a.creator != me.id && !staff) return fail("Only the game's creator can answer its content questions.");
        json answers = json::object();
        const json given = args.contains("answers") && args["answers"].is_object() ? args["answers"] : json::object();
        for (const Question& q : kQuestions) answers[q.id] = Online::cleanText(sv(given, q.id), 20);
        const std::string rating = ratingFor(answers);
        if (rating.empty()) return fail("Answer every question first.");
        const std::string from = a.meta.value("rating", std::string());
        a.meta["ratingAnswers"] = answers;
        if (rating != from) {
            a.meta["rating"] = rating;
            json log = json::array({{{"at", Online::unixNow()}, {"by", me.id}, {"from", from}, {"to", rating}, {"staff", fixing}}});
            if (a.meta.contains("ratingLog") && a.meta["ratingLog"].is_array())
                for (const json& h : a.meta["ratingLog"]) if (log.size() < kHistoryKept) log.push_back(h);
            a.meta["ratingLog"] = log;
            if (fixing) {
                auto c = m_users.find(a.creator);
                if (c != m_users.end()) notify(&c->second, "rating", "Staff changed the content level of " + a.name + " to " + ratingName(rating) + ".", a.id);
            }
        }
        saveAssets();
        json r = okay(); r["rating"] = rating; r["history"] = history(); return r;
    }
    return fail("Unknown request.");
}
