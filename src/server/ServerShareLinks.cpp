// Share links: links to a game, item, profile or group that count clicks, different people, and how
// many then played, bought (and spent), favourited, followed or joined. worker/sharelinks.js has the
// same rules (and the notes on why):
//   share.create {kind, id, label}  share.list {kind, id}  share.remove {code}
//   share.open {code, visitor} (anyone)  share.done {code} (signed up)
// Kept in sharelinks.json: {links: {code: {code, kind, id, by, label, created, days: {day: {...}}}},
//                           seen: {code: ["who|what", ...]}}.
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kMostLinks = 20, kLabel = 40;
constexpr long long kKeepDays = 90;
const char* kCodeChars = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const std::vector<std::string> kKinds = {"game", "item", "profile", "group"};
const std::vector<std::string> kLibraryKinds = {"model", "decal", "audio", "plugin", "mesh", "animation", "video"};
std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
std::vector<std::string> whatFor(const std::string& kind) {
    if (kind == "game") return {"play", "favorite"};
    if (kind == "item") return {"buy", "favorite"};
    if (kind == "profile") return {"follow"};
    return {"join"};
}
long long dayNum() { return Online::unixNow() / 86400; }
template <class C> bool has(const C& c, const std::string& x) { return std::find(c.begin(), c.end(), x) != c.end(); }
} // namespace

