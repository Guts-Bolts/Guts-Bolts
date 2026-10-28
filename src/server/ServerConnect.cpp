// Connected apps: "Log in with Guts&Bolts" for partner apps like Lyreto (OAuth 2.0, the
// authorization code kind). worker/server.js has the same apps and rules (CONNECT_APPS there
// explains the steps); worker/index.js turns the /api/oauth/... web addresses into these requests.
//
//   connect.app / connect.approve   the website's Connect page (signed, like every request)
//   connect.list / connect.remove   Settings > Connected apps
//   connect.picture                 a new avatar picture for connected apps (the website draws it)
//   connect.token / connect.userinfo / connect.avatarPicture / connect.revoke / connect.tokenInfo
//                                   from the app's own server: not signed (the client secret or
//                                   the access token is the proof)
// Apps can ask for permissions (scope=...: identify, studio.read, studio.write); the token keeps them.
// (The Studio permissions are used by https://gutsandbolts.net/mcp, which only the Cloudflare server
// runs: worker/mcp.js. Here they're kept and reported, so the same tokens mean the same things.)
//   admin.connectApps / admin.connectSecret   the Staff page (official account only)
//
// Everything is kept in connect.json; avatar pictures are files/avatar-<account>.png.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <vector>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
struct ConnectApp { const char* id; const char* name; const char* site; const char* redirect; std::vector<std::string> scopes; };
const ConnectApp kConnectApps[] = {
    {"lyreto", "Lyreto", "https://lyreto.com", "https://lyreto.com/api/connections/gutsandbolts/callback", {"identify", "studio.read", "studio.write"}},
};
// What an app can ask for, in order, and the words the Connect page shows (CONNECT_SCOPES in server.js).
const std::pair<const char*, const char*> kScopes[] = {
    {"identify", "See your username, user number and a picture of your avatar"},
    {"studio.read", "Look at the game open in your Studio: its objects, scripts, output, errors and screenshots"},
    {"studio.write", "Change the game open in your Studio: add, change and delete things, write scripts, run playtests, undo"},
};
constexpr long long kCodeLife = 300;              // the one-time code: 5 minutes to swap it
constexpr long long kTokenLife = 365LL * 86400;   // an access token: a year, or until they disconnect
constexpr size_t    kMaxPicture = 256 << 10;
constexpr unsigned  kPictureSide = 512;

const ConnectApp* findApp(const std::string& id) {
    for (const ConnectApp& a : kConnectApps) if (id == a.id) return &a;
    return nullptr;
}
json oauthFail(const std::string& word, const std::string& why) { json r = fail(why); r["oauth"] = word; return r; }
// The scopes asked for ("studio.read studio.write"), checked: the list (always with identify) or "" + `bad`.
json askedScopes(const ConnectApp& app, const std::string& text, std::string& bad) {
    std::vector<std::string> asked;
    std::string word;
    for (char ch : text + " ") {
        if (ch == ' ' || ch == ',' || ch == '+' || ch == '\t' || ch == '\n') { if (!word.empty()) asked.push_back(word); word.clear(); }
        else word += ch;
    }
    if (asked.size() > 10) { bad = "That app asked for too many permissions."; return json(); }
    for (const std::string& a : asked) {
        bool known = false;
        for (const auto& s : kScopes) if (a == s.first) known = true;
        if (!known) { bad = "There's no permission called \"" + a.substr(0, 40) + "\"."; return json(); }
        if (std::find(app.scopes.begin(), app.scopes.end(), a) == app.scopes.end()) { bad = std::string(app.name) + " isn't allowed to ask for \"" + a + "\"."; return json(); }
    }
    json out = json::array();
    for (const auto& s : kScopes)
        if (std::string(s.first) == "identify" || std::find(asked.begin(), asked.end(), s.first) != asked.end()) out.push_back(s.first);
    return out;
}
// A token's (or code's) scopes: ones from before scopes existed are "identify" only.
json scopesOf(const json& x) {
    return x.contains("scopes") && x["scopes"].is_array() && !x["scopes"].empty() ? x["scopes"] : json::array({"identify"});
}
std::string joined(const json& scopes) {
    std::string out;
    for (const auto& s : scopes) out += (out.empty() ? "" : " ") + s.get<std::string>();
    return out;
}
bool pngOk(const std::string& d) {
    if (d.size() <= 24 || d.size() > kMaxPicture || d.compare(0, 4, "\x89PNG") != 0) return false;
    auto u = [&](size_t i) { return (unsigned)(unsigned char)d[i]; };
    const unsigned w = u(16) << 24 | u(17) << 16 | u(18) << 8 | u(19), h = u(20) << 24 | u(21) << 16 | u(22) << 8 | u(23);
    return w > 0 && h > 0 && w <= kPictureSide && h <= kPictureSide;
}
} // namespace

