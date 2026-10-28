#include "OnlineClient.h"
#include "AssetCache.h"
#include "Protocol.h"
#include "../core/Account.h"
#include "../game/Profile.h"
#include "../game/Catalog.h"
#include "../net/Socket.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#include <cstring>
#endif

using json = nlohmann::json;

namespace Online {

namespace {

struct Done { Reply callback; json reply; };

struct State {
    std::mutex       lock;
    std::deque<Done> done;              // finished requests, waiting for update()
    std::atomic<int> running{0};
    Status           status = Status::Off;
    std::string      statusText = "Offline";
    json             me = json::object();
    json             server = json::object();
    double           retryAt = 0;       // seconds (steady clock) to try connecting again
    double           pingAt = 0;        // "still here", so friends see us online
    bool             helloOut = false;
};

State& S() {
    // Never destroyed: a request still running in the background when the app
    // quits can finish safely.
    static State* s = new State;
    return *s;
}

double clockSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// "host", "host:port", or a web address: "https://name.workers.dev" (a server on
// Cloudflare). Web addresses come back as "wss://name" (or "ws://name" for
// plain http), which Net::Connection understands.
bool splitAddress(const std::string& address, std::string& host, int& port) {
    std::string a = address;
    while (!a.empty() && (a.back() == ' ' || a.back() == '/')) a.pop_back();
    while (!a.empty() && a.front() == ' ') a.erase(a.begin());
    std::string scheme;
    for (const char* sc : {"https://", "wss://", "http://", "ws://"})
        if (a.rfind(sc, 0) == 0) { scheme = sc; a = a.substr(std::strlen(sc)); break; }
    if (size_t slash = a.find('/'); slash != std::string::npos) a.resize(slash);
    if (scheme.empty() && (a.find(".workers.dev") != std::string::npos || a.find("gutsandbolts.net") != std::string::npos))
        scheme = "https://";
    const bool web = !scheme.empty(), secure = scheme == "https://" || scheme == "wss://";
    host = a;
    port = web ? (secure ? 443 : 80) : kDefaultPort;
    // "[::1]:7780" isn't supported; "host:port" and plain "host" are.
    auto colon = a.rfind(':');
    if (colon != std::string::npos && a.find(':') == colon) {
        host = a.substr(0, colon);
        port = std::atoi(a.c_str() + colon + 1);
    }
    if (web && !host.empty()) host = (secure ? "wss://" : "ws://") + host;
    return !host.empty() && port > 0 && port < 65536;
}

void finish(Reply cb, json reply) {
    std::lock_guard<std::mutex> g(S().lock);
    S().done.push_back({std::move(cb), std::move(reply)});
}

} // namespace

// ---------------------------------------------------------------------------

std::string serverAddress() {
    if (const char* e = std::getenv("GB_SERVER"); e && *e) return e;
    return kOfficialServer;   // no offline play: everyone is on the main server
}

void setServerAddress(const std::string& address) {
    (void)address;
    S().me = json::object();
    S().status = Status::Off;
    S().statusText = "Offline";
    if (configured()) connect();
}

bool configured() { return !serverAddress().empty(); }
Status status() { return S().status; }
const std::string& statusText() { return S().statusText; }
bool online() { return S().status == Status::Online; }

namespace { bool g_guest = false; std::string g_testName; }
void setTestName(const std::string& name) { g_testName = name; }
void setGuest(bool on) {
    if (g_guest == on) return;
    g_guest = on;
    S().helloOut = false;
    connect();   // tell the server our guest name
}
bool isGuest() { return g_guest && S().me.value("userId", 0LL) == 0; }
std::string playerName() {
    if (!g_testName.empty()) return g_testName;
    return isGuest() && online() ? guestName() : Profile::get().name;
}
std::string guestName() {
    // Four digits from the account's public ID, so it doesn't change between games.
    const std::string& id = Account::id();
    unsigned n = 0;
    for (size_t i = 0; i < id.size() && i < 8; ++i) n = n * 16 + (unsigned)std::stoul(id.substr(i, 1), nullptr, 16);
    return "Guest " + std::to_string(1000 + n % 9000);
}
const json& me() { return S().me; }
const json& serverInfo() { return S().server; }
bool verified() { return online() && S().me.value("verified", false); }
bool staff() { return online() && S().me.value("staff", false); }
long long bolts() { return S().me.value("bolts", 0LL); }
int pending() { return S().running.load(); }

bool owns(const std::string& id) {
    const json& o = S().me.contains("owned") ? S().me["owned"] : json::array();
    for (const auto& x : o) if (x.is_string() && x.get<std::string>() == id) return true;
    return false;
}

static bool isServerItem(const std::string& id);

void takeMe(const json& reply) {
    if (!reply.contains("me") || !reply["me"].is_object()) return;
    S().me = reply["me"];
    // Signed up: your name everywhere (games, chat) is your username.
    if (std::string u = S().me.value("username", std::string()); !u.empty() && Profile::get().name != u) {
        Profile::get().name = u;
        Profile::get().save();
    }
    // Badges the server knows about go into the profile too, so they also show
    // (and can be checked) in multiplayer games.
    if (S().me.contains("grants")) {
        Profile& p = Profile::get();
        bool changed = false;
        for (const auto& g : S().me["grants"]) {
            if (!g.is_array() || g.size() != 2 || !g[0].is_string() || !g[1].is_string()) continue;
            std::string k = g[0], sig = g[1];
            bool have = false;
            for (auto& [pk, ps] : p.grants) if (pk == k) { have = true; if (ps != sig) { ps = sig; changed = true; } }
            if (!have) { p.grants.push_back({k, sig}); changed = true; }
        }
        if (changed) p.save();
    }
    // An avatar saved on the server more recently than ours (on the website, or
    // another device) replaces ours.
    if (S().me.contains("avatar") && S().me["avatar"].is_object()) {
        const json& a = S().me["avatar"];
        Profile& p = Profile::get();
        long long updated = a.value("updated", 0LL);
        if (updated > p.avatarUpdated) {
            auto color = [&](const char* k, glm::vec3 fallback) {
                if (!a.contains(k) || !a[k].is_array() || a[k].size() != 3) return fallback;
                if (a[k][0].get<double>() < 0) return glm::vec3(-1.0f);
                return glm::vec3(a[k][0].get<float>(), a[k][1].get<float>(), a[k][2].get<float>()) / 255.0f;
            };
            p.colors.head = color("head", p.colors.head);
            p.colors.torso = color("torso", p.colors.torso);
            p.colors.leftArm = color("leftArm", p.colors.leftArm);
            p.colors.rightArm = color("rightArm", p.colors.rightArm);
            p.colors.leftLeg = color("leftLeg", p.colors.leftLeg);
            p.colors.rightLeg = color("rightLeg", p.colors.rightLeg);
            p.hat = (HatStyle)std::clamp(a.value("hat", 0), 0, kHatStyleCount - 1);
            p.hatColor = color("hatColor", glm::vec3(-1.0f));
            if (a.contains("wearing") && a["wearing"].is_array()) {
                // Keep things worn from the built-in catalog; take the server's list for the rest.
                std::vector<std::string> keep;
                for (const auto& w : p.wearing) if (!isServerItem(w)) keep.push_back(w);
                for (const auto& w : a["wearing"]) if (w.is_string()) keep.push_back(w.get<std::string>());
                p.wearing = keep;
            }
            p.avatarUpdated = updated;
            p.save();
            // Dress up in what was put on there: the clothing pictures, face, T-shirt and
            // accessories come with the items (which our profile page gets).
            const long long myId = S().me.value("userId", 0LL);
            if (myId > 0)
                request("profile", {{"id", std::to_string(myId)}}, [](const json& r) {
                    if (!r.value("ok", false) || !r.contains("wearing") || !r["wearing"].is_array()) return;
                    Profile& me = Profile::get();
                    me.shirtImage.clear(); me.pantsImage.clear(); me.tshirtImage.clear(); me.faceImage.clear();
                    me.headless = false;
                    me.body.clear();
                    me.bodyParts.clear();
                    me.accessories.clear();
                    const std::vector<std::string> wearing = me.wearing;
                    for (const json& a : r["wearing"]) if (a.is_object()) {
                        Catalog::Item item = Catalog::fromServer(a);
                        if (item.type == Catalog::Type::BodyPart || (item.type == Catalog::Type::Bundle && !item.body.empty()))
                            Online::download(item.body.rfind("gb:", 0) == 0 ? item.body.substr(3) : item.id);  // warm the mesh cache for the next avatar apply
                        Catalog::applyLook(item);
                    }
                    me.wearing = wearing;   // (applyLook tidies the list; the server's is right)
                    me.save();
                });
        }
    }
}

// "hat-1a2b3c4d5e", "123", "123-leftarm" (one part of a body bundle) or "item-headless-head": clothes
// from the server's catalog (the built-in catalog's ids look different).
static bool isServerItem(const std::string& id) {
    if (!id.empty() && id.size() < 12 && std::all_of(id.begin(), id.end(), [](char c) { return std::isdigit((unsigned char)c); })) return true;
    if (const size_t cut = id.find('-'); cut > 0 && cut < 12 && cut != std::string::npos) {
        const std::string part = id.substr(cut + 1);
        if (std::all_of(id.begin(), id.begin() + (long)cut, [](char c) { return std::isdigit((unsigned char)c); }) &&
            (part == "head" || part == "torso" || part == "leftarm" || part == "rightarm" || part == "leftleg" || part == "rightleg"))
            return true;
    }
    if (id.rfind("item-", 0) == 0) return true;
    for (const char* k : {"hat-", "shirt-", "pants-", "tshirt-", "hair-", "faceacc-", "neck-", "shoulder-", "waist-", "face-"}) {
        size_t n = std::strlen(k);
        if (id.size() == n + 10 && id.compare(0, n, k) == 0 &&
            std::all_of(id.begin() + (long)n, id.end(), [](char c) { return std::isxdigit((unsigned char)c); }))
            return true;
    }
    return false;
}

void pushAvatar(std::function<void(const json&)> done) {
    const Profile& p = Profile::get();
    auto rgb = [](const glm::vec3& c) {
        if (c.x < 0.0f) return json::array({-1, -1, -1});
        return json::array({(int)std::lround(std::clamp(c.x, 0.0f, 1.0f) * 255.0f), (int)std::lround(std::clamp(c.y, 0.0f, 1.0f) * 255.0f),
                            (int)std::lround(std::clamp(c.z, 0.0f, 1.0f) * 255.0f)});
    };
    json wearing = json::array();
    for (const auto& w : p.wearing) if (isServerItem(w)) wearing.push_back(w);   // server items only
    json avatar = {{"head", rgb(p.colors.head)}, {"torso", rgb(p.colors.torso)}, {"leftArm", rgb(p.colors.leftArm)},
                   {"rightArm", rgb(p.colors.rightArm)}, {"leftLeg", rgb(p.colors.leftLeg)}, {"rightLeg", rgb(p.colors.rightLeg)},
                   {"hat", (int)p.hat}, {"hatColor", rgb(p.hatColor)}, {"wearing", wearing}};
    request("avatar.set", {{"avatar", avatar}}, [done](const json& r) {
        if (r.value("ok", false) && r.contains("me") && r["me"].contains("avatar") && r["me"]["avatar"].is_object()) {
            Profile& p = Profile::get();
            p.avatarUpdated = r["me"]["avatar"].value("updated", p.avatarUpdated);
            p.save();
        }
        if (done) done(r);
    });
}

void request(const std::string& op, const json& args, Reply done, int timeoutSeconds) {
    std::string address = serverAddress();
    if (address.empty()) {
        finish(std::move(done), json{{"ok", false}, {"error", "Not connected to a Guts&Bolts server."}});
        return;
    }
    // Sign here, on the main thread (the key lives here); the network part runs in the background.
    std::string text = signedRequest(op, args).dump();
    S().running++;
#ifdef __EMSCRIPTEN__
    // In a browser (the web Player): the page came from the Guts&Bolts website, so ask its
    // /api with the browser's own fetch (a web page has no sockets or threads of its own).
    struct Job { Reply done; std::string body; };
    Job* job = new Job{std::move(done), std::move(text)};
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "POST");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    static const char* kHeaders[] = {"Content-Type", "application/json", nullptr};
    attr.requestHeaders = kHeaders;
    attr.requestData = job->body.data();
    attr.requestDataSize = job->body.size();
    attr.timeoutMSecs = (unsigned)timeoutSeconds * 1000u;
    attr.userData = job;
    auto answered = [](emscripten_fetch_t* f) {
        Job* j = static_cast<Job*>(f->userData);
        json reply = f->numBytes ? json::parse(std::string(f->data, (size_t)f->numBytes), nullptr, false) : json();
        if (!reply.is_object())
            reply = {{"ok", false}, {"error", f->status ? "The server sent back something odd." : "Couldn't reach the server. Check your internet."}};
        if (!reply.contains("ok")) reply["ok"] = false;
        finish(std::move(j->done), std::move(reply));
        delete j;
        emscripten_fetch_close(f);
        S().running--;
    };
    attr.onsuccess = answered;
    attr.onerror = answered;   // (a 4xx answer still has the server's JSON in it)
    emscripten_fetch(&attr, "/api");
    (void)address;
    return;
#endif
    std::thread([address, text, done, timeoutSeconds]() {
        json reply;
        std::string host, err;
        int port = 0;
        if (!splitAddress(address, host, port)) {
            reply = {{"ok", false}, {"error", "That server address doesn't look right (use host or host:port)."}};
        } else if (Net::isWebAddress(host)) {
            // A server on the web (Cloudflare): one HTTPS request to its /api.
            std::string answer;
            if (Net::httpPost(host, port, "/api", text, answer, err, timeoutSeconds * 1000)) {
                reply = json::parse(answer, nullptr, false);
                if (!reply.is_object()) reply = {{"ok", false}, {"error", "The server sent back something odd."}};
            } else {
                reply = {{"ok", false}, {"error", "Couldn't reach the server at " + address + " (" + err + ")."}};
            }
        } else if (auto conn = Net::Connection::connectTo(host, port, err, 5000)) {
            conn->send(text);
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
            std::string msg;
            bool got = false;
            while (std::chrono::steady_clock::now() < until) {
                bool alive = conn->poll();
                if (conn->pop(msg)) { got = true; break; }
                if (!alive) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (got) {
                reply = json::parse(msg, nullptr, false);
                if (!reply.is_object()) reply = {{"ok", false}, {"error", "The server sent back something odd."}};
            } else {
                reply = {{"ok", false}, {"error", "The server didn't answer in time."}};
            }
        } else {
            reply = {{"ok", false}, {"error", "Couldn't reach the server at " + address + " (" + err + ")."}};
        }
        if (!reply.contains("ok")) reply["ok"] = false;
        finish(done, std::move(reply));
        S().running--;
    }).detach();
}

json signedRequest(const std::string& op, const json& args) {
    long long t = unixNow();
    std::string nonce = Account::randomHex(8);
    return {{"op", op}, {"account", Account::id()}, {"time", t}, {"nonce", nonce}, {"args", args},
            {"sig", Account::sign(requestText(op, Account::id(), t, nonce, args))}};
}

bool serverHostPort(std::string& host, int& port) { return splitAddress(serverAddress(), host, port); }

void connect() {
    if (!configured() || S().helloOut) return;
    S().helloOut = true;
    if (S().status != Status::Online) { S().status = Status::Connecting; S().statusText = "Connecting..."; }
    Profile& p = Profile::get();
    json grants = json::array();
    for (const auto& [k, sig] : p.grants) grants.push_back({k, sig});
    request("hello", {{"name", isGuest() ? guestName() : p.name}, {"grants", grants}, {"protocol", kProtocol}}, [](const json& r) {
        S().helloOut = false;
        if (r.value("ok", false)) {
            takeMe(r);
            if (r.contains("server")) S().server = r["server"];
            // The server says which account is its staff account (Guts). Believe it, so
            // that account can sign up as Guts and see the staff pages, even in a
            // downloaded app that doesn't have the staff key built in.
            std::string official = S().server.value("official", std::string());
            if (official.size() == 64 && official.find_first_not_of("0123456789abcdef") == std::string::npos)
                Account::setOfficialId(official);
            S().status = Status::Online;
            S().statusText = "Online: " + S().server.value("name", std::string("Guts&Bolts"));
        } else {
            S().status = Status::Failed;
            S().statusText = r.value("error", std::string("Couldn't connect."));
            S().retryAt = clockSeconds() + 30.0;
        }
    }, 10);
}

void update() {
    std::deque<Done> ready;
    {
        std::lock_guard<std::mutex> g(S().lock);
        ready.swap(S().done);
    }
    for (Done& d : ready) {
        // A reply saying our account changed (Bolts, badges...) keeps me() fresh for everyone.
        if (d.reply.value("ok", false)) takeMe(d.reply);
        // Banned while the app is open: say hello again, so me() has the ban (the ban screen shows).
        else if (d.reply.value("error", std::string()).rfind("This account has been banned", 0) == 0 &&
                 !(S().me.contains("ban") && S().me["ban"].is_object()))
            connect();
        if (d.callback) d.callback(d.reply);
    }
    if (configured() && S().status == Status::Off) connect();
    if (S().status == Status::Failed && clockSeconds() > S().retryAt) connect();
    if (S().status == Status::Online && clockSeconds() > S().pingAt) {
        bool first = S().pingAt == 0;   // hello just told the server we're here
        S().pingAt = clockSeconds() + 60.0;
        if (!first) request("ping", json::object());
    }
}

void finishAll(int timeoutMs) {
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto waiting = [] { std::lock_guard<std::mutex> g(S().lock); return !S().done.empty(); };
    while ((S().running.load() > 0 || waiting()) && std::chrono::steady_clock::now() < until) {
        update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    update();
}

} // namespace Online