void GbServer::saveShareLinks() {
    const long long old = dayNum() - kKeepDays;
    for (auto& [code, l] : m_shareLinks["links"].items()) {
        json& days = l["days"];
        for (auto it = days.begin(); it != days.end();) {
            if (std::atoll(it.key().c_str()) <= old) it = days.erase(it); else ++it;
        }
    }
    writeFile(m_opts.data / "sharelinks.json", m_shareLinks.dump());
}
void GbServer::loadShareLinks() {
    std::string text;
    if (readFile(m_opts.data / "sharelinks.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object()) m_shareLinks = j;
    }
    for (const char* k : {"links", "seen"})
        if (!m_shareLinks.contains(k) || !m_shareLinks[k].is_object()) m_shareLinks[k] = json::object();
}

// The thing a link points at: {id, name, path, owner, price}, or null.
json GbServer::shareThing(const std::string& kind, const json& id) {
    const std::string sid = id.is_string() ? id.get<std::string>() : id.is_number() ? std::to_string(id.get<long long>()) : std::string();
    if (kind == "game" || kind == "item") {
        auto it = findAsset(sid);
        if (it == m_assets.end() || (kind == "game") != (it->second.kind == "game")) return nullptr;
        const Asset& a = it->second;
        const std::string page = kind == "game" ? "game" : has(kLibraryKinds, a.kind) ? "library" : "item";
        return {{"id", a.id}, {"name", a.name}, {"path", "#/" + page + "/" + a.id}, {"owner", a.creator}, {"price", a.price},
                {"private", a.meta.value("access", std::string()) == "private"}};
    }
    if (kind == "profile") {
        User* u = findPerson(sid);
        if (!u || u->userId <= 0) return nullptr;
        return {{"id", u->id}, {"name", u->name}, {"path", "#/user/" + std::to_string(u->userId)}, {"owner", u->id}, {"price", 0}};
    }
    if (kind == "group") {
        auto g = m_groups.find(sid);
        if (g == m_groups.end()) return nullptr;
        return {{"id", g->second.id}, {"name", g->second.name}, {"path", "#/group/" + g->second.id}, {"owner", g->second.owner}, {"price", 0}};
    }
    return nullptr;
}

bool GbServer::shareIsDone(const json& t, const std::string& kind, const std::string& what, const User& me) const {
    const std::string id = sv(t, "id");
    if (what == "play") return has(me.played, id);
    if (what == "buy") return me.owned.count(id) > 0;
    if (what == "favorite") return kind == "game" ? has(me.favorites, id) : has(me.itemFavorites, id);
    if (what == "follow") return me.following.count(id) > 0 || me.friends.count(id) > 0;
    if (what == "join") { auto g = m_groups.find(id); return g != m_groups.end() && g->second.members.count(me.id) > 0; }
    return false;
}

json GbServer::shareJson(const json& l) const {
    json total = {{"clicks", 0}, {"people", 0}, {"play", 0}, {"buy", 0}, {"favorite", 0}, {"follow", 0}, {"join", 0}, {"spent", 0}};
    json days = json::array();
    for (auto& [d, v] : l["days"].items()) {
        for (auto& [k, n] : total.items()) n = n.get<long long>() + v.value(k, 0LL);
        json row = v;
        row["day"] = std::atoll(d.c_str());
        days.push_back(row);
    }
    std::sort(days.begin(), days.end(), [](const json& a, const json& b) { return a.value("day", 0LL) < b.value("day", 0LL); });
    const std::string kind = sv(l, "kind");
    const long long people = total["people"].get<long long>(), main = total[whatFor(kind)[0]].get<long long>();
    return {{"code", l["code"]}, {"label", l["label"]}, {"kind", l["kind"]}, {"id", l["id"]}, {"created", l["created"]},
            {"path", "/s/" + sv(l, "code")}, {"total", total}, {"days", days},
            {"conversion", people > 0 ? std::round(main * 1000.0 / people) / 10.0 : 0.0}};
}

json GbServer::shareOp(const std::string& name, User& me, const json& args) {
    json& links = m_shareLinks["links"];
    json& seen = m_shareLinks["seen"];
    auto first = [&](const std::string& code, const std::string& who, const std::string& what) {
        json& list = seen[code];
        if (!list.is_array()) list = json::array();
        const std::string key = who + "|" + what;
        if (std::find(list.begin(), list.end(), key) != list.end()) return false;
        list.push_back(key);
        return true;
    };
    auto dayOf = [&](json& l) -> json& {
        json& d = l["days"][std::to_string(dayNum())];
        if (!d.is_object()) d = {{"clicks", 0}, {"people", 0}, {"play", 0}, {"buy", 0}, {"favorite", 0}, {"follow", 0}, {"join", 0}, {"spent", 0}};
        return d;
    };
    const std::string code = sv(args, "code").substr(0, 16);

    if (name == "share.open") {
        if (!links.contains(code)) return fail("That link doesn't go anywhere any more.");
        json& l = links[code];
        const json t = shareThing(sv(l, "kind"), l["id"]);
        if (t.is_null()) return fail("That link doesn't go anywhere any more.");
        std::string visitor;
        for (char c : sv(args, "visitor")) if (std::isalnum((unsigned char)c) && visitor.size() < 40) visitor += c;
        if (visitor.empty()) visitor = "anon";
        json& d = dayOf(l);
        d["clicks"] = d["clicks"].get<long long>() + 1;
        if (first(code, "v:" + (me.userId > 0 ? me.id : visitor), "open")) d["people"] = d["people"].get<long long>() + 1;
        if (me.userId > 0)
            for (const std::string& w : whatFor(sv(l, "kind"))) if (shareIsDone(t, sv(l, "kind"), w, me)) first(code, me.id, w);
        saveShareLinks();
        json r = okay(); r["kind"] = l["kind"]; r["id"] = t["id"]; r["path"] = t["path"]; r["name"] = t["name"]; return r;
    }
    if (name == "share.done") {
        json r = okay(); r["counted"] = json::array();
        if (me.userId == 0 || !links.contains(code)) return r;
        json& l = links[code];
        const json t = shareThing(sv(l, "kind"), l["id"]);
        if (t.is_null() || sv(t, "owner") == me.id) return r;
        for (const std::string& w : whatFor(sv(l, "kind"))) {
            if (!shareIsDone(t, sv(l, "kind"), w, me) || !first(code, me.id, w)) continue;
            json& d = dayOf(l);
            d[w] = d[w].get<long long>() + 1;
            if (w == "buy") d["spent"] = d["spent"].get<long long>() + t.value("price", 0LL);
            r["counted"].push_back(w);
        }
        if (!r["counted"].empty()) saveShareLinks();
        return r;
    }
    if (me.userId == 0) return fail("Sign up to make share links.");
    if (name == "share.remove") {
        if (!links.contains(code)) return fail("That link isn't there.");
        const json& l = links[code];
        const json t = shareThing(sv(l, "kind"), l["id"]);
        if (!isStaff(me) && sv(l, "by") != me.id && !(t.is_object() && sv(t, "owner") == me.id)) return fail("Only its owner can remove that link.");
        links.erase(code);
        seen.erase(code);
        saveShareLinks();
        return okay();
    }
    const std::string kind = sv(args, "kind");
    if (!has(kKinds, kind)) return fail("Share links are for games, items, profiles and groups.");
    const json t = shareThing(kind, kind == "profile" && !args.contains("id") ? json(me.id) : args.value("id", json()));
    if (t.is_null()) return fail("That isn't there.");
    const bool mine = sv(t, "owner") == me.id;
    if (!mine && !(isStaff(me) && name == "share.list"))
        return fail(kind == "profile" ? "You can only make links to your own profile." : "Only its owner can make share links for it.");
    std::vector<const json*> ours;
    for (auto& [c, l] : links.items()) if (sv(l, "kind") == kind && sv(l, "id") == sv(t, "id")) ours.push_back(&l);
    if (name == "share.list") {
        std::sort(ours.begin(), ours.end(), [](const json* a, const json* b) { return a->value("created", 0LL) > b->value("created", 0LL); });
        json out = json::array();
        for (const json* l : ours) out.push_back(shareJson(*l));
        json r = okay(); r["links"] = out; r["most"] = kMostLinks; return r;
    }
    if (name == "share.create") {
        if (kind == "game" && t.value("private", false)) return fail("Make the game public (or friends-only) first: nobody could open it.");
        if (ours.size() >= kMostLinks) return fail("A thing can have " + std::to_string(kMostLinks) + " links. Remove an old one first.");
        std::string label;
        for (char c : sv(args, "label")) if ((unsigned char)c >= 32 && c != '<' && c != '>') label += c;
        while (!label.empty() && label.front() == ' ') label.erase(label.begin());
        while (!label.empty() && label.back() == ' ') label.pop_back();
        if (label.size() > kLabel) label.resize(kLabel);
        if (label.empty()) label = "Link " + std::to_string(ours.size() + 1);
        std::random_device rd;
        std::string fresh;
        do {
            fresh.clear();
            for (int i = 0; i < 8; ++i) fresh += kCodeChars[rd() % 56];
        } while (links.contains(fresh));
        links[fresh] = {{"code", fresh}, {"kind", kind}, {"id", t["id"]}, {"by", me.id}, {"label", label},
                        {"created", Online::unixNow()}, {"days", json::object()}};
        saveShareLinks();
        json r = okay(); r["link"] = shareJson(links[fresh]); return r;
    }
    return fail("Unknown request.");
}
