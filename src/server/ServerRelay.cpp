// Game servers through the Guts&Bolts server (the "relay").
//
// Someone's computer still runs the game (the host), but nobody connects to
// anybody directly: the host and every player connect to this server, and it
// passes the game's messages along. So players never see each other's IP
// address, and nobody has to open ports on their router.
//
//   host:    relay.host {game, title, private, max}  -> this connection becomes its control line
//   player:  relay.join {session | code}             -> we tell the host: {"t":"incoming","ticket"}
//   host:    new connection, {"t":"accept","ticket"}  -> the two connections are joined into a pipe
//
// Public servers are what the Play button finds (servers.play). Private ones
// need the host to be your friend, or their code.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../net/Socket.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cctype>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr int       kDefaultMax     = 12;
constexpr int       kMostPlayers    = 30;
constexpr size_t    kHostedEach     = 3;     // servers one account can host at once
constexpr long long kJoinWait       = 15;    // seconds for the host to pick up a joining player
constexpr long long kHostSilence    = 90;    // hosts ping every 20 s
constexpr long long kMoveWait       = 90;    // seconds a left-behind server's players have to move to a new one
constexpr long long kHeirWait       = 12;    // seconds each would-be new host gets to start it
constexpr long long kPipeSilence    = 120;
constexpr int       kPoolMost       = 50;    // games one game server machine runs at once
constexpr long long kPoolStartWait  = 25;    // seconds for it to start a game it was asked to
// Paid private servers: a game's creator can charge Bolts for one. Buying lasts 30 days (buy
// again to add 30 more); friends join free with its code.
constexpr long long kPrivateDays    = 30;

std::string makeCode() {
    static const char* letters = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // no 0/O or 1/I mix-ups
    std::string hex = Account::randomHex(6), code;
    for (size_t i = 0; i < 6; ++i) code += letters[std::stoi(hex.substr(i * 2, 2), nullptr, 16) % 32];
    return code;
}
std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
} // namespace

std::string GbServer::freeCode() const {
    auto used = [&](const std::string& code) {
        for (const auto& [id, x] : m_sessions) if (x.code == code) return true;
        for (const auto& [ctl, pool] : m_pools) if (pool.starting.count("code:" + code)) return true;
        return false;
    };
    std::string code;
    do code = makeCode();
    while (used(code));
    return code;
}

json GbServer::sessionJson(const Session& s) const {
    const User* h = nullptr;
    if (auto it = m_users.find(s.host); it != m_users.end()) h = &it->second;
    // Who's in it (the first few, with their avatars), for the server cards on game pages.
    json people = json::array();
    auto person = [&](const std::string& id) {
        if (people.size() >= 5) return;
        auto it = m_users.find(id);
        people.push_back({{"id", id}, {"name", it != m_users.end() ? it->second.name : "?"},
                          {"avatar", it != m_users.end() && !it->second.avatar.is_null() ? it->second.avatar : json()}});
    };
    if (!s.dedicated) person(s.host);
    for (const Client* p : s.players) person(p->account);
    return {{"id", s.id}, {"game", s.game}, {"title", s.title}, {"players", headcount(s)},
            {"max", s.max}, {"private", s.priv}, {"hostName", s.dedicated ? "Guts&Bolts" : h ? h->name : "?"},
            {"hostVerified", s.dedicated || (h && isVerified(*h))}, {"dedicated", s.dedicated}, {"people", people}};
}

const GbServer::Session* GbServer::sessionOf(const std::string& userId) const {
    for (const auto& [id, s] : m_sessions) {
        if (s.host == userId && !s.dedicated) return &s;
        for (const Client* p : s.players) if (p->account == userId) return &s;
    }
    return nullptr;
}

long long GbServer::privateUntil(const User& u, const std::string& gameId) const {
    const long long t = u.privateServers.is_object() ? u.privateServers.value(gameId, 0LL) : 0LL;
    return t > Online::unixNow() ? t : 0;
}

std::string GbServer::privateBlocked(const User& u, const std::string& gameId) const {
    auto a = m_assets.find(gameId);
    if (a == m_assets.end() || a->second.kind != "game") return "";
    const long long price = a->second.meta.value("privatePrice", 0LL);
    if (price <= 0 || a->second.creator == u.id || isStaff(u) || privateUntil(u, gameId)) return "";
    return "Private servers of " + a->second.name + " cost " + std::to_string(price) + " Bolts for " + std::to_string(kPrivateDays) +
           " days. Buy one on the game's page first.";
}

