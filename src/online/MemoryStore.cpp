#include "MemoryStore.h"

#include <algorithm>
#include <cmath>

using json = nlohmann::json;

namespace {
json okay() { return {{"ok", true}}; }
json fail(const std::string& error) { return {{"ok", false}, {"error", error}}; }

std::string str(const json& args, const char* k) {
    return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string();
}
double num(const json& args, const char* k, double fallback) {
    return args.contains(k) && args[k].is_number() ? args[k].get<double>() : fallback;
}

// Sorted maps keep items in sort key order (none first, then numbers, then text), then by key.
int sortRank(const json& s) { return s.is_number() ? 1 : s.is_string() ? 2 : 0; }
int compareSort(const json& a, const json& b) {
    const int ra = sortRank(a), rb = sortRank(b);
    if (ra != rb) return ra < rb ? -1 : 1;
    if (ra == 1) { const double x = a.get<double>(), y = b.get<double>(); return x < y ? -1 : x > y ? 1 : 0; }
    if (ra == 2) { const std::string& x = a.get_ref<const std::string&>(); const std::string& y = b.get_ref<const std::string&>(); return x < y ? -1 : x > y ? 1 : 0; }
    return 0;
}
// Where an item (sortKey, key) is compared with a bound {sortKey, key}: a bound with no key
// stands for every key with its sort key, so it's never equal (strictly outside, either way).
int compareToBound(const json& sortKey, const std::string& key, const json& bound, bool lower) {
    const int c = compareSort(sortKey, bound.contains("sortKey") ? bound["sortKey"] : json());
    if (c != 0) return c;
    if (!bound.contains("key") || !bound["key"].is_string()) return lower ? -1 : 1;
    const std::string& b = bound["key"].get_ref<const std::string&>();
    return key < b ? -1 : key > b ? 1 : 0;
}
} // namespace

void MemoryStore::expire(double now) {
    for (auto m = m_maps.begin(); m != m_maps.end();) {
        for (auto it = m->second.begin(); it != m->second.end();) {
            if (it->second.expires <= now) { m_used -= it->second.size; it = m->second.erase(it); }
            else ++it;
        }
        m = m->second.empty() ? m_maps.erase(m) : std::next(m);
    }
    for (auto q = m_queues.begin(); q != m_queues.end();) {
        auto& list = q->second;
        for (auto it = list.begin(); it != list.end();) {
            if (it->expires <= now) { m_used -= it->size; it = list.erase(it); }
            else ++it;
        }
        q = list.empty() ? m_queues.erase(q) : std::next(q);
    }
}

