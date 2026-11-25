// Levels, daily streaks, profile walls and showcases. worker/levels.js has the same rules
// (and the notes on why):
//   - XP for doing things (kXpFor: op -> XP, times a day it counts); level L needs
//     50 * L * (L - 1) XP in total, up to kMaxLevel; each new level gives free Bolts once
//   - bolts.daily: 25 Bolts, +5 for each day in a row (up to +35), and kDailyXp XP
//   - the wall: short posts on a profile; the owner picks who can post (friends / everyone /
//     nobody); the writer, the owner and staff can delete; blocked people can't post
//   - the showcase: up to kShowcase things you made or own, shown on your profile
//   levels.me {}  wall.list {id, offset}  wall.post {id, text}  wall.delete {id, post}
//   wall.who {who}  showcase.set {ids}
// Kept with the account (users.json "levels"): {xp, day, got, streak, best, lastDaily, wall,
// wallWho, wallDay, wallToday, showcase}.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <map>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
struct XpRule { const char* op; long long xp; long long perDay; };
constexpr XpRule kXpFor[] = {
    {"bolts.play", 10, 12},
    {"forum.post", 15, 4}, {"forum.reply", 5, 20}, {"comments.post", 5, 10}, {"wall.post", 5, 10},
    {"groups.post", 3, 10}, {"art.comment", 3, 10}, {"video.comment", 3, 10},
    {"upload", 25, 8}, {"update", 10, 3}, {"art.post", 20, 4}, {"video.start", 20, 3}, {"paste.make", 5, 4},
    {"academy.publish", 30, 2}, {"academy.done", 10, 5}, {"gamelog.post", 10, 3},
    {"friends.accept", 10, 5},
};
constexpr long long kDailyXp = 20, kMaxLevel = 100;
constexpr size_t kShowcase = 6, kWallKept = 200, kWallText = 300, kWallPage = 20;
constexpr long long kWallPerDay = 30;

const XpRule* ruleFor(const std::string& op) {
    for (const XpRule& r : kXpFor) if (op == r.op) return &r;
    return nullptr;
}
long long xpForLevel(long long L) { return 50 * L * (L - 1); }
long long levelOf(long long xp) {
    long long L = 1;
    while (L < kMaxLevel && xp >= xpForLevel(L + 1)) ++L;
    return L;
}
long long levelReward(long long L) { return std::min(100LL, 5 * L); }
long long dailyBolts(long long n) { return 25 + 5 * std::min(std::max(n, 1LL) - 1, 7LL); }
bool whoOk(const std::string& w) { return w == "friends" || w == "everyone" || w == "nobody"; }
std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
long long nv(const json& j, const char* k) { return j.contains(k) && j[k].is_number() ? j[k].get<long long>() : 0; }
std::string wallWho(const json& lv) { const std::string w = sv(lv, "wallWho"); return whoOk(w) ? w : "friends"; }
const json& arr(const json& lv, const char* k) { static const json empty = json::array(); return lv.contains(k) && lv[k].is_array() ? lv[k] : empty; }
} // namespace

// The streak as it stands today: 0 when they missed yesterday.
static long long streakNow(const json& lv, const std::string& today, long long t) {
    const std::string last = sv(lv, "lastDaily");
    return last == today || last == Online::utcDay(t - 86400) ? nv(lv, "streak") : 0;
}

json GbServer::levelsJson(const User& u) const {
    const json& lv = u.levels;
    const long long xp = nv(lv, "xp"), level = levelOf(xp), t = Online::unixNow();
    const long long streak = streakNow(lv, Online::utcDay(t), t);
    return {{"level", level}, {"xp", xp}, {"from", xpForLevel(level)}, {"to", level >= kMaxLevel ? xpForLevel(level) : xpForLevel(level + 1)},
            {"max", level >= kMaxLevel}, {"streak", streak}, {"best", nv(lv, "best")}, {"nextDaily", dailyBolts(streak + 1)}};
}

long long GbServer::giveXp(User& u, long long amount) {
    if (u.userId == 0 || u.banned || amount <= 0) return 0;
    if (!u.levels.is_object()) u.levels = json::object();
    const long long before = levelOf(nv(u.levels, "xp"));
    u.levels["xp"] = nv(u.levels, "xp") + amount;
    const long long after = levelOf(nv(u.levels, "xp"));
    for (long long L = before + 1; L <= after; ++L)
        if (!hasRef(u, "level:" + std::to_string(L))) add(u, levelReward(L), "Reached level " + std::to_string(L), "level:" + std::to_string(L));
    if (after > before)
        notify(&u, "level", "You reached level " + std::to_string(after) + "! Here are " + std::to_string(levelReward(after)) + " Bolts.", std::to_string(u.userId), false);
    saveUsers();
    return amount;
}

