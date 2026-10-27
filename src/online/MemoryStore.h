#pragma once
// MemoryStoreService's data for one game: quick, short-lived data that every server of the
// game shares (a global leaderboard, a matchmaking queue, an auction house). Everything
// has an expiration and goes away by itself; nothing is saved to disk.
//
// The same code keeps it in the C++ server (src/server/ServerData.cpp) and, when a game
// isn't running online, inside the game itself (ScriptEngine). worker/server.js memOp
// follows the same rules.
//
// Requests (each also has `game`, which picks the MemoryStore):
//   mem.get    {map, key, sorted}                    -> {found, value, sortKey}
//   mem.set    {map, key, value, expiration, sorted, sortKey} -> {created}
//   mem.remove {map, key, sorted}
//   mem.range  {map, direction "Ascending"/"Descending", count, lower, upper}
//              -> {items: [{key, value, sortKey}]}   (sorted maps; lower/upper: {key, sortKey}, left out)
//   mem.qadd   {queue, value, expiration, priority}
//   mem.qread  {queue, count, all, wait, invisible} -> {items: [value], id}
//   mem.qremove {queue, id}
// Hash maps and sorted maps with the same name are different things.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

class MemoryStore {
public:
    // Run one request (`now` = seconds, any clock that counts up). The reply is
    // {"ok": true, ...} or {"ok": false, "error": "..."}.
    nlohmann::json run(const std::string& op, const nlohmann::json& args, double now);
    // How much it holds (bytes of keys and values), and the most it may (64 KB + 1 KB per player).
    size_t used() const { return m_used; }
    static size_t limit(int players) { return 64 * 1024 + 1024 * (size_t)std::max(0, players); }
    void setPlayers(int players) { m_players = players; }
    bool empty() const { return m_maps.empty() && m_queues.empty(); }

    static constexpr size_t kName = 128;          // map / queue names and keys
    static constexpr size_t kValue = 32 * 1024;   // one value, as JSON
    static constexpr double kMostExpiration = 45 * 24 * 3600.0;
    static constexpr int    kMostRange = 200;     // items from one GetRangeAsync / ReadAsync

private:
    struct Item { nlohmann::json value, sortKey; double expires = 0.0; size_t size = 0; };
    struct QueueItem { nlohmann::json value; double priority = 0.0, expires = 0.0, hiddenUntil = 0.0; long long seq = 0; std::string readId; size_t size = 0; };
    void expire(double now);
    std::map<std::string, std::map<std::string, Item>> m_maps;   // "s:" + name sorted, "h:" + name hash
    std::map<std::string, std::vector<QueueItem>> m_queues;
    size_t m_used = 0;
    int m_players = 0;
    long long m_seq = 0, m_reads = 0;
};
