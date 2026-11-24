// The Audio portal: music, loops and sound effects to listen to on the website and use in games.
// worker/audio.js has the same rules (and the notes on why):
//   - a track IS a Library audio asset (uploaded with "upload"), so it already has an ID games use
//   - the portal keeps a category, genre, tags, plays, faves and comments for each one (audio.json)
//   - only public sounds that passed the staff check are in the lists ("My audio" shows your own)
//   - plays: one per person per sound a day, and the maker's own don't count
//   audio.list {sort, cat, genre, tag, by, q, page}  audio.get {id}  audio.listen {id}  audio.file {id}
//   audio.tag {id, cat, genre, tags}  audio.fave {id, on}  audio.comment {id, text}  audio.uncomment {id, comment}
// Kept in audio.json ({tracks: asset id -> {cat, genre, tags, plays, faves, comments}, heard: account -> {day, ids}}).
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cctype>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
const char* kCats[] = {"music", "loop", "sfx", "ambience", "voice", "other"};
const char* kGenres[] = {"electronic", "chiptune", "rock", "metal", "hiphop", "pop", "orchestral", "ambient", "lofi", "jazz", "funk", "other"};
constexpr size_t kTag = 20, kMostTags = 8, kCommentText = 500, kCommentsKept = 500, kPageSize = 30, kHeardKept = 500;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
template <size_t N> bool isOneOf(const std::string& s, const char* (&list)[N]) { for (const char* x : list) if (s == x) return true; return false; }
bool hasId(const json& arr, const std::string& id) { return std::find(arr.begin(), arr.end(), id) != arr.end(); }
std::string reviewOf(const json& meta) { return meta.is_object() ? meta.value("review", std::string()) : std::string(); }

// Tags: lower case letters, numbers and dashes, no repeats, at most kMostTags.
json audioTags(const json& in) {
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
} // namespace

