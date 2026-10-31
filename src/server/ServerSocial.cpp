// Saved outfits, favourite games, "Continue playing" and private messages
// (worker/server.js has the same requests and limits).
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <set>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kMaxOutfits = 30, kMaxFavorites = 200;
// Comments under games (worker/server.js has the same): the newest kMaxComments are kept.
constexpr size_t kMaxComments = 500, kCommentLength = 200, kCommentsPage = 20;
constexpr long long kCommentCooldown = 15;
constexpr size_t kMaxInbox = 100, kMaxSent = 50;
constexpr int    kMessagesPerDay = 40;
constexpr size_t kMaxBlurb = 1000, kMaxStatus = 140, kMaxPosts = 10;
constexpr int    kStatusesPerDay = 30;
// Player badges (worker/server.js PLAYER_BADGES): earned automatically.
struct PlayerBadge { const char* key; const char* name; const char* need; };
constexpr PlayerBadge kPlayerBadges[] = {
    {"creator", "Creator", "Publish a game."},
    {"builder", "Builder", "Get 100 visits on your games."},
    {"architect", "Architect", "Get 1,000 visits on your games."},
    {"friendly", "Friendly", "Have 20 friends."},
    {"collector", "Collector", "Own 10 things from the catalog."},
    {"oldtimer", "Old Timer", "Be a member for a year."},
};
} // namespace

json GbServer::allPlayerBadges() {
    json all = json::array();
    for (const PlayerBadge& b : kPlayerBadges) all.push_back({{"key", b.key}, {"name", b.name}, {"need", b.need}});
    return all;
}

json GbServer::playerBadgesOf(const User& u) const {
    long long games = 0, visits = 0, items = 0;
    for (const auto& [id, a] : m_assets)
        if (a.kind == "game" && a.creator == u.id) { ++games; visits += a.plays; }
    for (const std::string& id : u.owned)
        if (auto it = findAsset(id); it != m_assets.end() && Online::isCatalogItem(it->second.kind)) ++items;
    const long long age = Online::unixNow() - (u.created ? u.created : Online::unixNow());
    json out = json::array();
    for (const PlayerBadge& b : kPlayerBadges) {
        const std::string k = b.key;
        const bool has = (k == "creator" && games > 0) || (k == "builder" && visits >= 100) || (k == "architect" && visits >= 1000) ||
                         (k == "friendly" && u.friends.size() >= 20) || (k == "collector" && items >= 10) ||
                         (k == "oldtimer" && age >= 365LL * 86400);
        if (has) out.push_back({{"key", b.key}, {"name", b.name}, {"need", b.need}});
    }
    return out;
}

// "Continue playing": the games you opened last, newest first (Server.cpp "get").
void GbServer::rememberPlayed(User& me, const std::string& gameId) {
    if (me.userId == 0) return;
    me.recent.erase(std::remove(me.recent.begin(), me.recent.end(), gameId), me.recent.end());
    me.recent.insert(me.recent.begin(), gameId);
    if (me.recent.size() > kMaxRecentGames) me.recent.resize(kMaxRecentGames);
}

