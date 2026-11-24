// G&B Live: go live from a browser, watch, chat. worker/streams.js has the same rules (and the
// notes on why):
//   - the streamer's browser sends kSegSecs-second pieces (each a whole small WebM video); the
//     newest kKeep are kept (here in memory, not on disk), older ones let go
//   - only Verified people and staff can go live (nobody can check a live stream first)
//   - a stream ends on End, after kQuiet seconds without a new piece, or after kMostSecs;
//     its pieces go then
//   - chat: time outs (kTimeout), slow mode (kSlowChoices), deleting lines; the streamer and staff
//     moderate, staff can end any stream; followers are told when someone goes live (once per kTellGap)
//   stream.list {}  stream.get {id, after, viewer}  stream.start {title, cat, mime, thumb}  stream.snap {id, thumb}
//   stream.end {id}  stream.chat {id, text}  stream.unchat {id, chat}  stream.timeout {id, user}  stream.slow {id, secs}
//   No account (worker/index.js asks): stream.push {id, t, n, data}  stream.piece {id, n}  stream.picture {id}
// Kept in streams.json ({streams: id -> stream, made: account -> {day, n}, told: account -> when});
// pictures are files/livethumb-<id>.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;
namespace fs = std::filesystem;

namespace {
const char* kCats[] = {"gaming", "music", "funny", "howto", "animation", "vlog", "sports", "science", "news", "other"};
const char* kMimes[] = {"video/webm;codecs=vp8,opus", "video/webm;codecs=vp9,opus", "video/webm;codecs=vp8", "video/webm;codecs=vp9", "video/webm"};
constexpr long long kSegSecs = 4, kKeep = 15, kLeastSegBytes = 1000, kMostSegBytes = 4LL * 1024 * 1024;
constexpr long long kQuiet = 30, kMostSecs = 4 * 3600, kPerDay = 10, kTimeout = 600, kWatching = 15, kTellGap = 1800, kEndedKept = 7 * 86400;
constexpr long long kThumbBytes = 200 * 1024;
constexpr size_t kTitle = 100, kChatText = 300, kChatKept = 200, kChatPerGet = 50, kTellFollowers = 500;
const long long kSlowChoices[] = {0, 5, 15, 60};

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
double nv(const json& j, const char* k) { return j.contains(k) && j[k].is_number() ? j[k].get<double>() : 0; }
bool isCat(const std::string& c) { for (const char* x : kCats) if (c == x) return true; return false; }
bool isMime(const std::string& m) { for (const char* x : kMimes) if (m == x) return true; return false; }
bool isJpg(const std::string& d) { return d.size() > 3 && (unsigned char)d[0] == 0xff && (unsigned char)d[1] == 0xd8 && (unsigned char)d[2] == 0xff; }
bool isWebm(const std::string& d) {
    return d.size() >= 4 && (unsigned char)d[0] == 0x1a && (unsigned char)d[1] == 0x45 && (unsigned char)d[2] == 0xdf && (unsigned char)d[3] == 0xa3;
}
} // namespace

