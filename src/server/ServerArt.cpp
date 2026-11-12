// The Art portal: people post pictures, others fave and comment. worker/art.js has the same rules
// (and the notes on why):
//   - one picture (PNG, JPG, GIF or WebP, up to kMostBytes) with a title, words, tags and a category
//   - pictures from people who aren't Verified wait for a staff check ("pending")
//   - faves, comments (the artist is told), edits by the artist, deletes by the artist or staff
//   - kPostsPerDay posts a day (Verified: kPostsPerDayVerified)
//   art.list {sort, cat, tag, by, q, page}  art.get {id}  art.picture {id, thumb}  art.post {...}  art.edit {...}
//   art.delete {id}  art.fave {id, on}  art.comment {id, text}  art.uncomment {id, comment}  art.pending {}  art.review {id, ok, note}
// Kept in art.json ({pieces: id -> piece, posts: account -> {day, n}}); pictures are files/art-<id> and files/artthumb-<id>.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cctype>

using json = nlohmann::json;
using namespace ServerUtil;
namespace fs = std::filesystem;

namespace {
const char* kCats[] = {"drawing", "painting", "pixel", "comic", "fanart", "3d", "screenshot", "photo", "other"};
constexpr size_t kTitle = 60, kText = 1000, kTag = 20, kMostTags = 8, kCommentText = 500, kCommentsKept = 500;
constexpr size_t kMostBytes = 2 * 1024 * 1024, kThumbBytes = 200 * 1024, kPageSize = 24;
constexpr long long kPostsPerDay = 5, kPostsPerDayVerified = 25;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
bool isCat(const std::string& c) { for (const char* x : kCats) if (c == x) return true; return false; }

// What kind of picture these bytes are, by their first bytes.
std::string artType(const std::string& d) {
    auto b = [&](size_t i) { return (unsigned char)d[i]; };
    if (d.size() < 12) return "";
    if (b(0) == 0x89 && d.compare(1, 3, "PNG") == 0 && b(4) == 0x0d && b(5) == 0x0a && b(6) == 0x1a && b(7) == 0x0a) return "png";
    if (b(0) == 0xff && b(1) == 0xd8 && b(2) == 0xff) return "jpg";
    if (d.compare(0, 4, "GIF8") == 0) return "gif";
    if (d.compare(0, 4, "RIFF") == 0 && d.compare(8, 4, "WEBP") == 0) return "webp";
    return "";
}

// Tags: lower case letters, numbers and dashes, no repeats, at most kMostTags.
json artTags(const json& in) {
    std::vector<std::string> words;
    if (in.is_array()) { for (const json& x : in) if (x.is_string()) words.push_back(x.get<std::string>()); }
    else if (in.is_string()) {
        std::string w;
        for (char c : in.get<std::string>() + " ") {
            if (c == ',' || std::isspace((unsigned char)c)) { if (!w.empty()) words.push_back(w); w.clear(); }
            else w += c;
        }
    }
    json out = json::array();
    for (const std::string& w : words) {
        std::string t;
        for (char c : w) {
            const char l = (char)std::tolower((unsigned char)c);
            if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '-') t += l;
        }
        if (t.size() > kTag) t.resize(kTag);
        if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
        if (out.size() >= kMostTags) break;
    }
    return out;
}
bool hasId(const json& arr, const std::string& id) { return std::find(arr.begin(), arr.end(), id) != arr.end(); }
} // namespace

