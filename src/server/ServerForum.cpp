// The forum: boards of threads, each a list of posts, oldest first
// (worker/server.js forumOp has the same boards, limits and answers).
// Anyone can read; signed-up players post; staff pin, lock and delete.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cmath>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
struct Board { const char* id; const char* name; const char* about; bool staffOnly; };
constexpr Board kBoards[] = {
    {"news", "News & Announcements", "Updates from the Guts&Bolts team.", true},
    {"help", "Help", "Stuck? Ask how to do something in Guts&Bolts.", false},
    {"scripting", "Scripting Helpers", "Lua questions, scripts that won't work, and cool code.", false},
    {"building", "Building & Studio", "Tips, tricks and things you built in Studio.", false},
    {"games", "Game Ads", "Show off a game you made and get people playing it.", false},
    {"trading", "Trading", "Find people to trade Limiteds with.", false},
    {"offtopic", "Off Topic", "Anything else. Keep it friendly.", false},
};
constexpr size_t kForumTitle = 80, kForumText = 3000, kForumPage = 20, kMaxForumPosts = 500, kMaxBoardThreads = 1000;
constexpr long long kThreadCooldown = 60, kReplyCooldown = 15;

const Board* boardOf(const std::string& id) {
    for (const Board& b : kBoards) if (id == b.id) return &b;
    return nullptr;
}
json boardJson(const Board& b) {
    return {{"id", b.id}, {"name", b.name}, {"about", b.about}, {"staffOnly", b.staffOnly}};
}
std::string sv(const json& j, const char* k) { return j.value(k, std::string()); }
long long   lv(const json& j, const char* k) { return j.value(k, 0LL); }
} // namespace

