#include "Protocol.h"
#include "../core/Account.h"
#include "../core/TextFilter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstring>
#include <ctime>

namespace Online {

bool validKind(const std::string& k) {
    return k == "shirt" || k == "pants" || k == "audio" || k == "plugin" || k == "game" || k == "decal" ||
           k == "model" || k == "face" || k == "tshirt" || k == "gear" || k == "animation" || k == "bundle" || k == "bodypart" || isEmoteItem(k) || isAccessory(k);
}
bool isAccessory(const std::string& k) {
    return k == "hat" || k == "hair" || k == "faceacc" || k == "neck" || k == "shoulder" || k == "waist";
}
bool isClothing(const std::string& k) { return k == "shirt" || k == "pants" || k == "tshirt" || k == "face" || isAccessory(k); }
bool isCatalogItem(const std::string& k) { return isClothing(k) || k == "gear" || isEmoteItem(k) || k == "bundle" || k == "bodypart"; }
bool isEmoteItem(const std::string& k) { return k == "emote" || k == "animpack"; }
// A clip needs keyframes, and at most 30 seconds of them (worker/server.js clipProblem). "" if it's fine.
static std::string clipProblem(const nlohmann::json& c) {
    if (!c.is_object() || !c.contains("keys") || !c["keys"].is_array() || c["keys"].empty()) return "it has no keyframes";
    if (c["keys"].size() > 600) return "it has too many keyframes";
    for (const auto& k : c["keys"]) {
        if (!k.is_object() || !k.contains("t") || !k["t"].is_number()) return "its keyframes must be between 0 and 30 seconds";
        const double t = k["t"].get<double>();
        if (!(t >= 0 && t <= 30)) return "its keyframes must be between 0 and 30 seconds";
        if (!k.contains("poses") || !k["poses"].is_object()) return "a keyframe has no poses";
    }
    return "";
}
// An emote is {format: "gbanim", clip}; an animation pack is {format: "gbanimpack", clips:
// {idle, walk, run, jump, fall}} with at least one of them (worker/server.js emoteProblem).
std::string emoteProblem(const std::string& kind, const std::string& data) {
    nlohmann::json m = nlohmann::json::parse(data, nullptr, false);
    if (kind == "emote") {
        if (!m.is_object() || m.value("format", std::string()) != "gbanim" || !m.contains("clip") || m["clip"].is_null())
            return "An emote must be an animation made in Studio's Animation Editor.";
        std::string p = clipProblem(m["clip"]);
        return p.empty() ? "" : "That emote won't play: " + p + ".";
    }
    if (!m.is_object() || m.value("format", std::string()) != "gbanimpack" || !m.contains("clips") || !m["clips"].is_object())
        return "That isn't an animation pack.";
    static const char* kStates[] = {"idle", "walk", "run", "jump", "fall"};
    const nlohmann::json& clips = m["clips"];
    bool bad = clips.empty();
    for (auto it = clips.begin(); it != clips.end(); ++it)
        if (std::find_if(std::begin(kStates), std::end(kStates), [&](const char* s) { return it.key() == s; }) == std::end(kStates)) bad = true;
    if (bad) return "An animation pack has idle, walk, run, jump and fall animations (and nothing else).";
    for (auto it = clips.begin(); it != clips.end(); ++it)
        if (std::string p = clipProblem(it.value()); !p.empty()) return "The pack's " + it.key() + " animation won't play: " + p + ".";
    return "";
}
// Gear: one Tool published from Studio (worker/server.js gearProblem). "" if it's fine.
std::string gearProblem(const std::string& data) {
    nlohmann::json m = nlohmann::json::parse(data, nullptr, false);
    if (!m.is_object() || m.value("format", std::string()) != "gbmodel" || !m.contains("nodes") || !m["nodes"].is_array() ||
        m["nodes"].size() != 1)
        return "Gear must be one Tool published from Studio.";
    if (!m["nodes"][0].is_object() || m["nodes"][0].value("kind", std::string()) != "Tool")
        return "Gear must be a Tool (with a Handle part inside).";
    return "";
}
std::string bundleBody(const nlohmann::json& body, nlohmann::json& out) {
    using nlohmann::json;
    out = json::object();
    if (!body.is_object() || body.empty()) return "That bundle's body looks wrong.";
    auto numbers = [](const json& a, size_t n, float lo, float hi) {
        if (!a.is_array() || a.size() != n) return false;
        for (const json& x : a) if (!x.is_number() || !(x.get<double>() >= lo && x.get<double>() <= hi)) return false;
        return true;
    };
    for (const auto& [name, p] : body.items()) {
        if (std::find_if(std::begin(kBodyParts), std::end(kBodyParts), [&](const char* k) { return name == k; }) == std::end(kBodyParts))
            return "A bundle's body can only change the Head, Torso, arms and legs.";
        const std::string bad = name + "'s mesh looks wrong.";
        if (!p.is_object() || !p.contains("v") || !p.contains("f") || !p["v"].is_array() || !p["f"].is_array()) return bad;
        const json& v = p["v"];
        const json& f = p["f"];
        if (v.size() % 3 || v.size() < 9 || f.empty()) return bad;
        if ((int)(v.size() / 3) > kBodyMostCorners || (int)f.size() > kBodyMostCorners) return name + " has too many corners.";
        for (const json& x : v) if (!x.is_number() || !(std::abs(x.get<double>()) <= 1.0)) return bad;
        const size_t corners = v.size() / 3;
        for (const json& face : f) {
            if (!face.is_array() || face.size() < 3 || face.size() > 64) return bad;
            for (const json& k : face) if (!k.is_number_integer() || k.get<long long>() < 0 || k.get<long long>() >= (long long)corners) return bad;
        }
        if (!numbers(p.value("size", json()), 3, 0.1f, 8.0f)) return name + "'s size looks wrong.";
        if (!numbers(p.value("pos", json()), 3, -8.0f, 8.0f)) return name + "'s position looks wrong.";
        json part = {{"v", v}, {"f", f}, {"size", p["size"]}, {"pos", p["pos"]}, {"smooth", p.value("smooth", false) == true}};
        if (p.contains("uv")) {
            const json& uv = p["uv"];
            if (!uv.is_array() || uv.size() != f.size()) return bad;
            for (size_t i = 0; i < uv.size(); ++i)
                if (!numbers(uv[i], f[i].size() * 2, -1000.0f, 1000.0f)) return bad;
            part["uv"] = uv;
        }
        out[name] = std::move(part);
    }
    return "";
}

bool alwaysFree(const std::string& k) { return k == "decal" || k == "audio" || k == "animation"; }

const char* banReasonTitle(const std::string& key) {
    for (const BanReason& r : kBanReasons) if (key == r.key) return r.title;
    return nullptr;
}

std::string banMessage(const std::string& reason, const std::string& note) {
    std::string m = "This account has been banned";
    if (const char* t = banReasonTitle(reason)) m += std::string(" for: ") + t;
    m += ".";
    if (!note.empty()) m += " Note from staff: " + note;
    return m;
}

long long uploadFee(const std::string& k) {
    if (isClothing(k)) return kFeeClothing;
    if (k == "audio") return kFeeAudio;
    if (k == "plugin") return kFeePlugin;
    if (k == "decal") return kFeeDecal;
    return kFeeGame;   // (games, models, gear and animations are free to upload)
}

size_t maxSize(const std::string& k) {
    if (k == "audio") return kMaxAudio;
    if (k == "game") return kMaxGame;
    if (k == "plugin") return kMaxPlugin;
    if (k == "decal") return kMaxDecal;
    if (k == "model" || k == "gear") return 4u * 1024u * 1024u;   // objects from Studio for the Library
    if (k == "animation") return 1024u * 1024u;                    // from Studio's Animation Editor
    if (k == "emote") return 256u * 1024u;                         // keyframes for the character
    if (k == "animpack") return 512u * 1024u;                      // five of them
    if (k == "shirt" || k == "pants" || k == "tshirt") return 1024u * 1024u;   // a template picture / a T-shirt picture
    if (k == "face" || isAccessory(k) || k == "bundle" || k == "bodypart") return 1024u * 1024u;   // a face picture / an accessory or new body from Studio  // a face picture / an accessory from Studio
    return 64u * 1024u;   // clothing is just a little description of the look
}

const char* kindTitle(const std::string& k) {
    if (k == "hat") return "Hat";
    if (k == "shirt") return "Shirt";
    if (k == "pants") return "Pants";
    if (k == "audio") return "Audio";
    if (k == "plugin") return "Plugin";
    if (k == "game") return "Game";
    if (k == "decal") return "Decal";
    if (k == "model") return "Model";
    if (k == "hair") return "Hair";
    if (k == "faceacc") return "Face Accessory";
    if (k == "neck") return "Neck Accessory";
    if (k == "shoulder") return "Shoulder Accessory";
    if (k == "waist") return "Waist Accessory";
    if (k == "face") return "Face";
    if (k == "tshirt") return "T-Shirt";
    if (k == "gear") return "Gear";
    if (k == "animation") return "Animation";
    if (k == "emote") return "Emote";
    if (k == "animpack") return "Animation Pack";
    if (k == "gamepass") return "Game Pass";
    if (k == "bundle") return "Bundle";
    if (k == "bodypart") return "Body Part";
    return "?";
}

std::string requestText(const std::string& op, const std::string& account, long long time,
                        const std::string& nonce, const nlohmann::json& args) {
    return "gb-req:" + op + "\n" + account + "\n" + std::to_string(time) + "\n" + nonce + "\n" + args.dump();
}

std::string grantMessage(const std::string& key, const std::string& accountId) {
    return "gb-badge:" + key + ":" + accountId;
}

bool grantValid(const std::string& official, const std::string& key, const std::string& accountId, const std::string& sig) {
    if (official.empty()) return false;
    if (sig.rfind("s:", 0) != 0) return Account::verify(official, grantMessage(key, accountId), sig);
    if (key != "verified" && key != "partner") return false;   // the only badges Staff members can give
    size_t a = sig.find(':', 2), b = a == std::string::npos ? a : sig.find(':', a + 1);
    if (b == std::string::npos) return false;
    std::string staffId = sig.substr(2, a - 2), staffSig = sig.substr(a + 1, b - a - 1), grantSig = sig.substr(b + 1);
    return Account::verify(official, grantMessage("staff", staffId), staffSig) &&
           Account::verify(staffId, grantMessage(key, accountId), grantSig);
}

// --- Base64 ------------------------------------------------------------------

namespace {
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64Encode(const std::string& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16;
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8);
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += '=';
    }
    return out;
}