json GbServer::serverOp(const std::string& name, User& me, const json& args) {
    std::string game = Online::cleanText(args.value("game", std::string()), 80);
    if (auto f = findAsset(game); f != m_assets.end()) game = f->first;   // (its number works too)

    if (name == "servers.play") {
        // The fullest public server of this game that still has room, like Roblox.
        const Session* best = nullptr;
        for (const auto& [id, s] : m_sessions) {
            if (s.priv || s.game != game || (s.host == me.id && !s.dedicated) || headcount(s) + 1 > s.max) continue;
            if (!best || s.players.size() > best->players.size()) best = &s;
        }
        json r = okay();
        auto a = m_assets.find(game);
        if (best) r["join"] = best->id;
        // Nobody's playing: a game server machine starts it if one is free (ask again shortly),
        // so the game keeps going whoever leaves. Otherwise you start a new public server.
        else if (a != m_assets.end() && a->second.kind == "game" && args.value("dedicated", true) && startOnPool(a->second)) r["wait"] = 2;
        else r["host"] = true;
        return r;
    }
    // Your own private server. Like servers.play, a game server machine runs it if one is free
    // ("wait", then ask again with the code), so it stays up whoever leaves, you included.
    // Otherwise you host it yourself ("host").
    if (name == "servers.private") {
        auto a = m_assets.find(game);
        if (a == m_assets.end() || a->second.kind != "game") return fail("That game doesn't exist (any more).");
        if (const std::string blocked = privateBlocked(me, game); !blocked.empty()) return fail(blocked);
        const std::string code = upper(Online::cleanText(args.value("code", std::string()), 12));
        json r = okay();
        if (!code.empty()) {
            for (const auto& [id, s] : m_sessions)
                if (s.code == code) { r["join"] = id; r["code"] = code; return r; }
            const long long now = Online::unixNow();
            for (const auto& [ctl, pool] : m_pools)
                if (auto w = pool.starting.find("code:" + code); w != pool.starting.end() && now - w->second <= kPoolStartWait) {
                    r["wait"] = 2; r["code"] = code; return r;
                }
            r["host"] = true;   // the machine didn't start it: host it yourself
            return r;
        }
        if (args.value("dedicated", true)) {
            const std::string fresh = freeCode();
            if (startOnPool(a->second, {{"private", true}, {"code", fresh}, {"owner", me.id}})) { r["wait"] = 2; r["code"] = fresh; return r; }
        }
        r["host"] = true;
        return r;
    }
    if (name == "servers.list") {
        json list = json::array();
        for (const auto& [id, s] : m_sessions) {
            if (!game.empty() && s.game != game) continue;
            bool friendHost = me.friends.count(ownerOf(s)) > 0;
            if (s.priv && !friendHost && ownerOf(s) != me.id) continue;   // private: only its owner's friends see it
            json j = sessionJson(s);
            int friends = friendHost ? 1 : 0;
            for (const Client* p : s.players) if (me.friends.count(p->account)) ++friends;
            j["friends"] = friends;
            list.push_back(j);
            if (list.size() >= 100) break;
        }
        json r = okay();
        r["servers"] = list;
        if (auto a = m_assets.find(game); a != m_assets.end() && a->second.kind == "game") {
            r["privatePrice"] = a->second.meta.value("privatePrice", 0LL);
            r["privateUntil"] = privateUntil(me, game);
            r["privateNeedsBuy"] = !privateBlocked(me, game).empty();
        }
        return r;
    }
    // Buy (or add 30 days to) your own private server of a game.
    if (name == "servers.buyPrivate") {
        if (me.userId == 0) return fail("Sign up to buy things.");
        auto it = m_assets.find(game);
        if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
        Asset& a = it->second;
        const long long price = a.meta.value("privatePrice", 0LL);
        if (price <= 0) return fail("Private servers of this game are free.");
        if (a.creator == me.id) return fail("Your own game's private servers are free for you.");
        if (balance(me) < price) return fail("You need " + std::to_string(price - balance(me)) + " more Bolts for that.");
        const std::string ref = "private:" + game + ":" + Account::randomHex(4);
        add(me, -price, "Private server: " + a.name, ref);
        paySeller(a, me, price * Online::kCreatorSharePercent / 100, ref);   // to the group, or the creator
        if (!me.privateServers.is_object()) me.privateServers = json::object();
        const long long now = Online::unixNow();
        me.privateServers[game] = std::max(now, privateUntil(me, game)) + kPrivateDays * 86400;
        saveUsers();
        saveAssets();
        json r = okay(); r["until"] = me.privateServers[game]; r["me"] = meJson(me); return r;
    }
    return fail("Unknown request.");
}

