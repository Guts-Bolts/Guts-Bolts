// Support tickets (worker/server.js supportOp has the same): anyone, even someone who
// can't log in, can ask staff for help, like "I forgot my password". A ticket belongs to
// the device that sent it: only that device sees staff's answers.
//
// The website's server can also send a password reset code from a ticket. This server
// has no password resets yet, so staff answer with what to do instead.
// Tickets go in tickets.json, and staff work through them on the website's Staff page.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kMaxTickets    = 2000;
constexpr int    kOpenTickets   = 3;
constexpr int    kTicketsPerDay = 5;
constexpr size_t kTicketLength  = 1000;
bool isTopic(const std::string& t) { return t == "password" || t == "account" || t == "other"; }
// Good enough to email someone back (like worker/server.js isEmail).
bool looksLikeEmail(const std::string& e) {
    const size_t at = e.find('@');
    return e.size() <= 254 && at != std::string::npos && at > 0 && e.find('.', at) != std::string::npos &&
           e.find_first_of(" <>\"") == std::string::npos && e.back() != '.';
}
std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}
} // namespace

json GbServer::supportOp(const std::string& name, User& me, const json& args) {
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    const long long now = Online::unixNow();
    if (name == "support.send") {
        const std::string topic = isTopic(str("topic")) ? str("topic") : "other";
        const std::string message = trim(Online::cleanText(str("message"), kTicketLength, true));
        const std::string contact = trim(lower(Online::cleanText(str("contact"), 254)));
        std::string username = trim(Online::cleanText(str("username"), 32));
        if (!username.empty() && username[0] == '@') username.erase(0, 1);
        if (message.size() < 10) return fail("Tell staff a bit more (at least 10 letters), so they know how to help.");
        if (!contact.empty() && !looksLikeEmail(contact)) return fail("That doesn't look like an email address. You can leave it empty.");
        const User* about = me.userId > 0 ? &me : nullptr;
        if (!username.empty() || (topic == "password" && !about)) {
            about = findUsername(username);
            if (!about || about->userId == 0) return fail("There's no account called \"" + username + "\". Check the spelling.");
            if (isOfficial(*about)) return fail("The Guts account can't get help this way.");
        }
        int open = 0, today = 0;
        for (const json& x : m_tickets) {
            if (x.value("from", "") != me.id) continue;
            if (x.value("status", "") == "open") ++open;
            if (now - x.value("at", 0LL) < 86400) ++today;
        }
        if (open >= kOpenTickets)
            return fail("You already have " + std::to_string(kOpenTickets) + " open tickets. Wait for staff to answer those first.");
        if (today >= kTicketsPerDay) return fail("That's enough tickets for today. Try again tomorrow.");
        json tk = {{"id", "tk-" + Account::randomHex(6)}, {"from", me.id}, {"topic", topic}, {"about", about ? about->id : ""},
                   {"contact", contact}, {"message", message}, {"at", now}, {"status", "open"}, {"replies", json::array()}};
        m_tickets.push_back(tk);
        log("New support ticket (" + topic + ")");
        notify(findUserId(1), "ticket", "New support ticket" + (about ? " about " + about->name : "") + ": \"" + message.substr(0, 80) + "\"", tk["id"]);
        saveTickets();
        json r = okay(); r["ticket"] = ticketJson(tk, false); return r;
    }
    if (name == "support.mine") {
        json list = json::array();
        for (size_t i = m_tickets.size(); i > 0 && list.size() < 20; --i)
            if (m_tickets[i - 1].value("from", "") == me.id) list.push_back(ticketJson(m_tickets[i - 1], false));
        json r = okay(); r["tickets"] = list; return r;
    }
    return fail("Unknown request.");
}

