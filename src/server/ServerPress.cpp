// Press: news from the team, creator spotlights and behind-the-scenes posts. worker/press.js has
// the same rules (and the notes on why):
//   - only staff write; anyone reads (a read counts a view, unless quiet)
//   - a post: title, summary (the first paragraph when empty), text (paragraphs split by an empty
//     line, "## " starts a heading), kind (news, spotlight, devlog), a creator, up to kMostGames games
//   - drafts (published: false) only show to staff; a spotlight's creator is told once it's out
//   - the id is made from the title ("big-winter-jam-3f2a1c")
//   press.list {kind, page, drafts}  press.get {id, quiet}  press.write {...}  press.edit {id, ...}  press.delete {id}
// Kept in press.json ({posts: id -> post}).
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <sstream>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
const std::pair<const char*, const char*> kKinds[] = {{"news", "News"}, {"spotlight", "Creator spotlight"}, {"devlog", "Behind the scenes"}};
constexpr size_t kTitle = 120, kSummary = 300, kText = 20000, kMostGames = 6, kPageSize = 20;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
long long iv(const json& j, const char* k) { return j.contains(k) && j[k].is_number() ? (long long)j[k].get<double>() : 0; }
const char* kindName(const std::string& k) { for (const auto& [id, n] : kKinds) if (k == id) return n; return nullptr; }
std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    return s;
}

// "Big Winter Jam!" -> "big-winter-jam" (pressSlug; letters outside a-z are dropped here).
std::string slug(const std::string& title) {
    std::string out;
    for (unsigned char c : title) {
        if (std::isalnum(c) && c < 128) out += (char)std::tolower(c);
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.size() > 50) { out = out.substr(0, 50); while (!out.empty() && out.back() == '-') out.pop_back(); }
    return out.empty() ? "post" : out;
}
// The text in blocks: [{h} | {p}] (pressBlocks).
json pressBlocks(const std::string& text) {
    json out = json::array();
    std::vector<std::string> parts;
    std::string cur, line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        if (trim(line).empty()) { if (!trim(cur).empty()) parts.push_back(trim(cur)); cur.clear(); }
        else cur += (cur.empty() ? "" : "\n") + line;
    }
    if (!trim(cur).empty()) parts.push_back(trim(cur));
    for (const std::string& s : parts) {
        if (s.rfind("## ", 0) == 0) {
            const size_t nl = s.find('\n');
            out.push_back({{"h", trim(s.substr(3, nl == std::string::npos ? std::string::npos : nl - 3))}});
            if (nl != std::string::npos && !trim(s.substr(nl + 1)).empty()) out.push_back({{"p", trim(s.substr(nl + 1))}});
        } else out.push_back({{"p", s}});
    }
    return out;
}
long long readMinutes(const std::string& text) {
    std::istringstream in(text);
    std::string w; long long n = 0;
    while (in >> w) ++n;
    return std::max(1LL, (n + 110) / 220);
}
} // namespace

