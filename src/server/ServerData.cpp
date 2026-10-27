// DataStores: what games save between visits (coins, levels...), kept on the server
// so every server of a game sees the same data (worker/server.js dataOp has the same).
//
// Only servers we trust can read and write a game's data: game server machines and
// other staff, and the game's creator. Games other people host save on their own
// computer instead (ScriptEngine falls back), so nobody can hand themselves free coins.
//
//   data.can       {game}                     -> {save}  may this account use it?
//   data.get       {game, store, key}         -> {value} (null if nothing's saved)
//   data.set       {game, store, key, value}  (no value / null: remove it)
//   data.increment {game, store, key, delta}  -> {value} (in one step, so two servers adding at once both count)
//
// MessagingService (msgOp, below): messages between the running servers of one game.
//   msg.publish {game, topic, message}  -> {n, sent}
//   msg.poll    {game, after, topics[]} -> {server, messages: [{n, topic, message, sent}], last}
//
// MemoryStoreService (memOp, at the end): mem.* requests, see src/online/MemoryStore.h.
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

#include <cctype>
#include <cmath>
#include <set>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kDataName  = 100;           // store names and keys
constexpr size_t kDataValue = 256 * 1024;    // one value, as JSON
constexpr size_t kDataKeys  = 100000;        // keys in one game
// MessagingService (worker/server.js has the same rules).
constexpr size_t    kMsgTopic   = 80;        // topic names: 1-80 letters
constexpr size_t    kMsgSize    = 1024;      // one message, as JSON (bytes)
constexpr long long kMsgKeep    = 30;        // seconds a message waits to be picked up
constexpr size_t    kMsgKept    = 500;       // messages kept per game at most
constexpr size_t    kMsgPerPoll = 200;       // handed out per msg.poll
constexpr long long kMsgBase = 150, kMsgPerPlayer = 60;   // sends a minute per server: 150 + 60 for each player in it
} // namespace

json& GbServer::gameData(const std::string& game) {
    auto it = m_gameData.find(game);
    if (it != m_gameData.end()) return it->second;
    json& d = m_gameData[game];
    std::string text;
    if (readFile(m_opts.data / "gamedata" / (game + ".json"), text)) d = json::parse(text, nullptr, false);
    if (!d.is_object()) d = json::object();
    return d;
}

void GbServer::saveGameData(const std::string& game) {
    std::error_code ec;
    fs::create_directories(m_opts.data / "gamedata", ec);
    writeFile(m_opts.data / "gamedata" / (game + ".json"), gameData(game).dump());
}

json GbServer::dataOp(const std::string& name, User& me, const json& args) {
    std::string game = Online::cleanText(args.value("game", std::string()), 80);
    auto a = findAsset(game);
    if (a == m_assets.end() || a->second.kind != "game") return fail("That game isn't there.");
    game = a->first;
    for (char c : game)   // (it names a file)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return fail("That game isn't there.");
    const bool trusted = isStaff(me) || a->second.creator == me.id;
    if (name == "data.can") { json r = okay(); r["save"] = trusted; return r; }
    if (!trusted) return fail("Only the game's own servers can use its saved data.");
    const std::string store = args.contains("store") && args["store"].is_string() ? args["store"].get<std::string>() : "";
    const std::string key = args.contains("key") && args["key"].is_string() ? args["key"].get<std::string>() : "";
    if (store.empty() || key.empty() || store.size() > kDataName || key.size() > kDataName)
        return fail("DataStore names and keys are 1-" + std::to_string(kDataName) + " letters.");
    json& all = gameData(game);
    auto read = [&]() -> json {
        if (all.contains(store) && all[store].contains(key)) return all[store][key];
        return nullptr;
    };
    auto write = [&](const json& value) -> json {   // null = fine, else the failure reply
        if (value.is_null()) {
            if (all.contains(store)) { all[store].erase(key); if (all[store].empty()) all.erase(store); }
        } else {
            if (value.dump().size() > kDataValue) return fail("That's too much to save in one key (256 KB at most).");
            if (read().is_null()) {
                size_t n = 0;
                for (auto& [s, keys] : all.items()) n += keys.size();
                if (n >= kDataKeys) return fail("This game has saved " + std::to_string(kDataKeys) + " keys already. Remove some first.");
            }
            all[store][key] = value;
        }
        saveGameData(game);
        return nullptr;
    };
    if (name == "data.get") { json r = okay(); r["value"] = read(); return r; }
    if (name == "data.set") {
        json bad = write(args.contains("value") ? args["value"] : json());
        return bad.is_null() ? okay() : bad;
    }
    if (name == "data.increment") {
        json old = read();
        if (!old.is_null() && !old.is_number()) return fail("IncrementAsync: that key doesn't hold a number.");
        const double delta = args.contains("delta") && args["delta"].is_number() ? args["delta"].get<double>() : 1.0;
        json value;
        if ((old.is_null() || old.is_number_integer()) && (!args.contains("delta") || args["delta"].is_number_integer()))
            value = (old.is_null() ? 0LL : old.get<long long>()) + (long long)delta;
        else
            value = (old.is_null() ? 0.0 : old.get<double>()) + delta;
        json bad = write(value);
        if (!bad.is_null()) return bad;
        json r = okay(); r["value"] = value; return r;
    }
    return fail("Unknown request.");
}

