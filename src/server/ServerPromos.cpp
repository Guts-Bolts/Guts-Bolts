// Promo codes (worker/server.js redeemPromo / promoAdmin have the same): the official account
// makes codes like GUTSTOBER2026 that give Bolts, for a limited time if it likes. Everyone can
// use each one once (who used one is kept on their account, User::promos). Codes go in promos.json.
//   bolts.redeem      {code}                      (a code that doesn't start with BOLTS- comes here)
//   admin.promos      {}                          -> {promos} newest first (official account only)
//   admin.promoMake   {code, bolts, start, end}   (start 0 = now, end 0 = never)
//   admin.promoStop   {code}
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cctype>
#include <ctime>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr long long kPromoMaxBolts = 1000000;
constexpr size_t    kMaxPromos     = 500;
std::string promoName(const std::string& typed) {   // no spaces, upper case
    std::string out;
    for (char c : typed) if (!std::isspace((unsigned char)c)) out += (char)std::toupper((unsigned char)c);
    return out;
}
bool goodName(const std::string& s) {   // 3-30 letters and numbers
    if (s.size() < 3 || s.size() > 30) return false;
    for (char c : s) if (!std::isalnum((unsigned char)c)) return false;
    return true;
}
json findPromo(json& list, const std::string& code) {
    for (json& p : list) if (p.value("code", std::string()) == code) return p;
    return nullptr;
}
} // namespace

json GbServer::promoJson(const json& p, long long now) const {
    const long long start = p.value("start", 0LL), end = p.value("end", 0LL);
    const char* status = p.value("stopped", 0LL) ? "stopped" : now < start ? "waiting" : end && now >= end ? "ended" : "live";
    const std::string rewardType = p.value("rewardType", std::string("bolts"));
    const std::string itemId = p.value("itemId", std::string());
    const auto item = itemId.empty() ? m_assets.end() : m_assets.find(itemId);
    return {{"code", p.value("code", std::string())}, {"rewardType", rewardType}, {"itemId", itemId},
            {"itemName", item == m_assets.end() ? std::string() : item->second.name},
            {"bolts", p.value("bolts", 0LL)}, {"start", start}, {"end", end},
            {"made", p.value("made", 0LL)}, {"uses", p.value("uses", 0LL)}, {"status", status}};
}

json GbServer::redeemPromo(User& me, const std::string& typed) {
    const std::string code = promoName(typed);
    const long long now = Online::unixNow();
    json* p = nullptr;
    for (json& x : m_promos) if (x.value("code", std::string()) == code) p = &x;
    if (!goodName(code) || !p || p->value("stopped", 0LL)) return fail("That code doesn't exist. Check the spelling and try again.");
    if (now < p->value("start", 0LL)) return fail("That code doesn't work yet. Try again later!");
    if (p->value("end", 0LL) && now >= p->value("end", 0LL)) return fail("That code has run out.");
    if (std::find(me.promos.begin(), me.promos.end(), code) != me.promos.end()) return fail("You already used that code.");
    const std::string rewardType = p->value("rewardType", std::string("bolts"));
    const std::string itemId = p->value("itemId", std::string());
    if (rewardType == "item") {
        const auto item = findAsset(itemId);
        if (item == m_assets.end() || !item->second.meta.is_object() || !item->second.meta.value("promoOnly", false))
            return fail("This promo item is no longer available.");
        if (me.owned.count(itemId)) return fail("You already own this item.");
    }
    me.promos.push_back(code);
    (*p)["uses"] = p->value("uses", 0LL) + 1;
    savePromos();
    if (rewardType == "item") {
        me.owned.insert(itemId);
        saveUsers();
        const auto item = findAsset(itemId);
        json r = okay(); r["me"] = meJson(me); r["got"] = 0; r["code"] = code;
        r["rewardType"] = "item"; r["itemId"] = itemId; r["itemName"] = item == m_assets.end() ? itemId : item->second.name;
        return r;
    }
    const long long bolts = p->value("bolts", 0LL);
    add(me, bolts, "Promo code " + code, "promo:" + code);
    json r = okay(); r["me"] = meJson(me); r["got"] = bolts; r["code"] = code; r["rewardType"] = "bolts"; return r;
}

