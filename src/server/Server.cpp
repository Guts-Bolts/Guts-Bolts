#include "Server.h"
#include "../online/Protocol.h"
#include "../core/Account.h"
#include "../core/Paths.h"
#include "../net/Socket.h"
#include "ServerUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <set>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace ServerUtil;


GbServer::GbServer(Options opts) : m_opts(std::move(opts)) {
    if (!m_opts.official.empty()) Account::setOfficialId(m_opts.official);
}
GbServer::~GbServer() = default;

// ---------------------------------------------------------------------------
// Starting and running
// ---------------------------------------------------------------------------

bool GbServer::start(std::string& error) {
    std::error_code ec;
    fs::create_directories(m_opts.data / "files", ec);
    if (ec) { error = "Couldn't make the data folder " + m_opts.data.string() + ": " + ec.message(); return false; }
    load();
    addExampleGames();
    m_listener = std::make_unique<Net::Listener>();
    if (!m_listener->open(m_opts.port, error)) return false;
    m_running = true;
    return true;
}

void GbServer::runForever() {
    while (m_running) step(5);
}

void GbServer::step(int waitMs) {
    long long now = Online::unixNow();
    while (auto c = m_listener->accept()) {
        if (m_clients.size() >= 1024) continue;   // too busy: drop it
        auto cl = std::make_unique<Client>();
        cl->conn = std::move(c);
        cl->lastActive = cl->since = now;
        m_clients.push_back(std::move(cl));
    }
    for (size_t i = 0; i < m_clients.size(); ++i) {   // (relay requests can add clients while we go)
        Client* c = m_clients[i].get();
        if (!c->conn->poll() || c->closing) continue;
        std::string msg;
        while (!c->closing && c->conn->pop(msg)) {
            c->lastActive = now;
            if (c->mode == Client::Mode::Pipe) {           // game traffic: pass it on untouched
                if (c->peer) { c->peer->conn->send(msg); c->peer->lastActive = now; }
                continue;
            }
            json req = json::parse(msg, nullptr, false);
            if (c->mode == Client::Mode::HostControl || c->mode == Client::Mode::Pool) continue;   // pings keep it alive
            if (c->mode == Client::Mode::PendingJoin) continue;   // waiting for the host
            if (!req.is_object()) { c->conn->send(fail("That wasn't a proper request.").dump()); continue; }
            if (req.value("t", std::string()) == "accept") { relayAccept(*c, req.value("ticket", std::string())); continue; }
            if (req.value("op", std::string()).rfind("relay.", 0) == 0) { relayRequest(*c, req); continue; }
            json reply = handle(req);
            if (req.contains("id")) reply["id"] = req["id"];   // lets clients match replies
            c->conn->send(reply.dump());
        }
    }
    for (auto& c : m_clients) if (c->conn->pendingBytes()) c->conn->poll();   // send what we queued straight away
    relayStep(now);
    dropClients(now);
    // Forget old nonces.
    for (auto it = m_seenNonces.begin(); it != m_seenNonces.end();)
        it = now - it->second > 2 * Online::kMaxClockSkew ? m_seenNonces.erase(it) : std::next(it);
    std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
}

// ---------------------------------------------------------------------------
// Checking requests
// ---------------------------------------------------------------------------

json GbServer::handle(const json& req) {
    User* me = nullptr;
    json bad = checkRequest(req, me);
    if (!bad.is_null()) return bad;
    json args = req.contains("args") && req["args"].is_object() ? req["args"] : json::object();
    try {
        return op(req.value("op", std::string()), *me, args);
    } catch (const std::exception& e) {
        return fail(std::string("The server hit a problem: ") + e.what());
    }
}