// MessagingService: messages between the running servers of one game (every one of them gets
// each message, the sender too; other games never see them). Only the host of a live server
// of the game can send or read them. Servers ask for new ones about once a second.
//   msg.poll: server false = you aren't running a server of it (the game keeps its messages
//   on its own computer then). after: the last n you saw (leave it out to start from now).
json GbServer::msgOp(const std::string& name, User& me, const json& args) {
    auto a = findAsset(Online::cleanText(args.value("game", std::string()), 80));
    if (a == m_assets.end() || a->second.kind != "game") return fail("That game isn't there.");
    const std::string game = a->first;
    const long long now = Online::unixNow();
    std::vector<const Session*> mine;
    for (const auto& [sid, s] : m_sessions) {
        if (s.host != me.id) continue;
        auto g = findAsset(s.game);
        if (g != m_assets.end() && g->first == game) mine.push_back(&s);
    }
    MsgBox& box = m_msgs[game];
    while (!box.list.empty() && (box.list.front().value("sent", 0LL) < now - kMsgKeep || box.list.size() > kMsgKept)) box.list.pop_front();
    if (name == "msg.poll") {
        json r = okay();
        r["server"] = !mine.empty();
        r["messages"] = json::array();
        r["last"] = 0;
        if (mine.empty()) return r;
        const long long after = args.contains("after") && args["after"].is_number() ? (long long)std::floor(args["after"].get<double>()) : -1;
        if (after < 0) { r["last"] = box.seq; return r; }
        const long long from = after > box.seq ? 0 : after;   // (the server restarted since: start again)
        std::set<std::string> topics;
        const bool filter = args.contains("topics") && args["topics"].is_array();
        if (filter)
            for (const json& t : args["topics"])
                if (t.is_string() && topics.size() < 100) topics.insert(t.get<std::string>());
        long long last = from;
        for (const json& m : box.list) {
            const long long n = m.value("n", 0LL);
            if (n <= from) continue;
            if (r["messages"].size() >= kMsgPerPoll) break;
            last = n;
            if (!filter || topics.count(m.value("topic", std::string()))) r["messages"].push_back(m);
        }
        if (r["messages"].size() < kMsgPerPoll) last = std::max(last, box.seq);
        r["last"] = last;
        return r;
    }
    if (name == "msg.publish") {
        if (mine.empty()) return fail("MessagingService: only a running server of this game can send its messages.");
        const std::string topic = args.contains("topic") && args["topic"].is_string() ? args["topic"].get<std::string>() : "";
        if (topic.empty() || topic.size() > kMsgTopic) return fail("MessagingService: topic names are 1-" + std::to_string(kMsgTopic) + " letters.");
        const json message = args.contains("message") ? args["message"] : json();
        const size_t size = message.dump().size();
        if (size > kMsgSize)
            return fail("MessagingService: that message is " + std::to_string(size) + " bytes; " + std::to_string(kMsgSize) + " bytes (1 KB) is the most.");
        // Each server may send 150 + 60 per player a minute (all of yours together, if you run several).
        long long allowed = 0;
        for (const Session* s : mine) allowed += kMsgBase + kMsgPerPlayer * headcount(*s);
        std::deque<long long>& times = m_msgSent[me.id + "\n" + game];
        while (!times.empty() && times.front() <= now - 60) times.pop_front();
        if ((long long)times.size() >= allowed)
            return fail("MessagingService: too many messages (" + std::to_string(allowed) + " a minute is the most for this server). Wait a moment.");
        times.push_back(now);
        ++box.seq;
        box.list.push_back({{"n", box.seq}, {"topic", topic}, {"message", message}, {"sent", now}});
        if (box.list.size() > kMsgKept) box.list.pop_front();
        json r = okay(); r["n"] = box.seq; r["sent"] = now; return r;
    }
    return fail("Unknown request.");
}

// MemoryStoreService: quick data shared by the running servers of one game. Like
// MessagingService, only the host of a live server of the game can use it; anyone else
// gets {ok: false, local: true} and the game keeps its MemoryStores to itself.
json GbServer::memOp(const std::string& name, User& me, const json& args) {
    auto a = findAsset(Online::cleanText(args.value("game", std::string()), 80));
    if (a == m_assets.end() || a->second.kind != "game") return fail("That game isn't there.");
    const std::string game = a->first;
    bool mine = false;
    int players = 0;
    for (const auto& [sid, s] : m_sessions) {
        auto g = findAsset(s.game);
        if (g == m_assets.end() || g->first != game) continue;
        players += headcount(s);
        if (s.host == me.id) mine = true;
    }
    if (!mine) {
        json r = fail("MemoryStoreService: only a running server of this game can use its MemoryStores.");
        r["local"] = true;
        return r;
    }
    MemoryStore& store = m_memory[game];
    store.setPlayers(players);
    json r = store.run(name, args, (double)Online::unixNow());
    if (store.empty()) m_memory.erase(game);
    return r;
}