json GbServer::promoAdmin(const std::string& name, const User& me, const json& args) {
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto num = [&](const char* k) { return args.contains(k) && args[k].is_number() ? (long long)args[k].get<double>() : 0LL; };
    const long long now = Online::unixNow();
    auto list = [&]() {
        json r = okay(), out = json::array();
        for (auto it = m_promos.rbegin(); it != m_promos.rend(); ++it) out.push_back(promoJson(*it, now));
        r["promos"] = out;
        return r;
    };
    if (name == "admin.promos") return list();
    const std::string code = promoName(str("code"));
    if (name == "admin.promoStop") {
        for (json& p : m_promos)
            if (p.value("code", std::string()) == code) {
                p["stopped"] = now;
                savePromos();
                staffDid(me, "bolts", "Stopped the promo code " + code);
                return list();
            }
        return fail("That code doesn't exist.");
    }
    if (!goodName(code)) return fail("Codes are 3-30 letters and numbers (no spaces or symbols).");
    if (code.rfind("BOLTS", 0) == 0) return fail("Codes can't start with BOLTS (those are the one-person codes).");
    if (!findPromo(m_promos, code).is_null()) return fail("There's already a code called " + code + ". Pick another name.");
    long long start = std::max(0LL, num("start"));
    if (!start) start = now;
    const long long end = std::max(0LL, num("end"));
    if (end && end <= start) return fail("The end has to be after the start.");
    if (end && end <= now) return fail("That end time has already gone by.");
    const std::string rewardType = str("rewardType") == "item" ? "item" : "bolts";
    long long bolts = 0;
    std::string itemId;
    if (rewardType == "item") {
        itemId = str("itemId");
        if (itemId.rfind("gb:", 0) == 0) itemId = itemId.substr(3);
        auto item = findAsset(itemId);
        if (item == m_assets.end() || !Online::isCatalogItem(item->second.kind)) return fail("Enter the ID of an existing catalog item.");
        if (item->second.limited) return fail("Limited items cannot be promo-code rewards.");
        if (item->second.meta.is_object() && item->second.meta.value("award", std::string()) == "email")
            return fail("Email-award items cannot be promo-code rewards.");
        if (!item->second.meta.is_object()) item->second.meta = json::object();
        item->second.meta["promoOnly"] = true;
        saveAssets();
    } else {
        bolts = num("bolts");
        if (bolts < 1 || bolts > kPromoMaxBolts) return fail("Pick an amount between 1 and 1,000,000 Bolts.");
    }
    if (m_promos.size() >= kMaxPromos) m_promos.erase(m_promos.begin());
    json promo = {{"code", code}, {"rewardType", rewardType}, {"bolts", bolts}, {"start", start}, {"end", end}, {"made", now}, {"by", me.id}, {"uses", 0}};
    if (!itemId.empty()) promo["itemId"] = itemId;
    m_promos.push_back(promo);
    savePromos();
    std::string ends;
    if (end) {
        char buf[32];
        std::time_t t = (std::time_t)end;
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::gmtime(&t));
        ends = std::string(", ends ") + buf + " UTC";
    }
    const std::string rewardText = rewardType == "item" ? "item " + itemId : std::to_string(bolts) + " Bolts";
    staffDid(me, "bolts", "Made the promo code " + code + " (" + rewardText + ends + ")");
    return list();
}

void GbServer::savePromos() { writeFile(m_opts.data / "promos.json", m_promos.dump(1)); }

void GbServer::loadPromos() {
    std::string text;
    if (!readFile(m_opts.data / "promos.json", text)) return;
    json all = json::parse(text, nullptr, false);
    if (all.is_array()) m_promos = all;
}