void GbServer::saveStreams() {
    for (auto& [id, s] : m_streams["streams"].items()) {
        json& c = s["chat"];
        while (c.size() > kChatKept) c.erase(c.begin());
    }
    writeFile(m_opts.data / "streams.json", m_streams.dump(1));
}
void GbServer::loadStreams() {
    std::string text;
    if (readFile(m_opts.data / "streams.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object() && j.contains("streams") && j["streams"].is_object()) m_streams = j;
    }
    for (const char* k : {"made", "told"}) if (!m_streams.contains(k) || !m_streams[k].is_object()) m_streams[k] = json::object();
    // (pieces were only in memory: streams that were on when the server stopped have ended)
    const long long t = Online::unixNow();
    for (auto& [id, s] : m_streams["streams"].items()) if (sv(s, "state") == "live") streamEnd(s, t, "quiet");
}

void GbServer::streamEnd(json& s, long long t, const std::string& why) {
    if (sv(s, "state") != "live") return;
    s["state"] = "ended"; s["ended"] = t; s["why"] = why;
    s.erase("token");
    m_streamPieces.erase(sv(s, "id"));
    m_streamWatch.erase(sv(s, "id"));
    s["first"] = s.value("last", -1LL) + 1;
    saveStreams();
}
void GbServer::streamTidy(long long t) {
    std::vector<std::string> gone;
    for (auto& [id, s] : m_streams["streams"].items()) {
        const long long seen = s.value("seen", s.value("at", 0LL));
        if (sv(s, "state") == "live" && seen < t - kQuiet) streamEnd(s, t, "quiet");
        else if (sv(s, "state") == "live" && t - s.value("at", 0LL) > kMostSecs) streamEnd(s, t, "long");
        else if (sv(s, "state") == "ended" && s.value("ended", 0LL) < t - kEndedKept) gone.push_back(id);
    }
    for (const std::string& id : gone) {
        std::error_code ec;
        fs::remove(m_opts.data / "files" / ("livethumb-" + id), ec);
        m_streams["streams"].erase(id);
    }
    if (!gone.empty()) saveStreams();
}
long long GbServer::streamViewers(const std::string& id, long long t) {
    auto w = m_streamWatch.find(id);
    if (w == m_streamWatch.end()) return 0;
    for (auto it = w->second.begin(); it != w->second.end();) it = it->second < t - kWatching ? w->second.erase(it) : std::next(it);
    return (long long)w->second.size();
}

json GbServer::streamJson(const json& s, const User* me, long long t) {
    auto person = [&](const std::string& id) {
        auto u = m_users.find(id);
        if (u == m_users.end()) return json{{"id", id}, {"name", "?"}, {"userId", 0}, {"verified", false}, {"followers", 0}};
        return json{{"id", id}, {"name", u->second.name}, {"userId", u->second.userId}, {"verified", isVerified(u->second)}, {"followers", u->second.followers.size()}};
    };
    const std::string id = sv(s, "id");
    const bool mine = me && sv(s, "by") == me->id, staff = me && isStaff(*me), live = sv(s, "state") == "live";
    auto owner = m_users.find(sv(s, "by"));
    return {{"id", id}, {"title", sv(s, "title")}, {"cat", sv(s, "cat")}, {"at", s.value("at", 0LL)}, {"state", sv(s, "state")},
            {"ended", s.value("ended", 0LL)}, {"by", person(sv(s, "by"))}, {"viewers", live ? streamViewers(id, t) : 0},
            {"peak", s.value("peak", 0LL)}, {"first", s.value("first", -1LL)}, {"last", s.value("last", -1LL)}, {"segSecs", kSegSecs},
            {"slow", s.value("slow", 0LL)}, {"mime", sv(s, "mime").empty() ? kMimes[0] : sv(s, "mime")}, {"src", "/live/" + id + "/"},
            {"poster", s.value("thumb", false) ? "/live/" + id + "/pic?v=" + std::to_string(s.value("thumbAt", 0LL)) : ""},
            {"mine", mine}, {"canMod", mine || staff}, {"canEnd", (mine || staff) && live},
            {"following", me && owner != m_users.end() && owner->second.followers.count(me->id) > 0}};
}

// For report.send (ServerSafety.cpp): "" when it worked, or why not.
std::string GbServer::streamReportTarget(const std::string& id, const User& me, std::string& target, std::string& about, json& copy) {
    const size_t colon = id.find(':');
    const std::string sid = id.substr(0, colon);
    json& all = m_streams["streams"];
    if (!all.contains(sid)) return "That isn't there any more.";
    const json& s = all[sid];
    if (colon == std::string::npos) {
        if (sv(s, "by") == me.id) return "You can't report yourself.";
        target = sid; about = sv(s, "by");
        copy = {{"subject", "Live stream: " + sv(s, "title")}, {"body", "(a live stream: staff can watch it while it's on)"}, {"at", s.value("at", 0LL)}};
        return "";
    }
    const std::string cid = id.substr(colon + 1);
    for (const json& c : s["chat"]) {
        if (sv(c, "id") != cid) continue;
        if (sv(c, "by") == me.id) return "You can't report yourself.";
        target = sid + ":" + cid; about = sv(c, "by");
        copy = {{"subject", "Chat on live stream: " + sv(s, "title")}, {"body", sv(c, "text")}, {"at", c.value("at", 0LL)}};
        return "";
    }
    return "That chat line isn't there any more.";
}
std::string GbServer::streamReportName(const std::string& target) const {
    const std::string sid = target.substr(0, target.find(':'));
    return m_streams["streams"].contains(sid) ? sv(m_streams["streams"][sid], "title") : "(deleted)";
}

json GbServer::streamOp(const std::string& name, User* me, const json& args) {
    const long long t = Online::unixNow();
    streamTidy(t);
    json& all = m_streams["streams"];
    auto chatJson = [&](const json& s, const json& c) {
        User* by = findUser(sv(c, "by"));
        json who = by ? json{{"id", by->id}, {"name", by->name}, {"userId", by->userId}, {"verified", isVerified(*by)}, {"followers", by->followers.size()}}
                       : json{{"id", sv(c, "by")}, {"name", "?"}, {"userId", 0}, {"verified", false}, {"followers", 0}};
        return json{{"n", c.value("n", 0LL)}, {"id", sv(c, "id")}, {"text", sv(c, "text")}, {"at", c.value("at", 0LL)}, {"by", who},
                    {"canDelete", me && (sv(c, "by") == me->id || sv(s, "by") == me->id || isStaff(*me))}};
    };
    if (name == "stream.list") {
        std::vector<json> live, recent;
        for (const auto& [id, s] : all.items()) {
            if (sv(s, "state") == "live" && s.value("last", -1LL) >= 0) live.push_back(streamJson(s, me, t));
            else if (sv(s, "state") == "ended") recent.push_back(streamJson(s, me, t));
        }
        std::sort(live.begin(), live.end(), [](const json& x, const json& y) {
            if (x["viewers"] != y["viewers"]) return x.value("viewers", 0LL) > y.value("viewers", 0LL);
            return x.value("at", 0LL) > y.value("at", 0LL);
        });
        std::sort(recent.begin(), recent.end(), [](const json& x, const json& y) { return x.value("ended", 0LL) > y.value("ended", 0LL); });
        if (recent.size() > 12) recent.resize(12);
        json r = okay(); r["live"] = live; r["recent"] = recent;
        r["cats"] = json::array(); for (const char* c : kCats) r["cats"].push_back(c);
        return r;
    }
    const std::string id = sv(args, "id");
    json* s = all.contains(id) ? &all[id] : nullptr;

    // Without an account (worker/index.js): a piece arriving, a piece or the picture going out.
    if (name == "stream.push") {
        if (!s || sv(*s, "state") != "live" || sv(args, "t").empty() || sv(args, "t") != sv(*s, "token")) return fail("This stream has ended. Start a new one.");
        const long long n = (long long)nv(args, "n"), last = s->value("last", -1LL);
        if ((double)n != nv(args, "n") || n <= last || n > last + 10) return fail("That piece is out of order.");
        std::string data;
        if (!Online::base64Decode(sv(args, "data"), data)) return fail("That piece got scrambled.");
        if ((long long)data.size() < kLeastSegBytes) return fail("That piece is empty.");
        if ((long long)data.size() > kMostSegBytes) return fail("That piece is too big. Lower the quality.");
        if (!isWebm(data)) return fail("Pieces must be WebM video.");
        auto& pieces = m_streamPieces[id];
        pieces[n] = std::move(data);
        while ((long long)pieces.size() > kKeep) pieces.erase(pieces.begin());
        (*s)["last"] = n; (*s)["seen"] = t;
        (*s)["first"] = pieces.begin()->first;
        if (t - s->value("at", 0LL) > kMostSecs) { streamEnd(*s, t, "long"); return fail("Streams can be 4 hours long at most. Start a new one!"); }
        (*s)["peak"] = std::max(s->value("peak", 0LL), streamViewers(id, t));
        saveStreams();
        json r = okay(); r["n"] = n; r["viewers"] = streamViewers(id, t); return r;
    }
    if (name == "stream.piece") {
        auto p = m_streamPieces.find(id);
        if (!s || p == m_streamPieces.end()) return fail("No piece.");
        auto it = p->second.find((long long)nv(args, "n"));
        if (it == p->second.end()) return fail("No piece.");
        json r = okay(); r["data"] = Online::base64Encode(it->second); return r;
    }
    if (name == "stream.picture") {
        std::string data;
        if (!s || !s->value("thumb", false) || !readFile(m_opts.data / "files" / ("livethumb-" + id), data)) return fail("No picture.");
        json r = okay(); r["data"] = Online::base64Encode(data); return r;
    }

    if (name == "stream.get") {
        if (!s) return fail("That stream isn't here (any more).");
        if (sv(*s, "state") == "live" && !(me && me->id == sv(*s, "by"))) {
            const std::string who = me && me->userId ? me->id : "guest:" + sv(args, "viewer").substr(0, 40);
            m_streamWatch[id][who] = t;
            (*s)["peak"] = std::max(s->value("peak", 0LL), streamViewers(id, t));
        }
        const long long after = std::max(0LL, (long long)nv(args, "after"));
        json chat = json::array();
        for (const json& c : (*s)["chat"]) if (c.value("n", 0LL) > after) chat.push_back(chatJson(*s, c));
        while (chat.size() > kChatPerGet) chat.erase(chat.begin());
        json r = okay(); r["stream"] = streamJson(*s, me, t); r["chat"] = chat;
        if (me && s->contains("timeouts") && (*s)["timeouts"].value(me->id, 0LL) > t) r["timeoutUntil"] = (*s)["timeouts"][me->id];
        return r;
    }

    if (!me || me->userId == 0) return fail("Sign up to go live and chat.");
    const bool staff = isStaff(*me);

    if (name == "stream.start") {
        if (!isVerified(*me) && !staff) return fail("Only Verified people can go live for now (nobody can check a live stream first). Get Verified, then come back!");
        const std::string title = say(sv(args, "title"), kTitle);
        if (title.empty()) return fail("Give your stream a title.");
        const std::string cat = isCat(sv(args, "cat")) ? sv(args, "cat") : "other";
        const std::string mime = isMime(sv(args, "mime")) ? sv(args, "mime") : kMimes[0];
        std::string thumb;
        if (!sv(args, "thumb").empty() && (!Online::base64Decode(sv(args, "thumb"), thumb) || !isJpg(thumb) || (long long)thumb.size() > kThumbBytes))
            return fail("The stream's picture must be a .jpg under " + std::to_string(kThumbBytes >> 10) + " KB.");
        json& made = m_streams["made"][me->id];
        const long long today = t / 86400;
        if (!made.is_object() || made.value("day", 0LL) != today) made = {{"day", today}, {"n", 0}};
        if (made.value("n", 0LL) >= kPerDay) return fail("You've started " + std::to_string(kPerDay) + " streams today. Come back tomorrow!");
        for (auto& [k, old] : all.items()) if (sv(old, "by") == me->id && sv(old, "state") == "live") streamEnd(old, t, "replaced");   // one at a time
        const std::string sid = "live-" + Account::randomHex(6), token = Account::randomHex(16);
        all[sid] = {{"id", sid}, {"by", me->id}, {"title", title}, {"cat", cat}, {"mime", mime}, {"at", t}, {"state", "live"}, {"ended", 0},
                    {"token", token}, {"first", -1}, {"last", -1}, {"seen", t}, {"peak", 0}, {"slow", 0}, {"chat", json::array()}, {"chatN", 0},
                    {"timeouts", json::object()}, {"thumb", !thumb.empty()}, {"thumbAt", t}};
        if (!thumb.empty()) writeFile(m_opts.data / "files" / ("livethumb-" + sid), thumb);
        made["n"] = made.value("n", 0LL) + 1;
        // Followers hear about it (but not every time someone restarts a stream).
        if (m_streams["told"].value(me->id, 0LL) < t - kTellGap) {
            m_streams["told"][me->id] = t;
            size_t n = 0;
            for (const std::string& f : me->followers) {
                if (n++ >= kTellFollowers) break;
                User* u = findUser(f);
                if (u && !isOfficial(*u)) notify(u, "stream", me->name + " is live now: \"" + title + "\"", sid);
            }
        }
        saveStreams();
        json r = okay(); r["stream"] = streamJson(all[sid], me, t); r["push"] = "/liveup/" + sid + "?t=" + token; r["segSecs"] = kSegSecs;
        return r;
    }
    if (!s) return fail("That stream isn't here (any more).");
    const bool mod = sv(*s, "by") == me->id || staff;
    if (name == "stream.snap") {
        if (sv(*s, "by") != me->id) return fail("Only the streamer can do that.");
        if (sv(*s, "state") != "live") return fail("This stream has ended.");
        std::string thumb;
        if (!Online::base64Decode(sv(args, "thumb"), thumb) || !isJpg(thumb) || (long long)thumb.size() > kThumbBytes)
            return fail("The stream's picture must be a .jpg under " + std::to_string(kThumbBytes >> 10) + " KB.");
        writeFile(m_opts.data / "files" / ("livethumb-" + id), thumb);
        (*s)["thumb"] = true; (*s)["thumbAt"] = t;
        saveStreams();
        return okay();
    }
    if (name == "stream.end") {
        if (!mod) return fail("Only the streamer or staff can end this.");
        if (sv(*s, "state") == "live") {
            if (sv(*s, "by") != me->id) notify(findUser(sv(*s, "by")), "stream", "Staff ended your stream \"" + sv(*s, "title") + "\".", id);
            streamEnd(*s, t, sv(*s, "by") == me->id ? "ended" : "staff");
        }
        json r = okay(); r["stream"] = streamJson(*s, me, t); return r;
    }
    if (name == "stream.slow") {
        if (!mod) return fail("Only the streamer or staff can change this.");
        const long long secs = (long long)nv(args, "secs");
        (*s)["slow"] = std::find(std::begin(kSlowChoices), std::end(kSlowChoices), secs) != std::end(kSlowChoices) ? secs : 0;
        saveStreams();
        json r = okay(); r["stream"] = streamJson(*s, me, t); return r;
    }
    if (name == "stream.timeout") {
        if (!mod) return fail("Only the streamer or staff can do that.");
        User* who = findUser(sv(args, "user"));
        if (!who) return fail("There's nobody with that name or number.");
        if (who->id == sv(*s, "by")) return fail("You can't time out the streamer.");
        if (isStaff(*who)) return fail("You can't time out staff.");
        if (!s->contains("timeouts") || !(*s)["timeouts"].is_object()) (*s)["timeouts"] = json::object();
        (*s)["timeouts"][who->id] = t + kTimeout;
        json& chat = (*s)["chat"];   // (their lines go too)
        chat.erase(std::remove_if(chat.begin(), chat.end(), [&](const json& c) { return sv(c, "by") == who->id; }), chat.end());
        saveStreams();
        json r = okay(); r["until"] = t + kTimeout; return r;
    }
    if (name == "stream.unchat") {
        json& chat = (*s)["chat"];
        const std::string cid = sv(args, "chat");
        auto c = std::find_if(chat.begin(), chat.end(), [&](const json& x) { return sv(x, "id") == cid; });
        if (c == chat.end()) return fail("That chat line isn't there any more.");
        if (sv(*c, "by") != me->id && !mod) return fail("You can only delete your own chat lines.");
        chat.erase(c);
        saveStreams();
        return okay();
    }
    if (name == "stream.chat") {
        if (sv(*s, "state") != "live") return fail("This stream has ended, so its chat is closed.");
        const long long until = s->contains("timeouts") ? (*s)["timeouts"].value(me->id, 0LL) : 0;
        if (until > t) return fail("You're in a time out for this chat. Wait " + std::to_string((until - t + 59) / 60) + " minute(s).");
        const std::string text = say(sv(args, "text"), kChatText);
        if (text.empty()) return fail("Write something first.");
        const long long slow = s->value("slow", 0LL);
        if (slow && !mod) {
            const json& chat = (*s)["chat"];
            for (auto it = chat.rbegin(); it != chat.rend(); ++it) {
                if (sv(*it, "by") != me->id) continue;
                const long long since = t - it->value("at", 0LL);
                if (since < slow) return fail("Slow mode is on: wait " + std::to_string(slow - since) + " more second(s).");
                break;
            }
        }
        const long long n = s->value("chatN", 0LL) + 1;
        (*s)["chatN"] = n;
        json c = {{"n", n}, {"id", Account::randomHex(5)}, {"by", me->id}, {"text", text}, {"at", t}};
        (*s)["chat"].push_back(c);
        saveStreams();
        json r = okay(); r["chat"] = chatJson(*s, c); return r;
    }
    return fail("Unknown request.");
}