bool GbServer::isConnectOpen(const std::string& op) {
    return op == "connect.token" || op == "connect.userinfo" || op == "connect.avatarPicture" || op == "connect.revoke" ||
           op == "connect.tokenInfo";
}

void GbServer::loadConnect() {
    std::string text;
    if (readFile(m_opts.data / "connect.json", text)) {
        json all = json::parse(text, nullptr, false);
        if (all.is_object()) m_connect = all;
    }
    for (const char* k : {"secrets", "pictures"}) if (!m_connect.contains(k) || !m_connect[k].is_object()) m_connect[k] = json::object();
    for (const char* k : {"codes", "tokens"}) if (!m_connect.contains(k) || !m_connect[k].is_array()) m_connect[k] = json::array();
}
void GbServer::saveConnect() { writeFile(m_opts.data / "connect.json", m_connect.dump(1)); }

std::filesystem::path GbServer::avatarPicturePath(const std::string& account) const {
    return m_opts.data / "files" / ("avatar-" + account + ".png");
}

json GbServer::liveTokens(const std::string& account) const {
    const long long now = Online::unixNow();
    json out = json::array();
    for (const json& x : m_connect["tokens"])
        if (x.value("account", "") == account && x.value("expires", 0LL) > now) out.push_back(x);
    return out;
}

void GbServer::disconnectApps(const std::string& account, const std::string& app) {
    json kept = json::array();
    for (const json& x : m_connect["tokens"])
        if (x.value("account", "") != account || (!app.empty() && x.value("app", "") != app)) kept.push_back(x);
    m_connect["tokens"] = kept;
    if (liveTokens(account).empty() && m_connect["pictures"].contains(account)) {
        m_connect["pictures"].erase(account);
        std::error_code ec;
        std::filesystem::remove(avatarPicturePath(account), ec);
    }
    saveConnect();
}

std::string GbServer::saveAvatarPicture(const User& u, const std::string& b64) {
    std::string data;
    if (!Online::base64Decode(b64, data) || !pngOk(data)) return "That avatar picture is too big, or isn't a PNG.";
    if (!writeFile(avatarPicturePath(u.id), data)) return "Couldn't save the picture.";
    m_connect["pictures"][u.id] = Online::unixNow();
    saveConnect();
    return "";
}

json GbServer::connectsJson(const User& u) const {
    json out = json::array();
    for (const json& x : liveTokens(u.id))
        if (const ConnectApp* a = findApp(x.value("app", "")))
            out.push_back({{"id", a->id}, {"name", a->name}, {"site", a->site}, {"at", x.value("at", 0LL)}, {"scopes", scopesOf(x)}});
    return out;
}