json GbServer::checkRequest(const json& req, User*& out) {
    std::string opName = req.value("op", std::string());
    std::string account = lower(req.value("account", std::string()));
    std::string nonce = req.value("nonce", std::string());
    long long   time = req.value("time", 0LL);
    json        args = req.contains("args") && req["args"].is_object() ? req["args"] : json::object();

    if (opName.empty() || opName.size() > 40) return fail("Unknown request.");
    if (!isHex(account, 64, 64)) return fail("Missing account.");
    if (!isHex(nonce, 8, 64)) return fail("Missing nonce.");
    long long now = Online::unixNow();
    if (std::llabs(now - time) > Online::kMaxClockSkew)
        return fail("Your computer's clock is too far off from the server's. Fix the date and time and try again.");
    if (!Account::verify(account, Online::requestText(opName, account, time, nonce, args), req.value("sig", std::string())))
        return fail("That request wasn't signed by its account.");
    std::string key = account + nonce;
    if (m_seenNonces.count(key)) return fail("That request was already sent once.");
    m_seenNonces[key] = now;

    User& me = user(account);
    me.lastSeen = now;
    if (me.banned && me.bannedUntil > 0 && now >= me.bannedUntil) {   // a timed ban is over
        me.banned = false;
        me.banReason.clear(); me.banNote.clear();
        me.bannedAt = me.bannedUntil = 0;
        saveUsers();
    }
    if (me.banned && opName != "hello") return fail(Online::banMessage(me.banReason, me.banNote));
    // Everything else needs a signed-up account (hello just says who we are),
    // except looking around: visitors to the website can browse before signing up.
    // Guests can also play: download games, find and join servers (they can't chat in games).
    static const std::set<std::string> kLookOnly = {"forum.boards", "forum.list", "forum.thread", "pass.list", "pass.owned", "list", "asset.info", "profile", "profile.inventory", "people.list", "users.search", "groups.list", "groups.get",
                                                    "servers.list", "stats", "thumb.get", "updates.list", "comments.list",
                                                    "get", "servers.play", "relay.host", "relay.join", "product.pending", "product.grant"};
    if (me.userId == 0 && opName != "hello" && opName != "ping" && opName.rfind("account.", 0) != 0 &&
        !kLookOnly.count(opName) && opName != "support.send" && opName != "support.mine")
        return fail("Sign up or log in first.");
    out = &me;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Accounts
// ---------------------------------------------------------------------------

GbServer::User* GbServer::findUser(const std::string& id) {
    auto it = m_users.find(lower(id));
    return it == m_users.end() ? nullptr : &it->second;
}

// The bell (worker/server.js has the same): tell someone something happened. kind says what
// (the apps pick an icon and a page from it), about is what it's about. Saved with the account.
void GbServer::notify(User* u, const std::string& kind, const std::string& text, const std::string& about) {
    if (!u || u->userId == 0) return;
    if (!u->notes.is_array()) u->notes = json::array();
    u->notes.insert(u->notes.begin(), json{{"id", Account::randomHex(5)}, {"kind", kind}, {"text", text}, {"about", about},
                                           {"at", Online::unixNow()}, {"read", false}});
    if (u->notes.size() > 50) u->notes.erase(u->notes.begin() + 50, u->notes.end());
    saveUsers();
}

GbServer::User& GbServer::user(const std::string& id) {
    auto it = m_users.find(id);
    if (it != m_users.end()) return it->second;
    User& u = m_users[id];
    u.id = id;
    u.created = Online::unixNow();
    claimOfficial(u);   // (the welcome Bolts come with signing up)
    log("new account " + id.substr(0, 8));
    return u;
}

long long GbServer::balance(const User& u) const {
    long long b = 0;
    for (const Entry& e : u.ledger) b += e.amount;
    return b;
}

bool GbServer::hasRef(const User& u, const std::string& ref) const {
    for (const Entry& e : u.ledger) if (e.ref == ref) return true;
    return false;
}

void GbServer::add(User& u, long long amount, const std::string& reason, const std::string& ref) {
    u.ledger.push_back({amount, reason, Online::unixNow(), ref});
    if (u.ledger.size() > 2000) {   // keep the history short, but keep the balance
        long long old = 0;
        size_t cut = u.ledger.size() - 1500;
        for (size_t i = 0; i < cut; ++i) old += u.ledger[i].amount;
        u.ledger.erase(u.ledger.begin(), u.ledger.begin() + (long)cut);
        u.ledger.insert(u.ledger.begin(), Entry{old, "Earlier history", 0, "carry"});
    }
    saveUsers();
}

bool GbServer::isOfficial(const User& u) const { return Account::isOfficial(u.id); }

bool GbServer::isStaff(const User& u) const {
    if (isOfficial(u)) return true;
    auto it = u.grants.find("staff");
    return it != u.grants.end() && Online::grantValid(Account::officialId(), "staff", u.id, it->second);
}

bool GbServer::isVerified(const User& u) const {
    if (isOfficial(u)) return true;
    auto it = u.grants.find("verified");
    return it != u.grants.end() && Online::grantValid(Account::officialId(), "verified", u.id, it->second);
}

json GbServer::publicUser(const User& u) const {
    return {{"id", u.id}, {"name", u.name}, {"username", u.username}, {"userId", u.userId},
            {"verified", isVerified(u)}, {"staff", isStaff(u)},
            {"official", isOfficial(u)}, {"created", u.created}, {"banned", u.banned},
            {"banReason", u.banned ? u.banReason : std::string()}};
}

json GbServer::meJson(const User& u) const {
    json j = publicUser(u);
    j["bolts"] = balance(u);
    j["hasPassword"] = !u.keyBlob.empty();   // can log in on other devices
    j["authApp"] = !u.totpSecret.empty();    // logging in needs an authenticator-app code
    j["privacy"] = {{"status", u.privacyStatus}, {"join", u.privacyJoin}, {"messages", u.privacyMessages}, {"inventory", u.privacyInventory}};
    long long unread = 0;
    if (u.inbox.is_array()) for (const json& m : u.inbox) if (!m.value("read", false)) ++unread;
    j["unreadMessages"] = unread;
    long long unreadNotes = 0;
    if (u.notes.is_array()) for (const json& n : u.notes) if (!n.value("read", false)) ++unreadNotes;
    j["unreadNotes"] = unreadNotes;
    json gear = json::array();
    for (const auto& g : u.gear) if (u.owned.count(g)) gear.push_back(g);
    j["gear"] = gear;
    json g = json::array();
    for (const auto& [k, s] : u.grants) g.push_back({k, s});
    j["grants"] = g;
    std::string today = Online::utcDay(Online::unixNow());
    j["canDaily"] = !hasRef(u, "daily:" + today);
    j["playEarnedToday"] = u.playDay == today ? u.playEarned : 0;
    j["uploadsLeft"] = isVerified(u) ? -1
                     : Online::kDailyUploadsUnverified - (u.uploadDay == today ? u.uploadsToday : 0);
    j["owned"] = json(u.owned);
    j["avatar"] = u.avatar;
    // Banned: what for and until when (the app and the site show a ban screen).
    if (u.banned) {
        const char* title = Online::banReasonTitle(u.banReason);
        j["ban"] = {{"reason", u.banReason}, {"title", title ? title : "Breaking the rules"}, {"note", u.banNote},
                    {"at", u.bannedAt}, {"until", u.bannedUntil}};
    } else {
        j["ban"] = nullptr;
    }
    // Staff warnings not seen yet (shown once, until "I understand").
    json warn = json::array();
    if (u.warnings.is_array())
        for (const json& w : u.warnings) {
            if (!w.is_object() || w.value("seen", false)) continue;
            const char* title = Online::banReasonTitle(w.value("reason", std::string()));
            warn.push_back({{"id", w.value("id", std::string())}, {"reason", w.value("reason", std::string())},
                            {"title", title ? title : "Breaking the rules"}, {"note", w.value("note", std::string())},
                            {"at", w.value("at", 0LL)}});
        }
    j["warnings"] = warn;
    j["warningCount"] = u.warnings.is_array() ? u.warnings.size() : 0;
    return j;
}

 // Upload review (worker/server.js has the same): new decals, sounds and T-shirts from creators
// who aren't Verified wait for staff (meta "review": "pending" or "rejected"). Until then only
// the creator and staff can see them.
static bool reviewedKind(const std::string& kind) { return kind == "decal" || kind == "audio" || kind == "tshirt"; }
static std::string reviewOf(const nlohmann::json& meta) { return meta.is_object() ? meta.value("review", std::string()) : std::string(); }

bool GbServer::canSee(const Asset& a, const User& me) const {
    if (!reviewOf(a.meta).empty() && a.creator != me.id && !isStaff(me)) return false;
    return !Online::hasAccess(a.kind) || a.meta.value("access", std::string("public")) != "private" || a.creator == me.id || isStaff(me);
}

json GbServer::uploadsToReview() const {
    std::vector<const Asset*> list;
    for (const auto& [id, a] : m_assets) if (reviewOf(a.meta) == "pending") list.push_back(&a);
    auto when = [](const Asset* a) { return std::max(a->created, a->meta.value("updated", 0LL)); };
    std::sort(list.begin(), list.end(), [&](const Asset* x, const Asset* y) { return when(x) < when(y); });   // oldest first
    json out = json::array();
    for (size_t i = 0; i < list.size() && i < 50; ++i) out.push_back(publicAsset(*list[i]));
    return out;
}

json GbServer::publicAsset(const Asset& a) const {
    json j = {{"id", a.id}, {"num", a.num}, {"kind", a.kind}, {"name", a.name}, {"description", a.description},
              {"creator", a.creator}, {"price", a.price}, {"created", a.created}, {"sales", a.sales},
              {"plays", a.plays}, {"size", a.size}, {"meta", a.meta}, {"thumb", a.thumb}};
    if (Online::hasAccess(a.kind)) j["access"] = a.meta.value("access", std::string("public"));
    if (a.kind == "game") {
        j["badges"] = a.badges; j["allowGear"] = a.meta.value("allowGear", false); j["favorites"] = a.meta.value("favorites", 0LL);
        j["comments"] = !a.meta.value("commentsOff", false); j["commentCount"] = a.comments.size();
        j["privatePrice"] = a.meta.value("privatePrice", 0LL);
        if (auto g = m_groups.find(a.meta.value("group", std::string())); g != m_groups.end())
            j["group"] = {{"id", g->second.id}, {"name", g->second.name}, {"color", g->second.color}};
        if (a.meta.value("featured", 0LL) > 0) j["featured"] = true;
    }
    auto it = m_users.find(a.creator);
    j["creatorName"] = it != m_users.end() ? it->second.name : "?";
    j["creatorVerified"] = it != m_users.end() && isVerified(it->second);
    j["creatorStaff"] = it != m_users.end() && isStaff(it->second);
    // Timed items (worker/server.js): off sale from offsaleAt on (0 = for sale for good).
    const long long off = a.meta.is_object() ? a.meta.value("offsaleAt", 0LL) : 0LL;
    j["offsaleAt"] = off;
    j["offsale"] = off > 0 && Online::unixNow() >= off;
    if (const std::string review = reviewOf(a.meta); !review.empty()) {
        j["review"] = review;
        j["reviewNote"] = a.meta.value("reviewNote", std::string());
    }
    return j;
}

// ---------------------------------------------------------------------------
// The requests themselves
// ---------------------------------------------------------------------------

json GbServer::op(const std::string& name, User& me, const json& args) {
    const long long now = Online::unixNow();
    const std::string today = Online::utcDay(now);
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto num = [&](const char* k) { return args.contains(k) && args[k].is_number() ? args[k].get<long long>() : 0LL; };

    // --- Account -----------------------------------------------------------
    if (name == "hello") {
        std::string n = Online::cleanText(str("name"), 20);
        if (me.userId > 0) n = me.username;   // signed up: your name is your username
        if (!n.empty()) {
            if ((Account::nameIsReserved(n) && !isOfficial(me)) || TextFilter::changes(n)) n = "Player";
            me.name = n;
        }
        // Badges this player got offline (with a code) come along for the ride.
        if (args.contains("grants") && args["grants"].is_array())
            for (const auto& g : args["grants"])
                if (g.is_array() && g.size() == 2 && g[0].is_string() && g[1].is_string() &&
                    Online::grantValid(Account::officialId(), g[0], me.id, g[1]))
                    me.grants[g[0].get<std::string>()] = g[1].get<std::string>();
        saveUsers();
        json r = okay();
        r["me"] = meJson(me);
        r["server"] = {{"name", m_opts.name}, {"protocol", Online::kProtocol}, {"official", Account::officialId()}};
        return r;
    }
    if (name == "people.list") {   // someone's friends / following / followers (worker/server.js)
        User* u = findPerson(str("user"));
        if (!u || u->userId == 0) return fail("There's no account with that ID on this server.");
        std::string which = str("which");
        if (which != "following" && which != "followers") which = "friends";
        const std::set<std::string>& ids = which == "following" ? u->following : which == "followers" ? u->followers : u->friends;
        std::vector<const User*> all;
        for (const std::string& id : ids)
            if (auto it = m_users.find(id); it != m_users.end() && it->second.userId > 0) all.push_back(&it->second);
        std::sort(all.begin(), all.end(), [](const User* a, const User* b) { return a->userId < b->userId; });
        const long long offset = std::max(0LL, num("offset"));
        const long long limit = args.contains("limit") ? std::clamp(num("limit"), 1LL, 100LL) : 60;
        json people = json::array();
        for (size_t i = (size_t)offset; i < all.size() && (long long)people.size() < limit; ++i) {
            json p = publicUser(*all[i]);
            p["avatar"] = all[i]->avatar;
            p["online"] = presence(me, *all[i])["online"];
            people.push_back(p);
        }
        json r = okay();
        r["user"] = publicUser(*u);
        r["which"] = which;
        r["total"] = all.size();
        r["people"] = people;
        return r;
    }
    if (name == "profile") {
        std::string want = str("id");
        User* u = !want.empty() && want.size() < 12 && std::all_of(want.begin(), want.end(), ::isdigit)
                      ? findUserId(std::atoll(want.c_str())) : findPerson(want);   // user number, account key or name
        if (!u || u->userId == 0) return fail("There's no account with that ID on this server.");
        json r = okay();
        r["user"] = publicUser(*u);
        r["user"]["badges"] = badgesOf(*u);
        r["user"]["avatar"] = u->avatar;
        json made = json::array();
        for (const auto& [id, a] : m_assets) if (a.creator == u->id) made.push_back(publicAsset(a));
        r["creations"] = made;
        json groups = json::array();
        for (const Group* g : groupsOf(u->id)) {
            json pg = publicGroup(*g);
            pg["role"] = rankIn(*g, u->id).value("name", std::string("Member"));
            groups.push_back(pg);
        }
        r["groups"] = groups;
        r["friendCount"] = u->friends.size();
        r["followerCount"] = u->followers.size();
        r["followingCount"] = u->following.size();
        r["isFollowing"] = me.following.count(u->id) > 0;
        r["blocked"] = std::find(me.blocked.begin(), me.blocked.end(), u->id) != me.blocked.end();
        r["friendship"] = u->id == me.id ? "self" : me.friends.count(u->id) ? "friends"
                        : me.friendOut.count(u->id) ? "sent" : me.friendIn.count(u->id) ? "received" : "none";
        // Like a Roblox profile: what they're wearing, some friends, visits to their games.
        // (friends get theirs too, so their little pictures are dressed)
        auto wornItems = [&](const User& who) {
            json out = json::array();
            if (who.avatar.is_object() && who.avatar.contains("wearing") && who.avatar["wearing"].is_array())
                for (const auto& id : who.avatar["wearing"])
                    if (id.is_string())
                        if (auto it = findAsset(id.get<std::string>()); it != m_assets.end()) out.push_back(publicAsset(it->second));
            return out;
        };
        r["wearing"] = wornItems(*u);
        json friends = json::array();
        for (const std::string& fid : u->friends) {
            if (friends.size() >= 9) break;
            auto it = m_users.find(fid);
            if (it == m_users.end()) continue;
            json f = publicUser(it->second);
            f["avatar"] = it->second.avatar;
            f["online"] = presence(me, it->second)["online"];
            f["wearing"] = wornItems(it->second);
            friends.push_back(f);
        }
        r["friends"] = friends;
        {
            json pr = presence(me, *u);
            r["online"] = pr["online"];
            r["playing"] = pr["playing"];
        }
        long long visits = 0;
        for (const auto& [id, a] : m_assets) if (a.creator == u->id && a.kind == "game") visits += a.plays;
        r["placeVisits"] = visits;
        // Game badges (made by creators, earned in games), newest first.
        json gameBadges = json::array();
        for (const json& e : u->gameBadges) {
            if (!e.is_array() || e.size() < 3) continue;
            auto g = findAsset(e[1].get<std::string>());
            if (g == m_assets.end()) continue;
            for (const json& b : g->second.badges)
                if (b.value("id", std::string()) == e[0].get<std::string>())
                {
                    json one = {{"id", b["id"]}, {"name", b["name"]}, {"description", b["description"]},
                                {"color", b["color"]}, {"game", g->first}, {"gameName", g->second.name}, {"earned", e[2]}};
                    gameBadges.insert(gameBadges.begin(), one);
                }
        }
        r["gameBadges"] = gameBadges;
        r["blurb"] = u->blurb;
        r["status"] = u->posts.is_array() && !u->posts.empty() ? u->posts[0] : json();
        r["playerBadges"] = playerBadgesOf(*u);
        r["allPlayerBadges"] = allPlayerBadges();
        {   // their inventory (the first few; profile.inventory has the rest), unless they hid it
            const bool canSee = seesInventory(me, *u);
            json owns = canSee ? inventoryOf(*u, "all") : json::array();
            json first = json::array();
            for (size_t i = 0; i < owns.size() && i < 12; ++i) first.push_back(owns[i]);
            r["inventory"] = {{"hidden", !canSee}, {"total", owns.size()}, {"items", first}};
        }
        return r;
    }
    if (name == "profile.inventory") {   // everything someone owns from the catalog, a page at a time (worker/server.js)
        std::string want = str("id");
        User* u = !want.empty() && want.size() < 12 && std::all_of(want.begin(), want.end(), ::isdigit)
                      ? findUserId(std::atoll(want.c_str())) : findPerson(want);
        if (!u || u->userId == 0) return fail("There's no account with that ID on this server.");
        json r = okay();
        r["user"] = publicUser(*u);
        if (!seesInventory(me, *u)) {
            r["hidden"] = true; r["cat"] = "all"; r["total"] = 0; r["counts"] = json::object(); r["items"] = json::array();
            return r;
        }
        static const char* kCats[] = {"all", "accessories", "clothing", "faces", "gear", "limited"};
        std::string cat = "all";
        json counts = json::object();
        for (const char* c : kCats) {
            counts[c] = inventoryOf(*u, c).size();
            if (str("cat") == c) cat = c;
        }
        const json list = inventoryOf(*u, cat);
        const long long offset = std::max(0LL, num("offset"));
        const long long limit = args.contains("limit") ? std::clamp(num("limit"), 1LL, 100LL) : 60;
        json items = json::array();
        for (size_t i = (size_t)offset; i < list.size() && (long long)items.size() < limit; ++i) items.push_back(list[i]);
        r["hidden"] = false; r["cat"] = cat; r["total"] = list.size(); r["counts"] = counts; r["items"] = items;
        return r;
    }
    if (name == "users.search") {
        // Anyone can look people up by name (or the start of their account ID).
        std::string q = lower(Online::cleanText(str("query"), 64));
        if (!q.empty() && q[0] == '#') q.erase(0, 1);            // "#12" = user 12
        if (!q.empty() && q[0] == '@') q.erase(0, 1);            // "@name" = username
        long long wantId = !q.empty() && q.size() < 12 && std::all_of(q.begin(), q.end(), ::isdigit) ? std::atoll(q.c_str()) : -1;
        // No name typed: show who's been around lately. Only signed-up accounts show up.
        std::vector<const User*> found;
        for (const auto& [id, u] : m_users)
            if (!u.banned && u.userId > 0 &&
                (q.empty() || u.userId == wantId || lower(u.name).find(q) != std::string::npos ||
                 lower(u.username).find(q) != std::string::npos))
                found.push_back(&u);
        // Exact matches first, then Verified people, then the most recently seen.
        std::sort(found.begin(), found.end(), [&](const User* a, const User* b) {
            bool ea = lower(a->username) == q || a->userId == wantId, eb = lower(b->username) == q || b->userId == wantId;
            if (ea != eb) return ea;
            bool va = isVerified(*a), vb = isVerified(*b);
            if (va != vb) return va;
            return a->lastSeen > b->lastSeen;
        });
        json list = json::array();
        for (const User* u : found) {
            if (list.size() >= 50) break;
            list.push_back(publicUser(*u));
        }
        json r = okay(); r["users"] = list; return r;
    }
    if (name.rfind("account.", 0) == 0) return accountOp(name, me, args);
    if (name.rfind("support.", 0) == 0) return supportOp(name, me, args);
    if (name.rfind("groups.", 0) == 0) return groupOp(name, me, args);
    if (name.rfind("forum.", 0) == 0) return forumOp(name, me, args);
    if (name.rfind("friends.", 0) == 0 || name.rfind("follow.", 0) == 0) return friendOp(name, me, args);
    if (name.rfind("servers.", 0) == 0) return serverOp(name, me, args);
    if (name.rfind("data.", 0) == 0) return dataOp(name, me, args);
    if (name.rfind("block.", 0) == 0 || name.rfind("report.", 0) == 0) return safetyOp(name, me, args);
    if (name.rfind("outfit.", 0) == 0 || name.rfind("message.", 0) == 0 || name.rfind("notes.", 0) == 0 || name == "game.favorite" || name == "games.mine" || name.rfind("comments.", 0) == 0 ||
        name == "profile.set" || name == "feed.list")
        return socialOp(name, me, args);
    if (name == "ping") {   // "I'm still here" (for friends' online dots); the answer keeps your account fresh
        json r = okay(); r["me"] = meJson(me); return r;
    }

    // --- Your look, and pictures of games --------------------------------------
    if (name == "avatar.set") {
        // Shared by the website and the apps. Colours are 0-255 whole numbers.
        const json a = args.contains("avatar") && args["avatar"].is_object() ? args["avatar"] : json();
        if (a.is_null()) return fail("That avatar looks wrong.");
        auto rgb = [&](const char* k, bool allowNone, json& out) {
            if (!a.contains(k) || !a[k].is_array() || a[k].size() != 3) return false;
            out = json::array();
            bool none = true;
            for (const auto& v : a[k]) { if (!v.is_number_integer()) return false; none = none && v.get<long long>() < 0; }
            for (const auto& v : a[k]) out.push_back(allowNone && none ? -1LL : std::clamp(v.get<long long>(), 0LL, 255LL));
            return true;
        };
        json av = json::object();
        for (const char* part : {"head", "torso", "leftArm", "rightArm", "leftLeg", "rightLeg"}) {
            json c;
            if (!rgb(part, false, c)) return fail("That avatar looks wrong.");
            av[part] = c;
        }
        av["hat"] = a.contains("hat") && a["hat"].is_number_integer() ? std::clamp(a["hat"].get<long long>(), 0LL, 3LL) : 0LL;
        json hc;
        av["hatColor"] = rgb("hatColor", true, hc) ? hc : json::array({-1, -1, -1});
        json wearing = json::array();
        if (a.contains("wearing") && a["wearing"].is_array())
            for (const auto& w : a["wearing"])
                if (w.is_string() && me.owned.count(w.get<std::string>()) && wearing.size() < 12) wearing.push_back(w);
        av["wearing"] = wearing;
        av["updated"] = now;
        me.avatar = av;
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "thumb.set") {
        // A picture of a game (Studio sends one when publishing): a PNG or JPG, up to 400 KB.
        auto it = findAsset(str("id"));
        if (it == m_assets.end()) return fail("That doesn't exist (any more).");
        Asset& a = it->second;
        if (a.creator != me.id && !isStaff(me)) return fail("You can only change pictures of your own things.");
        // Catalog items aren't given pictures (worker/server.js): they're drawn from the item itself.
        if (Online::isCatalogItem(a.kind)) return fail("Catalog items don't take pictures: they're shown as the item itself.");
        std::string data;
        if (!Online::base64Decode(str("data"), data)) return fail("The picture got scrambled. Try again.");
        bool png = data.size() > 8 && data.compare(0, 4, "\x89PNG") == 0;
        bool jpg = data.size() > 3 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xD8;
        if (!png && !jpg) return fail("Pictures must be .png or .jpg.");
        if (data.size() > 400 * 1024) return fail("That picture is too big (the most is 400 KB).");
        if (!writeFile(m_opts.data / "files" / ("thumb-" + a.id), data)) return fail("The server couldn't save that picture.");
        a.thumb = now;
        saveAssets();
        json r = okay(); r["asset"] = publicAsset(a); return r;
    }
    if (name == "thumb.get") {
        auto it = findAsset(str("id"));
        std::string data;
        if (it == m_assets.end() || !it->second.thumb || !readFile(m_opts.data / "files" / ("thumb-" + it->first), data))
            return fail("No picture.");
        json r = okay(); r["data"] = Online::base64Encode(data); return r;
    }

    // --- Bolts --------------------------------------------------------------
    if (name == "bolts.history") {
        json h = json::array();
        size_t from = me.ledger.size() > 100 ? me.ledger.size() - 100 : 0;
        for (size_t i = from; i < me.ledger.size(); ++i)
            h.push_back({{"amount", me.ledger[i].amount}, {"reason", me.ledger[i].reason}, {"time", me.ledger[i].time}});
        json r = okay();
        r["history"] = h;
        r["me"] = meJson(me);
        return r;
    }
    if (name == "bolts.daily") {
        if (hasRef(me, "daily:" + today)) return fail("You already got today's Bolts. Come back tomorrow!");
        add(me, 25, "Daily reward", "daily:" + today);
        json r = okay(); r["me"] = meJson(me); r["got"] = 25; return r;
    }
    if (name == "bolts.play") {
        // The app asks every 5 minutes of play; the server checks the timing and the daily cap.
        if (now - me.lastPlay < 280) return fail("Keep playing!");
        if (me.playDay != today) { me.playDay = today; me.playEarned = 0; }
        me.lastPlay = now;
        if (me.playEarned >= 50) { saveUsers(); return fail("That's all the Bolts from playing for today."); }
        me.playEarned += 5;
        add(me, 5, "Playing games", "play:" + today);
        json r = okay(); r["me"] = meJson(me); r["got"] = 5; return r;
    }
    if (name == "bolts.redeem") {
        // BOLTS-<amount>-<nonce>-<signature>, made by the official account for this account.
        std::string code;
        for (char c : str("code")) if (!std::isspace((unsigned char)c)) code += c;
        size_t a = code.find('-'), b = a == std::string::npos ? a : code.find('-', a + 1),
               c = b == std::string::npos ? b : code.find('-', b + 1);
        if (code.rfind("BOLTS-", 0) != 0 || c == std::string::npos) return fail("That doesn't look like a Bolts code.");
        long long amount = 0;
        try { amount = std::stoll(code.substr(a + 1, b - a - 1)); } catch (...) {}
        std::string cn = code.substr(b + 1, c - b - 1);
        if (amount <= 0 || !Account::verify(Account::officialId(), "gb-bolts:" + std::to_string(amount) + ":" + me.id + ":" + cn,
                                            code.substr(c + 1)))
            return fail("That code isn't valid for your account.");
        if (hasRef(me, "code:" + cn)) return fail("You already used that code.");
        add(me, amount, "Bolts from Guts&Bolts staff", "code:" + cn);
        json r = okay(); r["me"] = meJson(me); r["got"] = amount; return r;
    }

    // --- Staff ---------------------------------------------------------------
    if (name.rfind("admin.", 0) == 0) {
        if (!isStaff(me)) return fail("Only staff can do that.");
        // (a number sent as a number works too)
        std::string toArg = args.contains("to") && args["to"].is_number_integer() ? std::to_string(args["to"].get<long long>()) : str("to");
        User* to = findPerson(toArg);
        // Staff can verify (or give Bolts to) someone who hasn't visited this server yet.
        if (!to && name != "admin.find" && isHex(lower(str("to")), 64, 64)) to = &user(lower(str("to")));
        if (name == "admin.find") {
            std::string q = lower(Online::cleanText(str("query"), 64));
            json list = json::array();
            for (const auto& [id, u] : m_users) {
                if (list.size() >= 40) break;
                const std::string digits = !q.empty() && q[0] == '#' ? q.substr(1) : q;
                const bool isNum = !digits.empty() && digits.size() <= 11 && std::all_of(digits.begin(), digits.end(), ::isdigit);
                if (q.empty() || (isNum && u.userId == std::atoll(digits.c_str())) ||
                    lower(u.name).find(q) != std::string::npos || id.rfind(q, 0) == 0)
                    list.push_back(publicUser(u));
            }
            json r = okay(); r["users"] = list; return r;
        }
        if (name == "admin.uploads") { json r = okay(); r["uploads"] = uploadsToReview(); return r; }
        if (name == "admin.review") {
            auto it = findAsset(str("id"));
            if (it == m_assets.end() || reviewOf(it->second.meta).empty()) return fail("That isn't waiting for a check any more.");
            Asset& a = it->second;
            const bool ok = args.value("ok", false) == true;
            if (ok) { a.meta.erase("review"); a.meta.erase("reviewNote"); }
            else { a.meta["review"] = "rejected"; a.meta["reviewNote"] = Online::cleanText(str("note"), 200); }
            const std::string note = a.meta.value("reviewNote", std::string());
            notify(findUser(a.creator), "upload", ok ? a.name + " passed the staff check. Everyone can see it now."
                   : a.name + " didn't pass the staff check." + (note.empty() ? "" : " Staff said: \"" + note + "\""), a.id);
            a.meta["reviewedBy"] = me.id;
            staffDid(me, "review", std::string(ok ? "Passed " : "Rejected ") + Online::kindTitle(a.kind) + " \"" + a.name + "\"" +
                     (note.empty() ? "" : " (\"" + note + "\")"), a.creator);
            saveAssets();
            json r = okay(); r["uploads"] = uploadsToReview(); return r;
        }
        if (name == "admin.reports") {
            json r = okay(); r["reports"] = reportsJson(str("status") == "closed" ? "closed" : "open"); return r;
        }
        if (name == "admin.closeReport") return closeReport(me, str("id"), str("outcome"));
        if (name == "admin.tickets") {
            json r = okay(); r["tickets"] = ticketsJson(str("status") == "closed" ? "closed" : "open"); return r;
        }
        if (name == "admin.replyTicket" || name == "admin.ticketReset" || name == "admin.closeTicket") return ticketAdminOp(name, me, args);
        if (name == "admin.mailStatus") {   // (the Staff page's Email box)
            json r = okay();
            r["mail"] = {{"service", ""}, {"from", ""}, {"on", false}, {"problem", "This server can't send email."}, {"last", nullptr}};
            return r;
        }
        if (name == "admin.mailTest") return fail("This server can't send email.");
        // Featured games (worker/server.js has the same): staff pick games for the Featured row.
        if (name == "admin.feature") {
            auto it = findAsset(str("id"));
            if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
            Asset& a = it->second;
            const bool on = args.value("on", true) != false;
            if (on && a.meta.value("access", std::string("public")) != "public") return fail("Only public games can be featured.");
            if (on && a.meta.value("featured", 0LL) <= 0) {
                notify(findUser(a.creator), "featured", "Your game " + a.name + " is featured! Everyone sees it on the home page now.", a.id);
                a.meta["featured"] = Online::unixNow();
            } else if (!on) a.meta.erase("featured");
            staffDid(me, "feature", std::string(on ? "Featured" : "Unfeatured") + " the game \"" + a.name + "\"", a.creator);
            saveAssets();
            json r = okay(); r["asset"] = publicAsset(a); return r;
        }
        if (name == "admin.log") {   // the newest 200, or just what one person did or had done to them
            std::string who;
            if (!str("user").empty()) {
                User* u = findPerson(str("user"));
                if (!u) return fail("There's no account with that ID on this server.");
                who = u->id;
            }
            json r = okay(); r["log"] = staffLogJson(who); return r;
        }
        if (!to) return fail("There's no account with that ID on this server.");
        if (name == "admin.grant") {
            // The signed badge comes from the app (made with the staff member's own key).
            std::string key = str("key"), sig = str("sig");
            if (!Online::grantValid(Account::officialId(), key, to->id, sig)) return fail("That badge signature isn't valid.");
            to->grants[key] = sig;
            saveUsers();
            staffDid(me, "badge", "Gave " + to->name + " the " + key + " badge", to->id);
            json r = okay(); r["user"] = publicUser(*to); return r;
        }
        if (name == "admin.revoke") {
            std::string key = str("key");
            if (key != "verified" && !isOfficial(me)) return fail("Only the official account can take that badge away.");
            to->grants.erase(key);
            saveUsers();
            staffDid(me, "unbadge", "Took the " + key + " badge from " + to->name, to->id);
            json r = okay(); r["user"] = publicUser(*to); return r;
        }
        if (!isOfficial(me)) return fail("Only the official Guts account can do that.");
        if (name == "admin.giveBolts") {
            long long amount = num("amount");
            if (amount == 0 || std::llabs(amount) > 10000000) return fail("Pick an amount between 1 and 10,000,000.");
            std::string why = Online::cleanText(str("reason"), 80);
            add(*to, amount, why.empty() ? (amount > 0 ? "Bolts from Guts&Bolts staff" : "Taken by staff") : why,
                "gift:" + Account::randomHex(6));
            staffDid(me, "bolts", (amount > 0 ? "Gave " + to->name + " " + std::to_string(amount) : "Took " + std::to_string(-amount) + " from " + to->name) +
                     " Bolts" + (why.empty() ? "" : " (\"" + why + "\")"), to->id);
            json r = okay(); r["user"] = publicUser(*to); r["bolts"] = balance(*to); return r;
        }
        if (name == "admin.ban") {
            if (isOfficial(*to)) return fail("You can't ban yourself.");
            const bool on = args.value("on", true);
            if (on) {
                std::string reason = str("reason");
                if (!Online::banReasonTitle(reason)) return fail("Pick a reason for the ban.");
                to->banReason = reason;
                to->banNote = Online::cleanText(str("note"), 200);
                to->bannedAt = Online::unixNow();
                const long long days = std::clamp(num("days"), 0LL, 3650LL);   // 0 = for good
                to->bannedUntil = days > 0 ? to->bannedAt + days * 86400 : 0;
            } else {
                to->banReason.clear();
                to->banNote.clear();
                to->bannedAt = to->bannedUntil = 0;
            }
            to->banned = on;
            if (on) {
                const char* why = Online::banReasonTitle(to->banReason);
                staffDid(me, "ban", "Banned " + to->name + " (" + (why ? why : to->banReason) + ", " +
                         (to->bannedUntil ? std::to_string(std::clamp(num("days"), 0LL, 3650LL)) + " days" : std::string("for good")) + ")" +
                         (to->banNote.empty() ? "" : ": \"" + to->banNote + "\""), to->id);
            } else staffDid(me, "unban", "Unbanned " + to->name, to->id);
            saveUsers();
            json r = okay(); r["user"] = publicUser(*to); return r;
        }
        if (name == "admin.warn") {
            // A warning: they see it (with the reason) next time they open the app or the site.
            std::string reason = str("reason");
            if (!Online::banReasonTitle(reason)) return fail("Pick a reason for the warning.");
            if (!to->warnings.is_array()) to->warnings = json::array();
            to->warnings.push_back({{"id", Account::randomHex(4)}, {"reason", reason},
                                    {"note", Online::cleanText(str("note"), 200)}, {"at", Online::unixNow()}, {"seen", false}});
            while (to->warnings.size() > 30) to->warnings.erase(to->warnings.begin());
            staffDid(me, "warn", "Warned " + to->name + " (" + Online::banReasonTitle(reason) + ")", to->id);
            saveUsers();
            json r = okay(); r["user"] = publicUser(*to); return r;
        }
        return fail("Unknown staff request.");
    }

    // --- Uploads, the catalog and games -------------------------------------------------
    if (name == "upload") {
        std::string kind = str("kind");
        if (!Online::validKind(kind)) return fail("You can't upload that kind of thing.");
        std::string title = say(str("name"), 50);
        if (title.empty()) return fail("Give it a name.");
        std::string desc = say(str("description"), 1000, true);
        const bool verified = isVerified(me);
        if (kind == "gear" && !isStaff(me)) return fail("Only Guts&Bolts staff can make gear.");
        if (Online::isAccessory(kind) && !verified && !isStaff(me))
            return fail("Only Verified creators can make hats and accessories. Shirts and pants are open to everyone!");
        long long price = std::clamp(num("price"), 0LL, 1000000LL);
        if (kind == "game" || Online::alwaysFree(kind)) price = 0;   // games, decals and audio are free
        if (price > 0 && !verified) return fail("Only Verified creators can sell things. Upload it for free, or get Verified!");
        if (!verified) {
            if (me.uploadDay != today) { me.uploadDay = today; me.uploadsToday = 0; }
            if (me.uploadsToday >= Online::kDailyUploadsUnverified)
                return fail("You've uploaded " + std::to_string(Online::kDailyUploadsUnverified) +
                            " things today. Come back tomorrow (Verified creators have no limit).");
        }
        std::string data;
        if (!Online::base64Decode(str("data"), data)) return fail("The upload got scrambled. Try again.");
        if (data.size() > Online::maxSize(kind))
            return fail("That's too big (the most is " + std::to_string(Online::maxSize(kind) / 1024) + " KB).");
        json meta = args.contains("meta") && args["meta"].is_object() ? args["meta"] : json::object();
        if (meta.dump().size() > 4096) return fail("Too much extra information.");
        meta.erase("image");
        meta.erase("model");
        meta.erase("allowGear");
        meta.erase("review");
        meta.erase("reviewNote");
        meta.erase("color");   // items don't have a colour: hats are Studio models, clothes are template pictures
        meta.erase("style");
        if (reviewedKind(kind) && !verified && !isStaff(me)) meta["review"] = "pending";
        if (kind == "gear")
            if (std::string problem = Online::gearProblem(data); !problem.empty()) return fail(problem);
        if (kind == "animation") {   // from Studio's Animation Editor (worker/server.js has the same)
            json anim = json::parse(data, nullptr, false);
            if (!anim.is_object() || anim.value("format", std::string()) != "gbanim" || !anim.contains("clip") || !anim["clip"].is_object())
                return fail("That isn't a Guts&Bolts animation.");
        }
        meta.erase("access");
        if (Online::hasAccess(kind)) meta["access"] = str("access") == "private" ? "private" : "public";
        if (Online::isAccessory(kind)) {   // made in Studio's Accessory window
            if (data.empty()) return fail("Make hats and accessories in Studio's Accessory window, then upload them from there.");
            json acc = json::parse(data, nullptr, false);
            if (!acc.is_object() || acc.value("format", std::string()) != "gbaccessory")
                return fail("That isn't a Guts&Bolts accessory. Make it in Studio's Accessory window.");
            meta["model"] = true;
        }
        if (kind == "face") {
            if (!isOfficial(me)) return fail("Only Guts can make faces.");
            if (data.size() < 8 || data.compare(1, 3, "PNG") != 0) return fail("Faces must be .png pictures.");
            meta["image"] = true;
            meta["ext"] = "png";
        }
        if (kind == "tshirt") {   // any picture, worn flat on the front of the torso
            bool png = data.size() > 24 && data.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0;
            bool jpg = data.size() > 3 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xD8 &&
                       (unsigned char)data[2] == 0xFF;
            if (!png && !jpg) return fail("T-shirts must be .png or .jpg pictures.");
            if (png) {
                auto u = [&](size_t i) { return (uint32_t)(unsigned char)data[i]; };
                uint32_t w = u(16) << 24 | u(17) << 16 | u(18) << 8 | u(19), h = u(20) << 24 | u(21) << 16 | u(22) << 8 | u(23);
                if (w > 1024 || h > 1024) return fail("T-shirt pictures can be at most 1024 x 1024.");
            }
            meta["image"] = true;
            meta["ext"] = png ? "png" : "jpg";
        }
        if (kind == "shirt" || kind == "pants") {   // a clothing template picture
            if (data.empty()) return fail("Pick a picture made from the " + kind + " template.");
            auto u = [&](size_t i) { return (uint32_t)(unsigned char)data[i]; };
            if (data.size() < 24 || data.compare(1, 3, "PNG") != 0)
                return fail("Clothing pictures must be .png files made from the template.");
            uint32_t w = u(16) << 24 | u(17) << 16 | u(18) << 8 | u(19), h = u(20) << 24 | u(21) << 16 | u(22) << 8 | u(23);
            if (w != 585 || h != 559) return fail("Clothing pictures must be 585 x 559 (the template's size).");
            meta["image"] = true;
            meta["ext"] = "png";
        }
        if (kind == "audio") {
            std::string ext = lower(meta.value("ext", std::string()));
            if (ext != "mp3" && ext != "wav" && ext != "ogg" && ext != "flac") return fail("Audio must be .mp3, .wav, .ogg or .flac.");
            if (data.empty()) return fail("That audio file is empty.");
        }
        if (kind == "decal") {
            // Only real pictures: check the first bytes, not just the name.
            bool png = data.size() > 8 && data.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0;
            bool jpg = data.size() > 3 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xD8 &&
                       (unsigned char)data[2] == 0xFF;
            if (!png && !jpg) return fail("Decals must be .png or .jpg pictures.");
            meta["ext"] = png ? "png" : "jpg";
        }
        if (kind == "game" && !json::accept(data)) return fail("That isn't a Guts&Bolts game file.");
        if (kind == "plugin" && data.empty()) return fail("That plugin is empty.");
        const long long fee = verified ? 0 : Online::uploadFee(kind);
        if (fee > 0 && balance(me) < fee)
            return fail("Uploading costs " + std::to_string(fee) + " Bolts, and you have " + std::to_string(balance(me)) +
                        ". (It's free for Verified creators.)");

        Asset a;
        a.num = m_nextAssetNum++;
        a.id = std::to_string(a.num);
        m_assetNums[a.id] = a.id;
        saveIds();
        a.kind = kind; a.name = title; a.description = desc; a.creator = me.id;
        a.price = price; a.created = now; a.size = data.size(); a.meta = meta;
        if (!writeFile(blobPath(a.id), data)) return fail("The server couldn't save that file.");
        m_assets[a.id] = a;
        saveAssets();
        me.owned.insert(a.id);
        if (!verified) me.uploadsToday++;
        if (fee > 0) add(me, -fee, "Upload fee: " + title, "upload:" + a.id);
        else saveUsers();
        log(me.name + " uploaded " + kind + " \"" + title + "\" (" + std::to_string(data.size()) + " bytes)");
        json r = okay(); r["asset"] = publicAsset(a); r["me"] = meJson(me); r["fee"] = fee; return r;
    }
    // --- Game passes: perks a game's creator sells for Bolts (worker/server.js has the same).
    if (name == "pass.create" || name == "pass.edit") {
        const bool creating = name == "pass.create";
        Asset* pass = nullptr;
        if (!creating) {
            auto it = findAsset(str("id"));
            if (it == m_assets.end() || (it->second.kind != "gamepass" && it->second.kind != "devproduct"))
                return fail("That pass doesn't exist (any more).");
            pass = &it->second;
        }
        // Developer products ("devproduct"): like passes, but bought again and again inside the game.
        const bool product = creating ? args.value("product", false) == true : pass->kind == "devproduct";
        const std::string kindName = product ? "devproduct" : "gamepass", what = product ? "product" : "pass",
                          whats = product ? "products" : "passes";
        auto git = findAsset(creating ? str("game") : pass->meta.value("game", std::string()));
        if (git == m_assets.end() || git->second.kind != "game") return fail("That game doesn't exist (any more).");
        const Asset& g = git->second;
        if (g.creator != me.id && !isStaff(me)) return fail("Only the game's creator can make or change its passes.");
        if (creating) {
            int count = 0;
            for (const auto& [id, x] : m_assets) count += x.kind == kindName && x.meta.value("game", std::string()) == g.id;
            if (count >= Online::kMostPasses) return fail("A game can have at most " + std::to_string(Online::kMostPasses) + " " + whats + ".");
        }
        const std::string title = creating || args.contains("name") ? say(str("name"), 50) : pass->name;
        if (title.empty()) return fail("Give the " + what + " a name.");
        const long long price = creating || args.contains("price") ? std::clamp(num("price"), 0LL, 1000000LL) : pass->price;
        if (price > 0 && !isVerified(me) && !isStaff(me))
            return fail("Only Verified creators can sell " + whats + ". Make it free for now, or get Verified!");
        std::string icon;
        if (!str("icon").empty()) {
            if (!Online::base64Decode(str("icon"), icon)) return fail("The picture got scrambled. Try again.");
            const bool png = icon.size() > 8 && icon.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0;
            const bool jpg = icon.size() > 3 && (unsigned char)icon[0] == 0xFF && (unsigned char)icon[1] == 0xD8;
            if (!png && !jpg) return fail("Pass pictures must be .png or .jpg.");
            if (icon.size() > 400 * 1024) return fail("That picture is too big (400 KB at most).");
        }
        const long long t = Online::unixNow();
        if (creating) {
            Asset a;
            a.num = m_nextAssetNum++;
            a.id = std::to_string(a.num);
            m_assetNums[a.id] = a.id;
            saveIds();
            a.kind = kindName; a.creator = g.creator; a.created = t;
            a.meta = {{"game", g.id}};
            writeFile(blobPath(a.id), "");
            m_assets[a.id] = a;
            pass = &m_assets[a.id];
            if (User* owner = findUser(g.creator); owner && !product) { owner->owned.insert(a.id); saveUsers(); }   // creators have their own passes
        }
        pass->name = title;
        pass->price = price;
        if (args.contains("description")) pass->description = say(str("description"), 1000, true);
        if (args.contains("offsale")) pass->meta["offsaleAt"] = args["offsale"] == true ? 1LL : 0LL;
        if (!icon.empty() && writeFile(m_opts.data / "files" / ("thumb-" + pass->id), icon)) pass->thumb = t;
        saveAssets();
        json r = okay(); r["asset"] = publicAsset(*pass); return r;
    }
    if (name == "pass.list") {   // (products: true lists the game's developer products instead)
        const std::string game = str("game"), kindName = args.value("products", false) == true ? "devproduct" : "gamepass";
        std::vector<const Asset*> found;
        for (const auto& [id, x] : m_assets)
            if (x.kind == kindName && x.meta.value("game", std::string()) == game) found.push_back(&x);
        std::sort(found.begin(), found.end(), [](const Asset* x, const Asset* y) { return x->created < y->created; });
        json list = json::array();
        for (const Asset* x : found) { json j = publicAsset(*x); j["owned"] = me.owned.count(x->id) > 0; list.push_back(j); }
        json r = okay(); r["passes"] = list; return r;
    }
    if (name == "pass.owned") {   // which of a game's passes someone owns (UserOwnsGamePassAsync); a user number or account id
        const std::string game = str("game");
        User* who = args.contains("user") && args["user"].is_number() ? findUserId(args["user"].get<long long>()) : findUser(str("user"));
        json list = json::array();
        if (who)
            for (const auto& [id, x] : m_assets)
                if (x.kind == "gamepass" && x.meta.value("game", std::string()) == game && who->owned.count(x.id))
                    list.push_back({{"id", x.id}, {"num", x.num}});
        json r = okay(); r["passes"] = list; return r;
    }
    // Developer products (worker/server.js has the same): buying one makes a receipt; the game's
    // scripts (ProcessReceipt, on whoever runs the server) ask for the buyer's waiting receipts
    // and say when each one was handed out.
    if (name == "product.buy") {
        if (me.userId == 0) return fail("Sign up to buy things.");
        auto it = findAsset(str("id"));
        if (it == m_assets.end() || it->second.kind != "devproduct") return fail("That product doesn't exist (any more).");
        Asset& a = it->second;
        if (const long long off = a.meta.value("offsaleAt", 0LL); off > 0 && now >= off) return fail("That isn't for sale right now.");
        if (balance(me) < a.price) return fail("You need " + std::to_string(a.price - balance(me)) + " more Bolts for that.");
        const std::string rid = "r-" + Account::randomHex(6);
        if (a.price > 0) {
            add(me, -a.price, "Bought " + a.name, "product:" + rid);
            paySeller(a, me, a.price * Online::kCreatorSharePercent / 100, "productsale:" + rid);
        }
        if (!me.receipts.is_array()) me.receipts = json::array();
        me.receipts.insert(me.receipts.begin(), json{{"id", rid}, {"product", a.id}, {"game", a.meta.value("game", std::string())},
                                                     {"price", a.price}, {"at", now}, {"granted", false}});
        if (me.receipts.size() > 200) me.receipts.erase(me.receipts.begin() + 200, me.receipts.end());
        a.sales++;
        tally(&a, "sales");
        saveAssets();
        saveUsers();
        json r = okay(); r["receipt"] = rid; r["me"] = meJson(me); return r;
    }
    if (name == "product.pending" || name == "product.grant") {
        // Only the buyer, or whoever runs an online server of that game, may look and say "handed out".
        const std::string game = str("game");
        User* who = args.contains("user") && args["user"].is_number() ? findUserId(args["user"].get<long long>()) : findUser(str("user"));
        if (!who) { json r = okay(); r["receipts"] = json::array(); return r; }
        bool hosts = false;
        for (const auto& [sid, s] : m_sessions) if (s.host == me.id && s.game == game) hosts = true;
        if (who->id != me.id && !hosts) return fail("Only a server of that game can see its receipts.");
        if (!who->receipts.is_array()) who->receipts = json::array();
        if (name == "product.grant") {
            for (json& r : who->receipts)
                if (r.value("game", std::string()) == game && r.value("id", std::string()) == str("receipt")) {
                    r["granted"] = true;
                    saveUsers();
                    return okay();
                }
            return fail("There's no receipt like that.");
        }
        json list = json::array();
        for (auto r = who->receipts.rbegin(); r != who->receipts.rend(); ++r) {   // oldest first
            if (r->value("game", std::string()) != game || r->value("granted", false)) continue;
            const std::string pid = r->value("product", std::string());
            auto p = m_assets.find(pid);
            list.push_back({{"id", r->value("id", std::string())}, {"product", pid}, {"num", p != m_assets.end() ? p->second.num : 0},
                            {"price", r->value("price", 0LL)}, {"at", r->value("at", 0LL)}});
        }
        json r = okay(); r["receipts"] = list; return r;
    }
    // --- Game badges: creators make them on their game's page, game scripts award them.
    if (name == "gamebadge.create" || name == "gamebadge.delete") {
        auto it = findAsset(str("game"));
        if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
        Asset& g = it->second;
        if (g.creator != me.id) return fail("Only the game's creator can change its badges.");
        if (name == "gamebadge.delete") {
            json keep = json::array();
            for (const json& b : g.badges) if (b.value("id", std::string()) != str("badge")) keep.push_back(b);
            g.badges = keep;
            saveAssets();
            json r = okay(); r["badges"] = g.badges; return r;
        }
        if (g.badges.size() >= 30) return fail("A game can have up to 30 badges.");
        std::string title = say(str("name"), 40);
        if (title.empty()) return fail("Give the badge a name.");
        json col = json::array({240, 180, 40});
        if (args.contains("color") && args["color"].is_array() && args["color"].size() == 3) {
            col = json::array();
            for (const auto& v : args["color"]) col.push_back(std::clamp(v.is_number() ? v.get<int>() : 0, 0, 255));
        }
        json b = {{"id", "badge-" + Account::randomHex(5)}, {"name", title},
                  {"description", say(str("description"), 300, true)}, {"color", col},
                  {"created", Online::unixNow()}, {"awarded", 0}};
        g.badges.push_back(b);
        saveAssets();
        json r = okay(); r["badge"] = b; r["badges"] = g.badges; return r;
    }
    if (name == "gamebadge.award") {
        // Only the host of a live server of the badge's game, for someone in that server.
        const std::string bid = str("badge");
        Asset* g = nullptr;
        json* b = nullptr;
        for (auto& [id, a] : m_assets) {
            if (a.kind != "game") continue;
            for (json& x : a.badges) if (x.value("id", std::string()) == bid) { g = &a; b = &x; }
            if (g) break;
        }
        if (!b) return fail("There's no badge with that ID.");
        const Session* session = nullptr;
        for (const auto& [sid, s] : m_sessions) if (s.host == me.id && s.game == g->id) session = &s;
        if (!session) return fail("Badges can only be given in an online server of " + g->name + ".");
        auto lower = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
        const std::string who = lower(str("to"));
        User* to = lower(me.name) == who ? &me : nullptr;
        for (const Client* p : session->players) {
            auto u = m_users.find(p->account);
            if (!to && u != m_users.end() && lower(u->second.name) == who) to = &u->second;
        }
        if (!to) return fail("That player isn't in this server.");
        if (to->userId == 0) return fail("Guests can't earn badges. Sign up to collect them!");
        for (const json& e : to->gameBadges)
            if (e.is_array() && !e.empty() && e[0] == bid) { json r = okay(); r["already"] = true; r["name"] = (*b)["name"]; return r; }
        to->gameBadges.push_back(json::array({bid, g->id, Online::unixNow()}));
        (*b)["awarded"] = b->value("awarded", 0) + 1;
        saveUsers();
        saveAssets();
        json r = okay(); r["awarded"] = true; r["name"] = (*b)["name"]; r["player"] = to->name; return r;
    }
    if (name == "update") {
        // The creator replaces their upload (a new version of a game or plugin).
        auto it = findAsset(str("id"));
        if (it == m_assets.end()) return fail("That doesn't exist (any more).");
        Asset& a = it->second;
        if (a.creator != me.id) return fail("You can only update your own things.");
        std::string data;
        if (!Online::base64Decode(str("data"), data)) return fail("The upload got scrambled. Try again.");
        if (data.size() > Online::maxSize(a.kind)) return fail("That's too big.");
        if (a.kind == "game" && !json::accept(data)) return fail("That isn't a Guts&Bolts game file.");
        if (a.kind == "gear")
            if (std::string problem = Online::gearProblem(data); !problem.empty()) return fail(problem);
        // A 3D accessory made in Studio: a new version of its model (e.g. with its pictures uploaded).
        const bool model = Online::isAccessory(a.kind) && a.meta.value("model", false) && !data.empty();
        if (model) {
            json acc = json::parse(data, nullptr, false);
            if (!acc.is_object() || acc.value("format", std::string()) != "gbaccessory" || !acc.contains("node"))
                return fail("That isn't a Guts&Bolts accessory.");
        }
        if ((!Online::isClothing(a.kind) || model) && !writeFile(blobPath(a.id), data)) return fail("The server couldn't save that file.");
        // A new picture or sound gets checked again.
        if (reviewedKind(a.kind) && !Online::isClothing(a.kind) && !isVerified(me) && !isStaff(me)) {
            a.meta["review"] = "pending";
            a.meta.erase("reviewNote");
        }
        std::string title = say(str("name"), 50);
        if (!title.empty()) a.name = title;
        if (args.contains("description")) a.description = say(str("description"), 1000, true);
        if (args.contains("price") && a.kind != "game" && !Online::alwaysFree(a.kind)) {
            long long price = std::clamp(num("price"), 0LL, 1000000LL);
            if (price > 0 && !isVerified(me)) return fail("Only Verified creators can sell things.");
            a.price = price;
        }
        if (!Online::isClothing(a.kind)) a.size = data.size();
        saveAssets();
        log(me.name + " updated " + a.kind + " \"" + a.name + "\"");
        json r = okay(); r["asset"] = publicAsset(a); return r;
    }
    if (name == "list") {
        std::string kind = str("kind"), q = lower(Online::cleanText(str("query"), 64)), creator = lower(str("creator"));
        std::string sort = str("sort");
        const bool ownedOnly = args.value("owned", false);   // your inventory (the Avatar page)
        std::vector<const Asset*> found;
        for (const auto& [id, a] : m_assets) {
            if (!kind.empty() && a.kind != kind && !(kind == "clothing" && Online::isCatalogItem(a.kind))) continue;
            if (ownedOnly && !me.owned.count(a.id)) continue;
            if (sort == "featured" && a.meta.value("featured", 0LL) <= 0) continue;   // picked by staff
            if (!creator.empty() && a.creator != creator) continue;
            if (!q.empty() && lower(a.name).find(q) == std::string::npos) continue;
            if (!canSee(a, me)) continue;
            found.push_back(&a);
        }
        std::sort(found.begin(), found.end(), [&](const Asset* x, const Asset* y) {
            if (sort == "popular") return x->plays + x->sales > y->plays + y->sales;
            if (sort == "featured") return x->meta.value("featured", 0LL) > y->meta.value("featured", 0LL);   // newest pick first
            return x->created > y->created;   // newest first
        });
        long long offset = std::max(0LL, num("offset")), limit = std::clamp(num("limit"), 1LL, 100LL);
        if (!args.contains("limit")) limit = 60;
        json list = json::array();
        for (size_t i = (size_t)offset; i < found.size() && (long long)list.size() < limit; ++i)
            list.push_back(publicAsset(*found[i]));
        json r = okay(); r["assets"] = list; r["total"] = found.size(); return r;
    }
    if (name == "gear.equip") {   // gear you own, in your backpack for games (worker/server.js)
        std::string id = str("id");
        if (auto f = findAsset(id); f != m_assets.end()) id = f->first;   // (its number works too)
        const bool on = args.value("on", true);
        std::vector<std::string> keep;
        for (const auto& g : me.gear) if (g != id && me.owned.count(g)) keep.push_back(g);
        me.gear = keep;
        if (on) {
            auto it = findAsset(id);
            if (it == m_assets.end() || it->second.kind != "gear") return fail("That gear doesn't exist (any more).");
            if (!me.owned.count(id)) return fail("Get it from the catalog first.");
            if ((int)me.gear.size() >= Online::kMostGear)
                return fail("You can have " + std::to_string(Online::kMostGear) + " gear equipped at once. Take one off first.");
            me.gear.push_back(id);
        }
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "game.settings") {   // (the website's game settings; here just "Allow gear")
        auto it = findAsset(str("id"));
        if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
        Asset& a = it->second;
        if (a.creator != me.id && !isStaff(me)) return fail("You can only change your own games.");
        if (args.contains("allowGear")) a.meta["allowGear"] = args["allowGear"] == true;
        if (args.contains("comments")) a.meta["commentsOff"] = args["comments"] == false;
        if (args.contains("privatePrice"))   // paid private servers (0 = free)
            a.meta["privatePrice"] = std::clamp(args["privatePrice"].is_number_integer() ? args["privatePrice"].get<long long>() : 0LL, 0LL, 10000LL);
        if (args.contains("group")) {   // put the game in one of your groups (its sales go to the group), or "" to take it out
            const std::string gid = str("group");
            if (!gid.empty()) {
                auto g = m_groups.find(gid);
                if (g == m_groups.end()) return fail("That group doesn't exist (any more).");
                if (!groupCan(g->second, me, "games") && !isStaff(me))
                    return fail("You need the \"Add games\" permission in " + g->second.name + ".");
            }
            a.meta["group"] = gid;
        }
        saveAssets();
        json r = okay(); r["asset"] = publicAsset(a); return r;
    }
    // One asset's page (the Library's "asset ID" pages): what it is, without downloading it.
    // Takes the ID people paste into games too ("gb:decal-..."). (worker/server.js has the same.)
    if (name == "asset.info") {
        std::string id = Online::cleanText(str("id"), 80);
        if (id.rfind("gb:", 0) == 0) id = id.substr(3);
        auto it = findAsset(id);
        if (it == m_assets.end() || !canSee(it->second, me)) return fail("There's nothing with that ID (or it's private).");
        json r = okay(); r["asset"] = publicAsset(it->second); r["owned"] = me.owned.count(id) > 0; return r;
    }
    if (name == "get") {
        auto it = findAsset(str("id"));
        if (it == m_assets.end()) return fail("That doesn't exist (any more).");
        Asset& a = it->second;
        const bool mine = me.owned.count(a.id) || a.creator == me.id;
        if (a.price > 0 && !mine && !isStaff(me) && (a.kind == "plugin" || a.kind == "audio" || a.kind == "gear"))
            return fail("Buy it first.");
        if (!canSee(a, me)) {
            const std::string review = reviewOf(a.meta);
            return fail(review == "pending" ? "This is waiting for a staff check." : review == "rejected" ? "This didn't pass the staff check."
                        : a.kind == "animation" ? "This animation is private." : "This model is private.");
        }
        std::string data;
        if (!readFile(blobPath(a.id), data)) return fail("The server lost that file.");
        if (a.kind == "game") { rememberPlayed(me, a.id); saveUsers(); }   // "Continue playing"
        if (a.kind == "game" && a.creator != me.id) { a.plays++; tally(&a, "plays"); saveAssets(); }   // creators opening their own game don't count
        json r = okay(); r["asset"] = publicAsset(a); r["data"] = Online::base64Encode(data); return r;
    }
    if (name == "buy") {
        auto it = findAsset(str("id"));
        if (it == m_assets.end()) return fail("That doesn't exist (any more).");
        Asset& a = it->second;
        if (me.owned.count(a.id)) { json r = okay(); r["me"] = meJson(me); r["already"] = true; return r; }
        if (!reviewOf(a.meta).empty() && a.creator != me.id) return fail("This is waiting for a staff check.");
        if (a.kind == "devproduct") return fail("Developer products are bought inside their game.");
        if (a.meta.is_object() && a.meta.value("award", std::string()) == "email")
            return fail("This hat can't be bought: confirm an email in Settings and it's yours.");
        if (const long long off = a.meta.is_object() ? a.meta.value("offsaleAt", 0LL) : 0LL; off > 0 && now >= off)
            return fail("This item is off sale: it was only for sale for a limited time.");
        if (a.price > 0) {
            if (balance(me) < a.price)
                return fail("You need " + std::to_string(a.price - balance(me)) + " more Bolts for that.");
            add(me, -a.price, "Bought " + a.name, "buy:" + a.id);
            paySeller(a, me, a.price * Online::kCreatorSharePercent / 100, "sale:" + a.id + ":" + Account::randomHex(4));
        }
        a.sales++;
        if (a.creator != me.id) tally(&a, "sales");
        me.owned.insert(a.id);
        saveAssets();
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "delete") {
        auto it = findAsset(str("id"));
        if (it == m_assets.end()) return fail("That doesn't exist (any more).");
        if (it->second.creator != me.id && !isStaff(me)) return fail("You can only delete your own things.");
        if (it->second.creator != me.id)
            staffDid(me, "delete", std::string("Deleted ") + Online::kindTitle(it->second.kind) + " \"" + it->second.name + "\"", it->second.creator);
        std::error_code ec;
        fs::remove(blobPath(it->first), ec);
        fs::remove(m_opts.data / "files" / ("thumb-" + it->first), ec);
        log(me.name + " deleted " + it->second.kind + " \"" + it->second.name + "\"");
        m_assets.erase(it);
        saveAssets();
        return okay();
    }
    // Creator stats: everything you made, with totals and the last 30 days (plays, sales, Bolts earned).
    if (name == "creator.stats") {
        if (me.userId == 0) return fail("Sign up first.");
        json days = json::array();
        for (int i = 29; i >= 0; i--) days.push_back(Online::utcDay(now - i * 86400LL));
        json items = json::array();
        for (const auto& [id, a] : m_assets) {
            if (a.creator != me.id) continue;
            auto series = [&](const char* f) {
                json out = json::array();
                for (const auto& d : days) {
                    const std::string k = d.get<std::string>();
                    out.push_back(a.days.contains(k) && a.days[k].is_object() ? a.days[k].value(f, 0LL) : 0LL);
                }
                return out;
            };
            long long bolts60 = 0;
            for (const auto& [k, v] : a.days.items()) if (v.is_object()) bolts60 += v.value("bolts", 0LL);
            int playing = 0;
            if (a.kind == "game") for (const auto& [sid, s] : m_sessions) if (s.game == a.id) playing += headcount(s);
            items.push_back({{"id", a.id}, {"num", a.num}, {"kind", a.kind}, {"name", a.name}, {"plays", a.plays}, {"sales", a.sales},
                             {"price", a.price}, {"favorites", a.meta.value("favorites", 0LL)}, {"likes", a.meta.value("likes", 0LL)},
                             {"dislikes", a.meta.value("dislikes", 0LL)}, {"playing", playing},
                             {"plays30", series("plays")}, {"sales30", series("sales")}, {"bolts30", series("bolts")}, {"bolts60", bolts60}});
        }
        std::sort(items.begin(), items.end(), [](const json& x, const json& y) {
            return x["plays"].get<long long>() + x["sales"].get<long long>() > y["plays"].get<long long>() + y["sales"].get<long long>(); });
        json r = okay(); r["days"] = days; r["items"] = items; return r;
    }
    if (name == "stats") {
        json r = okay();
        r["users"] = m_users.size();
        r["assets"] = m_assets.size();
        r["name"] = m_opts.name;
        return r;
    }
    if (name == "updates.list") {   // the website's update log lives on the main (Cloudflare) server
        json r = okay();
        r["updates"] = json::array();
        r["latest"] = "";
        return r;
    }
    return fail("The server doesn't know how to do \"" + name + "\". It might need updating.");
}

// ---------------------------------------------------------------------------
// Saving and loading
// ---------------------------------------------------------------------------

fs::path GbServer::blobPath(const std::string& assetId) const { return m_opts.data / "files" / assetId; }

void GbServer::saveUsers() {
    json all = json::object();
    for (const auto& [id, u] : m_users) {
        json ledger = json::array();
        for (const Entry& e : u.ledger) ledger.push_back({e.amount, e.reason, e.time, e.ref});
        all[id] = {{"name", u.name}, {"created", u.created}, {"lastSeen", u.lastSeen}, {"ledger", ledger},
                   {"grants", u.grants}, {"owned", u.owned}, {"uploadDay", u.uploadDay}, {"uploadsToday", u.uploadsToday},
                   {"playDay", u.playDay}, {"playEarned", u.playEarned}, {"lastPlay", u.lastPlay}, {"banned", u.banned},
                   {"banReason", u.banReason}, {"banNote", u.banNote}, {"bannedAt", u.bannedAt},
                   {"bannedUntil", u.bannedUntil}, {"warnings", u.warnings},
                   {"friends", u.friends}, {"friendIn", u.friendIn}, {"friendOut", u.friendOut},
                   {"following", u.following}, {"followers", u.followers},
                   {"username", u.username}, {"userId", u.userId}, {"pwSalt", u.pwSalt}, {"pwHash", u.pwHash},
                   {"keyBlob", u.keyBlob}, {"avatar", u.avatar}, {"gameBadges", u.gameBadges}};
        if (!u.totpSecret.empty()) { all[id]["totpSecret"] = u.totpSecret; all[id]["totpLast"] = u.totpLast; }
        if (!u.totpPending.empty()) all[id]["totpPending"] = u.totpPending;
        all[id]["privacy"] = {{"status", u.privacyStatus}, {"join", u.privacyJoin}, {"messages", u.privacyMessages}, {"inventory", u.privacyInventory}};
        all[id]["gear"] = u.gear;
        all[id]["outfits"] = u.outfits;
        all[id]["favorites"] = u.favorites;
        all[id]["recent"] = u.recent;
        all[id]["inbox"] = u.inbox;
        all[id]["notes"] = u.notes;
        all[id]["receipts"] = u.receipts;
        if (!u.privateServers.empty()) all[id]["privateServers"] = u.privateServers;
        all[id]["sent"] = u.sent;
        all[id]["messageDay"] = u.messageDay;
        all[id]["messagesToday"] = u.messagesToday;
        all[id]["blurb"] = u.blurb;
        all[id]["posts"] = u.posts;
        all[id]["statusDay"] = u.statusDay;
        all[id]["statusesToday"] = u.statusesToday;
        all[id]["blocked"] = u.blocked;
        all[id]["reportDay"] = u.reportDay;
        all[id]["reportsToday"] = u.reportsToday;
    }
    writeFile(m_opts.data / "accounts.json", all.dump(1));
}

// Creator stats: count something on today's date, keeping 60 days. Passes and products
// also count on their game, so a game's numbers include what was bought inside it.
void GbServer::tally(Asset* a, const std::string& field, long long n) {
    if (!a || n == 0) return;
    const std::string day = Online::utcDay(Online::unixNow());
    json& d = a->days[day];
    if (!d.is_object()) d = json::object();
    d[field] = d.value(field, 0LL) + n;
    while (a->days.size() > 60) a->days.erase(a->days.begin());   // keys sort by date, so the oldest is first
    if ((a->kind == "gamepass" || a->kind == "devproduct") && field != "plays") {
        auto it = findAsset(a->meta.value("game", std::string()));
        if (it != m_assets.end()) tally(&it->second, field, n);
    }
}

void GbServer::saveAssets() {
    json all = json::object();
    for (const auto& [id, a] : m_assets)
        all[id] = {{"kind", a.kind}, {"name", a.name}, {"description", a.description}, {"creator", a.creator},
                   {"price", a.price}, {"created", a.created}, {"sales", a.sales}, {"plays", a.plays},
                   {"size", a.size}, {"meta", a.meta}, {"thumb", a.thumb}, {"badges", a.badges}, {"num", a.num}, {"days", a.days}, {"comments", a.comments}};
    writeFile(m_opts.data / "assets.json", all.dump(1));
}

// The example games that come with Guts&Bolts (the games folder): always on the
// server as the staff account's games, kept up to date (the website's server does the same).
void GbServer::addExampleGames() {
    if (Account::officialId().empty()) return;
    std::error_code ec;
    static const std::pair<const char*, const char*> kGenres[] = {
        {"Demolition Yard", "Destruction"}, {"Mega Water Slide", "Adventure"}, {"Night Plaza", "Showcase"}, {"Obby of Doom", "Obby"}};
    bool changed = false;
    for (const auto& [title, genre] : kGenres) {
        fs::path file = Paths::gamesFolder() / (std::string(title) + ".gbscene");
        std::string text;
        if (!readFile(file, text)) continue;
        json j = json::parse(text, nullptr, false);
        if (!j.is_object()) continue;
        std::string id = "game-";
        for (char c : std::string(title)) id += c == ' ' ? '-' : (char)std::tolower((unsigned char)c);
        Asset& a = m_assets[id];
        if (a.id.empty()) {
            a.id = id; a.kind = "game"; a.creator = Account::officialId(); a.created = Online::unixNow();
            a.meta = {{"builtin", true}, {"genres", json::array({genre})}, {"access", "public"}};
        }
        const json& info = j.contains("info") ? j["info"] : json::object();
        a.name = Online::cleanText(info.value("title", std::string(title)), 50);
        a.description = Online::cleanText(info.value("description", std::string()), 1000);
        if (a.size != text.size()) { a.size = text.size(); writeFile(blobPath(id), text); }
        changed = true;
    }
    // The Verified Hat (worker/server.js VERIFIED_HAT, the same hat): an award for
    // confirming an email, not for sale. (This server has no email yet, so nobody
    // earns it here; it's listed so the catalogs match.)
    {
        static const char* kHat = R"HAT({"format":"gbaccessory","version":1,"kind":"hat","node":{"id":1,"name":"VerifiedHat","kind":"Model","pos":[0,2.66,0],"rot":[0,0,0],"size":[1,1,1],"children":[{"id":2,"name":"Crown","kind":"Part","shape":"Sphere","pos":[0,0.02,-0.02],"rot":[0,0,0],"size":[0.8,0.5,0.8],"color":[0.1,0.13,0.2],"material":"Fabric","anchored":true,"canCollide":false},{"id":3,"name":"Brim","kind":"Part","shape":"Cube","pos":[0,-0.06,0.42],"rot":[-8,0,0],"size":[0.62,0.04,0.36],"color":[0.1,0.13,0.2],"material":"Fabric","anchored":true,"canCollide":false},{"id":4,"name":"Badge","kind":"Part","shape":"Cylinder","pos":[0,0.08,0.36],"rot":[72,0,0],"size":[0.24,0.04,0.24],"color":[0.16,0.55,1],"material":"SmoothPlastic","anchored":true,"canCollide":false},{"id":5,"name":"CheckShort","kind":"Part","shape":"Cube","pos":[-0.035,0.065,0.385],"rot":[72,0,45],"size":[0.035,0.08,0.02],"color":[1,1,1],"material":"SmoothPlastic","anchored":true,"canCollide":false},{"id":6,"name":"CheckLong","kind":"Part","shape":"Cube","pos":[0.03,0.085,0.38],"rot":[72,0,-40],"size":[0.035,0.15,0.02],"color":[1,1,1],"material":"SmoothPlastic","anchored":true,"canCollide":false}]}})HAT";
        Asset& a = m_assets["item-verified-hat"];
        if (a.id.empty()) {
            a.id = "item-verified-hat"; a.kind = "hat"; a.creator = Account::officialId(); a.created = Online::unixNow();
            a.meta = {{"builtin", true}, {"award", "email"}};
        }
        a.name = "Verified Hat";
        a.description = "Given to everyone who confirms their email address. Can't be bought.";
        const std::string text = kHat;
        if (a.size != text.size()) { a.size = text.size(); writeFile(blobPath(a.id), text); }
        changed = true;
    }
    // Gutstober (worker/server.js timeGutstoberItems): pumpkin items are timed items that
    // go off sale when Gutstober (October) ends, midnight UTC on November 1st. Once per item.
    for (auto& [id, a] : m_assets) {
        if (!Online::isCatalogItem(a.kind) || !a.meta.is_object() || a.meta.contains("timedFor")) continue;
        std::string lower = a.name;
        for (char& c : lower) c = (char)std::tolower((unsigned char)c);
        if (lower.find("pumpkin") == std::string::npos) continue;
        // The year it was made in, then midnight UTC on November 1st of that year
        // (days from 1970 worked out by hand: timegm isn't on every system).
        const long long made = a.created ? a.created : Online::unixNow();
        long long year = 1970;
        auto daysTo = [](long long y) {   // days from 1970-01-01 to January 1st of year y
            const long long p = y - 1;
            return 365 * (y - 1970) + (p / 4 - 1969 / 4) - (p / 100 - 1969 / 100) + (p / 400 - 1969 / 400);
        };
        while (daysTo(year + 1) * 86400 <= made) ++year;
        const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        const long long nov1 = daysTo(year) + 304 + (leap ? 1 : 0);   // Jan..Oct = 304 days (+1 in leap years)
        a.meta["timedFor"] = "gutstober";
        a.meta["offsaleAt"] = nov1 * 86400;
        changed = true;
    }
    if (changed) saveAssets();
}

void GbServer::load() {
    std::string text;
    if (readFile(m_opts.data / "accounts.json", text)) {
        json all = json::parse(text, nullptr, false);
        if (all.is_object())
            for (auto& [id, j] : all.items()) {
                User u;
                u.id = id;
                u.name = j.value("name", std::string("Player"));
                u.created = j.value("created", 0LL);
                u.lastSeen = j.value("lastSeen", 0LL);
                if (j.contains("ledger"))
                    for (const auto& e : j["ledger"])
                        if (e.is_array() && e.size() == 4)
                            u.ledger.push_back({e[0].get<long long>(), e[1].get<std::string>(), e[2].get<long long>(),
                                                e[3].get<std::string>()});
                if (j.contains("grants") && j["grants"].is_object())
                    for (auto& [k, v] : j["grants"].items()) if (v.is_string()) u.grants[k] = v.get<std::string>();
                if (j.contains("owned")) for (const auto& o : j["owned"]) if (o.is_string()) u.owned.insert(o.get<std::string>());
                if (j.contains("privacy") && j["privacy"].is_object()) {
                    auto ok = [](const std::string& v) { return v == "everyone" || v == "friends" || v == "nobody"; };
                    std::string st = j["privacy"].value("status", std::string("everyone")), jn = j["privacy"].value("join", std::string("everyone"));
                    u.privacyStatus = ok(st) ? st : "everyone";
                    u.privacyJoin = ok(jn) ? jn : "everyone";
                    std::string ms = j["privacy"].value("messages", std::string("everyone"));
                    u.privacyMessages = ok(ms) ? ms : "everyone";
                    std::string inv = j["privacy"].value("inventory", std::string("everyone"));
                    u.privacyInventory = ok(inv) ? inv : "everyone";
                }
                if (j.contains("outfits") && j["outfits"].is_array()) u.outfits = j["outfits"];
                if (j.contains("inbox") && j["inbox"].is_array()) u.inbox = j["inbox"];
                if (j.contains("notes") && j["notes"].is_array()) u.notes = j["notes"];
                if (j.contains("receipts") && j["receipts"].is_array()) u.receipts = j["receipts"];
                if (j.contains("privateServers") && j["privateServers"].is_object()) u.privateServers = j["privateServers"];
                if (j.contains("sent") && j["sent"].is_array()) u.sent = j["sent"];
                for (const char* k : {"favorites", "recent"})
                    if (j.contains(k) && j[k].is_array())
                        for (const auto& g : j[k]) if (g.is_string()) (std::string(k) == "favorites" ? u.favorites : u.recent).push_back(g.get<std::string>());
                u.messageDay = j.value("messageDay", std::string());
                u.messagesToday = j.value("messagesToday", 0);
                u.blurb = j.value("blurb", std::string());
                if (j.contains("posts") && j["posts"].is_array()) u.posts = j["posts"];
                u.statusDay = j.value("statusDay", std::string());
                u.statusesToday = j.value("statusesToday", 0);
                if (j.contains("blocked") && j["blocked"].is_array())
                    for (const auto& b : j["blocked"]) if (b.is_string()) u.blocked.push_back(b.get<std::string>());
                u.reportDay = j.value("reportDay", std::string());
                u.reportsToday = j.value("reportsToday", 0);
                if (j.contains("gear") && j["gear"].is_array())
                    for (const auto& g : j["gear"]) if (g.is_string()) u.gear.push_back(g.get<std::string>());
                u.totpSecret = j.value("totpSecret", std::string());
                u.totpPending = j.value("totpPending", std::string());
                u.totpLast = j.value("totpLast", -1LL);
                u.uploadDay = j.value("uploadDay", std::string());
                u.uploadsToday = j.value("uploadsToday", 0);
                u.playDay = j.value("playDay", std::string());
                u.playEarned = j.value("playEarned", 0LL);
                u.lastPlay = j.value("lastPlay", 0LL);
                u.banned = j.value("banned", false);
                u.banReason = j.value("banReason", std::string());
                u.banNote = j.value("banNote", std::string());
                u.bannedAt = j.value("bannedAt", 0LL);
                u.bannedUntil = j.value("bannedUntil", 0LL);
                if (j.contains("warnings") && j["warnings"].is_array()) u.warnings = j["warnings"];
                u.username = j.value("username", std::string());
                u.userId = j.value("userId", 0LL);
                u.pwSalt = j.value("pwSalt", std::string());
                u.pwHash = j.value("pwHash", std::string());
                u.keyBlob = j.value("keyBlob", std::string());
                if (j.contains("avatar") && j["avatar"].is_object()) u.avatar = j["avatar"];
                if (j.contains("gameBadges") && j["gameBadges"].is_array()) u.gameBadges = j["gameBadges"];
                for (const char* k : {"friends", "friendIn", "friendOut", "following", "followers"}) {
                    const std::string key = k;
                    std::set<std::string>& set = key == "friends" ? u.friends : key == "friendIn" ? u.friendIn
                                               : key == "friendOut" ? u.friendOut : key == "following" ? u.following : u.followers;
                    if (j.contains(k) && j[k].is_array())
                        for (const auto& f : j[k]) if (f.is_string()) set.insert(f.get<std::string>());
                }
                m_users[id] = std::move(u);
            }
    }
    if (readFile(m_opts.data / "assets.json", text)) {
        json all = json::parse(text, nullptr, false);
        if (all.is_object())
            for (auto& [id, j] : all.items()) {
                Asset a;
                a.id = id;
                a.kind = j.value("kind", std::string());
                a.name = j.value("name", std::string());
                a.description = j.value("description", std::string());
                a.creator = j.value("creator", std::string());
                a.price = Online::alwaysFree(a.kind) ? 0LL : j.value("price", 0LL);
                a.created = j.value("created", 0LL);
                a.sales = j.value("sales", 0LL);
                a.plays = j.value("plays", 0LL);
                a.size = j.value("size", (size_t)0);
                if (j.contains("meta")) a.meta = j["meta"];
                if (j.contains("badges") && j["badges"].is_array()) a.badges = j["badges"];
                if (j.contains("days") && j["days"].is_object()) a.days = j["days"];
                if (j.contains("comments") && j["comments"].is_array()) a.comments = j["comments"];
                a.thumb = j.value("thumb", 0LL);
                a.num = j.value("num", 0LL);
                if (Online::validKind(a.kind)) m_assets[id] = a;
            }
    }
    loadGroups();
    loadReports();
    loadStaffLog();
    loadForum();
    loadIds();
    log("loaded " + std::to_string(m_users.size()) + " accounts, " + std::to_string(m_assets.size()) + " uploads and " +
        std::to_string(m_groups.size()) + " groups from " + m_opts.data.string());
}
