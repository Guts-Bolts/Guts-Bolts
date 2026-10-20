#pragma once
#include <nlohmann/json.hpp>
#include <string>

// What the Guts&Bolts apps and the Guts&Bolts server say to each other.
//
// Every request is a JSON message signed with the sender's account key:
//   {"op": "upload", "account": "<id>", "time": 1790000000, "nonce": "1a2b3c4d",
//    "args": {...}, "sig": "<signature of requestText()>"}
// so the server always knows exactly who is asking, and nobody can replay an
// old request or change one on the way. Replies are {"ok": true, ...} or
// {"ok": false, "error": "what went wrong, in plain words"}.
namespace Online {

inline constexpr int  kDefaultPort = 7780;
inline constexpr int  kProtocol    = 1;
// The Guts&Bolts server on Cloudflare (online even when nobody's computer is on).
// New players start on it; "Use the official server" in the server dialogs picks it.
inline constexpr const char* kOfficialServer = "https://gutsandbolts.net";
inline constexpr long long kMaxClockSkew = 600;   // seconds a request's time may be off by

// Uploading. Verified creators pay nothing and can sell their creations;
// everyone else pays a small fee and has a daily limit.
inline constexpr long long kFeeClothing = 10;     // hats, shirts, pants
inline constexpr long long kFeeAudio    = 20;
inline constexpr long long kFeePlugin   = 20;
inline constexpr long long kFeeGame     = 0;
inline constexpr long long kFeeDecal    = 5;
inline constexpr int       kDailyUploadsUnverified = 5;
inline constexpr int       kCreatorSharePercent = 70;   // of every sale goes to the creator
inline constexpr int       kMostPasses = 50;            // game passes a game can have

// Biggest uploads, in bytes.
inline constexpr size_t kMaxAudio  = 6u * 1024u * 1024u;
inline constexpr size_t kMaxGame   = 24u * 1024u * 1024u;
inline constexpr size_t kMaxPlugin = 512u * 1024u;
inline constexpr size_t kMaxDecal  = 4u * 1024u * 1024u;   // a .png or .jpg picture

// Kinds of things people upload.
bool        validKind(const std::string& kind);        // hat, shirt, pants, audio, plugin, game, decal
bool        isClothing(const std::string& kind);       // anything you wear (clothes, accessories, faces)
bool        isCatalogItem(const std::string& kind);    // sold in the catalog: clothing, gear, emotes and bundles
// Bundles: a pack of catalog items sold together (buying one gives you everything in it), and
// maybe a body change: meta.headless hides the head and face. Only staff make them.
constexpr int kBundleMost = 12;                        // items in one bundle
std::string gearProblem(const std::string& data);      // "" if it's one Tool from Studio
constexpr int kMostGear = 4;                           // gear equipped at once
// Emotes and animation packs: keyframes for the character, sold in the catalog. Only staff
// make them. Owners equip up to kEmoteSlots emotes (the emote menu's slots) and one pack.
bool        isEmoteItem(const std::string& kind);      // emote, animpack
std::string emoteProblem(const std::string& kind, const std::string& data);   // "" if it plays
constexpr int kEmoteSlots = 8;
bool        isAccessory(const std::string& kind);      // hat, hair, faceacc, neck, shoulder, waist
bool        alwaysFree(const std::string& kind);       // decals, audio and animations: free-use assets, never sold
// Models and animations are public (anyone can find and use them) or private (only their creator).
inline bool hasAccess(const std::string& kind) { return kind == "model" || kind == "animation"; }
long long   uploadFee(const std::string& kind);
size_t      maxSize(const std::string& kind);
const char* kindTitle(const std::string& kind);        // "Hat", "Audio", ...

// Why staff banned an account: {key, what the player sees}. Staff pick one.
struct BanReason { const char* key; const char* title; };
inline constexpr BanReason kBanReasons[] = {
    {"sexual", "Sexual content"},
    {"extremism", "Violent extremism"},
    {"harassment", "Harassment or bullying"},
    {"hate", "Hate speech or discrimination"},
    {"threats", "Threats of violence"},
    {"selfharm", "Promoting self-harm"},
    {"scam", "Scamming or phishing"},
    {"personal", "Sharing personal information"},
    {"exploit", "Cheating or exploiting"},
    {"spam", "Spam"},
    {"impersonation", "Impersonation"},
    {"inappropriate", "Inappropriate content"},
    {"underage", "Underage safety violation"},
    {"other", "Breaking the rules"},
};
const char* banReasonTitle(const std::string& key);   // null if it isn't one
// "This account has been banned for: Spam. Note from staff: ..."
std::string banMessage(const std::string& reason, const std::string& note);

// The exact text a request's signature covers.
std::string requestText(const std::string& op, const std::string& account, long long time,
                        const std::string& nonce, const nlohmann::json& args);

// Is this badge grant real? `sig` is signed by `official`, or for badges Staff
// may give ("verified"), "s:<staff id>:<their staff grant>:<signature>".
bool grantValid(const std::string& official, const std::string& key, const std::string& accountId,
                const std::string& sig);
std::string grantMessage(const std::string& key, const std::string& accountId);

// Binary data inside JSON.
std::string base64Encode(const std::string& bytes);
bool        base64Decode(const std::string& text, std::string& bytes);

// Names and descriptions: printable text only, trimmed, at most `maxLen` characters.
std::string cleanText(const std::string& s, size_t maxLen, bool allowNewlines = false);
// A Texture / SoundId / Image someone typed: a plain asset number ("123", like Roblox's
// IDs) becomes "gb:123"; anything else is kept as it is.
std::string assetRef(const std::string& s);

// Usernames: 3-20 letters, numbers or one underscore (not at the ends), not
// all numbers, and not one of the reserved names. "" if it's fine.
std::string usernameProblem(const std::string& name, bool official = false);
inline constexpr size_t kMinPassword = 8;

long long   unixNow();
std::string utcDay(long long t);    // "2026-09-28"

} // namespace Online