json GbServer::connectOp(const std::string& name, User& me, const json& args) {
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    const long long now = Online::unixNow();
    if (name == "connect.app" || name == "connect.approve") {
        const ConnectApp* app = findApp(str("client_id"));
        if (!app) return fail("That app isn't one Guts&Bolts knows.");
        if (str("redirect_uri") != app->redirect)
            return fail(std::string(app->name) + " asked to send you back to an address Guts&Bolts doesn't know, so it won't.");
        if (!m_connect["secrets"].contains(app->id))
            return fail(std::string(app->name) + " isn't set up on Guts&Bolts yet. Staff need to make its secret first.");
        std::string bad;
        const json scopes = askedScopes(*app, str("scope"), bad);
        if (!bad.empty()) return fail(bad);
        if (name == "connect.app") {
            bool connected = false;
            for (const json& x : liveTokens(me.id)) if (x.value("app", "") == app->id) connected = true;
            json r = okay();
            r["app"] = {{"id", app->id}, {"name", app->name}, {"site", app->site}};
            r["connected"] = connected;
            r["scopes"] = json::array();
            for (const auto& id : scopes)
                for (const auto& s : kScopes) if (id == s.first) r["scopes"].push_back({{"id", s.first}, {"text", s.second}});
            return r;
        }
        if (!str("picture").empty()) {
            const std::string bad = saveAvatarPicture(me, str("picture"));
            if (!bad.empty()) return fail(bad);
        }
        const std::string code = Account::randomHex(24);
        json kept = json::array();
        for (const json& x : m_connect["codes"])
            if (now - x.value("at", 0LL) < kCodeLife && x.value("account", "") != me.id) kept.push_back(x);
        kept.push_back({{"hash", Account::hashHex(code)}, {"account", me.id}, {"app", app->id}, {"redirect", str("redirect_uri")}, {"at", now}, {"scopes", scopes}});
        m_connect["codes"] = kept;
        saveConnect();
        json r = okay(); r["code"] = code; return r;
    }
    if (name == "connect.list") { json r = okay(); r["apps"] = connectsJson(me); return r; }
    if (name == "connect.remove") {
        disconnectApps(me.id, str("id"));
        json r = okay(); r["apps"] = connectsJson(me); return r;
    }
    if (name == "connect.picture") {   // a new look: the website sends a new picture, if any app is connected
        json r = okay();
        if (liveTokens(me.id).empty()) { r["saved"] = false; return r; }
        const std::string bad = saveAvatarPicture(me, str("picture"));
        if (!bad.empty()) return fail(bad);
        r["saved"] = true;
        return r;
    }
    return fail("Unknown request.");
}

