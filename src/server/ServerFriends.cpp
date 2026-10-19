// Friends on the Guts&Bolts server: requests, accepting, and a list that
// shows who's online and which game they're in (so you can join them).
// Only you can see your own friends list.
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t    kMaxFriends  = 200;
constexpr size_t    kMaxRequests = 100;   // waiting for someone to answer
constexpr long long kOnlineFor   = 150;   // seconds since we last heard from them
constexpr size_t    kMaxFollowing = 2000;
} // namespace

// Can `viewer` see what `u` owns? (Staff always can, to sort out trades and scams.)
bool GbServer::seesInventory(const User& viewer, const User& u) const {
    if (isStaff(viewer) || viewer.id == u.id) return true;
    const std::string& s = u.privacyInventory;
    return (s == "everyone" || (s == "friends" && u.friends.count(viewer.id) > 0)) && !blocks(viewer, u);
}

// The catalog items someone owns, in one group, sorted by name (this server has no
// purchase order), with the numbers of the Limited copies they hold.
nlohmann::json GbServer::inventoryOf(const User& u, const std::string& cat) const {
    std::vector<const Asset*> found;
    for (const std::string& id : u.owned) {
        auto it = findAsset(id);
        if (it == m_assets.end()) continue;
        const std::string& k = it->second.kind;
        if (!Online::isCatalogItem(k)) continue;
        const bool in = cat == "accessories" ? Online::isAccessory(k) : cat == "clothing" ? (k == "shirt" || k == "pants" || k == "tshirt")
                      : cat == "faces" ? k == "face" : cat == "gear" ? k == "gear" : cat == "emotes" ? Online::isEmoteItem(k) : cat == "limited" ? it->second.limited : true;
        if (in) found.push_back(&it->second);
    }
    std::sort(found.begin(), found.end(), [](const Asset* a, const Asset* b) { return a->name < b->name; });
    json out = json::array();
    for (const Asset* a : found) {
        json j = publicAsset(*a);
        std::vector<long long> serials;
        if (a->limited)
            for (const json& c : a->copies)
                if (c.value("owner", std::string()) == u.id) serials.push_back(c.value("serial", 0LL));
        std::sort(serials.begin(), serials.end());
        j["serials"] = serials;
        out.push_back(j);
    }
    return out;
}

nlohmann::json GbServer::presence(const User& viewer, const User& u) const {
    auto allows = [&](const std::string& setting) {
        if (viewer.id == u.id || setting == "everyone") return true;
        return setting == "friends" && u.friends.count(viewer.id) > 0;
    };
    json r = {{"online", false}, {"playing", nullptr}};
    if (!allows(u.privacyStatus) || blocks(viewer, u)) return r;
    r["online"] = isOnline(u);
    if (const Session* s = sessionOf(u.id)) {
        auto g = findAsset(s->game);
        r["playing"] = {{"game", s->game}, {"title", !s->title.empty() ? s->title : g != m_assets.end() ? g->second.name : std::string("a game")},
                        {"private", s->priv}, {"full", headcount(*s) >= s->max},
                        {"session", allows(u.privacyJoin) ? json(s->id) : json(nullptr)}};
    }
    return r;
}

bool GbServer::isOnline(const User& u) const {
    return Online::unixNow() - u.lastSeen <= kOnlineFor || sessionOf(u.id) != nullptr;
}