bool GbServer::startOnPool(const Asset& game, const json& extra) {
    const long long now = Online::unixNow();
    const std::string continues = extra.value("continues", std::string()), code = extra.value("code", std::string());
    const std::string what = !continues.empty() ? "moved:" + continues : !code.empty() ? "code:" + code : game.id;
    Client* pick = nullptr;
    int pickFree = 0;
    for (auto& [ctl, pool] : m_pools) {
        for (auto it = pool.starting.begin(); it != pool.starting.end();)
            it = now - it->second > kPoolStartWait ? pool.starting.erase(it) : std::next(it);
        if (pool.starting.count(what)) return true;   // on its way
        int running = 0;
        for (const auto& [id, s] : m_sessions) if (s.dedicated && s.host == pool.account) ++running;
        const int free = pool.slots - running - (int)pool.starting.size();
        if (free > pickFree && !ctl->closing) { pick = ctl; pickFree = free; }
    }
    if (!pick) return false;
    m_pools[pick].starting[what] = now;
    const int max = game.meta.contains("maxPlayers") && game.meta["maxPlayers"].is_number_integer() ? game.meta["maxPlayers"].get<int>() : kDefaultMax;
    json start = {{"t", "start"}, {"game", game.id}, {"title", game.name}, {"max", std::clamp(max, 2, kMostPlayers)}};
    if (!continues.empty()) start["continues"] = continues;
    else if (!code.empty()) { start["private"] = true; start["code"] = code; start["owner"] = extra.value("owner", std::string()); }
    pick->conn->send(start.dump());
    return true;
}

