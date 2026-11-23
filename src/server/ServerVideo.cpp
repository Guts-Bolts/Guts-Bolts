// G&B Videos: upload a video, watch it, like it, comment on it. worker/videos.js has the same rules
// (and the notes on why):
//   - two steps: video.start checks everything and hands out an upload address with a token, then
//     the file comes. Here it arrives in pieces (video.chunk), because worker/index.js talks to this
//     server in JSON; it's checked to really be a video (MP4, MOV or WebM) when the last piece is in
//   - videos from people who aren't Verified wait for a staff check ("pending"); until then only the
//     uploader and staff watch it, with the video's own key
//   - when one goes public, the uploader's followers are told
//   - likes, comments (the uploader is told), views; edits by the uploader, deletes by the uploader or staff
//   - kPerDay uploads a day (Verified: kPerDayVerified); all videos together fit in GB_VIDEO_STORAGE_GB (default kStorageGb)
//   video.list {sort, cat, tag, by, q, page}  video.get {id}  video.start {...}  video.edit {...}  video.delete {id}
//   video.like {id, on}  video.comment {id, text}  video.uncomment {id, comment}  video.pending {}  video.review {id, ok, note}
//   No account (worker/index.js asks): video.chunk {id, t, at, data, last}  video.bytes {id, k, from, size}  video.thumb {id, k}
// Kept in videos.json ({videos: id -> video, made: account -> {day, n}}); files are files/video-<id> and files/vidthumb-<id>.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>

using json = nlohmann::json;
using namespace ServerUtil;
namespace fs = std::filesystem;