json GbServer::ticketJson(const json& x, bool forStaff) const {
    auto who = [&](const std::string& id) -> json {
        auto it = m_users.find(id);
        return it != m_users.end() ? publicUser(it->second) : json();
    };
    json replies = json::array();
    for (const json& y : x.value("replies", json::array()))
        replies.push_back({{"text", y.value("text", "")}, {"at", y.value("at", 0LL)}, {"by", forStaff ? who(y.value("by", "")) : json()}});
    json r = {{"id", x.value("id", "")}, {"topic", x.value("topic", "")}, {"message", x.value("message", "")}, {"at", x.value("at", 0LL)},
              {"status", x.value("status", "")}, {"about", who(x.value("about", ""))}, {"replies", replies}};
    if (r["status"] == "closed") r["closedAt"] = x.value("closedAt", 0LL);
    if (forStaff) {
        auto from = m_users.find(x.value("from", ""));
        r["from"] = from != m_users.end() && from->second.userId > 0 ? publicUser(from->second) : json();
        r["contact"] = x.value("contact", "");
        r["resetSent"] = false;
        r["resetUsed"] = false;
        if (r["status"] == "closed") r["closedBy"] = who(x.value("closedBy", ""));
    }
    return r;
}

json GbServer::ticketsJson(const std::string& status) const {
    std::vector<const json*> list;
    for (const json& x : m_tickets) if (x.value("status", "") == status) list.push_back(&x);
    // Open: oldest first (first come, first served). Closed: the newest.
    json out = json::array();
    if (status == "open") for (size_t i = 0; i < list.size() && out.size() < 100; ++i) out.push_back(ticketJson(*list[i], true));
    else for (size_t i = list.size(); i > 0 && out.size() < 100; --i) out.push_back(ticketJson(*list[i - 1], true));
    return out;
}

json GbServer::ticketAdminOp(const std::string& name, const User& staff, const json& args) {
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    json* tk = nullptr;
    for (json& x : m_tickets) if (x.value("id", "") == str("id")) { tk = &x; break; }
    if (!tk) return fail("That ticket isn't there any more.");
    User* about = findUser(tk->value("about", ""));
    const std::string aboutName = about ? " about " + about->name : "";
    if (name == "admin.replyTicket") {
        const std::string text = trim(Online::cleanText(str("text"), kTicketLength, true));
        if (text.empty()) return fail("Type an answer first.");
        if (!(*tk)["replies"].is_array()) (*tk)["replies"] = json::array();
        (*tk)["replies"].push_back({{"by", staff.id}, {"text", text}, {"at", Online::unixNow()}});
        if (tk->value("status", "") == "closed") { (*tk)["status"] = "open"; tk->erase("closedBy"); tk->erase("closedAt"); }
        notify(findUser(tk->value("from", "")), "support", "Staff answered your support ticket: \"" + text + "\"", tk->value("id", ""));
        staffDid(staff, "ticket", "Answered a support ticket" + aboutName, about ? about->id : "");
    } else if (name == "admin.ticketReset") {
        return fail("This server can't reset passwords yet. Answer the ticket to tell them what to do instead.");
    } else if (name == "admin.closeTicket") {
        (*tk)["status"] = "closed"; (*tk)["closedBy"] = staff.id; (*tk)["closedAt"] = Online::unixNow();
        staffDid(staff, "ticket", "Closed a support ticket" + aboutName, about ? about->id : "");
    } else {
        return fail("Unknown request.");
    }
    saveTickets();
    json r = okay(); r["tickets"] = ticketsJson("open"); return r;
}

void GbServer::saveTickets() {
    // Keep every open ticket, and only the newest closed ones.
    if (m_tickets.size() > kMaxTickets) {
        size_t extra = m_tickets.size() - kMaxTickets;
        json kept = json::array();
        for (const json& x : m_tickets) {
            if (extra > 0 && x.value("status", "") != "open") { --extra; continue; }
            kept.push_back(x);
        }
        m_tickets = kept;
    }
    writeFile(m_opts.data / "tickets.json", m_tickets.dump(1));
}

void GbServer::loadTickets() {
    std::string text;
    if (!readFile(m_opts.data / "tickets.json", text)) return;
    json all = json::parse(text, nullptr, false);
    if (all.is_array()) m_tickets = all;
}
