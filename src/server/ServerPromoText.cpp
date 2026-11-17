// Promotional text on a game's listing: a short line for what's on right now, separate from the
// game's name. worker/promotext.js has the same rules:
//   - up to kPromoLength letters, one line, through the text filter
//   - ends by itself after 1-30 days (7 if not said); text "" takes it off at once
//   - at most kPromoChangesPerDay changes a day (staff: no limit)
//   game.promo {game, text, days?} -> {promo: {text, until} | null}
// Kept in the game's meta: promo {text, until, set} and promoLog {day, n}.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/TextFilter.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cmath>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr size_t kPromoLength = 80;
constexpr long long kPromoMostDays = 30, kPromoDefaultDays = 7, kPromoChangesPerDay = 10;

// One line: control characters become spaces, spaces squeezed, at most kPromoLength (whole UTF-8 letters).
std::string cleanPromo(const std::string& in) {
    std::string out;
    bool space = false;
    for (unsigned char c : in) {
        if (c < 0x20 || c == 0x7f || c == ' ') { space = !out.empty(); continue; }
        if (space) { out += ' '; space = false; }
        out += (char)c;
    }
    // Count letters, not bytes (like JavaScript's slice, close enough for text people type).
    size_t letters = 0, cut = out.size();
    for (size_t i = 0; i < out.size(); ++i)
        if (((unsigned char)out[i] & 0xC0) != 0x80 && ++letters > kPromoLength) { cut = i; break; }
    out.resize(cut);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}
} // namespace

json GbServer::promoJson(const Asset& a) const {
    if (!a.meta.is_object() || !a.meta.contains("promo") || !a.meta["promo"].is_object()) return nullptr;
    const json& p = a.meta["promo"];
    const std::string text = p.value("text", std::string());
    const long long until = p.value("until", 0LL);
    if (text.empty() || until <= Online::unixNow()) return nullptr;
    return {{"text", text}, {"until", until}};
}

json GbServer::promoTextOp(User& me, const json& args) {
    auto it = findAsset(Online::cleanText(args.contains("game") && args["game"].is_string() ? args["game"].get<std::string>() : "", 80));
    if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
    Asset& a = it->second;
    if (a.creator != me.id && !isStaff(me)) return fail("Only the game's creator can change its promotional text.");
    if (!a.meta.is_object()) a.meta = json::object();
    const long long t = Online::unixNow(), day = t / 86400;
    json log = a.meta.contains("promoLog") && a.meta["promoLog"].is_object() && a.meta["promoLog"].value("day", -1LL) == day
                   ? a.meta["promoLog"] : json{{"day", day}, {"n", 0}};
    if (log.value("n", 0LL) >= kPromoChangesPerDay && !isStaff(me))
        return fail("That's " + std::to_string(kPromoChangesPerDay) + " changes today. Try again tomorrow.");
    const std::string text = cleanPromo(args.contains("text") && args["text"].is_string() ? args["text"].get<std::string>() : "");
    if (text.empty()) {
        a.meta.erase("promo");
    } else {
        if (TextFilter::filter(text) != text) return fail("The promotional text has words that aren't allowed.");
        if (lower(text) == lower(a.name)) return fail("Say something new: that's already the game's name.");
        long long days = kPromoDefaultDays;
        if (args.contains("days") && args["days"].is_number()) {
            const double d = args["days"].get<double>();
            if (std::isfinite(d)) days = (long long)std::llround(d);
        }
        days = std::clamp(days, 1LL, kPromoMostDays);
        a.meta["promo"] = {{"text", text}, {"until", t + days * 86400}, {"set", t}};
    }
    log["n"] = log.value("n", 0LL) + 1;
    a.meta["promoLog"] = log;
    saveAssets();
    json r = okay();
    r["promo"] = promoJson(a);
    return r;
}
