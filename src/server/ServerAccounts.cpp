// Signing up and logging in.
//
// An account is still a key pair (it signs every request), but now it can
// also have a username and a user number:
//   - user numbers count up (1, 2, 3...) and are never given out twice;
//   - usernames can never be used again, even after a ban;
//   - user 1 is "Guts", the official staff account.
//
// To log in on another device, the app keeps a copy of your key on the
// server, locked with a key made from your password (see Account::passwordKeys).
// We only ever see a login token made from the password, never the password,
// and can't unlock the copy ourselves.
#include <cctype>
#include "../core/Totp.h"
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr int       kMaxWrongPasswords = 5;     // then wait...
constexpr long long kLockoutSeconds    = 600;   // ...10 minutes
constexpr long long kRenameCost        = 1000;  // Bolts to change your username

// Days from 1970-01-01 to a date (month 1-12; the day may run past the month's end, like
// JavaScript's Date.UTC). Howard Hinnant's days_from_civil.
long long daysFromCivil(long long y, long long m, long long d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468 + (d - 1);
}
} // namespace

void GbServer::claimOfficial(User& u) {
    if (!isOfficial(u) || u.userId == 1) return;
    u.userId = 1;
    u.username = Account::kStaffName;
    m_takenNames.insert(lower(u.username));
}

void GbServer::gutsFollows(User& u) {   // (worker/server.js gutsFollows)
    User* g = findUserId(1);
    if (!g || u.userId <= 1 || g->id == u.id) return;
    if (g->following.count(u.id) && u.followers.count(g->id)) return;
    g->following.insert(u.id);
    u.followers.insert(g->id);
    saveUsers();
}

// The user number (5 or "#5") is the main way to name someone; long account keys,
// "@username" and names work too. (worker/server.js findPerson)
GbServer::User* GbServer::findPerson(const std::string& raw) {
    std::string s = raw;
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    {
        std::string digits = !s.empty() && s[0] == '#' ? s.substr(1) : s;
        if (!digits.empty() && digits.size() <= 11 && std::all_of(digits.begin(), digits.end(), ::isdigit))
            return findUserId(std::atoll(digits.c_str()));
    }
    if (User* u = findUser(s)) return u;
    if (User* u = findUsername(!s.empty() && s[0] == '@' ? s.substr(1) : s)) return u;
    std::string want = s;
    for (char& c : want) c = (char)std::tolower((unsigned char)c);
    for (auto& [id, u] : m_users) {
        if (u.userId <= 0) continue;
        std::string n = u.name;
        for (char& c : n) c = (char)std::tolower((unsigned char)c);
        if (n == want) return &u;
    }
    return nullptr;
}

GbServer::User* GbServer::findUsername(const std::string& username) {
    std::string want = lower(username);
    for (auto& [id, u] : m_users) if (u.userId > 0 && lower(u.username) == want) return &u;
    return nullptr;
}

GbServer::User* GbServer::findUserId(long long userId) {
    if (userId <= 0) return nullptr;
    for (auto& [id, u] : m_users) if (u.userId == userId) return &u;
    return nullptr;
}

std::map<std::string, GbServer::Asset>::iterator GbServer::findAsset(const std::string& id) {
    auto it = m_assets.find(id);
    if (it != m_assets.end()) return it;
    auto n = m_assetNums.find(id);
    return n == m_assetNums.end() ? m_assets.end() : m_assets.find(n->second);
}
std::map<std::string, GbServer::Asset>::const_iterator GbServer::findAsset(const std::string& id) const {
    auto it = m_assets.find(id);
    if (it != m_assets.end()) return it;
    auto n = m_assetNums.find(id);
    return n == m_assetNums.end() ? m_assets.end() : m_assets.find(n->second);
}