json GbServer::forumOp(const std::string& name, User& me, const json& args) {
    const long long now = Online::unixNow();
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto num = [&](const char* k) { return args.contains(k) && args[k].is_number() ? (long long)std::floor(args[k].get<double>()) : 0LL; };
    const bool staff = isStaff(me);
    auto who = [&](const std::string& id) {
        const User* u = findUser(id);
        return u ? json{{"id", u->id}, {"userId", u->userId}, {"name", u->name}, {"verified", isVerified(*u)}, {"staff", isStaff(*u)}, {"partner", isPartner(*u)}}
                 : json{{"id", id}, {"userId", 0}, {"name", "?"}};
    };
    auto hidden = [&](const std::string& id) { const User* u = findUser(id); return u && blocks(me, *u); };
    // A board's threads: pinned first, then the ones with the newest posts.
    auto threadsIn = [&](const std::string& board) {
        std::vector<json*> list;
        for (auto& [id, th] : m_forum.items()) if (sv(th, "board") == board) list.push_back(&th);
        std::stable_sort(list.begin(), list.end(), [](const json* x, const json* y) {
            const bool px = x->value("pinned", false), py = y->value("pinned", false);
            if (px != py) return px;
            return lv(*x, "last") > lv(*y, "last");
        });
        return list;
    };
    auto summary = [&](const json& th) {
        return json{{"id", sv(th, "id")}, {"title", sv(th, "title")}, {"by", who(sv(th, "by"))}, {"at", lv(th, "at")},
                    {"last", lv(th, "last")}, {"lastBy", who(sv(th, "lastBy"))}, {"replies", (long long)th["posts"].size() - 1},
                    {"views", lv(th, "views")}, {"pinned", th.value("pinned", false)}, {"locked", th.value("locked", false)}};
    };
    // One page of a thread (page -1 = the last page).
    auto threadPage = [&](const json& th, long long page) {
        const json& posts = th["posts"];
        const long long pages = std::max<long long>(1, ((long long)posts.size() + (long long)kForumPage - 1) / (long long)kForumPage);
        const long long p = page < 0 ? pages - 1 : std::clamp(page, 0LL, pages - 1);
        const Board* b = boardOf(sv(th, "board"));
        const std::string firstId = posts.empty() ? std::string() : sv(posts[0], "id");
        json out = json::array();
        for (size_t i = (size_t)(p * (long long)kForumPage); i < posts.size() && i < (size_t)((p + 1) * (long long)kForumPage); ++i) {
            const json& x = posts[i];
            const std::string by = sv(x, "by");
            if (hidden(by)) continue;
            out.push_back({{"id", sv(x, "id")}, {"text", sv(x, "text")}, {"at", lv(x, "at")}, {"edited", lv(x, "edited")},
                           {"by", who(by)}, {"first", sv(x, "id") == firstId},
                           {"canDelete", me.userId != 0 && (by == me.id || staff)}});
        }
        json t = summary(th);
        t["board"] = sv(th, "board");
        t["boardName"] = b ? b->name : "?";
        json r = okay();
        r["thread"] = t; r["posts"] = out; r["page"] = p; r["pages"] = pages;
        r["canReply"] = me.userId != 0 && (!th.value("locked", false) || staff);
        r["canModerate"] = staff;
        return r;
    };

    if (name == "forum.boards") {
        json boards = json::array();
        for (const Board& b : kBoards) {
            auto list = threadsIn(b.id);
            const json* newest = nullptr;
            long long posts = 0;
            for (const json* th : list) {
                posts += (long long)(*th)["posts"].size();
                if (!newest || lv(*th, "last") > lv(*newest, "last")) newest = th;
            }
            json j = boardJson(b);
            j["threads"] = list.size();
            j["posts"] = posts;
            j["last"] = newest ? json{{"thread", sv(*newest, "id")}, {"title", sv(*newest, "title")}, {"by", who(sv(*newest, "lastBy"))},
                                      {"at", lv(*newest, "last")}}
                               : json();
            boards.push_back(j);
        }
        json r = okay();
        r["boards"] = boards;
        return r;
    }
    if (name == "forum.search") {   // threads whose title or a post has the words, newest first
        std::string q = str("q");
        q.erase(0, q.find_first_not_of(" \t\r\n"));
        q.erase(q.find_last_not_of(" \t\r\n") + 1);
        q = lower(q.substr(0, 100));
        if (q.size() < 2) return fail("Enter at least two characters to search.");
        const std::string wantBoard = str("board");
        const Board* filter = !wantBoard.empty() && wantBoard != "all" ? boardOf(wantBoard) : nullptr;
        if (!wantBoard.empty() && wantBoard != "all" && !filter) return fail("That board doesn\xE2\x80\x99t exist.");
        std::vector<json*> all;
        for (auto& [id, th] : m_forum.items())
            if ((!filter || sv(th, "board") == filter->id) && !hidden(sv(th, "by"))) all.push_back(&th);
        std::stable_sort(all.begin(), all.end(), [](const json* x, const json* y) { return lv(*x, "last") > lv(*y, "last"); });
        // Don't cut a letter in half at the ends of a snippet.
        auto atLetter = [](const std::string& s, size_t i) {
            while (i > 0 && i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) --i;
            return i;
        };
        json matches = json::array();
        for (json* thp : all) {
            const json& th = *thp;
            const json& posts = th["posts"];
            const bool titleMatch = lower(sv(th, "title")).find(q) != std::string::npos;
            long long matchedPost = -1;
            if (!titleMatch)
                for (size_t i = 0; i < posts.size(); ++i) {
                    const User* author = findUser(sv(posts[i], "by"));
                    if ((!author || !blocks(me, *author)) && lower(sv(posts[i], "text")).find(q) != std::string::npos) { matchedPost = (long long)i; break; }
                }
            if (!titleMatch && matchedPost < 0) continue;
            const Board* b = boardOf(sv(th, "board"));
            const std::string text = titleMatch ? sv(th, "title") : sv(posts[(size_t)matchedPost], "text");
            const size_t at = lower(text).find(q);
            const size_t start = atLetter(text, at > 65 ? at - 65 : 0);
            const size_t end = atLetter(text, std::min(text.size(), at + q.size() + 95));
            const std::string snippet = (start ? "\xE2\x80\xA6" : "") + text.substr(start, end - start) + (end < text.size() ? "\xE2\x80\xA6" : "");
            matches.push_back({{"id", sv(th, "id")}, {"title", sv(th, "title")}, {"board", sv(th, "board")}, {"boardName", b ? b->name : "?"},
                               {"by", who(sv(th, "by"))}, {"last", lv(th, "last")}, {"replies", (long long)posts.size() - 1},
                               {"match", titleMatch ? "title" : "post"}, {"snippet", snippet},
                               {"postPage", (matchedPost < 0 ? 0 : matchedPost) / (long long)kForumPage}});
        }
        const long long pageSize = 20, total = (long long)matches.size();
        const long long pages = std::max<long long>(1, (total + pageSize - 1) / pageSize), page = std::clamp(num("page"), 0LL, pages - 1);
        json results = json::array();
        for (long long i = page * pageSize; i < total && i < (page + 1) * pageSize; ++i) results.push_back(matches[(size_t)i]);
        json r = okay();
        r["results"] = results; r["total"] = total; r["page"] = page; r["pages"] = pages;
        return r;
    }
    if (name == "forum.list") {
        const Board* b = boardOf(str("board"));
        if (!b) return fail("That board doesn't exist.");
        std::vector<json*> list;
        for (json* th : threadsIn(b->id)) if (!hidden(sv(*th, "by"))) list.push_back(th);
        const long long pages = std::max<long long>(1, ((long long)list.size() + (long long)kForumPage - 1) / (long long)kForumPage);
        const long long page = std::clamp(num("page"), 0LL, pages - 1);
        json threads = json::array();
        for (size_t i = (size_t)(page * (long long)kForumPage); i < list.size() && i < (size_t)((page + 1) * (long long)kForumPage); ++i)
            threads.push_back(summary(*list[i]));
        json r = okay();
        r["board"] = boardJson(*b); r["threads"] = threads; r["page"] = page; r["pages"] = pages;
        r["canPost"] = me.userId != 0 && (!b->staffOnly || staff);
        return r;
    }
    if (name == "forum.post") {
        if (me.userId == 0) return fail("Sign up to post on the forum.");
        const Board* b = boardOf(str("board"));
        if (!b) return fail("That board doesn't exist.");
        if (b->staffOnly && !staff) return fail(std::string("Only Guts&Bolts staff post in ") + b->name + ".");
        const std::string title = say(str("title"), kForumTitle), text = say(str("text"), kForumText, true);
        if (title.empty()) return fail("Give your thread a title.");
        if (text.empty()) return fail("Write something first.");
        long long& last = m_lastPost["ft:" + me.id];
        if (now - last < kThreadCooldown) return fail("Slow down a little - wait a minute between new threads.");
        last = now;
        const std::string id = "f" + Account::randomHex(6);
        m_forum[id] = {{"id", id}, {"board", b->id}, {"title", title}, {"by", me.id}, {"at", now}, {"last", now}, {"lastBy", me.id},
                       {"views", 0}, {"pinned", false}, {"locked", false},
                       {"posts", json::array({{{"id", Account::randomHex(6)}, {"by", me.id}, {"text", text}, {"at", now}}})}};
        // A full board: the threads quiet the longest go (pinned ones stay).
        std::vector<std::string> drop;
        size_t kept = 0;
        for (const json* th : threadsIn(b->id))
            if (!th->value("pinned", false) && ++kept > kMaxBoardThreads) drop.push_back(sv(*th, "id"));
        for (const std::string& d : drop) m_forum.erase(d);
        saveForum();
        return threadPage(m_forum[id], 0);
    }
    const std::string tid = str("thread");
    if (!m_forum.contains(tid)) return fail("That thread isn't there any more.");
    json& th = m_forum[tid];
    json& posts = th["posts"];
    if (name == "forum.thread") {
        if (me.userId != 0 && me.id != sv(th, "by")) {
            th["views"] = lv(th, "views") + 1;
            if (now - m_forumSaved > 30) saveForum();   // (views don't need saving every time)
        }
        return threadPage(th, num("page"));
    }
    if (name == "forum.reply") {
        if (me.userId == 0) return fail("Sign up to post on the forum.");
        if (th.value("locked", false) && !staff) return fail("This thread is locked, so nobody can reply.");
        if (posts.size() >= kMaxForumPosts) return fail("This thread is full. Start a new one!");
        User* starter = findUser(sv(th, "by"));
        if (starter && blocks(me, *starter)) return fail("You can't reply to this thread.");
        const std::string text = say(str("text"), kForumText, true);
        if (text.empty()) return fail("Write something first.");
        long long& last = m_lastPost["fr:" + me.id];
        if (now - last < kReplyCooldown) return fail("Slow down a little - wait a few seconds between posts.");
        last = now;
        posts.push_back({{"id", Account::randomHex(6)}, {"by", me.id}, {"text", text}, {"at", now}});
        th["last"] = now;
        th["lastBy"] = me.id;
        saveForum();
        if (starter && starter != &me)
            notify(starter, "forum", me.name + " replied to \"" + sv(th, "title").substr(0, 60) + "\"", tid);
        return threadPage(th, -1);
    }
    if (name == "forum.delete") {
        auto p = std::find_if(posts.begin(), posts.end(), [&](const json& x) { return sv(x, "id") == str("post"); });
        if (p == posts.end()) return fail("That post is already gone.");
        const std::string by = sv(*p, "by");
        if (by != me.id && !staff) return fail("You can only delete your own posts.");
        if (by != me.id)
            staffDid(me, "forum", "Deleted a forum post in \"" + sv(th, "title") + "\": \"" + sv(*p, "text").substr(0, 80) + "\"", by);
        if (p == posts.begin()) {   // the first post: the whole thread goes
            const std::string board = sv(th, "board");
            m_forum.erase(tid);
            saveForum();
            json r = okay();
            r["gone"] = true; r["board"] = board;
            return r;
        }
        posts.erase(p);
        th["last"] = lv(posts.back(), "at");
        th["lastBy"] = sv(posts.back(), "by");
        saveForum();
        return threadPage(th, num("page"));
    }
    if (name == "forum.mod") {   // staff: pin it to the top, or lock it (no more replies)
        if (!staff) return fail("Only staff can do that.");
        if (args.contains("pinned")) th["pinned"] = args["pinned"].is_boolean() && args["pinned"].get<bool>();
        if (args.contains("locked")) th["locked"] = args["locked"].is_boolean() && args["locked"].get<bool>();
        staffDid(me, "forum", std::string(th.value("pinned", false) ? "Pinned" : "Unpinned") + " and " +
                              (th.value("locked", false) ? "locked" : "unlocked") + " the thread \"" + sv(th, "title") + "\"",
                 sv(th, "by"));
        saveForum();
        return threadPage(th, num("page"));
    }
    return fail("Unknown request.");
}

const json* GbServer::forumPost(const std::string& threadId, const std::string& postId, std::string* title) const {
    if (!m_forum.contains(threadId)) return nullptr;
    const json& th = m_forum[threadId];
    for (const json& p : th["posts"])
        if (sv(p, "id") == postId) {
            if (title) *title = sv(th, "title");
            return &p;
        }
    return nullptr;
}

void GbServer::saveForum() {
    m_forumSaved = Online::unixNow();
    writeFile(m_opts.data / "forum.json", m_forum.dump(1));
}

void GbServer::loadForum() {
    std::string text;
    if (!readFile(m_opts.data / "forum.json", text)) return;
    json all = json::parse(text, nullptr, false);
    if (!all.is_object()) return;
    for (auto& [id, th] : all.items())
        if (th.is_object() && th.contains("posts") && th["posts"].is_array() && !th["posts"].empty()) m_forum[id] = th;
}
