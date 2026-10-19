// Keeping people safe: blocking someone, and reporting people, games, items,
// messages and groups to staff (worker/server.js safetyOp has the same).
//
// Blocking works both ways: once either of you blocks the other, you can't
// message, friend, follow or join each other, and you don't see each other online.
// Reports go in reports.json, and staff work through them on the website's Staff page.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kMaxBlocked    = 200;
constexpr int    kReportsPerDay = 20;
constexpr size_t kMaxReports    = 3000;
bool isReportKind(const std::string& k) {
    return k == "user" || k == "game" || k == "item" || k == "message" || k == "group" || k == "comment" || k == "forum";
}
bool has(const std::vector<std::string>& list, const std::string& id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}
} // namespace

bool GbServer::blocks(const User& a, const User& b) const {
    return a.id != b.id && (has(a.blocked, b.id) || has(b.blocked, a.id));
}

json GbServer::safetyOp(const std::string& name, User& me, const json& args) {
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    if (me.userId == 0) return fail("Sign up first.");

    if (name == "block.list") {
        json people = json::array();
        for (const std::string& id : me.blocked)
            if (auto it = m_users.find(id); it != m_users.end()) people.push_back(publicUser(it->second));
        json r = okay(); r["people"] = people; return r;
    }
    if (name == "block.add" || name == "block.remove") {
        User* them = findPerson(args.contains("user") && args["user"].is_number_integer()
                                    ? std::to_string(args["user"].get<long long>()) : str("user"));
        if (!them || them->userId == 0) return fail("There's no account with that ID on this server.");
        if (them->id == me.id) return fail("You can't block yourself.");
        if (name == "block.remove") {
            me.blocked.erase(std::remove(me.blocked.begin(), me.blocked.end(), them->id), me.blocked.end());
            saveUsers();
            json r = okay(); r["blocked"] = false; return r;
        }
        if (isStaff(*them)) return fail("You can't block Guts&Bolts staff. Report them instead if something's wrong.");
        if (!has(me.blocked, them->id)) {
            if (me.blocked.size() >= kMaxBlocked)
                return fail("You've blocked " + std::to_string(kMaxBlocked) + " people already. Unblock some first.");
            me.blocked.push_back(them->id);
        }
        // Blocking ends everything between you: friends, requests, follows, open trades, and their messages to you.
        for (auto [a, b] : {std::pair<User*, User*>{&me, them}, std::pair<User*, User*>{them, &me}}) {
            a->friends.erase(b->id); a->friendIn.erase(b->id); a->friendOut.erase(b->id);
            a->following.erase(b->id); a->followers.erase(b->id);
        }
        if (me.inbox.is_array()) {
            json kept = json::array();
            for (const json& m : me.inbox) if (m.value("from", std::string()) != them->id) kept.push_back(m);
            me.inbox = kept;
        }
        bool tradesChanged = false;
        for (json& x : m_trades) {
            const std::string from = x.value("from", std::string()), to = x.value("to", std::string());
            if (x.value("status", std::string()) == "open" && ((from == me.id && to == them->id) || (from == them->id && to == me.id))) {
                finishTrade(x, "cancelled", Online::unixNow());   // (it goes in both people's Past trades)
                tradesChanged = true;
            }
        }
        if (tradesChanged) saveTrades();
        saveUsers();
        json r = okay(); r["blocked"] = true; r["me"] = meJson(me); return r;
    }
    if (name == "report.send") {
        // kind: user, game, item, message (one in your inbox) or group; id: which one.
        const std::string kind = str("kind"), id = str("id"), reason = str("reason");
        if (!isReportKind(kind)) return fail("You can't report that.");
        if (!Online::banReasonTitle(reason)) return fail("Pick what's wrong.");
        const std::string note = Online::cleanText(str("note"), 500, true);
        std::string target, about;
        json copy;
        if (kind == "user") {
            User* u = findPerson(id);
            if (!u || u->userId == 0) return fail("There's no account with that ID on this server.");
            if (u->id == me.id) return fail("You can't report yourself.");
            target = about = u->id;
        } else if (kind == "game" || kind == "item") {
            auto a = findAsset(id);
            if (a == m_assets.end() || (kind == "game") != (a->second.kind == "game")) return fail("That isn't there any more.");
            target = a->second.id;
            about = a->second.creator;
        } else if (kind == "message") {
            const json* found = nullptr;
            if (me.inbox.is_array())
                for (const json& m : me.inbox) if (m.value("id", std::string()) == id) { found = &m; break; }
            if (!found) return fail("That message isn't in your inbox any more.");
            // Staff see a copy: the sender can't delete it from here.
            target = id;
            about = found->value("from", std::string());
            copy = {{"subject", found->value("subject", std::string())}, {"body", found->value("body", std::string())},
                    {"at", found->value("at", 0LL)}};
        } else if (kind == "comment") {
            // id: "game:comment". Staff see a copy, in case it's deleted.
            const size_t colon = id.find(':');
            auto a = findAsset(id.substr(0, colon));
            const json* found = nullptr;
            if (colon != std::string::npos && a != m_assets.end())
                for (const json& c : a->second.comments) if (c.value("id", std::string()) == id.substr(colon + 1)) { found = &c; break; }
            if (!found) return fail("That comment isn't there any more.");
            if (found->value("by", std::string()) == me.id) return fail("You can't report yourself.");
            target = id;
            about = found->value("by", std::string());
            copy = {{"subject", "Comment on " + a->second.name}, {"body", found->value("text", std::string())}, {"at", found->value("at", 0LL)}};
        } else if (kind == "forum") {
            // id: "thread:post". Staff see a copy, in case it's deleted.
            const size_t colon = id.find(':');
            std::string title;
            const json* found = colon == std::string::npos ? nullptr : forumPost(id.substr(0, colon), id.substr(colon + 1), &title);
            if (!found) return fail("That post isn't there any more.");
            if (found->value("by", std::string()) == me.id) return fail("You can't report yourself.");
            target = id;
            about = found->value("by", std::string());
            copy = {{"subject", "Forum: " + title}, {"body", found->value("text", std::string())}, {"at", found->value("at", 0LL)}};
        } else {
            auto g = m_groups.find(id);
            if (g == m_groups.end()) return fail("That group isn't there any more.");
            target = g->first;
            about = g->second.owner;
        }
        const long long now = Online::unixNow();
        const std::string today = Online::utcDay(now);
        if (me.reportDay != today) { me.reportDay = today; me.reportsToday = 0; }
        for (json& x : m_reports)
            if (x.value("status", "") == "open" && x.value("from", "") == me.id && x.value("kind", "") == kind && x.value("target", "") == target) {
                // Reporting the same thing twice just updates it.
                x["reason"] = reason;
                if (!note.empty()) x["note"] = note;
                x["at"] = now;
                saveReports();
                return okay();
            }
        if (me.reportsToday >= kReportsPerDay)
            return fail("That's " + std::to_string(kReportsPerDay) + " reports today. Staff will look at them; try again tomorrow.");
        me.reportsToday++;
        m_reports.push_back({{"id", "rp-" + Account::randomHex(6)}, {"from", me.id}, {"kind", kind}, {"target", target},
                             {"about", about}, {"reason", reason}, {"note", note}, {"copy", copy}, {"at", now}, {"status", "open"}});
        log(me.name + " reported a " + kind + " (" + reason + ")");
        saveUsers();
        saveReports();
        return okay();
    }
    return fail("Unknown request.");
}