void GbServer::levelsEarn(const std::string& name, User& me) {
    const XpRule* rule = ruleFor(name);
    if (!rule || me.userId == 0) return;
    if (!me.levels.is_object()) me.levels = json::object();
    const std::string today = Online::utcDay(Online::unixNow());
    if (sv(me.levels, "day") != today || !me.levels.contains("got") || !me.levels["got"].is_object()) { me.levels["day"] = today; me.levels["got"] = json::object(); }
    json& got = me.levels["got"];
    const long long n = got.contains(name) && got[name].is_number() ? got[name].get<long long>() : 0;
    if (n >= rule->perDay) return;
    got[name] = n + 1;
    giveXp(me, rule->xp);
}

json GbServer::dailyClaim(User& me, const std::string& today) {
    if (hasRef(me, "daily:" + today)) return fail("You already got today's Bolts. Come back tomorrow!");
    if (!me.levels.is_object()) me.levels = json::object();
    const long long streak = streakNow(me.levels, today, Online::unixNow()) + 1;
    me.levels["streak"] = streak;
    me.levels["lastDaily"] = today;
    me.levels["best"] = std::max(nv(me.levels, "best"), streak);
    const long long got = dailyBolts(streak);
    add(me, got, streak > 1 ? "Daily reward (" + std::to_string(streak) + " days in a row)" : std::string("Daily reward"), "daily:" + today);
    giveXp(me, kDailyXp);
    json r = okay(); r["me"] = meJson(me); r["got"] = got; r["streak"] = streak;
    return r;
}

bool GbServer::wallCanPost(const User& me, const User& owner) const {
    if (me.userId == 0 || owner.userId == 0 || me.banned || owner.banned) return false;
    if (me.id == owner.id) return true;
    if (blocks(me, owner)) return false;
    const std::string who = wallWho(owner.levels);
    return who == "everyone" || (who == "friends" && owner.friends.count(me.id) > 0);
}

json GbServer::wallPostJson(const json& p, const User& owner, const User& me) const {
    const std::string by = sv(p, "by");
    auto it = m_users.find(by);
    json who = {{"id", by}, {"name", "?"}, {"userId", 0}, {"verified", false}, {"avatar", nullptr}};
    if (it != m_users.end())
        who = {{"id", by}, {"name", it->second.username.empty() ? it->second.name : it->second.username}, {"userId", it->second.userId},
               {"verified", isVerified(it->second)}, {"avatar", it->second.avatar}};
    return {{"id", sv(p, "id")}, {"text", sv(p, "text")}, {"at", nv(p, "at")}, {"by", who},
            {"canDelete", me.userId > 0 && (by == me.id || owner.id == me.id || isStaff(me))}};
}

json GbServer::levelsProfile(const User& u, const User& me) const {
    json r = levelsJson(u);
    json show = json::array();
    for (const json& id : arr(u.levels, "showcase")) {
        if (!id.is_string()) continue;
        auto it = m_assets.find(id.get<std::string>());
        if (it != m_assets.end() && canSee(it->second, me)) show.push_back(publicAsset(it->second, &me));
    }
    r["showcase"] = show;
    r["wallWho"] = wallWho(u.levels);
    r["wallCount"] = arr(u.levels, "wall").size();
    r["canPost"] = wallCanPost(me, u);
    return r;
}