json MemoryStore::run(const std::string& op, const json& args, double now) {
    expire(now);
    const bool queue = op == "mem.qadd" || op == "mem.qread" || op == "mem.qremove";
    const std::string name = str(args, queue ? "queue" : "map");
    const char* what = queue ? "Queue" : args.value("sorted", false) ? "SortedMap" : "HashMap";
    if (name.empty() || name.size() > kName)
        return fail(std::string("MemoryStore ") + what + " names are 1-" + std::to_string(kName) + " letters.");
    // A value going in: not too big, and an expiration (seconds, 45 days at most).
    auto checkValue = [&](const json& value, double& expires, size_t extra, size_t& size) -> json {
        size = value.dump().size() + extra;
        if (value.dump().size() > kValue)
            return fail("That value is too big for a MemoryStore (" + std::to_string(kValue / 1024) + " KB at most).");
        const double expiration = num(args, "expiration", -1.0);
        if (!(expiration > 0.0) || expiration > kMostExpiration)
            return fail("MemoryStore: the expiration is in seconds, from 1 to " + std::to_string((long long)kMostExpiration) + " (45 days).");
        expires = now + expiration;
        return nullptr;
    };
    auto roomFor = [&](size_t more, size_t freed) -> json {
        if (m_used - freed + more > limit(m_players))
            return fail("This game's MemoryStores are full (" + std::to_string(limit(m_players) / 1024) +
                        " KB: 64 KB + 1 KB per player). Remove some, or use shorter expirations.");
        return nullptr;
    };

    if (!queue) {
        const std::string key = str(args, "key");
        if (op != "mem.range" && (key.empty() || key.size() > kName))
            return fail("MemoryStore keys are 1-" + std::to_string(kName) + " letters.");
        const bool sorted = args.value("sorted", false);
        auto& map = m_maps[(sorted ? "s:" : "h:") + name];
        auto tidy = [&]() { if (map.empty()) m_maps.erase((sorted ? "s:" : "h:") + name); };
        if (op == "mem.get") {
            json r = okay();
            auto it = map.find(key);
            r["found"] = it != map.end();
            r["value"] = it != map.end() ? it->second.value : json();
            if (it != map.end() && !it->second.sortKey.is_null()) r["sortKey"] = it->second.sortKey;
            tidy();
            return r;
        }
        if (op == "mem.set") {
            if (!args.contains("value") || args["value"].is_null()) { tidy(); return fail("MemoryStore: SetAsync needs a value (use RemoveAsync to remove one)."); }
            Item item;
            item.value = args["value"];
            if (sorted && args.contains("sortKey") && !args["sortKey"].is_null()) {
                item.sortKey = args["sortKey"];
                if (!item.sortKey.is_number() && !item.sortKey.is_string()) { tidy(); return fail("MemoryStore: a sort key is a number or text."); }
                if (item.sortKey.is_string() && item.sortKey.get<std::string>().size() > kName) { tidy(); return fail("MemoryStore: a sort key is at most 128 letters."); }
            }
            json bad = checkValue(item.value, item.expires, key.size() + item.sortKey.dump().size(), item.size);
            auto old = map.find(key);
            if (bad.is_null()) bad = roomFor(item.size, old != map.end() ? old->second.size : 0);
            if (!bad.is_null()) { tidy(); return bad; }
            const bool created = old == map.end();
            if (!created) m_used -= old->second.size;
            m_used += item.size;
            map[key] = std::move(item);
            json r = okay();
            r["created"] = created;   // (SetAsync returns it)
            return r;
        }
        if (op == "mem.remove") {
            auto it = map.find(key);
            if (it != map.end()) { m_used -= it->second.size; map.erase(it); }
            tidy();
            return okay();
        }
        if (op == "mem.range") {
            if (!sorted) { tidy(); return fail("GetRangeAsync is for sorted maps."); }
            const bool down = str(args, "direction") == "Descending";
            const int count = (int)num(args, "count", 0);
            if (count < 1 || count > kMostRange) { tidy(); return fail("GetRangeAsync: count is 1-" + std::to_string(kMostRange) + "."); }
            const json lower = args.contains("lower") && args["lower"].is_object() ? args["lower"] : json();
            const json upper = args.contains("upper") && args["upper"].is_object() ? args["upper"] : json();
            std::vector<std::pair<const std::string*, const Item*>> all;
            for (const auto& [k, it] : map) {
                if (!lower.is_null() && compareToBound(it.sortKey, k, lower, true) <= 0) continue;
                if (!upper.is_null() && compareToBound(it.sortKey, k, upper, false) >= 0) continue;
                all.push_back({&k, &it});
            }
            std::sort(all.begin(), all.end(), [&](const auto& a, const auto& b) {
                int c = compareSort(a.second->sortKey, b.second->sortKey);
                if (c == 0) c = a.first->compare(*b.first);
                return down ? c > 0 : c < 0;
            });
            json r = okay();
            r["items"] = json::array();
            for (size_t i = 0; i < all.size() && (int)i < count; ++i) {
                json item = {{"key", *all[i].first}, {"value", all[i].second->value}};
                if (!all[i].second->sortKey.is_null()) item["sortKey"] = all[i].second->sortKey;
                r["items"].push_back(std::move(item));
            }
            tidy();
            return r;
        }
        tidy();
        return fail("Unknown request.");
    }

    auto& list = m_queues[name];
    auto tidy = [&]() { if (list.empty()) m_queues.erase(name); };
    if (op == "mem.qadd") {
        QueueItem item;
        item.value = args.contains("value") ? args["value"] : json();
        if (item.value.is_null()) { tidy(); return fail("MemoryStore: AddAsync needs a value."); }
        item.priority = num(args, "priority", 0.0);
        json bad = checkValue(item.value, item.expires, 16, item.size);
        if (bad.is_null()) bad = roomFor(item.size, 0);
        if (!bad.is_null()) { tidy(); return bad; }
        item.seq = ++m_seq;
        m_used += item.size;
        list.push_back(std::move(item));
        return okay();
    }
    if (op == "mem.qread") {
        const int count = (int)num(args, "count", 1);
        if (count < 1 || count > 100) { tidy(); return fail("ReadAsync: count is 1-100."); }
        const double invisible = std::clamp(num(args, "invisible", 30.0), 1.0, 3600.0);
        std::vector<QueueItem*> ready;
        for (QueueItem& q : list) if (q.hiddenUntil <= now) ready.push_back(&q);
        std::sort(ready.begin(), ready.end(), [](const QueueItem* a, const QueueItem* b) {
            return a->priority != b->priority ? a->priority > b->priority : a->seq < b->seq;
        });
        json r = okay();
        r["items"] = json::array();
        r["id"] = nullptr;
        if (ready.empty() || (args.value("all", false) && (int)ready.size() < count)) { tidy(); return r; }
        const std::string id = "r" + std::to_string(++m_reads);
        for (size_t i = 0; i < ready.size() && (int)i < count; ++i) {
            ready[i]->hiddenUntil = now + invisible;   // others don't see them unless they aren't removed in time
            ready[i]->readId = id;
            r["items"].push_back(ready[i]->value);
        }
        r["id"] = id;
        return r;
    }
    if (op == "mem.qremove") {
        const std::string id = str(args, "id");
        if (id.empty()) { tidy(); return fail("RemoveAsync needs the id ReadAsync gave."); }
        for (auto it = list.begin(); it != list.end();) {
            if (it->readId == id && it->hiddenUntil > now) { m_used -= it->size; it = list.erase(it); }
            else ++it;
        }
        tidy();
        return okay();
    }
    tidy();
    return fail("Unknown request.");
}