bool base64Decode(const std::string& in, std::string& out) {
    int map[256];
    for (int& m : map) m = -1;
    for (int k = 0; k < 64; ++k) map[(unsigned char)kB64[k]] = k;
    out.clear();
    out.reserve(in.size() / 4 * 3);
    uint32_t v = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' ) break;
        if (c == '\n' || c == '\r' || c == ' ') continue;
        int d = map[(unsigned char)c];
        if (d < 0) return false;
        v = (v << 6) | (uint32_t)d;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((v >> bits) & 0xFF); }
    }
    return true;
}

// --- Text ----------------------------------------------------------------------

std::string assetRef(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return s;
    std::string t = s.substr(a, b - a + 1);
    if (!t.empty() && t.size() < 19 && t.find_first_not_of("0123456789") == std::string::npos) return "gb:" + t;
    return t == s ? s : t;
}

std::string cleanText(const std::string& s, size_t maxLen, bool allowNewlines) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '\n' && allowNewlines) out += '\n';
        else if (c >= 32 && c != 127) out += (char)c;   // keeps UTF-8 letters too
        if (out.size() >= maxLen) break;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
    size_t start = 0;
    while (start < out.size() && (out[start] == ' ' || out[start] == '\n')) ++start;
    return out.substr(start);
}

std::string usernameProblem(const std::string& name, bool official) {
    if (name.size() < 3) return "Usernames need at least 3 characters.";
    if (name.size() > 20) return "Usernames can be at most 20 characters.";
    int underscores = 0, digits = 0;
    for (char c : name) {
        if (c == '_') ++underscores;
        else if (c >= '0' && c <= '9') ++digits;
        else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
            return "Usernames can only have letters, numbers and _.";
    }
    if (underscores > 1) return "Usernames can have only one _.";
    if (name.front() == '_' || name.back() == '_') return "Usernames can't start or end with _.";
    if (digits == (int)name.size()) return "Usernames can't be only numbers.";
    std::string l;
    for (char c : name) if (c != '_') l += (char)std::tolower((unsigned char)c);
    if (l == "guts") return official ? "" : "That username belongs to Guts&Bolts staff.";
    static const char* reserved[] = {"admin", "administrator", "staff", "moderator", "mod", "gutsandbolts",
                                     "gutsbolts", "official", "system", "server", "roblox", "support", "help"};
    for (const char* r : reserved) if (l == r) return "That username is reserved.";
    if (TextFilter::nameHasHateWord(name)) return "That username isn't allowed.";
    return "";
}

long long unixNow() {
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string utcDay(long long t) {
    std::time_t tt = (std::time_t)t;
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

} // namespace Online