namespace {
const char* kCats[] = {"gaming", "music", "funny", "howto", "animation", "vlog", "sports", "science", "news", "other"};
constexpr size_t kTitle = 100, kText = 3000, kTag = 20, kMostTags = 10, kCommentText = 1000, kCommentsKept = 1000;
constexpr long long kMostBytes = 95LL * 1024 * 1024, kThumbBytes = 200 * 1024, kChunk = 1024 * 1024, kMostSecs = 3600;
constexpr size_t kPageSize = 24, kTellFollowers = 500;
constexpr long long kPerDay = 3, kPerDayVerified = 15, kUploadWait = 3600;
constexpr double kStorageGb = 5;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
double nv(const json& j, const char* k) { return j.contains(k) && j[k].is_number() ? j[k].get<double>() : 0; }
bool isCat(const std::string& c) { for (const char* x : kCats) if (c == x) return true; return false; }
bool hasId(const json& arr, const std::string& id) { return std::find(arr.begin(), arr.end(), id) != arr.end(); }

// What kind of video these bytes are, by their first bytes.
std::string videoType(const std::string& d) {
    if (d.size() < 12) return "";
    if (d.compare(4, 4, "ftyp") == 0) return d.compare(8, 2, "qt") == 0 ? "mov" : "mp4";
    if ((unsigned char)d[0] == 0x1a && (unsigned char)d[1] == 0x45 && (unsigned char)d[2] == 0xdf && (unsigned char)d[3] == 0xa3) return "webm";
    return "";
}
bool isJpg(const std::string& d) { return d.size() > 3 && (unsigned char)d[0] == 0xff && (unsigned char)d[1] == 0xd8 && (unsigned char)d[2] == 0xff; }
bool isPng(const std::string& d) { return d.size() > 8 && (unsigned char)d[0] == 0x89 && d.compare(1, 3, "PNG") == 0; }

// Tags: lower case letters, numbers and dashes, no repeats, at most kMostTags.
json videoTags(const json& in) {
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
bool isPublic(const json& v) { return sv(v, "state") == "ready" && sv(v, "review").empty(); }
long long storageRoom() {
    const char* e = std::getenv("GB_VIDEO_STORAGE_GB");
    const double gb = e && *e ? std::max(0.0, std::atof(e)) : kStorageGb;
    return (long long)(gb * 1024 * 1024 * 1024);
}
} // namespace

void GbServer::saveVideos() {
    for (auto& [id, v] : m_videos["videos"].items()) {
        json& c = v["comments"];
        while (c.size() > kCommentsKept) c.erase(c.begin());
    }
    writeFile(m_opts.data / "videos.json", m_videos.dump(1));
}
void GbServer::loadVideos() {
    std::string text;
    if (readFile(m_opts.data / "videos.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object() && j.contains("videos") && j["videos"].is_object()) m_videos = j;
        if (!m_videos.contains("made") || !m_videos["made"].is_object()) m_videos["made"] = json::object();
    }
}
void GbServer::videoForget(const std::string& id) {
    std::error_code ec;
    fs::remove(m_opts.data / "files" / ("video-" + id), ec);
    fs::remove(m_opts.data / "files" / ("vidthumb-" + id), ec);
    m_videos["videos"].erase(id);
    saveVideos();
}

bool GbServer::videoCanSee(const json& v, const User* me) const {
    if (isPublic(v)) return true;
    return me && (sv(v, "by") == me->id || (isStaff(*me) && sv(v, "state") == "ready"));
}

json GbServer::videoJson(const json& v, const User* me, bool full) const {
    auto person = [&](const std::string& id) {
        auto u = m_users.find(id);
        if (u == m_users.end()) return json{{"id", id}, {"name", "?"}, {"username", ""}, {"userId", 0}, {"verified", false}, {"followers", 0}};
        return json{{"id", id}, {"name", u->second.name}, {"username", u->second.username}, {"userId", u->second.userId},
                    {"verified", isVerified(u->second)}, {"followers", u->second.followers.size()}};
    };
    const std::string id = sv(v, "id");
    const std::string k = isPublic(v) ? "" : "?k=" + sv(v, "key");
    const bool ready = sv(v, "state") == "ready";
    json out = {{"id", id}, {"title", sv(v, "title")}, {"cat", sv(v, "cat")}, {"tags", v.value("tags", json::array())},
                {"secs", v.value("secs", 0LL)}, {"at", v.value("at", 0LL)}, {"by", person(sv(v, "by"))}, {"likes", v["likes"].size()},
                {"views", v.value("views", 0LL)}, {"comments", v["comments"].size()}, {"state", sv(v, "state")},
                {"src", ready ? "/vid/" + id + k : ""}, {"poster", ready && v.value("thumb", false) ? "/vidthumb/" + id + k : ""}};
    if (!sv(v, "review").empty()) out["review"] = sv(v, "review");
    if (!full) return out;
    const bool mine = me && sv(v, "by") == me->id, staff = me && isStaff(*me);
    out["text"] = sv(v, "text");
    out["edited"] = v.value("edited", 0LL);
    out["w"] = v.value("w", 0LL); out["h"] = v.value("h", 0LL);
    out["type"] = sv(v, "type"); out["size"] = v.value("size", 0LL);
    if ((mine || staff) && !sv(v, "reviewNote").empty()) out["reviewNote"] = sv(v, "reviewNote");
    out["liked"] = me && hasId(v["likes"], me->id);
    out["mine"] = mine;
    out["canDelete"] = mine || staff;
    auto up = m_users.find(sv(v, "by"));
    out["following"] = me && up != m_users.end() && up->second.followers.count(me->id) > 0;
    json list = json::array();
    for (auto it = v["comments"].rbegin(); it != v["comments"].rend(); ++it)   // newest first
        list.push_back({{"id", sv(*it, "id")}, {"text", sv(*it, "text")}, {"at", it->value("at", 0LL)}, {"by", person(sv(*it, "by"))},
                        {"canDelete", me && (sv(*it, "by") == me->id || mine || staff)}});
    out["commentList"] = list;
    return out;
}

// It's public now: tell the uploader's followers (not Guts, who follows everyone).
void GbServer::videoTellFollowers(json& v) {
    User* u = findUser(sv(v, "by"));
    if (!u || v.value("told", false)) return;
    v["told"] = true;
    size_t n = 0;
    for (const std::string& id : u->followers) {
        if (n++ >= kTellFollowers) break;
        User* f = findUser(id);
        if (f && !isOfficial(*f)) notify(f, "video", u->name + " posted a new video: \"" + sv(v, "title") + "\"", sv(v, "id"));
    }
}

// For report.send (ServerSafety.cpp): "" when it worked, or why not.
std::string GbServer::videoReportTarget(const std::string& id, const User& me, std::string& target, std::string& about, json& copy) {
    const size_t colon = id.find(':');
    const std::string vid = id.substr(0, colon);
    json& all = m_videos["videos"];
    if (!all.contains(vid) || !videoCanSee(all[vid], &me)) return "That isn't there any more.";
    const json& v = all[vid];
    if (colon == std::string::npos) {
        if (sv(v, "by") == me.id) return "You can't report yourself.";
        target = vid; about = sv(v, "by");
        copy = {{"subject", "Video: " + sv(v, "title")}, {"body", sv(v, "text").empty() ? "(no description)" : sv(v, "text")}, {"at", v.value("at", 0LL)}};
        return "";
    }
    const std::string cid = id.substr(colon + 1);
    for (const json& c : v["comments"]) {
        if (sv(c, "id") != cid) continue;
        if (sv(c, "by") == me.id) return "You can't report yourself.";
        target = vid + ":" + cid; about = sv(c, "by");
        copy = {{"subject", "Comment on video: " + sv(v, "title")}, {"body", sv(c, "text")}, {"at", c.value("at", 0LL)}};
        return "";
    }
    return "That comment isn't there any more.";
}
std::string GbServer::videoReportName(const std::string& target) const {
    const std::string vid = target.substr(0, target.find(':'));
    return m_videos["videos"].contains(vid) ? sv(m_videos["videos"][vid], "title") : "(deleted)";
}

json GbServer::videoOp(const std::string& name, User* me, const json& args) {
    const long long t = Online::unixNow();
    json& all = m_videos["videos"];
    if (name == "video.list") {
        std::string sort = sv(args, "sort");
        if (sort != "week" && sort != "top" && sort != "views") sort = "new";
        const std::string cat = isCat(sv(args, "cat")) ? sv(args, "cat") : "";
        const json tags = videoTags(json::array({sv(args, "tag")}));
        const std::string tag = tags.empty() ? "" : tags[0].get<std::string>();
        const std::string q = lower(sv(args, "q")).substr(0, 60);
        const User* by = nullptr;
        if (args.contains("by") && !(args["by"].is_string() && args["by"].get<std::string>().empty())) {
            by = findPerson(args["by"].is_number() ? std::to_string(args["by"].get<long long>()) : sv(args, "by"));
            if (!by) return fail("There's nobody with that name or number.");
        }
        const bool mine = me && by && by->id == me->id;
        long long page = args.contains("page") && args["page"].is_number() ? (long long)args["page"].get<double>() : 1;
        page = std::clamp(page, 1LL, 1000LL);
        std::vector<const json*> list;
        for (const auto& [id, v] : all.items()) {
            if (mine ? sv(v, "by") != me->id : by ? !(sv(v, "by") == by->id && isPublic(v)) : !isPublic(v)) continue;
            if (!cat.empty() && sv(v, "cat") != cat) continue;
            if (!tag.empty() && !hasId(v["tags"], tag)) continue;
            if (!q.empty()) {
                bool hit = lower(sv(v, "title")).find(q) != std::string::npos;
                for (const json& x : v["tags"]) if (x.get<std::string>().find(q) != std::string::npos) hit = true;
                if (!hit) continue;
            }
            if (sort == "week" && v.value("at", 0LL) <= t - 7 * 86400) continue;
            list.push_back(&v);
        }
        auto score = [&](const json* v) {
            return sort == "views" ? v->value("views", 0LL) : (long long)v->at("likes").size() * 10 + v->value("views", 0LL);   // (a like counts as ten views)
        };
        std::sort(list.begin(), list.end(), [&](const json* x, const json* y) {
            if (sort != "new" && score(x) != score(y)) return score(x) > score(y);
            return x->value("at", 0LL) > y->value("at", 0LL);
        });
        json out = json::array();
        const size_t from = (size_t)(page - 1) * kPageSize;
        for (size_t i = from; i < list.size() && i < from + kPageSize; ++i) out.push_back(videoJson(*list[i], me, false));
        json r = okay(); r["videos"] = out; r["more"] = list.size() > from + kPageSize; r["page"] = page; r["total"] = list.size();
        r["cats"] = json::array(); for (const char* c : kCats) r["cats"].push_back(c);
        return r;
    }
    const std::string id = sv(args, "id");
    json* v = all.contains(id) ? &all[id] : nullptr;

    // Without an account (worker/index.js): the file arriving, and the file and picture going out.
    if (name == "video.chunk") {
        if (!v || sv(*v, "state") != "uploading" || !v->contains("up") || sv(args, "t").empty() || sv(args, "t") != sv((*v)["up"], "token"))
            return fail("That upload isn't open (any more). Start again.");
        if ((*v)["up"].value("until", 0LL) < t) { videoForget(id); return fail("That upload took too long. Start again."); }
        std::string data;
        if (!Online::base64Decode(sv(args, "data"), data) || (long long)data.size() > kChunk) return fail("That piece got scrambled. Start again.");
        const fs::path file = m_opts.data / "files" / ("video-" + id);
        const long long at = (long long)nv(args, "at"), got = (*v)["up"].value("got", 0LL);
        if (at != got || got + (long long)data.size() > v->value("size", 0LL)) { videoForget(id); return fail("The file isn't the size you said it was. Start again."); }
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        {
            std::ofstream f(file, at == 0 ? std::ios::binary | std::ios::trunc : std::ios::binary | std::ios::app);
            if (!f || !f.write(data.data(), (std::streamsize)data.size())) { videoForget(id); return fail("The server couldn't save that video."); }
        }
        (*v)["up"]["got"] = got + (long long)data.size();
        if (!args.value("last", false)) { json r = okay(); r["got"] = (*v)["up"]["got"]; return r; }
        if ((*v)["up"].value("got", 0LL) != v->value("size", 0LL)) { videoForget(id); return fail("The file isn't the size you said it was. Start again."); }
        std::string first;
        { std::ifstream f(file, std::ios::binary); first.resize(16); f.read(first.data(), 16); first.resize((size_t)f.gcount()); }
        const std::string type = videoType(first);
        if (type.empty()) { videoForget(id); return fail("That isn't a video this site can play. Use .mp4, .mov or .webm."); }
        (*v)["type"] = type;
        (*v)["state"] = "ready";
        v->erase("up");
        if (isPublic(*v)) videoTellFollowers(*v);
        saveVideos();
        json r = okay(); r["video"] = videoJson(*v, findUser(sv(*v, "by")), true); return r;
    }
    if (name == "video.bytes" || name == "video.thumb") {
        if (!v || sv(*v, "state") != "ready" || !(isPublic(*v) || sv(args, "k") == sv(*v, "key"))) return fail("No video.");
        std::string data;
        if (name == "video.thumb") {
            if (!v->value("thumb", false) || !readFile(m_opts.data / "files" / ("vidthumb-" + id), data)) return fail("No picture.");
            json r = okay(); r["type"] = isPng(data) ? "png" : "jpg"; r["data"] = Online::base64Encode(data); return r;
        }
        const long long size = v->value("size", 0LL), from = (long long)nv(args, "from");
        const long long want = std::clamp((long long)nv(args, "size"), 0LL, kChunk);
        if (from < 0 || (from >= size && want > 0)) return fail("Bad range.");
        const long long n = std::min(want, size - from);
        data.resize((size_t)std::max(0LL, n));
        if (n > 0) {
            std::ifstream f(m_opts.data / "files" / ("video-" + id), std::ios::binary);
            if (!f || !f.seekg(from) || !f.read(data.data(), n)) return fail("No video.");
        }
        json r = okay(); r["type"] = sv(*v, "type"); r["size"] = size; r["data"] = Online::base64Encode(data); return r;
    }

    if (name == "video.get") {
        if (!v || !videoCanSee(*v, me)) return fail("That video isn't here (any more).");
        if (sv(*v, "state") == "ready" && (!me || sv(*v, "by") != me->id)) { (*v)["views"] = v->value("views", 0LL) + 1; saveVideos(); }
        json r = okay(); r["video"] = videoJson(*v, me, true); return r;
    }

    if (!me || me->userId == 0) return fail("Sign up to upload videos, like and comment.");
    const bool staff = isStaff(*me), verified = isVerified(*me);

    if (name == "video.start" || name == "video.edit") {
        const std::string title = say(sv(args, "title"), kTitle);
        if (title.empty()) return fail("Give your video a title.");
        const std::string text = say(sv(args, "text"), kText, true);
        const std::string cat = isCat(sv(args, "cat")) ? sv(args, "cat") : "other";
        const json tags = videoTags(args.contains("tags") ? args["tags"] : json());
        std::string thumb;
        if (!sv(args, "thumb").empty() && (!Online::base64Decode(sv(args, "thumb"), thumb) || !(isJpg(thumb) || isPng(thumb)) || (long long)thumb.size() > kThumbBytes))
            return fail("The video's picture must be a .jpg or .png under " + std::to_string(kThumbBytes >> 10) + " KB.");
        if (name == "video.edit") {
            if (!v || !videoCanSee(*v, me)) return fail("That video isn't here (any more).");
            if (sv(*v, "by") != me->id) return fail("Only the uploader can change this.");
            (*v)["title"] = title; (*v)["text"] = text; (*v)["cat"] = cat; (*v)["tags"] = tags; (*v)["edited"] = t;
            if (!thumb.empty() && writeFile(m_opts.data / "files" / ("vidthumb-" + id), thumb)) (*v)["thumb"] = true;
            saveVideos();
            json r = okay(); r["video"] = videoJson(*v, me, true); return r;
        }
        // Uploads that never finished are let go first.
        std::vector<std::string> stale;
        for (const auto& [k, x] : all.items()) if (sv(x, "state") == "uploading" && x.contains("up") && x["up"].value("until", 0LL) < t) stale.push_back(k);
        for (const std::string& k : stale) videoForget(k);
        json& made = m_videos["made"][me->id];
        const long long today = t / 86400;
        if (!made.is_object() || made.value("day", 0LL) != today) made = {{"day", today}, {"n", 0}};
        const long long most = verified || staff ? kPerDayVerified : kPerDay;
        if (made.value("n", 0LL) >= most) return fail("You've uploaded " + std::to_string(most) + " videos today. Come back tomorrow!");
        const long long size = (long long)nv(args, "size");
        if (size <= 0) return fail("Pick a video first.");
        if (size > kMostBytes) return fail("That video is too big (the most is " + std::to_string(kMostBytes >> 20) + " MB). Try a shorter one, or save it smaller.");
        long long used = 0;
        for (const auto& [k, x] : all.items()) used += x.value("size", 0LL);
        if (used + size > storageRoom()) return fail("Videos are full for now (they have to fit in the free space). Staff are on it: try again later.");
        const std::string vid = "vid-" + Account::randomHex(6), token = Account::randomHex(16);
        json video = {{"id", vid}, {"by", me->id}, {"title", title}, {"text", text}, {"cat", cat}, {"tags", tags}, {"type", ""}, {"size", size},
                      {"at", t}, {"edited", 0}, {"views", 0}, {"likes", json::array()}, {"comments", json::array()},
                      {"secs", std::clamp((long long)std::llround(nv(args, "secs")), 0LL, kMostSecs)},
                      {"w", std::clamp((long long)nv(args, "w"), 0LL, 8192LL)}, {"h", std::clamp((long long)nv(args, "h"), 0LL, 8192LL)},
                      {"thumb", !thumb.empty()}, {"key", Account::randomHex(12)}, {"state", "uploading"},
                      {"up", {{"token", token}, {"until", t + kUploadWait}, {"got", 0}}}};
        if (!verified && !staff) video["review"] = "pending";
        if (!thumb.empty()) writeFile(m_opts.data / "files" / ("vidthumb-" + vid), thumb);
        all[vid] = video;
        made["n"] = made.value("n", 0LL) + 1;
        saveVideos();
        json r = okay(); r["video"] = videoJson(all[vid], me, true); r["upload"] = "/vidup/" + vid + "?t=" + token; r["most"] = kMostBytes;
        return r;
    }
    if (name == "video.pending") {
        if (!staff) return fail("Only staff can do that.");
        std::vector<const json*> list;
        for (const auto& [k, x] : all.items()) if (sv(x, "review") == "pending" && sv(x, "state") == "ready") list.push_back(&x);
        std::sort(list.begin(), list.end(), [](const json* x, const json* y) { return x->value("at", 0LL) < y->value("at", 0LL); });
        json out = json::array();
        for (const json* x : list) out.push_back(videoJson(*x, me, false));
        json r = okay(); r["videos"] = out; return r;
    }

    if (!v || !videoCanSee(*v, me)) return fail("That video isn't here (any more).");
    if (name == "video.delete") {
        if (sv(*v, "by") != me->id && !staff) return fail("Only the uploader or staff can delete this.");
        videoForget(id);
        return okay();
    }
    if (name == "video.review") {
        if (!staff) return fail("Only staff can do that.");
        if (sv(*v, "review").empty()) return fail("That isn't waiting for a check any more.");
        User* uploader = findUser(sv(*v, "by"));
        const std::string title = sv(*v, "title");
        if (args.value("ok", false)) {
            v->erase("review"); v->erase("reviewNote");
            notify(uploader, "video", "Your video \"" + title + "\" passed the staff check. Everyone can watch it now!", id);
            videoTellFollowers(*v);
        } else {
            (*v)["review"] = "rejected";
            (*v)["reviewNote"] = say(sv(args, "note"), 200);
            const std::string note = sv(*v, "reviewNote");
            notify(uploader, "video", "Your video \"" + title + "\" didn't pass the staff check" + (note.empty() ? "." : ": " + note), id);
        }
        saveVideos();
        json r = okay(); r["video"] = videoJson(*v, me, true); return r;
    }
    if (!isPublic(*v)) return fail(sv(*v, "state") != "ready" ? "This video is still uploading." : "This hasn't passed the staff check yet.");
    if (name == "video.like") {
        const bool on = !(args.contains("on") && args["on"].is_boolean() && !args["on"].get<bool>());
        json& likes = (*v)["likes"];
        const bool had = hasId(likes, me->id);
        if (on && !had) likes.push_back(me->id);
        if (!on && had) likes.erase(std::remove(likes.begin(), likes.end(), json(me->id)), likes.end());
        if (on != had) saveVideos();
        json r = okay(); r["likes"] = likes.size(); r["liked"] = on; return r;
    }
    if (name == "video.comment") {
        const std::string text = say(sv(args, "text"), kCommentText, true);
        if (text.empty()) return fail("Write something first.");
        (*v)["comments"].push_back({{"id", Account::randomHex(5)}, {"by", me->id}, {"text", text}, {"at", t}});
        saveVideos();
        if (sv(*v, "by") != me->id) notify(findUser(sv(*v, "by")), "video", me->name + " commented on your video \"" + sv(*v, "title") + "\".", id);
        json r = okay(); r["video"] = videoJson(*v, me, true); return r;
    }
    if (name == "video.uncomment") {
        json& list = (*v)["comments"];
        const std::string cid = sv(args, "comment");
        auto c = std::find_if(list.begin(), list.end(), [&](const json& x) { return sv(x, "id") == cid; });
        if (c == list.end()) return fail("That comment isn't there any more.");
        if (sv(*c, "by") != me->id && sv(*v, "by") != me->id && !staff) return fail("You can only delete your own comments, or ones on your video.");
        list.erase(c);
        saveVideos();
        json r = okay(); r["video"] = videoJson(*v, me, true); return r;
    }
    return fail("Unknown request.");
}