json GbServer::reportsJson(const std::string& status) const {
    auto who = [&](const std::string& id) {
        auto it = m_users.find(id);
        return it != m_users.end() ? publicUser(it->second) : json{{"id", id}, {"name", "?"}, {"userId", 0}};
    };
    std::vector<const json*> list;
    for (const json& x : m_reports) if (x.value("status", "") == status) list.push_back(&x);
    // Open: oldest first (first come, first served). Closed: the newest.
    std::vector<const json*> shown;
    if (status == "open") shown.assign(list.begin(), list.begin() + std::min<size_t>(list.size(), 100));
    else for (size_t i = list.size(); i > 0 && shown.size() < 100; --i) shown.push_back(list[i - 1]);
    json out = json::array();
    for (const json* x : shown) {
        const std::string kind = x->value("kind", ""), target = x->value("target", ""), about = x->value("about", "");
        int same = 0;
        for (const json* y : list) if (y->value("kind", "") == kind && y->value("target", "") == target) ++same;
        json r = {{"id", x->value("id", "")}, {"kind", kind}, {"target", target}, {"reason", x->value("reason", "")},
                  {"note", x->value("note", "")}, {"at", x->value("at", 0LL)}, {"status", status},
                  {"from", who(x->value("from", ""))}, {"about", about.empty() ? json() : who(about)},
                  {"copy", x->contains("copy") ? (*x)["copy"] : json()}, {"reports", same}};
        if (kind == "game" || kind == "item") {
            auto a = findAsset(target);
            r["name"] = a != m_assets.end() ? a->second.name : "(deleted)";
            r["assetKind"] = a != m_assets.end() ? a->second.kind : "";
        }
        if (kind == "comment") {
            auto a = findAsset(target.substr(0, target.find(':')));
            r["name"] = a != m_assets.end() ? a->second.name : "(deleted)";
            r["game"] = a != m_assets.end() ? a->second.id : "";
        }
        if (kind == "forum") {
            const std::string tid = target.substr(0, target.find(':'));
            const bool there = m_forum.contains(tid);
            r["name"] = there ? m_forum[tid].value("title", std::string()) : "(deleted)";
            r["thread"] = there ? tid : "";
        }
        if (kind == "group") {
            auto g = m_groups.find(target);
            r["name"] = g != m_groups.end() ? g->second.name : "(deleted)";
        }
        if (status == "closed") {
            r["outcome"] = x->value("outcome", "");
            r["closedBy"] = who(x->value("closedBy", ""));
            r["closedAt"] = x->value("closedAt", 0LL);
        }
        out.push_back(r);
    }
    return out;
}

