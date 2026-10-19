// Groups (communities) on the Guts&Bolts server: members with roles, a shout,
// a wall people can post on, and join requests for closed groups.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr long long kGroupFee      = 50;   // making a group (free for Verified people)
constexpr size_t    kMaxOwned      = 5;
constexpr size_t    kMaxJoined     = 50;
constexpr size_t    kWallSize      = 200;
constexpr long long kPostCooldown  = 10;   // seconds between wall posts
// Ranks: each has a level (higher = more in charge) and permissions. The Owner can do everything.
const std::vector<std::string> kGroupPerms = {"shout", "manage", "ranks", "games", "funds"};
constexpr size_t    kMaxRanks      = 10;
constexpr size_t    kGroupLedger   = 500;
json defaultRanks() {
    return json::array({json{{"id", "Owner"}, {"name", "Owner"}, {"level", 255}, {"perms", kGroupPerms}},
                        json{{"id", "Admin"}, {"name", "Admin"}, {"level", 200}, {"perms", {"shout", "manage", "games"}}},
                        json{{"id", "Member"}, {"name", "Member"}, {"level", 1}, {"perms", json::array()}}});
}
bool hasPerm(const json& rank, const std::string& perm) {
    if (rank.value("id", std::string()) == "Owner") return true;
    for (const auto& p : rank.value("perms", json::array())) if (p == perm) return true;
    return false;
}
} // namespace

json GbServer::ranksOf(const Group& g) const { return g.ranks.is_array() && !g.ranks.empty() ? g.ranks : defaultRanks(); }

json GbServer::rankIn(const Group& g, const std::string& userId) const {
    auto m = g.members.find(userId);
    if (m == g.members.end()) return nullptr;
    const json ranks = ranksOf(g);
    for (const auto& r : ranks) if (r.value("id", std::string()) == m->second) return r;
    for (const auto& r : ranks) if (r.value("id", std::string()) == "Member") return r;   // a deleted rank counts as Member
    return defaultRanks()[2];
}

bool GbServer::groupCan(const Group& g, const User& u, const std::string& perm) const {
    json r = rankIn(g, u.id);
    return !r.is_null() && hasPerm(r, perm);
}

long long GbServer::groupFunds(const Group& g) const {
    long long n = 0;
    for (const Entry& e : g.ledger) n += e.amount;
    return n;
}

void GbServer::groupAdd(Group& g, long long amount, const std::string& reason, const std::string& ref) {
    g.ledger.push_back({amount, reason, Online::unixNow(), ref});
    if (g.ledger.size() > kGroupLedger + 100) {
        const size_t cut = g.ledger.size() - kGroupLedger;
        long long old = 0;
        for (size_t i = 0; i < cut; ++i) old += g.ledger[i].amount;
        g.ledger.erase(g.ledger.begin(), g.ledger.begin() + cut);
        g.ledger.insert(g.ledger.begin(), Entry{old, "Earlier history", 0, "carry"});
    }
    saveGroups();
}

GbServer::Group* GbServer::groupFor(const Asset& a) {
    const std::string gameId = a.kind == "game" ? a.id : a.meta.value("game", std::string());
    auto game = m_assets.find(gameId);
    if (game == m_assets.end()) return nullptr;
    auto g = m_groups.find(game->second.meta.value("group", std::string()));
    return g == m_groups.end() ? nullptr : &g->second;
}

void GbServer::paySeller(Asset& a, User& buyer, long long share, const std::string& ref) {
    User* seller = findUser(a.creator);
    if (!seller || seller == &buyer) return;
    if (Group* g = groupFor(a)) {
        if (share > 0) groupAdd(*g, share, buyer.name + " bought " + a.name, ref);
        tally(&a, "bolts", share);
        notify(seller, "sale", buyer.name + " bought " + a.name + ". " + std::to_string(share) + " Bolts went to " + g->name + ".", a.id);
        return;
    }
    if (share > 0) add(*seller, share, "Sold " + a.name, ref);
    tally(&a, "bolts", share);
    notify(seller, "sale", buyer.name + " bought " + a.name + ". You got " + std::to_string(share) + " Bolts.", a.id);
}