json GbServer::levelsOp(const std::string& name, User& me, const json& args) {
    const long long t = Online::unixNow();
    const std::string today = Online::utcDay(t);
    auto owner = [&]() -> User* {
        std::string want = sv(args, "id");
        if (want.empty() && args.contains("id") && args["id"].is_number_integer()) want = std::to_string(args["id"].get<long long>());
        User* u = !want.empty() && want.size() < 12 && std::all_of(want.begin(), want.end(), ::isdigit)
                      ? findUserId(std::atoll(want.c_str())) : findPerson(want);
        return u && u->userId > 0 ? u : nullptr;
    };
    if (name == "levels.me") { json r = okay(); r["levels"] = levelsJson(me); return r; }
    if (name == "wall.list") {
        User* u = owner();
        if (!u) return fail("There's no account with that ID on this server.");
        const long long offset = std::max(0LL, nv(args, "offset"));
        json all = json::array();
        for (const json& p : arr(u->levels, "wall")) {
            auto it = m_users.find(sv(p, "by"));
            if (it != m_users.end() && me.userId > 0 && blocks(me, it->second)) continue;
            all.push_back(p);
        }
        json posts = json::array();
        for (size_t i = (size_t)offset; i < all.size() && posts.size() < kWallPage; ++i) posts.push_back(wallPostJson(all[i], *u, me));
        json r = okay();
        r["posts"] = posts; r["more"] = all.size() > (size_t)offset + kWallPage; r["total"] = all.size();
        r["canPost"] = wallCanPost(me, *u); r["who"] = wallWho(u->levels);
        return r;
    }
    if (me.userId == 0) return fail("Sign up first.");
    if (!me.levels.is_object()) me.levels = json::object();
    if (name == "wall.post") {
        User* u = owner();
        if (!u) return fail("There's no account with that ID on this server.");
        if (!wallCanPost(me, *u)) {
            const std::string who = wallWho(u->levels);
            return fail(who == "nobody" ? "This wall is closed." : who == "friends" ? "Only their friends can post here." : "You can't post here.");
        }
        std::string text = say(sv(args, "text"), kWallText, true);
        for (size_t i; (i = text.find("\n\n\n")) != std::string::npos;) text.erase(i, 1);
        if (text.empty()) return fail("Write something first.");
        if (sv(me.levels, "wallDay") != today) { me.levels["wallDay"] = today; me.levels["wallToday"] = 0; }
        if (nv(me.levels, "wallToday") >= kWallPerDay) return fail("That's enough wall posts for today. Come back tomorrow!");
        me.levels["wallToday"] = nv(me.levels, "wallToday") + 1;
        if (!u->levels.is_object()) u->levels = json::object();
        if (!u->levels.contains("wall") || !u->levels["wall"].is_array()) u->levels["wall"] = json::array();
        json p = {{"id", Account::randomHex(6)}, {"by", me.id}, {"text", text}, {"at", t}};
        json& wall = u->levels["wall"];
        wall.insert(wall.begin(), p);
        if (wall.size() > kWallKept) wall.erase(wall.begin() + kWallKept, wall.end());
        if (u->id != me.id) notify(u, "wall", (me.username.empty() ? me.name : me.username) + " wrote on your wall.", std::to_string(u->userId), false);
        saveUsers();
        json r = okay(); r["post"] = wallPostJson(p, *u, me); return r;
    }
    if (name == "wall.delete") {
        User* u = owner();
        if (!u) return fail("There's no account with that ID on this server.");
        if (!u->levels.is_object() || !u->levels.contains("wall") || !u->levels["wall"].is_array()) return fail("That post is gone already.");
        json& wall = u->levels["wall"];
        const std::string post = sv(args, "post");
        auto it = std::find_if(wall.begin(), wall.end(), [&](const json& p) { return sv(p, "id") == post; });
        if (post.empty() || it == wall.end()) return fail("That post is gone already.");
        if (sv(*it, "by") != me.id && u->id != me.id && !isStaff(me)) return fail("You can only delete your own posts, or posts on your wall.");
        wall.erase(it);
        saveUsers();
        return okay();
    }
    if (name == "wall.who") {
        const std::string who = sv(args, "who");
        if (!whoOk(who)) return fail("Pick friends, everyone or nobody.");
        me.levels["wallWho"] = who;
        saveUsers();
        json r = okay(); r["who"] = who; return r;
    }
    if (name == "showcase.set") {
        std::vector<std::string> ids;
        if (args.contains("ids") && args["ids"].is_array())
            for (const json& x : args["ids"])
                if (x.is_string() && std::find(ids.begin(), ids.end(), x.get<std::string>()) == ids.end()) ids.push_back(x.get<std::string>());
        if (ids.size() > kShowcase) return fail("You can show up to " + std::to_string(kShowcase) + " things.");
        for (const std::string& id : ids) {
            auto it = m_assets.find(id);
            if (it == m_assets.end() || (it->second.creator != me.id && !me.owned.count(id))) return fail("You can only show things you made or own.");
        }
        me.levels["showcase"] = ids;
        saveUsers();
        json r = okay(); r["showcase"] = levelsProfile(me, me)["showcase"]; return r;
    }
    return fail("Unknown request.");
}