void GbServer::savePress() { writeFile(m_opts.data / "press.json", m_press.dump(1)); }
void GbServer::loadPress() {
    std::string text;
    if (readFile(m_opts.data / "press.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object() && j.contains("posts") && j["posts"].is_object()) m_press = j;
    }
}

json GbServer::pressJson(const json& p, const User* me, bool full) const {
    auto who = [&](const std::string& id) -> json {
        auto u = id.empty() ? m_users.end() : m_users.find(id);
        if (u == m_users.end()) return nullptr;
        return {{"id", u->second.id}, {"name", u->second.name}, {"userId", u->second.userId}, {"verified", isVerified(u->second)}};
    };
    json by = who(sv(p, "by"));
    if (by.is_null()) by = {{"id", sv(p, "by")}, {"name", "Guts&Bolts"}, {"userId", 0}, {"verified", true}};
    const char* kn = kindName(sv(p, "kind"));
    const long long published = iv(p, "published");
    json out = {{"id", sv(p, "id")}, {"title", sv(p, "title")}, {"summary", sv(p, "summary")}, {"kind", sv(p, "kind")},
                {"kindName", kn ? kn : "News"}, {"published", published != 0}, {"at", published ? published : iv(p, "at")},
                {"edited", iv(p, "edited")}, {"views", iv(p, "views")}, {"minutes", readMinutes(sv(p, "text"))},
                {"by", by}, {"creator", who(sv(p, "creator"))}};
    json games = json::array();
    std::string picture;
    if (p.contains("games") && p["games"].is_array())
        for (const json& g : p["games"]) {
            auto a = g.is_string() ? m_assets.find(g.get<std::string>()) : m_assets.end();
            if (a == m_assets.end() || a->second.kind != "game" || (me && !canSee(a->second, *me))) continue;
            if (picture.empty()) picture = a->second.id;
            games.push_back(publicAsset(a->second, me));
        }
    out["picture"] = picture;
    if (full) {
        out["text"] = sv(p, "text");
        out["blocks"] = pressBlocks(sv(p, "text"));
        out["games"] = games;
        out["gameIds"] = p.contains("games") ? p["games"] : json::array();
        out["canEdit"] = me && isStaff(*me);
    }
    return out;
}

json GbServer::pressOp(const std::string& name, User* me, const json& args) {
    const long long t = Online::unixNow();
    json& all = m_press["posts"];
    const bool staff = me && me->userId != 0 && isStaff(*me);
    if (name == "press.list") {
        const std::string kind = kindName(sv(args, "kind")) ? sv(args, "kind") : "";
        const bool drafts = staff && args.contains("drafts") && args["drafts"] == true;
        std::vector<const json*> list;
        for (const auto& [id, p] : all.items())
            if ((drafts ? !iv(p, "published") : iv(p, "published") != 0) && (kind.empty() || sv(p, "kind") == kind)) list.push_back(&p);
        auto when = [](const json* p) { return iv(*p, "published") ? iv(*p, "published") : iv(*p, "at"); };
        std::sort(list.begin(), list.end(), [&](const json* x, const json* y) { return when(x) > when(y); });
        long long page = args.contains("page") && args["page"].is_number() ? (long long)args["page"].get<double>() : 1;
        page = std::clamp(page, 1LL, 1000LL);
        const size_t from = (size_t)(page - 1) * kPageSize;
        json out = json::array();
        for (size_t i = from; i < list.size() && i < from + kPageSize; ++i) out.push_back(pressJson(*list[i], me, false));
        json kinds = json::object();
        for (const auto& [id, n] : kKinds) kinds[id] = n;
        json r = okay(); r["posts"] = out; r["more"] = list.size() > from + kPageSize; r["page"] = page; r["kinds"] = kinds; r["canWrite"] = staff;
        return r;
    }
    const std::string id = sv(args, "id");
    json* p = !id.empty() && all.contains(id) ? &all[id] : nullptr;
    if (name == "press.get") {
        if (!p || (!iv(*p, "published") && !staff)) return fail("That post isn't here (any more).");
        if (!(args.contains("quiet") && args["quiet"] == true) && iv(*p, "published")) { (*p)["views"] = iv(*p, "views") + 1; savePress(); }
        json r = okay(); r["post"] = pressJson(*p, me, true); return r;
    }
    if (!staff) return fail("Only Guts&Bolts staff write Press posts.");
    auto fields = [&](json& f) -> std::string {
        const std::string title = say(sv(args, "title"), kTitle);
        if (title.empty()) return "Give the post a title.";
        const std::string text = say(sv(args, "text"), kText, true);
        if (text.empty()) return "Write the post first.";
        std::string summary = say(sv(args, "summary"), kSummary);
        if (summary.empty()) {
            for (const json& b : pressBlocks(text)) if (b.contains("p")) { summary = b["p"].get<std::string>(); break; }
            for (char& c : summary) if (c == '\n') c = ' ';
            if (summary.size() > kSummary) summary = summary.substr(0, kSummary);
        }
        const std::string kind = kindName(sv(args, "kind")) ? sv(args, "kind") : "news";
        std::string creator;
        if (args.contains("creator") && !args["creator"].is_null() && !(args["creator"].is_string() && args["creator"].get<std::string>().empty())) {
            const User* u = findPerson(args["creator"].is_number() ? std::to_string((long long)args["creator"].get<double>()) : sv(args, "creator"));
            if (!u) return "There's nobody with that name or number to spotlight.";
            creator = u->id;
        }
        std::vector<std::string> raw;
        if (args.contains("games") && args["games"].is_array()) {
            for (const json& g : args["games"]) raw.push_back(g.is_string() ? g.get<std::string>() : g.is_number() ? std::to_string((long long)g.get<double>()) : "");
        } else {
            std::string s = sv(args, "games");
            for (char& c : s) if (c == ',') c = ' ';
            std::istringstream in(s); std::string w;
            while (in >> w) raw.push_back(w);
        }
        json games = json::array();
        size_t seen = 0;
        for (const std::string& g0 : raw) {
            if (++seen > 20) break;
            const std::string g = trim(g0);
            if (g.empty()) continue;
            auto a = m_assets.find(g);
            if (a == m_assets.end() || a->second.kind != "game") return "\"" + g.substr(0, 30) + "\" isn't a game's ID.";
            if (std::find(games.begin(), games.end(), json(a->second.id)) == games.end()) games.push_back(a->second.id);
        }
        if (games.size() > kMostGames) return "A post can show at most " + std::to_string(kMostGames) + " games.";
        f = {{"title", title}, {"summary", summary}, {"text", text}, {"kind", kind}, {"creator", creator}, {"games", games}};
        return "";
    };
    auto tellCreator = [&](json& post) {
        User* u = findUser(sv(post, "creator"));
        if (u && iv(post, "published") && sv(post, "toldAbout") != u->id) {
            notify(u, "press", "You're in a Guts&Bolts creator spotlight: \"" + sv(post, "title").substr(0, 60) + "\"", sv(post, "id"));
            post["toldAbout"] = u->id;
        }
    };
    if (name == "press.write") {
        json f;
        if (std::string bad = fields(f); !bad.empty()) return fail(bad);
        std::string nid;
        do nid = slug(f["title"].get<std::string>()) + "-" + Account::randomHex(3); while (all.contains(nid));
        f["id"] = nid; f["by"] = me->id; f["at"] = t; f["views"] = 0;
        f["published"] = args.contains("published") && args["published"] == false ? 0 : t;
        tellCreator(f);
        all[nid] = f;
        savePress();
        staffDid(*me, "press", std::string(iv(f, "published") ? "Published" : "Saved a draft of") + " the Press post \"" + sv(f, "title") + "\"");
        json r = okay(); r["post"] = pressJson(all[nid], me, true); return r;
    }
    if (!p) return fail("That post isn't here (any more).");
    if (name == "press.edit") {
        json f;
        if (std::string bad = fields(f); !bad.empty()) return fail(bad);
        for (auto& [k, v] : f.items()) (*p)[k] = v;
        (*p)["edited"] = t;
        if (args.contains("published") && args["published"] == false) (*p)["published"] = 0;
        else if (args.contains("published") && args["published"] == true && !iv(*p, "published")) (*p)["published"] = t;
        tellCreator(*p);
        savePress();
        staffDid(*me, "press", "Changed the Press post \"" + sv(*p, "title") + "\"");
        json r = okay(); r["post"] = pressJson(*p, me, true); return r;
    }
    if (name == "press.delete") {
        const std::string title = sv(*p, "title");
        all.erase(id);
        savePress();
        staffDid(*me, "press", "Deleted the Press post \"" + title + "\"");
        return okay();
    }
    return fail("Unknown request.");
}