void GbServer::saveAudio() {
    for (auto& [id, x] : m_audio["tracks"].items()) {
        json& c = x["comments"];
        while (c.size() > kCommentsKept) c.erase(c.begin());
    }
    writeFile(m_opts.data / "audio.json", m_audio.dump(1));
}
void GbServer::loadAudio() {
    std::string text;
    if (readFile(m_opts.data / "audio.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object() && j.contains("tracks") && j["tracks"].is_object()) m_audio = j;
        if (!m_audio.contains("heard") || !m_audio["heard"].is_object()) m_audio["heard"] = json::object();
    }
}

// A sound's portal bits (made the first time anyone asks: older sounds don't have any yet).
json& GbServer::audioExtra(const std::string& id) {
    json& tracks = m_audio["tracks"];
    if (!tracks.contains(id) || !tracks[id].is_object())
        tracks[id] = {{"cat", "other"}, {"genre", ""}, {"tags", json::array()}, {"plays", 0}, {"faves", json::array()}, {"comments", json::array()}};
    return tracks[id];
}
// In the portal for everyone: public, and passed the staff check.
bool GbServer::audioListed(const Asset& a) const {
    return a.kind == "audio" && reviewOf(a.meta).empty() && a.meta.value("access", std::string("public")) == "public";
}
bool GbServer::audioCanSee(const Asset& a, const User* me) const {
    return a.kind == "audio" && (audioListed(a) || (me && (a.creator == me->id || isStaff(*me))));
}

json GbServer::audioJson(const Asset& a, const User* me, bool full) {
    auto person = [&](const std::string& id) {
        auto u = m_users.find(id);
        if (u == m_users.end()) return json{{"id", id}, {"name", "Guts"}, {"username", ""}, {"userId", 0}, {"verified", false}};
        return json{{"id", id}, {"name", u->second.name}, {"username", u->second.username}, {"userId", u->second.userId}, {"verified", isVerified(u->second)}};
    };
    const json& x = audioExtra(a.id);
    json out = {{"id", a.id}, {"num", a.num}, {"title", a.name}, {"cat", sv(x, "cat")}, {"genre", sv(x, "genre")}, {"tags", x["tags"]},
                {"ext", a.meta.value("ext", std::string())}, {"size", a.size}, {"at", a.created}, {"by", person(a.creator)},
                {"plays", x.value("plays", 0LL)}, {"faves", x["faves"].size()}, {"comments", x["comments"].size()},
                {"access", a.meta.value("access", std::string("public"))}};
    if (!reviewOf(a.meta).empty()) out["review"] = reviewOf(a.meta);
    if (!full) return out;
    const bool mine = me && a.creator == me->id, staff = me && isStaff(*me);
    out["text"] = a.description;
    out["faved"] = me && hasId(x["faves"], me->id);
    out["mine"] = mine;
    out["canTag"] = mine || staff;
    json list = json::array();
    for (auto it = x["comments"].rbegin(); it != x["comments"].rend(); ++it)   // newest first
        list.push_back({{"id", sv(*it, "id")}, {"text", sv(*it, "text")}, {"at", it->value("at", 0LL)}, {"by", person(sv(*it, "by"))},
                        {"canDelete", me && (sv(*it, "by") == me->id || mine || staff)}});
    out["commentList"] = list;
    return out;
}

// For report.send (ServerSafety.cpp): "<id>" is the sound, "<id>:<comment>" a comment under it.
std::string GbServer::audioReportTarget(const std::string& id, const User& me, std::string& target, std::string& about, json& copy) {
    const size_t colon = id.find(':');
    auto it = findAsset(id.substr(0, colon));
    if (it == m_assets.end() || !audioCanSee(it->second, &me)) return "That isn't there any more.";
    const Asset& a = it->second;
    if (colon == std::string::npos) {
        if (a.creator == me.id) return "You can't report yourself.";
        target = a.id; about = a.creator;
        copy = {{"subject", "Audio: " + a.name}, {"body", a.description.empty() ? "(no description)" : a.description}, {"at", a.created}};
        return "";
    }
    const std::string cid = id.substr(colon + 1);
    for (const json& c : audioExtra(a.id)["comments"]) {
        if (sv(c, "id") != cid) continue;
        if (sv(c, "by") == me.id) return "You can't report yourself.";
        target = a.id + ":" + cid; about = sv(c, "by");
        copy = {{"subject", "Comment on audio: " + a.name}, {"body", sv(c, "text")}, {"at", c.value("at", 0LL)}};
        return "";
    }
    return "That comment isn't there any more.";
}
std::string GbServer::audioReportName(const std::string& target) const {
    auto it = findAsset(target.substr(0, target.find(':')));
    return it != m_assets.end() ? it->second.name : "(deleted)";
}

json GbServer::audioOp(const std::string& name, User* me, const json& args) {
    const long long t = Online::unixNow();
    if (name == "audio.list") {
        std::string sort = sv(args, "sort");
        if (sort != "week" && sort != "top" && sort != "plays") sort = "new";
        const std::string cat = isOneOf(sv(args, "cat"), kCats) ? sv(args, "cat") : "";
        const std::string genre = isOneOf(sv(args, "genre"), kGenres) ? sv(args, "genre") : "";
        const json tags = audioTags(json::array({sv(args, "tag")}));
        const std::string tag = tags.empty() ? "" : tags[0].get<std::string>();
        const std::string q = lower(sv(args, "q")).substr(0, 60);
        const User* by = nullptr;
        if (args.contains("by") && !(args["by"].is_string() && args["by"].get<std::string>().empty())) {
            by = findPerson(args["by"].is_number() ? std::to_string(args["by"].get<long long>()) : sv(args, "by"));
            if (!by) return fail("There's nobody with that name or number.");
        }
        long long page = args.contains("page") && args["page"].is_number() ? (long long)args["page"].get<double>() : 1;
        page = std::clamp(page, 1LL, 1000LL);
        std::vector<const Asset*> list;
        for (const auto& [id, a] : m_assets) {
            if (a.kind != "audio") continue;
            if (by ? !(a.creator == by->id && audioCanSee(a, me)) : !audioListed(a)) continue;
            const json& x = audioExtra(a.id);
            if (!cat.empty() && sv(x, "cat") != cat) continue;
            if (!genre.empty() && sv(x, "genre") != genre) continue;
            if (!tag.empty() && !hasId(x["tags"], tag)) continue;
            if (!q.empty()) {
                bool hit = lower(a.name).find(q) != std::string::npos;
                for (const json& g : x["tags"]) if (g.get<std::string>().find(q) != std::string::npos) hit = true;
                if (!hit) continue;
            }
            if (sort == "week" && a.created <= t - 7 * 86400) continue;
            list.push_back(&a);
        }
        std::sort(list.begin(), list.end(), [&](const Asset* x, const Asset* y) {
            const json &ex = audioExtra(x->id), &ey = audioExtra(y->id);
            const long long px = ex.value("plays", 0LL), py = ey.value("plays", 0LL);
            if (sort == "plays" && px != py) return px > py;
            if (sort == "top" || sort == "week") {
                if (ex["faves"].size() != ey["faves"].size()) return ex["faves"].size() > ey["faves"].size();
                if (px != py) return px > py;
            }
            return x->created > y->created;
        });
        json out = json::array();
        const size_t from = (size_t)(page - 1) * kPageSize;
        for (size_t i = from; i < list.size() && i < from + kPageSize; ++i) out.push_back(audioJson(*list[i], me, false));
        json r = okay(); r["tracks"] = out; r["more"] = list.size() > from + kPageSize; r["page"] = page; r["total"] = list.size();
        r["cats"] = json::array(); for (const char* c : kCats) r["cats"].push_back(c);
        r["genres"] = json::array(); for (const char* g : kGenres) r["genres"].push_back(g);
        return r;
    }
    std::string id = sv(args, "id");
    if (id.rfind("gb:", 0) == 0) id = id.substr(3);
    auto it = findAsset(id);
    Asset* a = it != m_assets.end() && it->second.kind == "audio" ? &it->second : nullptr;
    if (name == "audio.file") {   // (also without an account: worker/index.js asks for /sound/)
        if (!a || !reviewOf(a->meta).empty()) return fail("No sound.");
        std::string data;
        if (!readFile(blobPath(a->id), data)) return fail("No sound.");
        json r = okay(); r["ext"] = a->meta.value("ext", std::string()); r["data"] = Online::base64Encode(data); return r;
    }
    if (!a || !audioCanSee(*a, me)) return fail("That sound isn't here (any more), or it's private.");
    if (name == "audio.get") { json r = okay(); r["track"] = audioJson(*a, me, true); return r; }
    json& x = audioExtra(a->id);
    if (name == "audio.listen") {
        if (me && a->creator != me->id && reviewOf(a->meta).empty()) {
            const long long today = t / 86400;
            json& heard = m_audio["heard"][me->id];
            if (!heard.is_object() || heard.value("day", 0LL) != today) heard = {{"day", today}, {"ids", json::array()}};
            if (!hasId(heard["ids"], a->id)) {
                heard["ids"].push_back(a->id);
                while (heard["ids"].size() > kHeardKept) heard["ids"].erase(heard["ids"].begin());
                x["plays"] = x.value("plays", 0LL) + 1;
                saveAudio();
            }
        }
        json r = okay(); r["plays"] = x.value("plays", 0LL); return r;
    }

    if (!me || me->userId == 0) return fail("Sign up to fave sounds and comment.");
    const bool staff = isStaff(*me);
    if (name == "audio.tag") {
        if (a->creator != me->id && !staff) return fail("Only the person who uploaded it can change this.");
        x["cat"] = isOneOf(sv(args, "cat"), kCats) ? sv(args, "cat") : "other";
        const bool genreOk = sv(x, "cat") == "music" || sv(x, "cat") == "loop";
        x["genre"] = genreOk && isOneOf(sv(args, "genre"), kGenres) ? sv(args, "genre") : "";
        x["tags"] = audioTags(args.contains("tags") ? args["tags"] : json());
        saveAudio();
        json r = okay(); r["track"] = audioJson(*a, me, true); return r;
    }
    if (!reviewOf(a->meta).empty()) return fail("This hasn't passed the staff check yet.");
    if (name == "audio.fave") {
        const bool on = !(args.contains("on") && args["on"].is_boolean() && !args["on"].get<bool>());
        json& faves = x["faves"];
        const bool had = hasId(faves, me->id);
        if (on && !had) faves.push_back(me->id);
        if (!on && had) faves.erase(std::remove(faves.begin(), faves.end(), json(me->id)), faves.end());
        if (on != had) saveAudio();
        json r = okay(); r["faves"] = faves.size(); r["faved"] = on; return r;
    }
    if (name == "audio.comment") {
        const std::string text = say(sv(args, "text"), kCommentText, true);
        if (text.empty()) return fail("Write something first.");
        x["comments"].push_back({{"id", Account::randomHex(5)}, {"by", me->id}, {"text", text}, {"at", t}});
        saveAudio();
        if (a->creator != me->id) notify(findUser(a->creator), "audio", me->name + " commented on your sound \"" + a->name + "\".", a->id);
        json r = okay(); r["track"] = audioJson(*a, me, true); return r;
    }
    if (name == "audio.uncomment") {
        json& list = x["comments"];
        const std::string cid = sv(args, "comment");
        auto c = std::find_if(list.begin(), list.end(), [&](const json& y) { return sv(y, "id") == cid; });
        if (c == list.end()) return fail("That comment isn't there any more.");
        if (sv(*c, "by") != me->id && a->creator != me->id && !staff) return fail("You can only delete your own comments, or ones on your sounds.");
        list.erase(c);
        saveAudio();
        json r = okay(); r["track"] = audioJson(*a, me, true); return r;
    }
    return fail("Unknown request.");
}