void GbServer::relayRequest(Client& c, const json& req) {
    auto reply = [&](json r) {
        r["t"] = "relay";
        c.conn->send(r.dump());
    };
    User* me = nullptr;
    json bad = checkRequest(req, me);
    if (!bad.is_null()) { reply(bad); c.closing = true; return; }
    std::string op = req.value("op", std::string());
    json args = req.contains("args") && req["args"].is_object() ? req["args"] : json::object();
    long long now = Online::unixNow();

    if (op == "relay.pool") {   // a game server machine says it's ready to run games (staff only)
        if (!isStaff(*me)) { reply(fail("Only Guts&Bolts staff can run game servers.")); c.closing = true; return; }
        c.mode = Client::Mode::Pool;
        c.account = me->id;
        Pool& pool = m_pools[&c];
        pool.account = me->id;
        pool.slots = std::clamp(args.contains("slots") && args["slots"].is_number_integer() ? args["slots"].get<int>() : 4, 1, kPoolMost);
        log("game server machine ready (" + std::to_string(pool.slots) + " games) by " + me->id.substr(0, 8));
        json r = okay();
        r["pool"] = true;
        reply(r);
        return;
    }
    if (op == "relay.host") {
        // A game server machine starting a game it was asked to: nobody plays on it.
        const bool dedicated = args.value("dedicated", false) && isStaff(*me);
        size_t hosting = 0;
        for (const auto& [id, s] : m_sessions) if (s.host == me->id && !s.dedicated) ++hosting;
        if (!dedicated && hosting >= kHostedEach) { reply(fail("You're already running " + std::to_string(kHostedEach) + " servers.")); c.closing = true; return; }
        Session s;
        s.id = "s-" + Account::randomHex(6);
        s.game = Online::cleanText(args.value("game", std::string()), 80);
        if (auto f = findAsset(s.game); f != m_assets.end()) s.game = f->first;
        s.title = say(args.value("title", std::string()), 60);
        if (s.title.empty()) s.title = "A game";
        s.host = me->id;
        s.priv = args.value("private", false);
        s.max = std::clamp(args.contains("max") && args["max"].is_number_integer() ? args["max"].get<int>() : kDefaultMax, 2, kMostPlayers);
        s.created = now;
        s.control = &c;
        s.dedicated = dedicated;
        if (s.dedicated) {
            // Someone's private server (we gave the machine its code and owner in startOnPool).
            const std::string code = upper(Online::cleanText(args.value("code", std::string()), 12));
            s.priv = s.priv && !code.empty();
            if (s.priv) {
                s.code = code;
                std::string owner = Online::cleanText(args.value("owner", std::string()), 64);
                for (char& ch : owner) ch = (char)std::tolower((unsigned char)ch);
                if (isHex(owner, 64, 64)) s.owner = owner;
            }
            for (auto& [ctl, pool] : m_pools) {
                if (pool.account != me->id) continue;
                pool.starting.erase(s.priv ? "code:" + s.code : s.game);
                pool.starting.erase("moved:" + args.value("continues", std::string()));
            }
        }
        // Taking over a server whose host left: same game, name, privacy and code, so everyone can
        // follow. (One of its players, or a game server machine.)
        if (auto old = m_moved.find(args.value("continues", std::string())); old != m_moved.end()) {
            Moved& m = old->second;
            if (!m.newId.empty() || (!dedicated && std::find(m.members.begin(), m.members.end(), me->id) == m.members.end())) {
                reply(fail("Someone else is already hosting the new server."));
                c.closing = true;
                return;
            }
            s.game = m.game; s.title = m.title; s.priv = m.priv; s.code = m.code; s.owner = m.owner; s.max = m.max;
            m.newId = s.id;
        }
        if (s.priv && !dedicated && args.value("continues", std::string()).empty()) {
            if (const std::string blocked = privateBlocked(*me, s.game); !blocked.empty()) { reply(fail(blocked)); c.closing = true; return; }
        }
        if (s.priv && s.code.empty()) {
            do s.code = makeCode();
            while (std::any_of(m_sessions.begin(), m_sessions.end(), [&](const auto& kv) { return kv.second.code == s.code; }));
        }
        c.mode = Client::Mode::HostControl;
        c.account = me->id;
        c.session = s.id;
        json r = okay();
        r["session"] = s.id;
        r["code"] = s.code;
        r["private"] = s.priv;
        log(std::string(s.priv ? "private" : "public") + " server " + s.id + " (" + s.title + ") by " + me->id.substr(0, 8));
        m_sessions[s.id] = std::move(s);
        reply(r);
        return;
    }

    if (op == "relay.join") {
        Session* s = nullptr;
        std::string code = upper(Online::cleanText(args.value("code", std::string()), 12));
        if (!code.empty()) {
            for (auto& [id, x] : m_sessions) if (x.code == code) s = &x;
            if (!s) { reply(fail("There's no private server with that code (it may have closed).")); c.closing = true; return; }
        } else if (auto it = m_sessions.find(args.value("session", std::string())); it != m_sessions.end()) {
            s = &it->second;
        }
        // Its host left: follow everyone to the new server (or be asked to host it).
        bool following = false;
        if (auto old = !s && code.empty() ? m_moved.find(args.value("session", std::string())) : m_moved.end();
            old != m_moved.end() && std::find(old->second.members.begin(), old->second.members.end(), me->id) != old->second.members.end()) {
            Moved& m = old->second;
            if (auto it = m.newId.empty() ? m_sessions.end() : m_sessions.find(m.newId); it != m_sessions.end()) s = &it->second;
            if (!s) {
                if (!m.newId.empty()) { reply(fail("The new server closed too.")); c.closing = true; return; }
                if (m.machineUntil) {
                    if (now <= m.machineUntil) {   // a game server machine is starting it: everyone just waits
                        reply(json{{"ok", false}, {"hostLeft", true}, {"you", false}, {"error", "The host left. Moving to a new server..."}});
                        c.closing = true;
                        return;
                    }
                    m.machineUntil = 0; m.heirSince = now;   // it didn't: one of the players hosts it after all
                }
                if (now - m.heirSince > kHeirWait) { ++m.heir; m.heirSince = now; }   // they didn't start it: next in line
                if (m.heir >= m.members.size()) { reply(fail("That server has closed.")); c.closing = true; return; }
                reply(json{{"ok", false}, {"hostLeft", true}, {"you", m.members[m.heir] == me->id},
                           {"error", "The host left. Moving to a new server..."}});
                c.closing = true;
                return;
            }
            following = true;
        }
        if (!s) { reply(fail("That server has closed.")); c.closing = true; return; }
        if (s->host == me->id && !s->dedicated) { reply(fail("That's your own server.")); c.closing = true; return; }
        if (s->priv && code.empty() && !following && !me->friends.count(ownerOf(*s)) && ownerOf(*s) != me->id) {
            reply(fail("That's a private server. You need to be the host's friend, or have its code."));
            c.closing = true;
            return;
        }
        int waiting = 0;
        for (const auto& o : m_clients) if (o->mode == Client::Mode::PendingJoin && o->session == s->id) ++waiting;
        if (headcount(*s) + 1 + waiting > s->max) { reply(fail("That server is full.")); c.closing = true; return; }
        c.mode = Client::Mode::PendingJoin;
        c.account = me->id;
        c.session = s->id;
        c.ticket = Account::randomHex(16);
        c.since = now;
        // The host only hears a ticket (and who's coming), never an address.
        s->control->conn->send(json{{"t", "incoming"}, {"ticket", c.ticket}, {"account", me->id}, {"name", me->name},
                                                    {"guest", me->userId == 0}}.dump());
        return;
    }
    reply(fail("Unknown request."));
    c.closing = true;
}