json GbServer::friendOp(const std::string& name, User& me, const json& args) {
    auto person = [&](const User& u) {
        return json{{"id", u.id}, {"userId", u.userId}, {"username", u.username}, {"name", u.name}, {"verified", isVerified(u)}};
    };

    if (name == "friends.list") {
        json friends = json::array(), in = json::array(), out = json::array();
        for (const std::string& id : me.friends) {
            const User* u = findUser(id);
            if (!u) continue;
            json f = person(*u);
            json pr = presence(me, *u);
            f["online"] = pr["online"];
            if (!pr["playing"].is_null()) f["playing"] = pr["playing"];   // (friends can join private servers too)
            friends.push_back(f);
        }
        for (const std::string& id : me.friendIn) if (const User* u = findUser(id)) in.push_back(person(*u));
        for (const std::string& id : me.friendOut) if (const User* u = findUser(id)) out.push_back(person(*u));
        json r = okay();
        r["friends"] = friends;
        r["incoming"] = in;
        r["outgoing"] = out;
        return r;
    }

    User* them = findPerson(args.contains("user") && args["user"].is_number_integer()
                                ? std::to_string(args["user"].get<long long>()) : args.value("user", std::string()));
    if (!them || them->userId == 0) return fail("There's no account with that ID on this server.");
    // Where you stand with someone (the in-game player list asks before showing its menu).
    if (name == "friends.relation") {
        json r = okay();
        r["id"] = them->id;
        r["name"] = them->name;
        r["friendship"] = them->id == me.id ? "self" : me.friends.count(them->id) ? "friends"
                        : me.friendOut.count(them->id) ? "sent" : me.friendIn.count(them->id) ? "received" : "none";
        r["following"] = me.following.count(them->id) > 0;
        r["followers"] = them->followers.size();
        return r;
    }
    // Following: one way, no asking (like Roblox).
    if (name == "follow.add" || name == "follow.remove") {
        if (them->id == me.id) return fail("You can't follow yourself.");
        if (name == "follow.add") {
            if (them->banned || blocks(me, *them)) return fail("You can't follow that account.");
            if (!me.following.count(them->id) && me.following.size() >= kMaxFollowing)
                return fail("You already follow " + std::to_string(kMaxFollowing) + " people.");
            me.following.insert(them->id);
            if (them->followers.insert(me.id).second) notify(them, "follow", me.name + " follows you now.", me.id);
        } else {
            me.following.erase(them->id);
            them->followers.erase(me.id);
        }
        saveUsers();
        json r = okay();
        r["following"] = name == "follow.add";
        r["followers"] = them->followers.size();
        return r;
    }
    if (them->id == me.id) return fail("You can't be friends with yourself (but we like you).");

    auto becomeFriends = [&]() {
        if (me.friends.size() >= kMaxFriends || them->friends.size() >= kMaxFriends)
            return fail("One of you already has " + std::to_string(kMaxFriends) + " friends.");
        me.friendIn.erase(them->id);   me.friendOut.erase(them->id);
        them->friendIn.erase(me.id);   them->friendOut.erase(me.id);
        me.friends.insert(them->id);
        them->friends.insert(me.id);
        notify(them, "friend", me.name + " is your friend now.", me.id);
        saveUsers();
        json r = okay();
        r["status"] = "friends";
        return r;
    };

    if (name == "friends.add") {
        if (me.friends.count(them->id)) { json r = okay(); r["status"] = "friends"; return r; }
        if (me.friendIn.count(them->id)) return becomeFriends();   // they asked first
        if (them->banned || blocks(me, *them)) return fail("You can't add that account.");
        if (me.friendOut.size() >= kMaxRequests) return fail("You have too many friend requests waiting. Cancel some first.");
        if (them->friendIn.size() >= kMaxRequests) return fail(them->name + " has too many friend requests waiting.");
        me.friendOut.insert(them->id);
        them->friendIn.insert(me.id);
        notify(them, "friendRequest", me.name + " sent you a friend request.", me.id);
        saveUsers();
        json r = okay();
        r["status"] = "sent";
        return r;
    }
    if (name == "friends.accept") {
        if (!me.friendIn.count(them->id)) return fail("That friend request isn't there any more.");
        return becomeFriends();
    }
    if (name == "friends.decline" || name == "friends.cancel" || name == "friends.remove") {
        me.friendIn.erase(them->id);   me.friendOut.erase(them->id);
        them->friendIn.erase(me.id);   them->friendOut.erase(me.id);
        if (name == "friends.remove") { me.friends.erase(them->id); them->friends.erase(me.id); }
        saveUsers();
        json r = okay();
        r["status"] = "none";
        return r;
    }
    return fail("Unknown request.");
}