json GbServer::badgesOf(const User& u) const {
    json b = json::array();
    if (isOfficial(u)) b.push_back("admin");
    for (const auto& [key, sig] : u.grants)
        if (Online::grantValid(Account::officialId(), key, u.id, sig)) b.push_back(key);
    return b;
}

std::vector<const GbServer::Group*> GbServer::groupsOf(const std::string& userId) const {
    std::vector<const Group*> out;
    for (const auto& [id, g] : m_groups) if (g.members.count(userId)) out.push_back(&g);
    return out;
}

json GbServer::publicGroup(const Group& g) const {
    auto it = m_users.find(g.owner);
    json j = {{"id", g.id}, {"name", g.name}, {"description", g.description}, {"owner", g.owner},
            {"ownerName", it != m_users.end() ? it->second.name : "?"},
            {"ownerVerified", it != m_users.end() && isVerified(it->second)},
            {"members", g.members.size()}, {"color", g.color}, {"open", g.open}, {"created", g.created},
            {"shout", g.shout.text}};
    if (!g.lyreto.empty()) j["lyreto"] = {{"link", g.lyreto}};
    return j;
}

json GbServer::groupOp(const std::string& name, User& me, const json& args) {
    const long long now = Online::unixNow();
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto userJson = [&](const std::string& id) {
        auto it = m_users.find(id);
        return json{{"id", id}, {"name", it != m_users.end() ? it->second.name : "?"},
                    {"verified", it != m_users.end() && isVerified(it->second)}};
    };

    if (name == "groups.list") {
        std::string q = lower(Online::cleanText(str("query"), 64));
        std::vector<const Group*> found;
        for (const auto& [id, g] : m_groups)
            if (q.empty() || lower(g.name).find(q) != std::string::npos) found.push_back(&g);
        std::sort(found.begin(), found.end(), [](const Group* a, const Group* b) {
            return a->members.size() != b->members.size() ? a->members.size() > b->members.size() : a->created > b->created;
        });
        json list = json::array();
        for (const Group* g : found) { if (list.size() >= 60) break; list.push_back(publicGroup(*g)); }
        json r = okay(); r["groups"] = list; return r;
    }
    if (name == "groups.mine") {
        json list = json::array();
        for (const Group* g : groupsOf(me.id)) {
            json pg = publicGroup(*g);
            const json rank = rankIn(*g, me.id);
            pg["role"] = rank.value("name", std::string("Member"));
            pg["perms"] = rank.value("id", std::string()) == "Owner" ? json(kGroupPerms) : rank.value("perms", json::array());
            list.push_back(pg);
        }
        json r = okay(); r["groups"] = list; return r;
    }
    if (name == "groups.create") {
        std::string title = say(str("name"), 40);
        if (title.size() < 3) return fail("Group names need at least 3 letters.");
        if (Account::nameIsReserved(title) && !isOfficial(me)) return fail("That name belongs to Guts&Bolts.");
        for (const auto& [id, g] : m_groups)
            if (lower(g.name) == lower(title)) return fail("There's already a group called that.");
        size_t owned = 0;
        for (const auto& [id, g] : m_groups) if (g.owner == me.id) ++owned;
        if (owned >= kMaxOwned) return fail("You already own " + std::to_string(kMaxOwned) + " groups.");
        if (groupsOf(me.id).size() >= kMaxJoined) return fail("You're in too many groups. Leave one first.");
        const long long fee = isVerified(me) ? 0 : kGroupFee;
        if (balance(me) < fee)
            return fail("Making a group costs " + std::to_string(fee) + " Bolts (free for Verified people).");
        Group g;
        g.id = "g-" + Account::randomHex(5);
        g.name = title;
        g.description = say(str("description"), 1000, true);
        g.owner = me.id;
        g.created = now;
        g.color = (int)std::clamp(args.value("color", 0x3A7BD5LL), 0LL, 0xFFFFFFLL);
        g.open = args.value("open", true);
        g.members[me.id] = "Owner";
        m_groups[g.id] = g;
        if (fee > 0) add(me, -fee, "Made the group " + title, "group:" + g.id);
        saveGroups();
        log(me.name + " made the group \"" + title + "\"");
        json r = okay(); r["group"] = publicGroup(g); r["me"] = meJson(me); return r;
    }

    // Everything else is about one group.
    auto it = m_groups.find(str("id"));
    if (it == m_groups.end()) return fail("That group doesn't exist (any more).");
    Group& g = it->second;
    const bool staff = isStaff(me);
    const json myRank = rankIn(g, me.id);
    const bool inGroup = !myRank.is_null();
    const int myLevel = inGroup ? myRank.value("level", 0) : 0;
    const bool isOwner = inGroup && myRank.value("id", std::string()) == "Owner";
    auto can = [&](const char* perm) { return inGroup && hasPerm(myRank, perm); };
    auto sortedRanks = [&]() {
        json ranks = ranksOf(g);
        std::vector<json> v(ranks.begin(), ranks.end());
        std::stable_sort(v.begin(), v.end(), [](const json& x, const json& y) { return x.value("level", 0) > y.value("level", 0); });
        return json(v);
    };

    if (name == "groups.get") {
        json r = okay();
        r["group"] = publicGroup(g);
        r["myRole"] = inGroup ? myRank.value("name", std::string()) : std::string();
        r["myRank"] = myRank;
        r["ranks"] = sortedRanks();
        r["requested"] = g.requests.count(me.id) > 0;
        std::vector<std::pair<std::string, json>> sorted;
        for (const auto& [id, rid] : g.members) sorted.push_back({id, rankIn(g, id)});
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.value("level", 0) > b.second.value("level", 0); });
        json members = json::array();
        for (const auto& [id, rank] : sorted) {
            if (members.size() >= 500) break;
            json u = userJson(id);
            u["role"] = rank.value("name", std::string());
            u["rank"] = rank.value("id", std::string());
            u["level"] = rank.value("level", 0);
            members.push_back(u);
        }
        r["memberList"] = members;
        json shout = json::object();
        if (!g.shout.text.empty()) { shout = userJson(g.shout.by); shout["text"] = g.shout.text; shout["time"] = g.shout.time; }
        r["shoutInfo"] = shout;
        json wall = json::array();
        size_t from = g.wall.size() > 60 ? g.wall.size() - 60 : 0;
        for (size_t i = from; i < g.wall.size(); ++i) {
            json p = userJson(g.wall[i].by);
            p["text"] = g.wall[i].text;
            p["time"] = g.wall[i].time;
            wall.push_back(p);
        }
        r["wall"] = wall;
        std::vector<const Asset*> games;
        for (const auto& [id, a] : m_assets)
            if (a.kind == "game" && a.meta.value("group", std::string()) == g.id && canSee(a, me)) games.push_back(&a);
        std::sort(games.begin(), games.end(), [](const Asset* x, const Asset* y) { return x->plays > y->plays; });
        json gl = json::array();
        for (const Asset* a : games) { if (gl.size() >= 50) break; gl.push_back(publicAsset(*a)); }
        r["games"] = gl;
        if (can("manage")) {
            json reqs = json::array();
            for (const auto& id : g.requests) reqs.push_back(userJson(id));
            r["requests"] = reqs;
        }
        if (can("funds") || staff) {
            r["funds"] = groupFunds(g);
            json ledger = json::array();
            for (size_t i = g.ledger.size(); i-- > 0 && ledger.size() < 50;)
                ledger.push_back({{"amount", g.ledger[i].amount}, {"reason", g.ledger[i].reason}, {"at", g.ledger[i].time}});
            r["ledger"] = ledger;
        }
        return r;
    }
    if (name == "groups.join") {
        if (inGroup) return okay();
        if (groupsOf(me.id).size() >= kMaxJoined) return fail("You're in too many groups. Leave one first.");
        if (!g.open) {
            if (g.requests.insert(me.id).second) notify(findUser(g.owner), "group", me.name + " wants to join " + g.name + ".", g.id);
            saveGroups();
            json r = okay(); r["requested"] = true; return r;
        }
        g.members[me.id] = "Member";
        saveGroups();
        return okay();
    }
    if (name == "groups.leave") {
        if (isOwner) return fail("Owners can't leave. Give the group to someone else first, or delete it.");
        g.members.erase(me.id);
        g.requests.erase(me.id);
        saveGroups();
        return okay();
    }
    if (name == "groups.post") {
        if (!inGroup) return fail("Join the group to post on its wall.");
        std::string text = say(str("text"), 300, true);
        if (text.empty()) return fail("Write something first.");
        if (now - m_lastPost[me.id] < kPostCooldown) return fail("Slow down a little - wait a few seconds between posts.");
        m_lastPost[me.id] = now;
        g.wall.push_back({me.id, text, now});
        if (g.wall.size() > kWallSize) g.wall.erase(g.wall.begin());
        saveGroups();
        return okay();
    }
    if (name == "groups.deletePost") {
        long long t = args.value("time", 0LL);
        std::string by = str("by");
        auto p = std::find_if(g.wall.begin(), g.wall.end(), [&](const Post& x) { return x.time == t && x.by == by; });
        if (p == g.wall.end()) return fail("That post is already gone.");
        if (p->by != me.id && !can("manage") && !staff) return fail("You can only delete your own posts.");
        g.wall.erase(p);
        saveGroups();
        return okay();
    }
    if (name == "groups.shout") {
        if (!can("shout")) return fail("Your rank can't shout in this group.");
        g.shout = {me.id, say(str("text"), 200), now};
        saveGroups();
        return okay();
    }
    if (name == "groups.edit") {
        if (!can("manage")) return fail("Your rank can't change this group.");
        if (args.contains("description")) g.description = say(str("description"), 1000, true);
        if (args.contains("color")) g.color = (int)std::clamp(args.value("color", 0LL), 0LL, 0xFFFFFFLL);
        if (args.contains("lyreto")) {   // its Lyreto server's invite link ("" for none)
            std::string link;
            if (!lyretoLink(str("lyreto"), link)) return fail("That isn't a Lyreto link. Copy your server's invite link in Lyreto: it starts with https://lyreto.com/");
            g.lyreto = link;
        }
        if (args.contains("open")) {
            g.open = args.value("open", true);
            if (g.open) {   // opening up lets everyone who asked in
                for (const auto& id : g.requests) g.members.emplace(id, "Member");
                g.requests.clear();
            }
        }
        saveGroups();
        json r = okay(); r["group"] = publicGroup(g); return r;
    }
    if (name == "groups.request") {
        if (!can("manage")) return fail("Your rank can't let people in.");
        std::string who = lower(str("user"));
        if (!g.requests.erase(who)) return fail("They're not waiting any more.");
        if (args.value("accept", false)) {
            g.members[who] = "Member";
            notify(findUser(who), "group", "You're in " + g.name + " now!", g.id);
        }
        saveGroups();
        return okay();
    }
    if (name == "groups.member") {
        std::string who = lower(str("user")), action = str("action"), rankId = str("rank");
        const json theirRank = rankIn(g, who);
        if (theirRank.is_null()) return fail("They're not in this group.");
        if (who == me.id) return fail("You can't do that to yourself.");
        const int theirLevel = theirRank.value("level", 0);
        if (theirRank.value("id", std::string()) == "Owner" && action != "owner") return fail("Nobody can change the owner.");
        if (action == "admin" || action == "member") { rankId = action == "admin" ? "Admin" : "Member"; action = "rank"; }   // (older apps)
        if (action == "kick") {
            if (!((can("manage") && theirLevel < myLevel) || staff)) return fail("You can't remove them.");
            g.members.erase(who);
        } else if (action == "rank") {
            json r;
            for (const auto& x : ranksOf(g)) if (x.value("id", std::string()) == rankId) r = x;
            if (r.is_null() || rankId == "Owner") return fail("Pick a rank.");
            if (!can("ranks") || theirLevel >= myLevel || (r.value("level", 0) >= myLevel && !isOwner))
                return fail("You can only change the rank of people below you, to a rank below yours.");
            g.members[who] = rankId;
        } else if (action == "owner") {
            if (!isOwner) return fail("Only the owner can give the group away.");
            std::string next = "Member";
            int best = -1;
            for (const auto& x : ranksOf(g))
                if (x.value("id", std::string()) != "Owner" && x.value("level", 0) > best) { best = x.value("level", 0); next = x.value("id", std::string()); }
            g.members[who] = "Owner";
            g.members[me.id] = next;
            g.owner = who;
        } else {
            return fail("Unknown member action.");
        }
        saveGroups();
        log(me.name + " " + action + " " + who.substr(0, 8) + " in \"" + g.name + "\"");
        return okay();
    }
    // The owner makes and changes ranks: {rank: {id (empty = new), name, level, perms}}.
    if (name == "groups.rank") {
        if (!isOwner) return fail("Only the owner can change the ranks.");
        const json want = args.contains("rank") && args["rank"].is_object() ? args["rank"] : json::object();
        json ranks = ranksOf(g);
        const std::string title = say(want.value("name", std::string()), 24);
        if (title.empty()) return fail("Give the rank a name.");
        json perms = json::array();
        if (want.contains("perms") && want["perms"].is_array())
            for (const auto& p : want["perms"])
                if (p.is_string() && std::find(kGroupPerms.begin(), kGroupPerms.end(), p.get<std::string>()) != kGroupPerms.end() &&
                    std::find(perms.begin(), perms.end(), p) == perms.end())
                    perms.push_back(p);
        const std::string wantId = want.value("id", std::string());
        int idx = -1;
        for (size_t i = 0; i < ranks.size(); ++i) if (!wantId.empty() && ranks[i].value("id", std::string()) == wantId) idx = (int)i;
        if (idx < 0) {
            if (ranks.size() >= kMaxRanks) return fail("A group can have up to " + std::to_string(kMaxRanks) + " ranks.");
            ranks.push_back({{"id", "r-" + Account::randomHex(4)}});
            idx = (int)ranks.size() - 1;
        }
        json& r = ranks[idx];
        for (size_t i = 0; i < ranks.size(); ++i)
            if ((int)i != idx && lower(ranks[i].value("name", std::string())) == lower(title)) return fail("There's already a rank called that.");
        r["name"] = title;
        const std::string id = r.value("id", std::string());
        if (id == "Owner") { r["level"] = 255; r["perms"] = kGroupPerms; }
        else {
            r["perms"] = perms;
            const int level = id == "Member" ? 1 : (want.contains("level") && want["level"].is_number_integer() ? want["level"].get<int>() : 0);
            if (id != "Member" && (level < 2 || level > 254)) return fail("Rank levels go from 2 to 254 (higher is more in charge).");
            for (size_t i = 0; i < ranks.size(); ++i)
                if ((int)i != idx && ranks[i].value("level", 0) == level) return fail("Another rank already has level " + std::to_string(level) + ".");
            r["level"] = level;
        }
        g.ranks = ranks;
        saveGroups();
        return okay();
    }
    if (name == "groups.rankDelete") {
        if (!isOwner) return fail("Only the owner can change the ranks.");
        const std::string id = str("rank");
        if (id == "Owner" || id == "Member") return fail("The Owner and Member ranks can't be deleted.");
        json ranks = ranksOf(g), kept = json::array();
        bool found = false;
        for (const auto& x : ranks) { if (x.value("id", std::string()) == id) found = true; else kept.push_back(x); }
        if (!found) return fail("That rank is already gone.");
        g.ranks = kept;
        for (auto& [uid, rid] : g.members) if (rid == id) rid = "Member";
        saveGroups();
        return okay();
    }
    // Pay a member from the group's Bolts.
    if (name == "groups.payout") {
        if (!can("funds")) return fail("Your rank can't spend the group's Bolts.");
        User* who = findUser(lower(str("user")));
        if (!who || !g.members.count(who->id)) return fail("You can only pay people in the group.");
        if (!args.contains("amount") || !args["amount"].is_number_integer() || args["amount"].get<long long>() < 1)
            return fail("Pay at least 1 Bolt.");
        const long long amount = args["amount"].get<long long>();
        if (amount > groupFunds(g)) return fail("The group only has " + std::to_string(groupFunds(g)) + " Bolts.");
        const std::string ref = "payout:" + g.id + ":" + Account::randomHex(4);
        groupAdd(g, -amount, "Paid " + who->name + " (by " + me.name + ")", ref);
        add(*who, amount, "Payout from " + g.name, ref);
        notify(who, "group", g.name + " paid you " + std::to_string(amount) + " Bolts!", g.id);
        saveUsers();
        json r = okay(); r["funds"] = groupFunds(g); return r;
    }
    if (name == "groups.removeGame") {   // the owner (or whoever adds games) takes a game out of the group
        auto a = m_assets.find(str("game"));
        if (a == m_assets.end() || a->second.meta.value("group", std::string()) != g.id) return fail("That game isn't in this group.");
        if (!isOwner && !(can("games") && a->second.creator == me.id) && !staff) return fail("Only the owner can take other people's games out.");
        a->second.meta["group"] = "";
        saveAssets();
        return okay();
    }
    if (name == "groups.delete") {
        if (!isOwner && !staff) return fail("Only the owner can delete the group.");
        log(me.name + " deleted the group \"" + g.name + "\"");
        for (auto& [id, a] : m_assets) if (a.meta.value("group", std::string()) == g.id) a.meta["group"] = "";
        saveAssets();
        m_groups.erase(it);
        saveGroups();
        return okay();
    }
    return fail("The server doesn't know how to do \"" + name + "\". It might need updating.");
}