json GbServer::socialOp(const std::string& name, User& me, const json& args) {
    const long long now = Online::unixNow();
    const std::string today = Online::utcDay(now);
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };

    // --- Classic profile: "About me" and "Right now I'm..." ---
    if (name == "profile.set") {
        if (me.userId == 0) return fail("Sign up first.");
        if (args.contains("blurb")) me.blurb = say(str("blurb"), kMaxBlurb, true);
        if (args.contains("status")) {
            const std::string text = say(str("status"), kMaxStatus);
            if (!text.empty()) {
                if (me.statusDay != today) { me.statusDay = today; me.statusesToday = 0; }
                if (me.statusesToday >= kStatusesPerDay) return fail("That's enough status updates for today.");
                me.statusesToday++;
                if (!me.posts.is_array()) me.posts = json::array();
                me.posts.insert(me.posts.begin(), json{{"text", text}, {"at", now}});
                while (me.posts.size() > kMaxPosts) me.posts.erase(me.posts.end() - 1);
            }
        }
        saveUsers();
        json r = okay(); r["me"] = meJson(me); r["blurb"] = me.blurb;
        r["status"] = me.posts.is_array() && !me.posts.empty() ? me.posts[0] : json();
        return r;
    }
    // My Feed: what your friends and the people you follow said lately (newest first).
    if (name == "feed.list") {
        json feed = json::array();
        if (me.userId != 0) {
            std::set<std::string> ids(me.friends.begin(), me.friends.end());
            ids.insert(me.following.begin(), me.following.end());
            ids.insert(me.id);
            std::vector<json> all;
            for (const std::string& id : ids) {
                auto it = m_users.find(id);
                if (it == m_users.end() || it->second.banned || blocks(me, it->second) || !it->second.posts.is_array()) continue;
                const User& u = it->second;
                for (size_t k = 0; k < u.posts.size() && k < 5; ++k)
                    all.push_back({{"text", u.posts[k].value("text", std::string())}, {"at", u.posts[k].value("at", 0LL)},
                                   {"user", {{"id", u.id}, {"userId", u.userId}, {"name", u.name}, {"verified", isVerified(u)}, {"avatar", u.avatar}}}});
            }
            std::sort(all.begin(), all.end(), [](const json& a, const json& b) { return a.value("at", 0LL) > b.value("at", 0LL); });
            for (size_t k = 0; k < all.size() && k < 40; ++k) feed.push_back(all[k]);
        }
        json r = okay(); r["feed"] = feed; return r;
    }

    // --- Saved outfits: your whole look, kept to put back on later ---
    if (name.rfind("outfit.", 0) == 0) {
        if (me.userId == 0) return fail("Sign up first.");
        if (!me.outfits.is_array()) me.outfits = json::array();
        auto list = [&]() {
            json out = json::array();
            for (const json& o : me.outfits) {
                json wearing = json::array();
                if (o.contains("avatar") && o["avatar"].contains("wearing"))
                    for (const json& id : o["avatar"]["wearing"])
                        if (id.is_string()) if (json w = wearableJson(id.get<std::string>()); !w.is_null())
                            wearing.push_back(w);
                out.push_back({{"id", o.value("id", std::string())}, {"name", o.value("name", std::string())},
                               {"avatar", o.value("avatar", json::object())}, {"created", o.value("created", 0LL)}, {"wearing", wearing}});
            }
            json r = okay(); r["outfits"] = out; r["me"] = meJson(me); return r;
        };
        auto find = [&]() -> json* {
            for (json& o : me.outfits) if (o.value("id", std::string()) == str("id")) return &o;
            return nullptr;
        };
        if (name == "outfit.list") return list();
        if (name == "outfit.save") {
            if (!me.avatar.is_object()) return fail("Change your look first, then save it as an outfit.");
            std::string title = say(str("name"), 40);
            if (title.empty()) title = "Outfit " + std::to_string(me.outfits.size() + 1);
            if (json* old = find()) { (*old)["avatar"] = me.avatar; (*old)["name"] = title; }
            else {
                if (me.outfits.size() >= kMaxOutfits) return fail("You have " + std::to_string(kMaxOutfits) + " outfits. Delete one first.");
                me.outfits.insert(me.outfits.begin(), json{{"id", Account::randomHex(6)}, {"name", title}, {"avatar", me.avatar}, {"created", now}});
            }
            saveUsers();
            return list();
        }
        json* o = find();
        if (!o) return fail("That outfit isn't there any more.");
        if (name == "outfit.wear") {
            json av = (*o)["avatar"];
            json wearing = json::array();   // things sold or traded away since are left off
            if (av.contains("wearing") && av["wearing"].is_array())
                for (const json& w : av["wearing"]) if (w.is_string() && canWear(me, w.get<std::string>())) wearing.push_back(w);
            av["wearing"] = wearing;
            av["updated"] = now;
            me.avatar = av;
            saveUsers();
            return list();
        }
        if (name == "outfit.rename") {
            const std::string title = say(str("name"), 40);
            if (title.empty()) return fail("Give it a name.");
            (*o)["name"] = title;
            saveUsers();
            return list();
        }
        if (name == "outfit.delete") {
            const std::string id = str("id");
            json kept = json::array();
            for (const json& x : me.outfits) if (x.value("id", std::string()) != id) kept.push_back(x);
            me.outfits = kept;
            saveUsers();
            return list();
        }
        return fail("Unknown request.");
    }

    // --- Comments under a game, newest first. Anyone can read; signed-up players can write. ---
    if (name == "comments.list" || name == "comments.post" || name == "comments.delete") {
        auto it = findAsset(str("game"));
        if (it == m_assets.end() || it->second.kind != "game" || !canSee(it->second, me)) return fail("That game doesn't exist (any more).");
        Asset& a = it->second;
        if (!a.comments.is_array()) a.comments = json::array();
        const bool mod = a.creator == me.id || isStaff(me), off = a.meta.value("commentsOff", false);
        User* owner = findUser(a.creator);
        if (name == "comments.post") {
            if (me.userId == 0) return fail("Sign up to comment.");
            if (off) return fail("Comments are turned off for this game.");
            if (owner && blocks(me, *owner)) return fail("You can't comment on this game.");
            const std::string text = say(str("text"), kCommentLength);
            if (text.empty()) return fail("Write something first.");
            long long& last = m_lastPost["c:" + me.id];
            if (now - last < kCommentCooldown) return fail("Slow down a little - wait a few seconds between comments.");
            last = now;
            a.comments.insert(a.comments.begin(), json{{"id", Account::randomHex(6)}, {"by", me.id}, {"text", text}, {"at", now}});
            if (a.comments.size() > kMaxComments) a.comments.erase(a.comments.begin() + kMaxComments, a.comments.end());
            saveAssets();
            if (owner && owner != &me) notify(owner, "comment", me.name + " commented on " + a.name + ": \"" + text.substr(0, 60) + "\"", a.id);
        }
        if (name == "comments.delete") {
            auto c = std::find_if(a.comments.begin(), a.comments.end(), [&](const json& x) { return x.value("id", std::string()) == str("id"); });
            if (c == a.comments.end()) return fail("That comment is already gone.");
            if (c->value("by", std::string()) != me.id && !mod) return fail("You can only delete your own comments.");
            if (c->value("by", std::string()) != me.id && a.creator != me.id)
                staffDid(me, "comment", "Deleted a comment on \"" + a.name + "\": \"" + c->value("text", std::string()).substr(0, 80) + "\"",
                         c->value("by", std::string()));
            a.comments.erase(c);
            saveAssets();
        }
        // A page of comments: "before" is the id of the last one you already have.
        size_t from = 0;
        if (const std::string before = str("before"); !before.empty())
            for (size_t i = 0; i < a.comments.size(); ++i) if (a.comments[i].value("id", std::string()) == before) { from = i + 1; break; }
        json out = json::array();
        for (size_t i = from; i < a.comments.size() && i < from + kCommentsPage; ++i) {
            const json& c = a.comments[i];
            const std::string by = c.value("by", std::string());
            const User* u = findUser(by);
            if (u && blocks(me, *u)) continue;
            json who = u ? json{{"id", u->id}, {"userId", u->userId}, {"name", u->name}, {"verified", isVerified(*u)}, {"staff", isStaff(*u)}, {"partner", isPartner(*u)}}
                         : json{{"id", by}, {"userId", 0}, {"name", "?"}};
            out.push_back({{"id", c.value("id", std::string())}, {"text", c.value("text", std::string())}, {"at", c.value("at", 0LL)},
                           {"by", who}, {"creator", by == a.creator}, {"canDelete", me.userId != 0 && (by == me.id || mod)}});
        }
        json r = okay();
        r["comments"] = out; r["more"] = from + kCommentsPage < a.comments.size(); r["off"] = off; r["count"] = a.comments.size();
        return r;
    }
    // --- Favourite games, and the ones you played last ---
    if (name == "game.favorite") {
        if (me.userId == 0) return fail("Sign up first.");
        auto it = findAsset(str("id"));
        if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
        Asset& a = it->second;
        const bool had = std::find(me.favorites.begin(), me.favorites.end(), a.id) != me.favorites.end();
        const bool want = !(args.contains("on") && args["on"].is_boolean() && !args["on"].get<bool>());
        long long count = a.meta.is_object() ? a.meta.value("favorites", 0LL) : 0LL;
        if (want && !had) {
            if (me.favorites.size() >= kMaxFavorites) return fail("You have " + std::to_string(kMaxFavorites) + " favourites. Take one off first.");
            me.favorites.insert(me.favorites.begin(), a.id);
            ++count;
        } else if (!want && had) {
            me.favorites.erase(std::remove(me.favorites.begin(), me.favorites.end(), a.id), me.favorites.end());
            count = std::max(0LL, count - 1);
        }
        a.meta["favorites"] = count;
        saveUsers();
        saveAssets();
        json pa = publicAsset(a);
        pa["myFavorite"] = want;
        json r = okay(); r["asset"] = pa; return r;
    }
    if (name == "games.mine") {   // which: "recent" (Continue playing) or "favorites"
        const std::vector<std::string>& ids = str("which") == "favorites" ? me.favorites : me.recent;
        long long limit = args.contains("limit") && args["limit"].is_number_integer() ? args["limit"].get<long long>() : 30;
        limit = std::clamp(limit, 1LL, 200LL);
        json out = json::array();
        for (const std::string& id : ids) {
            auto it = findAsset(id);
            if (it == m_assets.end() || it->second.kind != "game") continue;
            json pa = publicAsset(it->second);
            pa["myFavorite"] = std::find(me.favorites.begin(), me.favorites.end(), id) != me.favorites.end();
            out.push_back(pa);
            if ((long long)out.size() >= limit) break;
        }
        json r = okay(); r["assets"] = out; return r;
    }

    // --- Chat conversations and parties (separate from the classic inbox). ---
    if (name.rfind("party.", 0) == 0) {
        if (me.userId == 0) return fail("Sign up first.");
        if (!me.chats.is_array()) me.chats = json::array();
        if (!me.partyInvites.is_array()) me.partyInvites = json::array();
        auto person = [&](const User& u) {
            return json{{"id", u.id}, {"userId", u.userId}, {"name", u.username.empty() ? u.name : u.username}, {"verified", isVerified(u)}};
        };
        auto findChat = [&](User& u, const std::string& id) -> json* {
            if (!u.chats.is_array()) return nullptr;
            for (json& x : u.chats) if (x.value("id", std::string()) == id) return &x;
            return nullptr;
        };
        auto syncChat = [&](const json& chat) {
            if (!chat.contains("members") || !chat["members"].is_array()) return;
            for (const json& member : chat["members"]) {
                const std::string id = member.value("id", std::string());
                auto it = m_users.find(id);
                if (it == m_users.end() || it->second.userId == 0) continue;
                User& u = it->second;
                if (!u.chats.is_array()) u.chats = json::array();
                bool found = false;
                for (json& x : u.chats) if (x.value("id", std::string()) == chat.value("id", std::string())) { x = chat; found = true; break; }
                if (!found) u.chats.insert(u.chats.begin(), chat);
                while (u.chats.size() > 100) u.chats.erase(u.chats.end() - 1);
            }
            saveUsers();
        };
        if (name == "party.list") {
            json r = okay(); r["chats"] = me.chats; r["invites"] = me.partyInvites; r["me"] = meJson(me); return r;
        }
        if (name == "party.create") {
            if (!args.contains("members") || !args["members"].is_array()) return fail("Choose at least one friend to invite.");
            std::set<std::string> ids;
            for (const json& v : args["members"]) if (v.is_string() && !v.get<std::string>().empty() && v.get<std::string>() != me.id) ids.insert(v.get<std::string>());
            if (ids.empty()) return fail("Choose at least one friend to invite.");
            if (ids.size() > 19) return fail("A party can have up to 20 people, including you.");
            std::vector<User*> invitees;
            for (const std::string& id : ids) {
                auto it = m_users.find(id);
                if (it == m_users.end() || it->second.userId == 0 || !me.friends.count(id)) return fail("Parties can only be created with your friends.");
                User& u = it->second;
                if (u.banned || blocks(me, u) || blocks(u, me)) return fail("You cannot invite someone you have blocked or who has blocked you.");
                if (u.privacyMessages == "nobody" || (u.privacyMessages == "friends" && !u.friends.count(me.id)))
                    return fail(u.name + " does not accept messages from you.");
                invitees.push_back(&u);
            }
            std::string title = say(str("title"), 60);
            if (title.empty()) {
                for (size_t i = 0; i < invitees.size(); ++i) { if (i) title += ", "; title += invitees[i]->username.empty() ? invitees[i]->name : invitees[i]->username; }
            }
            const std::string id = "p-" + Account::randomHex(8);
            json chat = {{"id", id}, {"title", title}, {"owner", me.id}, {"members", json::array({person(me)})},
                         {"messages", json::array()}, {"readAt", json{{me.id, now}}}, {"created", now}, {"updated", now}};
            me.chats.insert(me.chats.begin(), chat);
            for (User* u : invitees) {
                if (!u->partyInvites.is_array()) u->partyInvites = json::array();
                u->partyInvites.insert(u->partyInvites.begin(), json{{"id", id}, {"owner", me.id},
                    {"ownerName", me.username.empty() ? me.name : me.username}, {"title", title}, {"at", now}});
                while (u->partyInvites.size() > 50) u->partyInvites.erase(u->partyInvites.end() - 1);
            }
            saveUsers();
            json r = okay(); r["chat"] = chat; r["me"] = meJson(me); return r;
        }
        if (name == "party.respond") {
            const std::string id = str("id");
            size_t index = me.partyInvites.size();
            for (size_t i = 0; i < me.partyInvites.size(); ++i) if (me.partyInvites[i].value("id", std::string()) == id) { index = i; break; }
            if (index == me.partyInvites.size()) return fail("That party invitation is no longer available.");
            const json invite = me.partyInvites[index];
            me.partyInvites.erase(me.partyInvites.begin() + (long)index);
            if (!args.value("accept", false)) { saveUsers(); json r = okay(); r["declined"] = true; r["me"] = meJson(me); return r; }
            auto owner = m_users.find(invite.value("owner", std::string()));
            json* chat = owner == m_users.end() ? nullptr : findChat(owner->second, id);
            if (!chat || !chat->contains("members") || !(*chat)["members"].is_array()) { saveUsers(); return fail("That party has ended."); }
            if ((*chat)["members"].size() >= 20) { saveUsers(); return fail("That party is full."); }
            bool already = false;
            for (const json& member : (*chat)["members"]) if (member.value("id", std::string()) == me.id) already = true;
            if (!already) (*chat)["members"].push_back(person(me));
            (*chat)["readAt"][me.id] = now; (*chat)["updated"] = now;
            const json copy = *chat; syncChat(copy);
            json r = okay(); r["chat"] = copy; r["me"] = meJson(me); return r;
        }
        const std::string id = str("id");
        json* chat = findChat(me, id);
        if (!chat || !chat->contains("members") || !(*chat)["members"].is_array()) return fail("That conversation is no longer available.");
        bool member = false;
        for (const json& m : (*chat)["members"]) if (m.value("id", std::string()) == me.id) member = true;
        if (!member) return fail("That conversation is no longer available.");
        if (name == "party.read") {
            (*chat)["readAt"][me.id] = now; const json copy = *chat; syncChat(copy);
            json r = okay(); r["chat"] = copy; r["me"] = meJson(me); return r;
        }
        if (name == "party.send" || name == "party.game") {
            json message;
            if (name == "party.game") {
                auto game = findAsset(str("game"));
                if (game == m_assets.end() || game->second.kind != "game") return fail("That game is not available to play.");
                message = {{"id", Account::randomHex(8)}, {"from", me.id}, {"name", me.username.empty() ? me.name : me.username},
                           {"body", "Invited everyone to play " + game->second.name + "."}, {"game", game->second.id}, {"at", now}};
            } else {
                const std::string body = say(str("body"), 1000, true);
                if (body.empty()) return fail("Write a message first.");
                if (me.messageDay != today) { me.messageDay = today; me.messagesToday = 0; }
                if (me.messagesToday >= kMessagesPerDay) return fail("That is " + std::to_string(kMessagesPerDay) + " messages today. Try again tomorrow.");
                ++me.messagesToday;
                message = {{"id", Account::randomHex(8)}, {"from", me.id}, {"name", me.username.empty() ? me.name : me.username}, {"body", body}, {"at", now}};
            }
            if (!(*chat)["messages"].is_array()) (*chat)["messages"] = json::array();
            (*chat)["messages"].push_back(message);
            while ((*chat)["messages"].size() > 200) (*chat)["messages"].erase((*chat)["messages"].begin());
            (*chat)["updated"] = now;
            const json copy = *chat; syncChat(copy);
            json r = okay(); r["chat"] = copy; r["me"] = meJson(me); return r;
        }
        if (name == "party.invite") {
            if (chat->value("owner", std::string()) != me.id) return fail("Only the party owner can invite people.");
            auto it = m_users.find(str("user"));
            if (it == m_users.end() || it->second.userId == 0 || !me.friends.count(it->first)) return fail("You can only invite friends.");
            User& u = it->second;
            for (const json& m : (*chat)["members"]) if (m.value("id", std::string()) == u.id) return fail("That player is already in the party.");
            if ((*chat)["members"].size() >= 20) return fail("That party is full.");
            if (u.banned || blocks(me, u) || blocks(u, me) || u.privacyMessages == "nobody" || (u.privacyMessages == "friends" && !u.friends.count(me.id)))
                return fail("You cannot invite that player.");
            if (!u.partyInvites.is_array()) u.partyInvites = json::array();
            u.partyInvites.insert(u.partyInvites.begin(), json{{"id", id}, {"owner", me.id},
                {"ownerName", me.username.empty() ? me.name : me.username}, {"title", chat->value("title", std::string("Party"))}, {"at", now}});
            while (u.partyInvites.size() > 50) u.partyInvites.erase(u.partyInvites.end() - 1);
            saveUsers(); return okay({{"chat", *chat}});
        }
        if (name == "party.rename") {
            if (chat->value("owner", std::string()) != me.id) return fail("Only the party owner can rename this party.");
            const std::string title = say(str("title"), 60);
            if (title.empty()) return fail("Give the party a name.");
            (*chat)["title"] = title; (*chat)["updated"] = now;
            const json copy = *chat; syncChat(copy); return okay({{"chat", copy}});
        }
        if (name == "party.kick" || name == "party.leave") {
            const std::string target = name == "party.leave" ? me.id : str("user");
            if (name == "party.kick" && chat->value("owner", std::string()) != me.id) return fail("Only the party owner can remove people.");
            if (name == "party.kick" && target == me.id) return fail("Use Leave party to leave.");
            size_t index = (*chat)["members"].size();
            for (size_t i = 0; i < (*chat)["members"].size(); ++i) if ((*chat)["members"][i].value("id", std::string()) == target) { index = i; break; }
            if (index == (*chat)["members"].size()) return fail("That player is not in this party.");
            if (name == "party.leave" && target == chat->value("owner", std::string()) && (*chat)["members"].size() > 1) {
                for (const json& m : (*chat)["members"]) if (m.value("id", std::string()) != target) { (*chat)["owner"] = m.value("id", std::string()); break; }
            }
            (*chat)["members"].erase((*chat)["members"].begin() + (long)index);
            auto leaving = m_users.find(target);
            if (leaving != m_users.end() && leaving->second.chats.is_array()) {
                json& list = leaving->second.chats;
                for (size_t i = 0; i < list.size(); ++i) if (list[i].value("id", std::string()) == id) { list.erase(list.begin() + (long)i); break; }
            }
            if ((*chat)["members"].empty()) { saveUsers(); json r = okay(); r["left"] = true; return r; }
            (*chat)["updated"] = now; const json copy = *chat; syncChat(copy);
            json r = okay(); r["chat"] = copy; r["left"] = target == me.id; return r;
        }
        return fail("Unknown party request.");
    }

    // --- Private messages. Guests can't send or get them. ---
    if (name == "message.send") {
        if (me.userId == 0) return fail("Sign up first.");
        const std::string want = str("to");
        bool number = !want.empty();
        for (size_t i = 0; i < want.size(); ++i) if (!(std::isdigit((unsigned char)want[i]) || (i == 0 && want[i] == '#'))) number = false;
        User* them = number && want != "#" ? findUserId(std::atoll(want.c_str() + (want[0] == '#' ? 1 : 0))) : findPerson(want);
        if (!them || them->userId == 0) return fail("There's no account with that ID on this server.");
        if (them->id == me.id) return fail("You can't send a message to yourself.");
        if (them->banned || blocks(me, *them)) return fail("You can't send messages to that account.");
        const std::string& setting = them->privacyMessages;
        if (setting == "nobody" || (setting == "friends" && !them->friends.count(me.id)))
            return fail(setting == "friends" ? them->name + " only gets messages from friends." : them->name + " doesn't get messages.");
        std::string subject = say(str("subject"), 80);
        if (subject.empty()) subject = "(no subject)";
        const std::string body = say(str("body"), 2000, true);
        if (body.empty()) return fail("Write something first.");
        if (me.messageDay != today) { me.messageDay = today; me.messagesToday = 0; }
        if (me.messagesToday >= kMessagesPerDay) return fail("That's " + std::to_string(kMessagesPerDay) + " messages today. Try again tomorrow.");
        me.messagesToday++;
        const std::string id = Account::randomHex(8);
        if (!them->inbox.is_array()) them->inbox = json::array();
        them->inbox.insert(them->inbox.begin(), json{{"id", id}, {"from", me.id}, {"subject", subject}, {"body", body}, {"at", now}, {"read", false}});
        while (them->inbox.size() > kMaxInbox) them->inbox.erase(them->inbox.end() - 1);
        if (!me.sent.is_array()) me.sent = json::array();
        me.sent.insert(me.sent.begin(), json{{"id", id}, {"to", them->id}, {"subject", subject}, {"body", body}, {"at", now}});
        while (me.sent.size() > kMaxSent) me.sent.erase(me.sent.end() - 1);
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    // The bell (worker/server.js has the same): newest first, and mark them all read.
    if (name == "notes.list") { json r = okay(); r["notes"] = me.notes; r["me"] = meJson(me); return r; }
    if (name == "notes.read") {
        for (json& n : me.notes) n["read"] = true;
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "message.list") {   // box: "inbox" or "sent"
        const bool sent = str("box") == "sent";
        auto who = [&](const std::string& id) {
            auto it = m_users.find(id);
            if (it == m_users.end()) return json{{"id", id}, {"userId", 0}, {"name", "?"}};
            return json{{"id", id}, {"userId", it->second.userId}, {"name", it->second.name}, {"verified", isVerified(it->second)}};
        };
        json out = json::array();
        const json& box = sent ? me.sent : me.inbox;
        if (me.userId != 0 && box.is_array())
            for (const json& m : box)
                out.push_back({{"id", m.value("id", std::string())}, {"subject", m.value("subject", std::string())},
                               {"body", m.value("body", std::string())}, {"at", m.value("at", 0LL)},
                               {"read", sent ? true : m.value("read", false)},
                               {"from", sent ? who(me.id) : who(m.value("from", std::string()))},
                               {"to", sent ? who(m.value("to", std::string())) : who(me.id)}});
        json r = okay(); r["messages"] = out; r["me"] = meJson(me); return r;
    }
    if (name == "message.read" || name == "message.delete") {
        json& box = str("box") == "sent" ? me.sent : me.inbox;
        if (!box.is_array()) return fail("That message isn't there any more.");
        for (size_t i = 0; i < box.size(); ++i) {
            if (box[i].value("id", std::string()) != str("id")) continue;
            if (name == "message.read") box[i]["read"] = true;
            else box.erase(box.begin() + (long)i);
            saveUsers();
            json r = okay(); r["me"] = meJson(me); return r;
        }
        return fail("That message isn't there any more.");
    }
    return fail("Unknown request.");
}
