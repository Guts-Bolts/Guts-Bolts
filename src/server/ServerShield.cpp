// The risk check's Step 3 (worker/server.js COSTLY has the same): the slow, expensive
// requests get their own limits, per IP and per account, so nobody can flood the server
// with sign ups, logins, emails, uploads or purchases. (Steps 1 and 2, scoring every
// request and challenging the doubtful ones, happen in front of the server, in the
// website's worker: worker/shield.js.)
#include "Server.h"
#include "ServerUtil.h"
#include "../net/Socket.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <chrono>
#include <cmath>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
// op -> per account, per IP, in how many seconds. Only requests that worked count, except
// the guesses (isGuess). Accounts less than a day old get half; staff have no limit.
struct Rule { const char* op; int each, perIp, per; };
constexpr Rule kCostly[] = {
    {"account.signup", 3, 20, 3600},
    {"account.salt", 30, 120, 600},
    {"account.login", 20, 60, 600},
    {"account.forgot", 3, 10, 3600},
    {"account.email", 5, 15, 3600},
    {"account.reset", 10, 30, 3600},
    {"account.emailVerify", 10, 30, 600},
    {"account.password", 10, 30, 3600},
    {"support.send", 5, 15, 3600},
    {"upload", 30, 90, 3600},
    {"update", 60, 180, 3600},
    {"thumb.set", 30, 90, 600},
    {"icon.set", 30, 90, 600},
    {"shots.add", 30, 90, 600},
    {"buy", 60, 180, 600},
    {"gift.buy", 30, 90, 600},
    {"resale.buy", 30, 90, 600},
    {"product.buy", 120, 360, 600},
    {"auction.bid", 60, 180, 600},
    {"servers.buyPrivate", 10, 30, 600},
    {"trade.send", 30, 90, 600},
    {"report.send", 20, 60, 600},
    {"message.send", 60, 180, 600},
    {"comments.post", 30, 90, 600},
    {"forum.post", 10, 30, 600},
    {"forum.reply", 30, 90, 600},
    {"groups.create", 5, 15, 3600},
};
// One connection can send this many requests every 10 seconds (games use it to send
// messages and save data too, so it's roomy).
constexpr int kWsRequestsPer10s = 600;

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

bool GbServer::isGuess(const std::string& op) {
    return op == "account.salt" || op == "account.login" || op == "account.reset" || op == "account.emailVerify";
}

bool GbServer::tooFast(Client& c) {
    const long long t = nowMs();
    if (!c.window || t - c.window >= 10000) { c.window = t; c.sent = 0; }
    return ++c.sent > kWsRequestsPer10s;
}

// Who sent it: the connection's address, or (behind the website's worker) what the worker says.
GbServer::From GbServer::fromOf(const Client& c, const json& req) const {
    From f;
    f.ip = c.conn->address();
    if (m_opts.behindWorker && req.contains("from") && req["from"].is_object()) {
        const json& w = req["from"];
        f.ip = w.value("ip", std::string());
        f.web = w.value("web", false);
        f.human = w.value("human", false);
        f.sitekey = w.value("sitekey", std::string());
    }
    return f;
}

// null = go ahead (counters: what to count once it's done), else the reply that says no.
json GbServer::costlyCheck(const std::string& op, const User& me, const From& from, std::vector<Costly*>& counters) {
    // Signing up on the website needs the "are you a person?" check (once Turnstile is on).
    if (op == "account.signup" && from.web && !from.sitekey.empty() && !from.human) {
        json r = fail("Quick check that you're a person, then try again.");
        r["challenge"] = true;
        r["sitekey"] = from.sitekey;
        return r;
    }
    const Rule* rule = nullptr;
    for (const Rule& r : kCostly) if (op == r.op) { rule = &r; break; }
    if (!rule || isStaff(me)) return nullptr;
    const long long t = Online::unixNow();
    const bool young = me.userId > 0 && t - me.created < 86400;
    std::vector<std::pair<std::string, int>> who = {{"acct:" + me.id, rule->each}};
    if (!from.ip.empty()) who.push_back({"ip:" + from.ip, rule->perIp});
    if (m_costly.size() > 50000)
        for (auto it = m_costly.begin(); it != m_costly.end();)
            it = t - it->second.start >= it->second.per ? m_costly.erase(it) : std::next(it);
    for (const auto& [key, allowed] : who) {
        Costly& c = m_costly[op + "|" + key];
        if (!c.start || t - c.start >= rule->per) c = Costly{t, rule->per, 0};
        if (c.n >= std::max(1, young ? allowed / 2 : allowed)) {
            const long long wait = std::max(1LL, (long long)std::ceil((c.start + rule->per - t) / 60.0));
            json r = fail("You're doing that too often. Try again in " + std::to_string(wait) + (wait == 1 ? " minute." : " minutes."));
            r["slowDown"] = true;
            counters.clear();
            return r;
        }
        counters.push_back(&c);
    }
    return nullptr;
}