void GbServer::saveArt() {
    for (auto& [id, p] : m_art["pieces"].items()) {
        json& c = p["comments"];
        while (c.size() > kCommentsKept) c.erase(c.begin());
    }
    writeFile(m_opts.data / "art.json", m_art.dump(1));
}
void GbServer::loadArt() {
    std::string text;
    if (readFile(m_opts.data / "art.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object() && j.contains("pieces") && j["pieces"].is_object()) m_art = j;
        if (!m_art.contains("posts") || !m_art["posts"].is_object()) m_art["posts"] = json::object();
    }
}

bool GbServer::artCanSee(const json& p, const User* me) const {
    if (sv(p, "review").empty()) return true;
    return me && (sv(p, "by") == me->id || isStaff(*me));
}

json GbServer::artJson(const json& p, const User* me, bool full) const {
    auto person = [&](const std::string& id) {
        auto u = m_users.find(id);
        if (u == m_users.end()) return json{{"id", id}, {"name", "?"}, {"username", ""}, {"userId", 0}, {"verified", false}};
        return json{{"id", id}, {"name", u->second.name}, {"username", u->second.username}, {"userId", u->second.userId}, {"verified", isVerified(u->second)}};
    };
    json out = {{"id", sv(p, "id")}, {"title", sv(p, "title")}, {"cat", sv(p, "cat")}, {"tags", p.value("tags", json::array())},
                {"type", sv(p, "type")}, {"at", p.value("at", 0LL)}, {"by", person(sv(p, "by"))}, {"faves", p["faves"].size()},
                {"views", p.value("views", 0LL)}, {"comments", p["comments"].size()}};
    if (!sv(p, "review").empty()) out["review"] = sv(p, "review");
    if (!full) return out;
    const bool mine = me && sv(p, "by") == me->id, staff = me && isStaff(*me);
    out["text"] = sv(p, "text");
    out["edited"] = p.value("edited", 0LL);
    if ((mine || staff) && !sv(p, "reviewNote").empty()) out["reviewNote"] = sv(p, "reviewNote");
    out["faved"] = me && hasId(p["faves"], me->id);
    out["mine"] = mine;
    out["canDelete"] = mine || staff;
    json list = json::array();
    for (auto it = p["comments"].rbegin(); it != p["comments"].rend(); ++it)   // newest first
        list.push_back({{"id", sv(*it, "id")}, {"text", sv(*it, "text")}, {"at", it->value("at", 0LL)}, {"by", person(sv(*it, "by"))},
                        {"canDelete", me && (sv(*it, "by") == me->id || mine || staff)}});
    out["commentList"] = list;
    return out;
}

// For report.send (ServerSafety.cpp): "" when it worked, or why not.
std::string GbServer::artReportTarget(const std::string& id, const User& me, std::string& target, std::string& about, json& copy) {
    const size_t colon = id.find(':');
    const std::string pid = id.substr(0, colon);
    json& pieces = m_art["pieces"];
    if (!pieces.contains(pid) || !artCanSee(pieces[pid], &me)) return "That isn't there any more.";
    const json& p = pieces[pid];
    if (colon == std::string::npos) {
        if (sv(p, "by") == me.id) return "You can't report yourself.";
        target = pid; about = sv(p, "by");
        copy = {{"subject", "Art: " + sv(p, "title")}, {"body", sv(p, "text").empty() ? "(no description)" : sv(p, "text")}, {"at", p.value("at", 0LL)}};
        return "";
    }
    const std::string cid = id.substr(colon + 1);
    for (const json& c : p["comments"]) {
        if (sv(c, "id") != cid) continue;
        if (sv(c, "by") == me.id) return "You can't report yourself.";
        target = pid + ":" + cid; about = sv(c, "by");
        copy = {{"subject", "Comment on art: " + sv(p, "title")}, {"body", sv(c, "text")}, {"at", c.value("at", 0LL)}};
        return "";
    }
    return "That comment isn't there any more.";
}
std::string GbServer::artReportName(const std::string& target) const {
    const std::string pid = target.substr(0, target.find(':'));
    return m_art["pieces"].contains(pid) ? sv(m_art["pieces"][pid], "title") : "(deleted)";
}

json GbServer::artOp(const std::string& name, User* me, const json& args) {
    const long long t = Online::unixNow();
    json& pieces = m_art["pieces"];
    if (name == "art.list") {
        std::string sort = sv(args, "sort");
        if (sort != "week" && sort != "top") sort = "new";
        const std::string cat = isCat(sv(args, "cat")) ? sv(args, "cat") : "";
        const json tags = artTags(json::array({sv(args, "tag")}));
        const std::string tag = tags.empty() ? "" : tags[0].get<std::string>();
        const std::string q = lower(sv(args, "q")).substr(0, 60);
        const User* by = nullptr;
        if (args.contains("by") && !(args["by"].is_string() && args["by"].get<std::string>().empty())) {
            by = findPerson(args["by"].is_number() ? std::to_string(args["by"].get<long long>()) : sv(args, "by"));
            if (!by) return fail("There's nobody with that name or number.");
        }
        long long page = args.contains("page") && args["page"].is_number() ? (long long)args["page"].get<double>() : 1;
        page = std::clamp(page, 1LL, 1000LL);
        std::vector<const json*> list;
        for (const auto& [id, p] : pieces.items()) {
            if (by ? !(sv(p, "by") == by->id && artCanSee(p, me)) : !sv(p, "review").empty()) continue;
            if (!cat.empty() && sv(p, "cat") != cat) continue;
            if (!tag.empty() && !hasId(p["tags"], tag)) continue;
            if (!q.empty()) {
                bool hit = lower(sv(p, "title")).find(q) != std::string::npos;
                for (const json& x : p["tags"]) if (x.get<std::string>().find(q) != std::string::npos) hit = true;
                if (!hit) continue;
            }
            if (sort == "week" && p.value("at", 0LL) <= t - 7 * 86400) continue;
            list.push_back(&p);
        }
        std::sort(list.begin(), list.end(), [&](const json* x, const json* y) {
            if (sort != "new" && x->at("faves").size() != y->at("faves").size()) return x->at("faves").size() > y->at("faves").size();
            return x->value("at", 0LL) > y->value("at", 0LL);
        });
        json out = json::array();
        const size_t from = (size_t)(page - 1) * kPageSize;
        for (size_t i = from; i < list.size() && i < from + kPageSize; ++i) out.push_back(artJson(*list[i], me, false));
        json r = okay(); r["pieces"] = out; r["more"] = list.size() > from + kPageSize; r["page"] = page; r["total"] = list.size();
        r["cats"] = json::array(); for (const char* c : kCats) r["cats"].push_back(c);
        return r;
    }
    const std::string id = sv(args, "id");
    json* p = pieces.contains(id) ? &pieces[id] : nullptr;
    if (name == "art.picture") {
        if (!p || !artCanSee(*p, me)) return fail("No picture.");
        const bool thumb = args.value("thumb", false) && !sv(*p, "thumb").empty();
        std::string data;
        if (!readFile(m_opts.data / "files" / ((thumb ? "artthumb-" : "art-") + id), data)) return fail("No picture.");
        json r = okay(); r["type"] = thumb ? sv(*p, "thumb") : sv(*p, "type"); r["data"] = Online::base64Encode(data); return r;
    }
    if (name == "art.get") {
        if (!p || !artCanSee(*p, me)) return fail("That piece isn't here (any more).");
        if (!me || sv(*p, "by") != me->id) { (*p)["views"] = p->value("views", 0LL) + 1; saveArt(); }
        json r = okay(); r["piece"] = artJson(*p, me, true); return r;
    }

    if (!me || me->userId == 0) return fail("Sign up to post art, fave and comment.");
    const bool staff = isStaff(*me), verified = isVerified(*me);

    if (name == "art.post" || name == "art.edit") {
        const std::string title = say(sv(args, "title"), kTitle);
        if (title.empty()) return fail("Give your piece a title.");
        const std::string text = say(sv(args, "text"), kText, true);
        const std::string cat = isCat(sv(args, "cat")) ? sv(args, "cat") : "other";
        const json tags = artTags(args.contains("tags") ? args["tags"] : json());
        if (name == "art.edit") {
            if (!p || !artCanSee(*p, me)) return fail("That piece isn't here (any more).");
            if (sv(*p, "by") != me->id) return fail("Only the artist can change this.");
            (*p)["title"] = title; (*p)["text"] = text; (*p)["cat"] = cat; (*p)["tags"] = tags; (*p)["edited"] = t;
            saveArt();
            json r = okay(); r["piece"] = artJson(*p, me, true); return r;
        }
        json& posts = m_art["posts"][me->id];
        const long long today = t / 86400;
        if (!posts.is_object() || posts.value("day", 0LL) != today) posts = {{"day", today}, {"n", 0}};
        const long long most = verified || staff ? kPostsPerDayVerified : kPostsPerDay;
        if (posts.value("n", 0LL) >= most) return fail("You've posted " + std::to_string(most) + " pieces today. Come back tomorrow!");
        std::string data, thumb;
        std::string b64 = sv(args, "data");
        if (const size_t comma = b64.find(','); b64.rfind("data:", 0) == 0 && comma != std::string::npos) b64 = b64.substr(comma + 1);
        if (!Online::base64Decode(b64, data)) return fail("The picture got scrambled. Try again.");
        const std::string type = artType(data);
        if (type.empty()) return fail("Art must be a .png, .jpg, .gif or .webp picture.");
        if (data.size() > kMostBytes) return fail("That picture is too big (the most is " + std::to_string(kMostBytes >> 20) + " MB).");
        if (!sv(args, "thumb").empty() && (!Online::base64Decode(sv(args, "thumb"), thumb) || artType(thumb).empty() || thumb.size() > kThumbBytes))
            thumb.clear();   // (the gallery uses the full picture instead)
        const std::string pid = "art-" + Account::randomHex(6);
        if (!writeFile(m_opts.data / "files" / ("art-" + pid), data)) return fail("The server couldn't save that picture.");
        if (!thumb.empty()) writeFile(m_opts.data / "files" / ("artthumb-" + pid), thumb);
        json piece = {{"id", pid}, {"by", me->id}, {"title", title}, {"text", text}, {"cat", cat}, {"tags", tags}, {"type", type},
                      {"thumb", thumb.empty() ? "" : artType(thumb)}, {"size", data.size()}, {"at", t}, {"edited", 0}, {"views", 0},
                      {"faves", json::array()}, {"comments", json::array()}};
        if (!verified && !staff) piece["review"] = "pending";
        pieces[pid] = piece;
        posts["n"] = posts.value("n", 0LL) + 1;
        saveArt();
        json r = okay(); r["piece"] = artJson(pieces[pid], me, true); return r;
    }
    if (name == "art.pending") {
        if (!staff) return fail("Only staff can do that.");
        std::vector<const json*> list;
        for (const auto& [k, x] : pieces.items()) if (sv(x, "review") == "pending") list.push_back(&x);
        std::sort(list.begin(), list.end(), [](const json* x, const json* y) { return x->value("at", 0LL) < y->value("at", 0LL); });
        json out = json::array();
        for (const json* x : list) out.push_back(artJson(*x, me, false));
        json r = okay(); r["pieces"] = out; return r;
    }

    if (!p || !artCanSee(*p, me)) return fail("That piece isn't here (any more).");
    if (name == "art.delete") {
        if (sv(*p, "by") != me->id && !staff) return fail("Only the artist or staff can delete this.");
        std::error_code ec;
        fs::remove(m_opts.data / "files" / ("art-" + id), ec);
        fs::remove(m_opts.data / "files" / ("artthumb-" + id), ec);
        pieces.erase(id);
        saveArt();
        return okay();
    }
    if (name == "art.review") {
        if (!staff) return fail("Only staff can do that.");
        if (sv(*p, "review").empty()) return fail("That isn't waiting for a check any more.");
        User* artist = findUser(sv(*p, "by"));
        const std::string title = sv(*p, "title");
        if (args.value("ok", false)) {
            p->erase("review"); p->erase("reviewNote");
            notify(artist, "art", "Your art \"" + title + "\" passed the staff check. Everyone can see it now!", id);
        } else {
            (*p)["review"] = "rejected";
            (*p)["reviewNote"] = say(sv(args, "note"), 200);
            const std::string note = sv(*p, "reviewNote");
            notify(artist, "art", "Your art \"" + title + "\" didn't pass the staff check" + (note.empty() ? "." : ": " + note), id);
        }
        saveArt();
        json r = okay(); r["piece"] = artJson(*p, me, true); return r;
    }
    if (!sv(*p, "review").empty()) return fail("This hasn't passed the staff check yet.");
    if (name == "art.fave") {
        const bool on = !(args.contains("on") && args["on"].is_boolean() && !args["on"].get<bool>());
        json& faves = (*p)["faves"];
        const bool had = hasId(faves, me->id);
        if (on && !had) faves.push_back(me->id);
        if (!on && had) faves.erase(std::remove(faves.begin(), faves.end(), json(me->id)), faves.end());
        if (on != had) saveArt();
        json r = okay(); r["faves"] = faves.size(); r["faved"] = on; return r;
    }
    if (name == "art.comment") {
        const std::string text = say(sv(args, "text"), kCommentText, true);
        if (text.empty()) return fail("Write something first.");
        (*p)["comments"].push_back({{"id", Account::randomHex(5)}, {"by", me->id}, {"text", text}, {"at", t}});
        saveArt();
        if (sv(*p, "by") != me->id) notify(findUser(sv(*p, "by")), "art", me->name + " commented on your art \"" + sv(*p, "title") + "\".", id);
        json r = okay(); r["piece"] = artJson(*p, me, true); return r;
    }
    if (name == "art.uncomment") {
        json& list = (*p)["comments"];
        const std::string cid = sv(args, "comment");
        auto c = std::find_if(list.begin(), list.end(), [&](const json& x) { return sv(x, "id") == cid; });
        if (c == list.end()) return fail("That comment isn't there any more.");
        if (sv(*c, "by") != me->id && sv(*p, "by") != me->id && !staff) return fail("You can only delete your own comments, or ones on your art.");
        list.erase(c);
        saveArt();
        json r = okay(); r["piece"] = artJson(*p, me, true); return r;
    }
    return fail("Unknown request.");
}