// Give every asset without one its number (oldest first); a number is never handed out twice.
void GbServer::numberAssets() {
    std::vector<Asset*> todo;
    for (auto& [id, a] : m_assets) {
        if (a.num > 0) m_nextAssetNum = std::max(m_nextAssetNum, a.num + 1);
        else todo.push_back(&a);
    }
    std::sort(todo.begin(), todo.end(), [](const Asset* x, const Asset* y) { return x->created != y->created ? x->created < y->created : x->id < y->id; });
    for (Asset* a : todo) a->num = m_nextAssetNum++;
    m_assetNums.clear();
    for (auto& [id, a] : m_assets) m_assetNums[std::to_string(a.num)] = id;
    if (!todo.empty()) saveAssets();
}

void GbServer::saveIds() {
    writeFile(m_opts.data / "ids.json", json{{"next", m_nextUserId}, {"taken", m_takenNames}, {"asset", m_nextAssetNum}}.dump(1));
}

void GbServer::loadIds() {
    std::string text;
    if (readFile(m_opts.data / "ids.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object()) {
            m_nextUserId = std::max<long long>(2, j.value("next", 2LL));
            m_nextAssetNum = std::max<long long>(1, j.value("asset", 1LL));
            if (j.contains("taken"))
                for (const auto& n : j["taken"]) if (n.is_string()) m_takenNames.insert(n.get<std::string>());
        }
    }
    // Whatever the file says, never hand out a number or name that's in use.
    for (auto& [id, u] : m_users) {
        claimOfficial(u);
        if (u.userId > 0) {
            m_nextUserId = std::max(m_nextUserId, u.userId + 1);
            m_takenNames.insert(lower(u.username));
        }
    }
    m_takenNames.insert("guts");
    numberAssets();
    saveIds();
    for (auto& [id, u] : m_users) gutsFollows(u);
}