// ---------------------------------------------------------------------------

void GbServer::saveGroups() {
    json all = json::object();
    for (const auto& [id, g] : m_groups) {
        json wall = json::array();
        for (const Post& p : g.wall) wall.push_back({p.by, p.text, p.time});
        all[id] = {{"name", g.name}, {"description", g.description}, {"owner", g.owner}, {"created", g.created},
                   {"color", g.color}, {"open", g.open}, {"members", g.members}, {"requests", g.requests},
                   {"shout", {g.shout.by, g.shout.text, g.shout.time}}, {"wall", wall}};
        if (!g.ranks.empty()) all[id]["ranks"] = g.ranks;
        if (!g.lyreto.empty()) all[id]["lyreto"] = g.lyreto;
        if (!g.ledger.empty()) {
            json ledger = json::array();
            for (const Entry& e : g.ledger) ledger.push_back({e.amount, e.reason, e.time, e.ref});
            all[id]["ledger"] = ledger;
        }
    }
    writeFile(m_opts.data / "groups.json", all.dump(1));
}

void GbServer::loadGroups() {
    std::string text;
    if (!readFile(m_opts.data / "groups.json", text)) return;
    json all = json::parse(text, nullptr, false);
    if (!all.is_object()) return;
    for (auto& [id, j] : all.items()) {
        Group g;
        g.id = id;
        g.name = j.value("name", std::string());
        g.description = j.value("description", std::string());
        g.owner = j.value("owner", std::string());
        g.created = j.value("created", 0LL);
        g.color = j.value("color", 0x3A7BD5);
        g.open = j.value("open", true);
        g.lyreto = j.value("lyreto", std::string());
        if (j.contains("members") && j["members"].is_object())
            for (auto& [k, v] : j["members"].items()) if (v.is_string()) g.members[k] = v.get<std::string>();
        if (j.contains("requests")) for (const auto& r : j["requests"]) if (r.is_string()) g.requests.insert(r.get<std::string>());
        if (j.contains("shout") && j["shout"].is_array() && j["shout"].size() == 3)
            g.shout = {j["shout"][0].get<std::string>(), j["shout"][1].get<std::string>(), j["shout"][2].get<long long>()};
        if (j.contains("wall"))
            for (const auto& p : j["wall"])
                if (p.is_array() && p.size() == 3) g.wall.push_back({p[0].get<std::string>(), p[1].get<std::string>(), p[2].get<long long>()});
        if (j.contains("ranks") && j["ranks"].is_array()) g.ranks = j["ranks"];
        if (j.contains("ledger") && j["ledger"].is_array())
            for (const auto& e : j["ledger"])
                if (e.is_array() && e.size() == 4)
                    g.ledger.push_back({e[0].get<long long>(), e[1].get<std::string>(), e[2].get<long long>(), e[3].get<std::string>()});
        if (!g.name.empty() && !g.owner.empty()) m_groups[id] = g;
    }
}