void GbServer::relayAccept(Client& c, const std::string& ticket) {
    Client* joiner = nullptr;
    if (ticket.size() >= 16)
        for (auto& o : m_clients)
            if (o->mode == Client::Mode::PendingJoin && o->ticket == ticket) joiner = o.get();
    auto it = joiner ? m_sessions.find(joiner->session) : m_sessions.end();
    if (!joiner || it == m_sessions.end()) { c.closing = true; return; }
    Session& s = it->second;
    joiner->mode = c.mode = Client::Mode::Pipe;
    joiner->peer = &c;
    joiner->ticket.clear();
    c.peer = joiner;
    c.session = s.id;
    c.account = s.host;
    s.players.insert(joiner);
    // Everything after this line is the game's own messages, passed straight through.
    joiner->conn->send(json{{"t", "relay"}, {"ok", true}, {"title", s.title}, {"session", s.id}, {"game", s.game}}.dump());
}

void GbServer::relayStep(long long now) {
    for (auto it = m_moved.begin(); it != m_moved.end();) it = now > it->second.until ? m_moved.erase(it) : std::next(it);
    for (auto& c : m_clients) {
        if (c->mode != Client::Mode::PendingJoin || c->closing) continue;
        bool gone = !m_sessions.count(c->session);
        if (gone || now - c->since > kJoinWait) {
            c->conn->send(json{{"t", "relay"}, {"ok", false},
                               {"error", gone ? "That server just closed." : "The server's host didn't answer. Try another server."}}.dump());
            c->closing = true;
        }
    }
}

void GbServer::dropClients(long long now) {
    std::set<Client*> dead;
    for (auto& up : m_clients) {
        Client* c = up.get();
        long long quiet = now - c->lastActive;
        bool idle = c->mode == Client::Mode::HostControl || c->mode == Client::Mode::Pool ? quiet > kHostSilence
                  : c->mode == Client::Mode::Pipe        ? quiet > kPipeSilence
                  : c->mode == Client::Mode::PendingJoin ? false
                  : quiet > 120 && c->conn->pendingBytes() == 0;
        if (!c->conn->alive() || idle || (c->closing && c->conn->pendingBytes() == 0)) dead.insert(c);
    }
    if (dead.empty()) return;
    // A pipe takes its other end with it; a host leaving closes its server (and everyone in it).
    for (bool grew = true; grew;) {
        grew = false;
        for (Client* c : std::vector<Client*>(dead.begin(), dead.end()))
            if (c->peer && dead.insert(c->peer).second) grew = true;
        for (auto it = m_sessions.begin(); it != m_sessions.end();) {
            if (!dead.count(it->second.control)) { ++it; continue; }
            // The players stay together: remember them (longest-playing first) so one can host a new server.
            std::vector<Client*> left;
            for (Client* p : it->second.players) if (!p->account.empty()) left.push_back(p);   // (their pipes die with the host, but they're still here)
            std::sort(left.begin(), left.end(), [](Client* a, Client* b) { return a->since < b->since; });
            if (!left.empty()) {
                Moved m;
                m.game = it->second.game; m.title = it->second.title; m.code = it->second.code;
                m.owner = ownerOf(it->second); m.priv = it->second.priv; m.max = it->second.max;
                for (Client* p : left) m.members.push_back(p->account);
                m.heirSince = now;
                m.until = now + kMoveWait;
                // A free game server machine takes it over, so nobody has to host it (and it won't
                // happen again when the next person leaves). If none is free, one of the players does.
                if (auto a = m_assets.find(m.game); a != m_assets.end() && a->second.kind == "game" &&
                    startOnPool(a->second, {{"continues", it->first}}))
                    m.machineUntil = now + kPoolStartWait;
                m_moved[it->first] = std::move(m);
            }
            for (Client* p : it->second.players) if (dead.insert(p).second) grew = true;
            log("server " + it->first + " closed");
            it = m_sessions.erase(it);
        }
    }
    for (auto& [id, s] : m_sessions)
        for (Client* c : dead) s.players.erase(c);
    for (Client* c : dead) m_pools.erase(c);
    m_clients.erase(std::remove_if(m_clients.begin(), m_clients.end(),
                                   [&](const std::unique_ptr<Client>& c) { return dead.count(c.get()) > 0; }),
                    m_clients.end());
}