json GbServer::closeReport(const User& staff, const std::string& id, const std::string& outcomeArg) {
    // Staff looked at it: "done" (they did something, like a ban or a warning) or "dismissed" (nothing wrong).
    const std::string outcome = outcomeArg == "dismissed" ? "dismissed" : "done";
    const json* found = nullptr;
    for (const json& x : m_reports) if (x.value("id", "") == id) { found = &x; break; }
    if (!found) return fail("That report isn't there any more.");
    const std::string kind = found->value("kind", ""), target = found->value("target", "");
    // Close the other open reports about the same thing too.
    for (json& x : m_reports)
        if (x.value("status", "") == "open" && x.value("kind", "") == kind && x.value("target", "") == target) {
            x["status"] = "closed";
            x["outcome"] = outcome;
            x["closedBy"] = staff.id;
            x["closedAt"] = Online::unixNow();
        }
    const char* why = Online::banReasonTitle(found->value("reason", std::string()));
    staffDid(staff, "report", std::string(outcome == "dismissed" ? "Dismissed" : "Closed") + " the reports about a " + kind + " (" +
             (why ? why : found->value("reason", std::string())) + ")", found->value("about", std::string()));
    saveReports();
    json r = okay(); r["reports"] = reportsJson("open"); return r;
}

void GbServer::saveReports() {
    // Keep every open report, and only the newest closed ones.
    if (m_reports.size() > kMaxReports) {
        size_t extra = m_reports.size() - kMaxReports;
        json kept = json::array();
        for (const json& x : m_reports) {
            if (extra > 0 && x.value("status", "") != "open") { --extra; continue; }
            kept.push_back(x);
        }
        m_reports = kept;
    }
    writeFile(m_opts.data / "reports.json", m_reports.dump(1));
}

// The staff action log (worker/server.js has the same): bans, warnings, badges, Bolts, upload
// checks, closed reports and things staff deleted. The newest kMaxStaffLog are kept.
void GbServer::staffDid(const User& staff, const std::string& action, const std::string& text, const std::string& about) {
    constexpr size_t kMaxStaffLog = 3000;
    m_staffLog.push_back({{"at", Online::unixNow()}, {"by", staff.id}, {"action", action}, {"text", text}, {"about", about}});
    if (m_staffLog.size() > kMaxStaffLog) m_staffLog.erase(m_staffLog.begin(), m_staffLog.begin() + (m_staffLog.size() - kMaxStaffLog));
    log(staff.name + ": " + text);
    writeFile(m_opts.data / "stafflog.json", m_staffLog.dump(1));
}

json GbServer::staffLogJson(const std::string& who) const {
    auto name = [&](const std::string& id) {
        auto it = m_users.find(id);
        return it != m_users.end() ? json{{"id", id}, {"userId", it->second.userId}, {"name", it->second.name}} : json{{"id", id}, {"userId", 0}, {"name", "?"}};
    };
    json out = json::array();
    for (size_t i = m_staffLog.size(); i > 0 && out.size() < 200; --i) {
        const json& x = m_staffLog[i - 1];
        const std::string by = x.value("by", std::string()), about = x.value("about", std::string());
        if (!who.empty() && by != who && about != who) continue;
        out.push_back({{"at", x.value("at", 0LL)}, {"by", name(by)}, {"action", x.value("action", std::string())},
                       {"text", x.value("text", std::string())}, {"about", about.empty() ? json() : name(about)}});
    }
    return out;
}

void GbServer::loadStaffLog() {
    std::string text;
    if (!readFile(m_opts.data / "stafflog.json", text)) return;
    json all = json::parse(text, nullptr, false);
    if (all.is_array()) m_staffLog = all;
}

void GbServer::loadReports() {
    loadTickets();   // (support tickets, ServerSupport.cpp, load with the reports)
    std::string text;
    if (!readFile(m_opts.data / "reports.json", text)) return;
    json all = json::parse(text, nullptr, false);
    if (all.is_array()) m_reports = all;
}
