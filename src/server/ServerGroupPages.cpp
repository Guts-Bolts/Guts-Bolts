// Group pages and People cards on the Guts&Bolts server. worker/group-pages.js has the same rules:
//   - a group's icon and banner are decals from the Library (shown once they pass their staff check)
//   - announcements: ranks that can Shout post them, the writer / "manage" / staff delete them,
//     the newest kNewsKept stay, and everyone in the group gets a notification
//   - groups.members: the members with one rank (or everyone), with avatars, a page at a time
//   - people cards (users.search): public info plus avatar, clothes, online, playing, level, groups
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t    kNewsKept    = 30;
constexpr size_t    kNewsTitle   = 80;
constexpr size_t    kNewsText    = 2000;
constexpr long long kMembersPage = 60;
constexpr size_t    kNotifyMost  = 1000;   // a huge group stays quick
std::string reviewOf(const json& meta) { return meta.is_object() ? meta.value("review", std::string()) : std::string(); }
} // namespace

json GbServer::groupPicture(const std::string& id) const {
    if (id.empty()) return nullptr;
    auto it = m_assets.find(id);
    if (it == m_assets.end() || it->second.kind != "decal" || !reviewOf(it->second.meta).empty()) return nullptr;
    return {{"id", it->second.id}, {"num", it->second.num}};
}

json GbServer::groupNewsJson(const Group& g) const {
    json out = json::array();
    if (!g.news.is_array()) return out;
    for (size_t i = g.news.size(); i-- > 0;) {
        const json& n = g.news[i];
        const std::string by = n.value("by", std::string());
        auto u = m_users.find(by);
        out.push_back({{"id", n.value("id", std::string())}, {"title", n.value("title", std::string())}, {"text", n.value("text", std::string())},
                       {"time", n.value("time", 0LL)}, {"by", by}, {"name", u != m_users.end() ? u->second.name : "?"},
                       {"verified", u != m_users.end() && isVerified(u->second)}});
    }
    return out;
}

json GbServer::personCard(const User& u, const User& viewer) const {
    json p = publicUser(u);
    const json seen = presence(viewer, u);
    p["avatar"] = u.avatar;
    json worn = json::array();
    if (u.avatar.is_object() && u.avatar.contains("wearing") && u.avatar["wearing"].is_array())
        for (const auto& id : u.avatar["wearing"])
            if (id.is_string())
                if (json w = wearableJson(id.get<std::string>()); !w.is_null()) worn.push_back(w);
    p["wearing"] = worn;
    p["online"] = seen.value("online", false);
    p["playing"] = seen.contains("playing") && seen["playing"].is_object() ? seen["playing"].value("title", std::string()) : std::string();
    p["level"] = levelsJson(u).value("level", 1);
    p["groups"] = groupsOf(u.id).size();
    return p;
}

bool GbServer::peopleShown(const std::string& show, const User& u, const User& viewer) const {
    if (show == "online") return presence(viewer, u).value("online", false);
    if (show == "verified") return isVerified(u);
    if (show == "club") return !clubOf(u).empty();
    return true;
}

json GbServer::groupPageOp(const std::string& name, User& me, const json& args, Group& g) {
    const long long now = Online::unixNow();
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto num = [&](const char* k) { return args.contains(k) && args[k].is_number() ? (long long)args[k].get<double>() : 0LL; };
    const bool staff = isStaff(me);
    auto can = [&](const char* perm) { return groupCan(g, me, perm); };

    if (name == "groups.members") {
        const std::string rank = str("rank");
        if (!rank.empty()) {
            bool found = false;
            for (const auto& r : ranksOf(g)) if (r.value("id", std::string()) == rank) found = true;
            if (!found) return fail("That rank is gone.");
        }
        struct Row { const User* u; json r; };
        std::vector<Row> rows;
        for (const auto& [uid, rid] : g.members) {
            auto it = m_users.find(uid);
            if (it == m_users.end()) continue;
            json r = rankIn(g, uid);
            if (!rank.empty() && r.value("id", std::string()) != rank) continue;
            rows.push_back({&it->second, r});
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            const int la = a.r.value("level", 0), lb = b.r.value("level", 0);
            return la != lb ? la > lb : a.u->userId < b.u->userId;
        });
        const long long offset = std::max(0LL, num("offset"));
        const long long limit = args.contains("limit") ? std::clamp(num("limit"), 1LL, 100LL) : kMembersPage;
        json people = json::array();
        for (size_t i = (size_t)offset; i < rows.size() && (long long)people.size() < limit; ++i) {
            json p = personCard(*rows[i].u, me);
            p["role"] = rows[i].r.value("name", std::string());
            p["rank"] = rows[i].r.value("id", std::string());
            p["rankLevel"] = rows[i].r.value("level", 0);
            people.push_back(p);
        }
        json r = okay(); r["rank"] = rank; r["total"] = rows.size(); r["people"] = people; return r;
    }
    if (name == "groups.pictures") {
        if (!can("manage") && !staff) return fail("Your rank can't change this group.");
        for (const char* key : {"icon", "banner"}) {
            if (!args.contains(key)) continue;
            std::string want = args[key].is_string() ? Online::cleanText(args[key].get<std::string>(), 80)
                             : args[key].is_number_integer() ? std::to_string(args[key].get<long long>()) : std::string();
            if (want.rfind("gb:", 0) == 0) want = want.substr(3);
            std::string& slot = std::string(key) == "icon" ? g.icon : g.banner;
            if (want.empty()) { slot.clear(); continue; }
            auto it = findAsset(want);
            if (it == m_assets.end() || it->second.kind != "decal") return fail("That isn't a decal. Pick a decal from the Library (or upload your own).");
            slot = it->second.id;
        }
        saveGroups();
        bool waiting = false;
        for (const std::string* id : {&g.icon, &g.banner})
            if (!id->empty()) if (auto it = m_assets.find(*id); it != m_assets.end() && !reviewOf(it->second.meta).empty()) waiting = true;
        json r = okay(); r["group"] = publicGroup(g); r["waiting"] = waiting; return r;
    }
    if (name == "groups.announce") {
        if (!can("shout")) return fail("Your rank can't post announcements in this group.");
        const std::string title = say(str("title"), kNewsTitle);
        const std::string text = say(str("text"), kNewsText, true);
        if (title.empty()) return fail("Give the announcement a title.");
        if (text.empty()) return fail("Write the announcement first.");
        if (!g.news.is_array()) g.news = json::array();
        g.news.push_back({{"id", "n-" + Account::randomHex(4)}, {"by", me.id}, {"title", title}, {"text", text}, {"time", now}});
        while (g.news.size() > kNewsKept) g.news.erase(g.news.begin());
        size_t told = 0;
        for (const auto& [uid, rid] : g.members) {
            if (told++ >= kNotifyMost) break;
            if (uid != me.id) notify(findUser(uid), "group", g.name + ": " + title, g.id, false);
        }
        saveUsers();
        saveGroups();
        json r = okay(); r["news"] = groupNewsJson(g); return r;
    }
    if (name == "groups.announceDelete") {
        const std::string post = str("post");
        if (!g.news.is_array()) g.news = json::array();
        auto it = std::find_if(g.news.begin(), g.news.end(), [&](const json& n) { return n.value("id", std::string()) == post; });
        if (it == g.news.end()) return fail("That announcement is already gone.");
        if (it->value("by", std::string()) != me.id && !can("manage") && !staff) return fail("Only the writer or a group admin can delete that.");
        g.news.erase(it);
        saveGroups();
        json r = okay(); r["news"] = groupNewsJson(g); return r;
    }
    return fail("Unknown request.");
}