json GbServer::connectOpen(const std::string& name, const json& args) {
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    const long long now = Online::unixNow();
    if (name == "connect.token") {
        if (!str("grant_type").empty() && str("grant_type") != "authorization_code")
            return oauthFail("unsupported_grant_type", "Only grant_type=authorization_code is supported.");
        const ConnectApp* app = findApp(str("client_id"));
        const json& secrets = m_connect["secrets"];
        if (!app || !secrets.contains(app->id) || Account::hashHex(str("client_secret")) != secrets[app->id].value("hash", ""))
            return oauthFail("invalid_client", "Wrong client_id or client_secret.");
        const std::string hash = Account::hashHex(str("code"));
        json code, kept = json::array();
        for (const json& x : m_connect["codes"]) {   // every code works once
            if (now - x.value("at", 0LL) >= kCodeLife) continue;
            if (code.is_null() && x.value("hash", "") == hash && x.value("app", "") == app->id) { code = x; continue; }
            kept.push_back(x);
        }
        m_connect["codes"] = kept;
        saveConnect();
        if (code.is_null()) return oauthFail("invalid_grant", "That code is wrong, used or too old (they last 5 minutes).");
        if (!str("redirect_uri").empty() && str("redirect_uri") != code.value("redirect", ""))
            return oauthFail("invalid_grant", "redirect_uri doesn't match.");
        const User* u = findUser(code.value("account", ""));
        if (!u || u->deleted || u->userId == 0) return oauthFail("invalid_grant", "That account is gone.");
        const std::string token = "gbt_" + Account::randomHex(32);
        json tokens = json::array();
        for (const json& x : m_connect["tokens"])
            if (x.value("expires", 0LL) > now && !(x.value("account", "") == u->id && x.value("app", "") == app->id)) tokens.push_back(x);
        const json scopes = scopesOf(code);
        tokens.push_back({{"hash", Account::hashHex(token)}, {"account", u->id}, {"app", app->id}, {"at", now}, {"expires", now + kTokenLife},
                          {"scopes", scopes}});
        m_connect["tokens"] = tokens;
        saveConnect();
        json r = okay();
        r["access_token"] = token; r["token_type"] = "Bearer"; r["expires_in"] = kTokenLife; r["scope"] = joined(scopes);
        return r;
    }
    // An app hands back a token it doesn't want any more (RFC 7009: the same answer whether it was live or not).
    if (name == "connect.revoke") {
        const ConnectApp* app = findApp(str("client_id"));
        const json& secrets = m_connect["secrets"];
        if (!app || !secrets.contains(app->id) || Account::hashHex(str("client_secret")) != secrets[app->id].value("hash", ""))
            return oauthFail("invalid_client", "Wrong client_id or client_secret.");
        const std::string hash = Account::hashHex(str("token"));
        json kept = json::array();
        for (const json& x : m_connect["tokens"])
            if (!(x.value("hash", "") == hash && x.value("app", "") == app->id)) kept.push_back(x);
        if (kept.size() != m_connect["tokens"].size()) { m_connect["tokens"] = kept; saveConnect(); }
        return okay();
    }
    // Whose a token is and what it may do: never the account's key.
    if (name == "connect.tokenInfo") {
        const std::string hash = Account::hashHex(str("token"));
        const json* tok = nullptr;
        if (!str("token").empty())
            for (const json& x : m_connect["tokens"])
                if (x.value("hash", "") == hash && x.value("expires", 0LL) > now && findApp(x.value("app", ""))) tok = &x;
        const User* u = tok ? findUser(tok->value("account", "")) : nullptr;
        if (!u || u->deleted || u->banned) return oauthFail("invalid_token", "That access token is wrong, too old, or the person disconnected the app.");
        json r = okay();
        r["user"] = {{"id", std::to_string(u->userId)}, {"username", u->username}};
        r["app"] = tok->value("app", "");
        r["scopes"] = scopesOf(*tok);
        return r;
    }
    if (name == "connect.userinfo") {
        const std::string hash = Account::hashHex(str("token"));
        const User* u = nullptr;
        if (!str("token").empty())
            for (const json& x : m_connect["tokens"])
                if (x.value("hash", "") == hash && x.value("expires", 0LL) > now) u = findUser(x.value("account", ""));
        if (!u || u->deleted) return oauthFail("invalid_token", "That access token is wrong, too old, or the person disconnected the app.");
        std::string origin = str("origin");
        const bool originOk = (origin.rfind("https://", 0) == 0 || origin.rfind("http://", 0) == 0) && origin.size() < 200 &&
            origin.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-/", 0) == std::string::npos &&
            origin.find('/', 8) == std::string::npos;
        if (!originOk) origin = "https://gutsandbolts.net";
        const json& pics = m_connect["pictures"];
        json user = {{"id", std::to_string(u->userId)}, {"username", u->username}, {"avatar", nullptr}};
        if (pics.contains(u->id))
            user["avatar"] = origin + "/avatar/" + std::to_string(u->userId) + ".png?v=" + std::to_string(pics[u->id].get<long long>());
        json r = okay(); r["user"] = user; return r;
    }
    if (name == "connect.avatarPicture") {   // (worker/index.js serves /avatar/<user number>.png with it)
        const std::string id = str("id");
        const User* u = !id.empty() && id.size() < 12 && id.find_first_not_of("0123456789") == std::string::npos ? findUserId(std::stoll(id)) : nullptr;
        std::string data;
        if (!u || u->deleted || !m_connect["pictures"].contains(u->id) || !readFile(avatarPicturePath(u->id), data)) return fail("No picture.");
        json r = okay(); r["data"] = Online::base64Encode(data); return r;
    }
    return fail("Unknown request.");
}

json GbServer::connectAdmin(const std::string& name, const User& me, const json& args) {
    if (!isOfficial(me)) return fail("Only the official Guts account can do that.");
    const long long now = Online::unixNow();
    json r = okay();
    if (name == "admin.connectSecret") {
        const ConnectApp* app = findApp(args.value("id", std::string()));
        if (!app) return fail("That app isn't one Guts&Bolts knows.");
        const std::string secret = "gbs_" + Account::randomHex(32);
        m_connect["secrets"][app->id] = {{"hash", Account::hashHex(secret)}, {"at", now}};
        saveConnect();
        staffDid(me, "connect", std::string("Made a new client secret for ") + app->name);
        r["secret"] = secret;
    }
    json apps = json::array();
    for (const ConnectApp& a : kConnectApps) {
        int people = 0;
        for (const json& x : m_connect["tokens"]) if (x.value("app", "") == a.id && x.value("expires", 0LL) > now) ++people;
        const json& s = m_connect["secrets"];
        apps.push_back({{"id", a.id}, {"name", a.name}, {"site", a.site}, {"redirects", json::array({a.redirect})},
                        {"secretAt", s.contains(a.id) ? s[a.id].value("at", 0LL) : 0LL}, {"people", people}});
    }
    r["apps"] = apps;
    return r;
}