json GbServer::accountOp(const std::string& name, User& me, const json& args) {
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    std::string username = Online::cleanText(str("username"), 30);

    if (name == "account.ackWarning") {   // "I understand" on a staff warning
        std::string id = str("id");
        if (me.warnings.is_array())
            for (json& w : me.warnings)
                if (w.is_object() && w.value("id", std::string()) == id) {
                    w["seen"] = true;
                    saveUsers();
                    json r = okay(); r["me"] = meJson(me); return r;
                }
        return fail("That warning is gone.");
    }
    // Authenticator app (worker/server.js does the same): setup gives a secret and an
    // otpauth:// link for the QR code; a code from the app turns it on; after that,
    // logging in needs a code from the app too.
    auto provePassword = [&]() {
        std::string auth = lower(str("auth"));
        return !me.keyBlob.empty() && isHex(auth, 64, 64) && Account::hashHex(auth) == me.pwHash;
    };
    if (name == "account.privacy") {   // who sees you online / can join you (worker/server.js)
        if (me.userId == 0) return fail("Sign up first.");
        auto ok = [](const std::string& v) { return v == "everyone" || v == "friends" || v == "nobody"; };
        if (ok(str("status"))) me.privacyStatus = str("status");
        if (ok(str("join"))) me.privacyJoin = str("join");
        if (ok(str("messages"))) me.privacyMessages = str("messages");   // who can send you messages
        if (ok(str("inventory"))) me.privacyInventory = str("inventory");   // who can see your inventory
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "account.authAppSetup") {
        if (me.userId == 0) return fail("Sign up first.");
        if (!me.keyBlob.empty() && !provePassword()) return fail("Wrong password.");
        const std::string hex = Account::randomHex(20);   // 20 random bytes
        std::vector<uint8_t> raw;
        for (size_t i = 0; i + 1 < hex.size(); i += 2) raw.push_back((uint8_t)std::stoi(hex.substr(i, 2), nullptr, 16));
        me.totpPending = Totp::base32(raw);
        saveUsers();
        auto enc = [](const std::string& s) {
            std::string o;
            for (unsigned char c : s) {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.') o += (char)c;
                else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
            }
            return o;
        };
        json r = okay();
        r["secret"] = me.totpPending;
        r["uri"] = "otpauth://totp/" + enc("Guts&Bolts:" + me.username) + "?secret=" + me.totpPending + "&issuer=" +
                   enc("Guts&Bolts") + "&digits=6&period=30";
        return r;
    }
    if (name == "account.authAppEnable") {
        if (me.totpPending.empty()) return fail("Start setting it up first.");
        long long step = Totp::check(me.totpPending, str("code"), Online::unixNow());
        if (step < 0) return fail("That code isn't right. Check your phone's clock, and type the newest code.");
        me.totpSecret = me.totpPending;
        me.totpPending.clear();
        me.totpLast = step;
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "account.authAppDisable") {
        if (me.totpSecret.empty()) { json r = okay(); r["me"] = meJson(me); return r; }
        if (!me.keyBlob.empty() && !provePassword()) return fail("Wrong password.");
        if (Totp::check(me.totpSecret, str("code"), Online::unixNow()) < 0) return fail("That code isn't right.");
        me.totpSecret.clear();
        me.totpLast = -1;
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    // Email (adding one, confirming it, two-step codes, "forgot password"): only the main
    // (Cloudflare) server can send email, so this one says so instead of "Unknown request."
    if (name == "account.email" || name == "account.emailVerify" || name == "account.emailRemove" ||
        name == "account.twoStep" || name == "account.forgot" || name == "account.reset")
        return fail("This server can't send email.");
    if (name == "account.joinDate") {
        // Guts only: set the account's join date (earlier than it really was, e.g. when
        // the project started). Everyone sees it on the profile. (worker/server.js has the same.)
        if (!isOfficial(me)) return fail("Only Guts can change a join date.");
        const std::string date = str("date");
        bool shape = date.size() == 10 && date[4] == '-' && date[7] == '-';
        for (size_t i = 0; shape && i < date.size(); ++i)
            if (i != 4 && i != 7 && !std::isdigit((unsigned char)date[i])) shape = false;
        long long when = -1;
        if (shape) {
            long long y = std::stoll(date.substr(0, 4)), m = std::stoll(date.substr(5, 2)) - 1, d = std::stoll(date.substr(8, 2));
            y += m >= 0 ? m / 12 : -((11 - m) / 12);   // (a month past December rolls over, like Date.UTC)
            m = ((m % 12) + 12) % 12;
            when = daysFromCivil(y, m + 1, d) * 86400 + 12 * 3600;   // noon UTC
        }
        if (!shape || when < daysFromCivil(2000, 1, 1) * 86400 || when > Online::unixNow())
            return fail("Pick a date in the past (like 2025-09-15).");
        me.created = when;
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "account.rename") {
        // Change your username for 1000 Bolts. Old usernames stay reserved for you: nobody
        // else can take them, and you can switch back to one (for the same price).
        if (me.userId == 0) return fail("Sign up first.");
        if (isOfficial(me)) return fail("The staff account is always Guts.");
        const std::string wanted = Online::cleanText(str("username"), 20);
        if (std::string problem = Online::usernameProblem(wanted); !problem.empty()) return fail(problem);
        const std::string want = lower(wanted);
        if (want == lower(me.username)) {
            if (wanted == me.username) return fail("That's already your username.");
        } else {
            const bool mine = std::any_of(me.pastNames.begin(), me.pastNames.end(), [&](const std::string& p) { return lower(p) == want; });
            if (!mine && m_takenNames.count(want)) return fail("That username is taken (usernames are never reused).");
        }
        if (balance(me) < kRenameCost)
            return fail("Changing your username costs " + std::to_string(kRenameCost) + " Bolts. You have " + std::to_string(balance(me)) + ".");
        const std::string old = me.username;
        add(me, -kRenameCost, "Username change: " + old + " to " + wanted, "rename:" + Account::randomHex(6));
        me.pastNames.erase(std::remove_if(me.pastNames.begin(), me.pastNames.end(), [&](const std::string& p) { return lower(p) == want; }),
                           me.pastNames.end());
        if (lower(old) != want &&
            std::none_of(me.pastNames.begin(), me.pastNames.end(), [&](const std::string& p) { return lower(p) == lower(old); }))
            me.pastNames.push_back(old);
        me.username = wanted;
        me.name = wanted;
        m_takenNames.insert(want);
        saveIds();
        saveUsers();
        log("rename: #" + std::to_string(me.userId) + " " + old + " is now " + wanted);
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "account.password") {
        // Change your password: the new locked key comes from this device (which has the key).
        if (me.userId == 0 || me.keyBlob.empty()) return fail("Set a password in the app first.");
        if (!provePassword()) return fail("Your current password is wrong.");
        const std::string salt = lower(str("salt")), auth = lower(str("newAuth")), blob = lower(str("key"));
        if (!isHex(salt, 32, 32) || !isHex(auth, 64, 64) || !isHex(blob, 208, 208)) return fail("Something was missing. Try again.");
        me.pwSalt = salt;
        me.pwHash = Account::hashHex(auth);
        me.keyBlob = blob;
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "account.delete") {
        // Delete your account and what's personal in it, after typing your password (see deleteAccount).
        if (me.userId == 0) return fail("Sign up first.");
        if (isOfficial(me)) return fail("The staff account can't be deleted.");
        if (me.keyBlob.empty()) return fail("Set a password in the app first.");
        if (!provePassword()) return fail("Wrong password.");
        deleteAccount(me);
        json r = okay(); r["deleted"] = true; return r;
    }
    if (name == "account.check") {   // is this username free? (for the sign-up page, as you type)
        json r = okay();
        std::string problem = Online::usernameProblem(username, isOfficial(me));
        if (problem.empty() && m_takenNames.count(lower(username)) && !(isOfficial(me) && lower(username) == "guts"))
            problem = "That username is taken.";
        r["available"] = problem.empty();
        r["problem"] = problem;
        return r;
    }

    if (name == "account.signup") {
        std::string salt = lower(str("salt")), auth = lower(str("auth")), blob = lower(str("key"));
        if (!isHex(salt, 32, 32) || !isHex(auth, 64, 64) || !isHex(blob, 208, 208))
            return fail("The sign-up form was missing something. Update the app and try again.");
        bool official = isOfficial(me);
        if (me.userId > 0 && !(official && me.keyBlob.empty()))
            return fail("This device is already signed up as " + me.username + ".");
        if (official) {
            // Guts just sets a password (the name and number are already theirs).
            if (lower(username) != "guts") return fail("The staff account's username is Guts.");
            me.name = me.username;
        } else {
            if (std::string problem = Online::usernameProblem(username); !problem.empty()) return fail(problem);
            if (m_takenNames.count(lower(username))) return fail("That username is taken. Try another one.");
            me.userId = m_nextUserId++;
            me.username = username;
            m_takenNames.insert(lower(username));
            me.name = username;
            gutsFollows(me);
        }
        me.pwSalt = salt;
        me.pwHash = Account::hashHex(auth);
        me.keyBlob = blob;
        saveIds();
        if (!hasRef(me, "welcome")) add(me, 100, "Welcome to Guts&Bolts!", "welcome");   // (also saves)
        else saveUsers();
        log("signed up: #" + std::to_string(me.userId) + " " + me.username);
        json r = okay();
        r["me"] = meJson(me);
        return r;
    }

    // Logging in happens from a new device, so `me` is that device's own
    // (empty) account; the real one is found by its username.
    User* u = findUsername(username);
    // An account made on a device that never set a password can't be logged into anywhere else yet.
    auto noLogin = [](const User* acc) -> std::string {
        if (!acc) return "There's no account with that username.";
        return "That account hasn't set a password yet, so it only works on the device it was made on. On that device, open the "
               "Guts&Bolts Player, go to Avatar > Your account and press \"Set a password\". Then you can log in here.";
    };
    if (name == "account.salt") {
        if (!u || u->keyBlob.empty()) return fail(noLogin(u));
        json r = okay();
        r["salt"] = u->pwSalt;
        return r;
    }
    if (name == "account.login") {
        if (!u || u->keyBlob.empty()) return fail(noLogin(u));
        if (u->banned) return fail(Online::banMessage(u->banReason, u->banNote));
        long long now = Online::unixNow();
        auto& fails = m_failedLogins[lower(username)];
        fails.erase(std::remove_if(fails.begin(), fails.end(), [&](long long t) { return now - t > kLockoutSeconds; }), fails.end());
        if ((int)fails.size() >= kMaxWrongPasswords)
            return fail("Too many wrong passwords. Wait 10 minutes and try again.");
        std::string auth = lower(str("auth"));
        if (!isHex(auth, 64, 64) || Account::hashHex(auth) != u->pwHash) {
            fails.push_back(now);
            return fail("Wrong password.");
        }
        if (!u->totpSecret.empty()) {   // authenticator app
            std::string code = str("code");
            if (code.empty()) {
                json r = fail("Type the 6-digit code from your authenticator app.");
                r["needCode"] = true; r["app"] = true; return r;
            }
            long long step = Totp::check(u->totpSecret, code, now, u->totpLast);
            if (step < 0) {
                fails.push_back(now);
                json r = fail("That code isn't right (or was already used). Try the newest one.");
                r["needCode"] = true; r["app"] = true; return r;
            }
            u->totpLast = step;
            saveUsers();
        }
        fails.clear();
        log("log in: #" + std::to_string(u->userId) + " " + u->username + " on a new device");
        json r = okay();
        r["account"] = u->id;
        r["key"] = u->keyBlob;
        r["username"] = u->username;
        r["name"] = u->name;
        return r;
    }
    return fail("Unknown request.");
}

// Delete an account (account.delete; worker/server.js deleteAccount has the same). What's personal
// goes: password and key backup, two-step, avatar, About me, posts, outfits, messages, friends and
// follows (on both sides), favourites and notifications. Games and items stay (so what people
// bought keeps working), made by "Deleted user". Its username (and old ones) and user number stay
// reserved: never given to anyone else. Limited copies it holds stay its own, but come off resale
// and can't be traded (nobody can reach a deleted account), so they're out of circulation for good.
void GbServer::deleteAccount(User& u) {
    const long long now = Online::unixNow(), number = u.userId;
    if (!u.username.empty()) m_takenNames.insert(lower(u.username));
    for (const std::string& p : u.pastNames) m_takenNames.insert(lower(p));
    for (auto& [id, v] : m_users) {
        if (&v == &u) continue;
        v.friends.erase(u.id); v.friendIn.erase(u.id); v.friendOut.erase(u.id);
        v.following.erase(u.id); v.followers.erase(u.id);
    }
    for (const std::string& gameId : u.favorites) {
        auto it = findAsset(gameId);
        if (it == m_assets.end() || !it->second.meta.is_object()) continue;
        const long long count = it->second.meta.value("favorites", 0LL);
        if (count > 0) it->second.meta["favorites"] = count - 1;
    }
    for (auto& [gid, g] : m_groups) {   // groups they own stay (with "Deleted user" as the owner)
        if (g.owner != u.id) g.members.erase(u.id);
        g.requests.erase(u.id);
    }
    std::set<std::string> limited;
    for (auto& [aid, a] : m_assets) {
        if (!a.limited) continue;
        for (json& c : a.copies)
            if (c.value("owner", std::string()) == u.id) { c["price"] = 0; limited.insert(aid); }
    }
    for (json& x : m_trades)
        if (x.value("status", std::string()) == "open" && (x.value("from", std::string()) == u.id || x.value("to", std::string()) == u.id)) {
            x["status"] = "cancelled";
            x["updated"] = now;
        }
    for (json& x : m_tickets)   // email addresses left on support tickets
        if (x.value("from", std::string()) == u.id || x.value("about", std::string()) == u.id) { x["contact"] = ""; x.erase("code"); }
    // What's left: the ID (so its requests are refused), when it was made, and its Limited copies.
    User left;
    left.id = u.id;
    left.name = "Deleted user";
    left.created = u.created;
    left.owned = limited;
    left.deleted = now;
    u = std::move(left);
    saveIds();
    saveUsers();
    saveAssets();
    saveGroups();
    saveTrades();
    saveTickets();
    User system;
    system.id = "system";
    system.name = "System";
    staffDid(system, "account", "Account #" + std::to_string(number) + " was deleted by its owner");
}
